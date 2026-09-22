"""
ui/draw_canvas.py

Interactive drawing canvas for drone map annotation.
All overlay rendering done with PIL — no OpenCV dependency.
"""

import math
import tkinter as tk
from typing import Callable, List, Optional, Tuple

try:
    from PIL import Image, ImageDraw, ImageFont, ImageTk
    PIL_OK = True
except ImportError:
    PIL_OK = False

from sar_planner.core.map_builder import (
    compute_axes,
    compute_scale,
    field_from_image_corners,
    pixel_to_project,
    project_to_pixel,
)

# ── Phases ─────────────────────────────────────────────────────────────────────
PH_ORIGIN, PH_XDIR, PH_OBSTACLES, PH_STARTS, PH_END, PH_SAMPLING, PH_DONE = range(7)

PHASE_HINTS = {
    PH_ORIGIN:    '1/7  Click origin  (bottom-left corner of venue)',
    PH_XDIR:      '2/7  Click +X direction  (toward door wall)',
    PH_OBSTACLES: '3/7  Drag obstacles  —  R-click=delete  |  [Next] or N',
    PH_STARTS:    '4/7  Click start position',
    PH_END:       '5/7  Click end point',
    PH_SAMPLING:  '6/7  Click sampling area corner',
    PH_DONE:      '7/7  Done  —  press [Convert]',
}

# ── Rotation map ──────────────────────────────────────────────────────────────
try:
    _ROT_MAP = {
        90:  Image.Transpose.ROTATE_90,
        180: Image.Transpose.ROTATE_180,
        270: Image.Transpose.ROTATE_270,
    }
except AttributeError:
    _ROT_MAP = {
        90:  Image.ROTATE_90,
        180: Image.ROTATE_180,
        270: Image.ROTATE_270,
    }

# ── Font ──────────────────────────────────────────────────────────────────────
_FONT_PATHS = [
    '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf',
]
_FONT_SM = _FONT_XS = None

def _init_fonts():
    global _FONT_SM, _FONT_XS
    if _FONT_SM is not None or not PIL_OK:
        return
    for p in _FONT_PATHS:
        try:
            from PIL import ImageFont
            _FONT_SM = ImageFont.truetype(p, 13)
            _FONT_XS = ImageFont.truetype(p, 11)
            return
        except Exception:
            pass
    from PIL import ImageFont
    _FONT_SM = _FONT_XS = ImageFont.load_default()

# ── Colours ───────────────────────────────────────────────────────────────────
BG_CANVAS    = '#DFE6E9'
BORDER       = '#CCD6DD'
ROBOT_COLORS = ['#E74C3C', '#E67E22', '#F1C40F', '#2ECC71']

def _hex2rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))

# ── PIL drawing helpers ───────────────────────────────────────────────────────

def _arrow(draw, p1, p2, color, w=2):
    draw.line([p1, p2], fill=color, width=w)
    ang = math.atan2(p2[1]-p1[1], p2[0]-p1[0])
    for da in (-0.45, 0.45):
        tip = (p2[0]-14*math.cos(ang-da), p2[1]-14*math.sin(ang-da))
        draw.line([p2, tip], fill=color, width=w)

def _dot(draw, pt, color, r=8):
    x, y = int(pt[0]), int(pt[1])
    draw.ellipse([x-r, y-r, x+r, y+r], fill=color, outline=(255, 255, 255, 200))

def _poly_fill(base, pts, fill_rgba, line_rgba, lw=2):
    ov = Image.new('RGBA', base.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    od.polygon(pts, fill=fill_rgba)
    od.line(pts + [pts[0]], fill=line_rgba, width=lw)
    return Image.alpha_composite(base, ov)

def _label(draw, pt, text, color, font):
    x, y = int(pt[0]) + 10, int(pt[1]) - 15
    draw.text((x+1, y+1), text, fill=(0, 0, 0, 110), font=font)
    draw.text((x,   y),   text, fill=color,          font=font)


# ═══════════════════════════════════════════════════════════════════════════════

class DrawCanvas(tk.Canvas):
    """
    Interactive drawing canvas.
    Drone image is the background; PIL overlays drawn per frame.
    Supports 90-degree rotation of source image before drawing.
    """

    def __init__(self, parent, on_state: Callable = None, **kw):
        super().__init__(parent, bg=BG_CANVAS,
                         highlightthickness=1, highlightbackground=BORDER, **kw)
        self._cb     = on_state
        self._tk_img = None

        # Image / scale
        self._img_orig  = None
        self._img_scale = 1.0
        self._rotation  = 0        # 0 / 90 / 180 / 270
        self._ds = 1.0             # display scale
        self._ox = 0.0             # display offset x
        self._oy = 0.0             # display offset y

        # Coordinate system
        self._origin = None
        self._xh = self._yh = self._field = None

        # Drawing state
        self._phase      = PH_ORIGIN
        self._n_robots   = 4
        self._obstacles  = []
        self._starts     = []
        self._end        = None
        self._sampling   = []
        self._drag0      = None
        self._cursor     = None

        # Map preview mode (after Convert)
        self._map_mode = False
        self._map_pil  = None

        self.bind('<Button-1>',        self._ldown)
        self.bind('<B1-Motion>',       self._lmove)
        self.bind('<ButtonRelease-1>', self._lup)
        self.bind('<Button-3>',        self._rdown)
        self.bind('<Motion>',          self._mmove)
        self.bind('<Configure>',       self._on_resize)
        self.bind('<Key>',             self._on_key)
        self.bind('<Enter>',           lambda e: self.focus_set())

    # ── Public API ────────────────────────────────────────────────────────

    def load(self, path: str, altitude_m: float, fov_deg: float, n_robots: int):
        _init_fonts()
        img = Image.open(path)
        self._img_orig  = img
        w, _ = img.size
        self._img_scale = compute_scale(altitude_m, fov_deg, w)
        self._n_robots  = n_robots
        self._rotation  = 0
        self._reset()
        self._map_mode  = False
        self._recalc()
        self._draw()
        self._notify()

    def rotate_cw(self):
        self._do_rotate((self._rotation + 270) % 360)

    def rotate_ccw(self):
        self._do_rotate((self._rotation + 90) % 360)

    def rotation_deg(self) -> int:
        return self._rotation

    def set_robots(self, n: int):
        self._n_robots = n

    def is_done(self) -> bool:
        return self._phase == PH_DONE

    def phase(self) -> int:
        return self._phase

    def n_starts(self) -> int:
        return len(self._starts)

    def n_sampling(self) -> int:
        return len(self._sampling)

    def hint(self) -> str:
        ph = self._phase
        ns, nr = len(self._starts), self._n_robots
        sp = len(self._sampling)
        if ph == PH_STARTS:
            return f'4/7  Click start position  ({ns+1}/{nr})'
        if ph == PH_SAMPLING:
            return f'6/7  Click sampling area corner  ({sp+1}/2)'
        return PHASE_HINTS.get(ph, '')

    def advance_obstacles(self):
        if self._phase == PH_OBSTACLES:
            self._phase = PH_STARTS
            self._draw()
            self._notify()

    def undo(self):
        ph = self._phase
        if ph == PH_ORIGIN:
            return
        elif ph == PH_XDIR:
            self._origin = None; self._phase = PH_ORIGIN
        elif ph == PH_OBSTACLES:
            if self._obstacles:
                self._obstacles.pop(); self._renum()
            else:
                self._xh = self._yh = self._field = None
                self._phase = PH_XDIR
        elif ph == PH_STARTS:
            if self._starts: self._starts.pop()
            else: self._phase = PH_OBSTACLES
        elif ph == PH_END:
            self._starts.pop(); self._phase = PH_STARTS
        elif ph == PH_SAMPLING:
            if self._sampling: self._sampling.pop()
            else: self._end = None; self._phase = PH_END
        elif ph == PH_DONE:
            self._sampling.pop(); self._phase = PH_SAMPLING
        self._draw(); self._notify()

    def get_draw_snapshot(self) -> Optional['Image.Image']:
        """Return annotated frame with all overlays (for Draw Map tab)."""
        if self._working() is None:
            return None
        self._recalc()
        return self._render_frame()

    def map_data(self) -> Optional[dict]:
        if not self.is_done():
            return None
        from sar_planner.core.map_builder import assemble_map_data
        starts = {f"spot_0{i+1}": m for i, m in enumerate(self._starts)}
        return assemble_map_data(
            starts=starts,
            end=self._end,
            field=self._field,
            sampling=(self._sampling[0], self._sampling[1]),
            obstacles=self._obstacles,
        )

    def show_placeholder(self, text: str = 'Select a drone image to begin'):
        self._map_mode = False
        self._img_orig = None
        self._ph_text(text)

    # ── Rotation ──────────────────────────────────────────────────────────

    def _do_rotate(self, new_rot: int):
        if self._img_orig is None:
            return
        if self._phase > PH_ORIGIN:
            self._reset()
            self._notify()
        self._rotation = new_rot
        self._recalc()
        self._draw()
        self._notify()

    def _working(self) -> Optional['Image.Image']:
        if self._img_orig is None:
            return None
        if self._rotation == 0:
            return self._img_orig
        return self._img_orig.transpose(_ROT_MAP[self._rotation])

    # ── Internal helpers ──────────────────────────────────────────────────

    def _reset(self):
        self._phase = PH_ORIGIN
        self._origin = self._xh = self._yh = self._field = None
        self._obstacles = []; self._starts = []
        self._end = self._drag0 = None; self._sampling = []

    def _renum(self):
        for i, o in enumerate(self._obstacles):
            o['id'] = f"obs_{i+1:02d}"

    def _notify(self):
        if self._cb:
            self._cb()

    def _recalc(self):
        img = self._working()
        if img is None:
            return
        self.update_idletasks()
        cw = max(self.winfo_width(),  10)
        ch = max(self.winfo_height(), 10)
        iw, ih = img.size
        s = min(cw / iw, ch / ih)
        self._ds = s
        self._ox = (cw - iw * s) / 2
        self._oy = (ch - ih * s) / 2

    def _c2i(self, cx, cy):
        return ((cx - self._ox) / self._ds, (cy - self._oy) / self._ds)

    def _i2d(self, px, py):
        return (px * self._ds, py * self._ds)

    def _m2d(self, xm, ym):
        if self._origin is None:
            return (0, 0)
        px, py = project_to_pixel(xm, ym, self._origin,
                                   self._xh, self._yh, self._img_scale)
        return self._i2d(px, py)

    # ── Rendering ─────────────────────────────────────────────────────────

    def _render_frame(self) -> 'Image.Image':
        """
        Render the annotated frame at current display scale (_ds, _ox, _oy).
        Returns a PIL Image (canvas size) with all overlays.
        _recalc() must be called before this.
        """
        working = self._working()
        iw, ih = working.size
        dw = max(1, int(iw * self._ds))
        dh = max(1, int(ih * self._ds))
        img  = working.resize((dw, dh), Image.BILINEAR).convert('RGBA')
        draw = ImageDraw.Draw(img)

        # Field
        if self._field:
            fx1, fx2, fy1, fy2 = self._field
            fp = [self._m2d(fx1,fy1), self._m2d(fx2,fy1),
                  self._m2d(fx2,fy2), self._m2d(fx1,fy2)]
            img = _poly_fill(img, fp, (180,180,0,20), (180,180,0,200))
            draw = ImageDraw.Draw(img)

        # Axes
        if self._xh and self._origin:
            ox, oy = self._i2d(*self._origin)
            L = max(60, int(70 * self._ds))
            _arrow(draw, (ox,oy), (ox+self._xh[0]*L, oy+self._xh[1]*L),
                   (255,60,60,240))
            _arrow(draw, (ox,oy), (ox+self._yh[0]*L, oy+self._yh[1]*L),
                   (60,100,255,240))
            _label(draw, (ox+self._xh[0]*L, oy+self._xh[1]*L),
                   '+X', (255,80,80,240), _FONT_SM)
            _label(draw, (ox+self._yh[0]*L, oy+self._yh[1]*L),
                   '+Y', (80,120,255,240), _FONT_SM)

        # Origin
        if self._origin:
            r = max(6, int(8 * self._ds))
            _dot(draw, self._i2d(*self._origin), (0,220,0,255), r)
            _label(draw, self._i2d(*self._origin),
                   'O(0,0)', (0,230,0,240), _FONT_SM)

        # Obstacles
        for obs in self._obstacles:
            cp = [self._m2d(obs['x_min'],obs['y_min']),
                  self._m2d(obs['x_max'],obs['y_min']),
                  self._m2d(obs['x_max'],obs['y_max']),
                  self._m2d(obs['x_min'],obs['y_max'])]
            img = _poly_fill(img, cp, (0,80,220,80), (0,130,255,220))
            draw = ImageDraw.Draw(img)
            _label(draw, cp[0], obs['id'], (230,230,255,220), _FONT_XS)

        # Starts
        for i, m in enumerate(self._starts):
            rgb = _hex2rgb(ROBOT_COLORS[i % len(ROBOT_COLORS)])
            cp  = self._m2d(*m)
            r   = max(6, int(8 * self._ds))
            _dot(draw, cp, rgb + (255,), r)
            _label(draw, cp, f'S{i+1}({m[0]},{m[1]})', rgb + (230,), _FONT_XS)

        # End
        if self._end:
            cp = self._m2d(*self._end)
            _dot(draw, cp, (0,220,220,255), max(7, int(9*self._ds)))
            _label(draw, cp,
                   f'END({self._end[0]},{self._end[1]})',
                   (0,230,230,230), _FONT_XS)

        # Sampling
        if len(self._sampling) == 1:
            _dot(draw, self._m2d(*self._sampling[0]), (80,255,120,255), 6)
        elif len(self._sampling) == 2:
            (x1,y1),(x2,y2) = self._sampling
            sp = [self._m2d(min(x1,x2),min(y1,y2)),
                  self._m2d(max(x1,x2),min(y1,y2)),
                  self._m2d(max(x1,x2),max(y1,y2)),
                  self._m2d(min(x1,x2),max(y1,y2))]
            img = _poly_fill(img, sp, (80,255,120,25), (80,255,120,200))
            draw = ImageDraw.Draw(img)

        # Composite onto background
        self.update_idletasks()
        cw = max(self.winfo_width(),  1)
        ch = max(self.winfo_height(), 1)
        final = Image.new('RGB', (cw, ch), _hex2rgb(BG_CANVAS))
        final.paste(img.convert('RGB'),
                    (max(0, int(self._ox)), max(0, int(self._oy))))
        return final

    def _draw(self):
        working = self._working()
        if working is None:
            self._ph_text('Select a drone image to begin')
            return
        self._recalc()
        frame = self._render_frame()
        # Cursor coordinate label — live display only, not in snapshot
        if self._cursor and self._origin and self._xh:
            from PIL import ImageDraw as _ID
            draw = _ID.Draw(frame)
            m  = pixel_to_project(*self._cursor, self._origin,
                                   self._xh, self._yh, self._img_scale)
            cu = (self._cursor[0] * self._ds + self._ox + 2,
                  self._cursor[1] * self._ds + self._oy + 2)
            _label(draw, cu, f'({m[0]:.1f},{m[1]:.1f})m',
                   (200,200,200,200), _FONT_XS)
        self._display(frame)

    def _display(self, pil_img: 'Image.Image'):
        self.update_idletasks()
        cw = max(self.winfo_width(),  1)
        ch = max(self.winfo_height(), 1)
        img = pil_img.copy()
        img.thumbnail((cw, ch), Image.BILINEAR)
        iw, ih = img.size
        bg = Image.new('RGB', (cw, ch), _hex2rgb(BG_CANVAS))
        bg.paste(img, ((cw-iw)//2, (ch-ih)//2))
        self._tk_img = ImageTk.PhotoImage(bg)
        self.delete('all')
        self.create_image(0, 0, anchor='nw', image=self._tk_img)

    def _ph_text(self, text: str):
        self.update_idletasks()
        w = self.winfo_width()  or 400
        h = self.winfo_height() or 300
        self.delete('all')
        self.create_text(w//2, h//2, text=text,
                         fill='#B2BEC3', font=('Helvetica', 12))

    # ── Events ────────────────────────────────────────────────────────────

    def _on_resize(self, _e):
        self._recalc(); self._draw()

    def _mmove(self, e):
        if self._working() is None: return
        self._cursor = self._c2i(e.x, e.y)
        self._draw()

    def _ldown(self, e):
        if self._working() is None: return
        px, py = self._c2i(e.x, e.y)
        if self._phase == PH_OBSTACLES:
            self._drag0 = (px, py)
        else:
            self._click(px, py)

    def _lmove(self, e):
        if self._phase == PH_OBSTACLES and self._drag0:
            self._cursor = self._c2i(e.x, e.y)
            self._draw()

    def _lup(self, e):
        if self._phase == PH_OBSTACLES and self._drag0:
            self._end_drag(*self._c2i(e.x, e.y))
            self._drag0 = None

    def _rdown(self, e):
        if self._phase == PH_OBSTACLES and self._obstacles:
            self._obstacles.pop(); self._renum(); self._draw()

    def _on_key(self, e):
        k = e.keysym.lower()
        if k == 'z':
            self.undo()
        elif k in ('n', 'return') and self._phase == PH_OBSTACLES:
            self.advance_obstacles()

    def _click(self, px, py):
        ph = self._phase
        if ph == PH_ORIGIN:
            self._origin = (px, py); self._phase = PH_XDIR
        elif ph == PH_XDIR:
            xh, yh = compute_axes(self._origin, (px, py))
            if xh is None: return
            self._xh, self._yh = xh, yh
            iw, ih = self._working().size
            self._field = field_from_image_corners(
                iw, ih, self._origin, xh, yh, self._img_scale)
            self._phase = PH_OBSTACLES
        elif ph == PH_STARTS:
            m = pixel_to_project(px, py, self._origin,
                                  self._xh, self._yh, self._img_scale)
            self._starts.append(m)
            if len(self._starts) >= self._n_robots:
                self._phase = PH_END
        elif ph == PH_END:
            self._end = pixel_to_project(px, py, self._origin,
                                          self._xh, self._yh, self._img_scale)
            self._phase = PH_SAMPLING
        elif ph == PH_SAMPLING:
            m = pixel_to_project(px, py, self._origin,
                                  self._xh, self._yh, self._img_scale)
            self._sampling.append(m)
            if len(self._sampling) >= 2:
                self._phase = PH_DONE
        self._draw(); self._notify()

    def _end_drag(self, px2, py2):
        if not self._drag0 or not self._xh: return
        m1 = pixel_to_project(*self._drag0, self._origin,
                               self._xh, self._yh, self._img_scale)
        m2 = pixel_to_project(px2, py2, self._origin,
                               self._xh, self._yh, self._img_scale)
        xmn, xmx = min(m1[0],m2[0]), max(m1[0],m2[0])
        ymn, ymx = min(m1[1],m2[1]), max(m1[1],m2[1])
        if xmx - xmn < 0.05 or ymx - ymn < 0.05: return
        idx = len(self._obstacles) + 1
        self._obstacles.append({
            "id": f"obs_{idx:02d}",
            "x_min": round(xmn,2), "x_max": round(xmx,2),
            "y_min": round(ymn,2), "y_max": round(ymx,2),
            "clearance": 0.3,
        })
        self._draw()
