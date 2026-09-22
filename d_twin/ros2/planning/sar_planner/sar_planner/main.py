"""
sar_planner/main.py

SAR Planner — Main window and application entry point.

Thread model:
    Main thread  : tkinter mainloop + root.after() polling
    Worker thread: path generation (daemon)
    ROS2 thread  : rclpy.spin() (daemon, only when ROS2 available)

State machine:
    IDLE → DRAWING → MAP_SAVED → GENERATING → PATH_READY → PUBLISHED
    [Select Image] resets to IDLE from any state.
"""

import os
import queue
import threading
import tkinter as tk
from enum import Enum, auto
from pathlib import Path
from tkinter import messagebox, ttk

import yaml

try:
    from PIL import Image, ImageTk
    PIL_OK = True
except ImportError:
    PIL_OK = False
    print('[ERROR] Pillow is required: pip install Pillow --break-system-packages')

# ── ROS2 (optional) ────────────────────────────────────────────────────────────
ROS2_OK  = False
ros2_mgr = None

try:
    import rclpy
    rclpy.init()
    ROS2_OK = True
except Exception:
    pass

# ── Internal modules ───────────────────────────────────────────────────────────
from sar_planner.core.path_set  import (MapConfig, build_path_set,
                                         validate_path_set, select_paths,
                                         _build_map_cells)
from sar_planner.core.visualizer import render_map, render_paths
from sar_planner.ui.draw_canvas  import DrawCanvas, PH_OBSTACLES
from sar_planner.ui.image_picker import ImagePickerDialog
from sar_planner.ui.result_tabs  import ResultTabs

if ROS2_OK:
    from sar_planner.ros.path_publisher import ROS2Manager

# ── Default paths ──────────────────────────────────────────────────────────────
# Priority:
#   1. Resolve __file__ via realpath (works with --symlink-install → points to source)
#   2. Fallback: get_package_share_directory (non-symlink install)
#
# With colcon build --symlink-install (standard dev workflow),
# realpath(__file__) → .../src/planning/sar_planner/sar_planner/main.py
# Two levels up                    → .../src/planning/sar_planner/
# → config/map.yaml stays in the source tree and survives rebuilds.

_REAL_FILE = os.path.realpath(os.path.abspath(__file__))
_PKG_ROOT  = os.path.dirname(os.path.dirname(_REAL_FILE))   # two levels up
_SOURCE_CFG = os.path.join(_PKG_ROOT, 'config', 'map.yaml')

if os.path.isdir(os.path.join(_PKG_ROOT, 'config')):
    # Source layout found (symlink-install or running directly)
    DEFAULT_MAP_PATH = _SOURCE_CFG
else:
    # Non-symlink install: fall back to share directory
    try:
        from ament_index_python.packages import get_package_share_directory
        DEFAULT_MAP_PATH = os.path.join(
            get_package_share_directory('sar_planner'), 'config', 'map.yaml')
    except Exception:
        DEFAULT_MAP_PATH = _SOURCE_CFG

# ── Palette ────────────────────────────────────────────────────────────────────
BG_MAIN   = '#ECEFF1'
BG_PANEL  = '#F5F6FA'
BG_HEADER = '#2C3E50'
FG_HEADER = '#FFFFFF'
BG_CANVAS = '#DFE6E9'
BORDER    = '#CCD6DD'


# ── App state ──────────────────────────────────────────────────────────────────
class AppState(Enum):
    IDLE       = auto()
    DRAWING    = auto()
    MAP_SAVED  = auto()
    GENERATING = auto()
    PATH_READY = auto()
    PUBLISHED  = auto()


# ═══════════════════════════════════════════════════════════════════════════════
# MainWindow
# ═══════════════════════════════════════════════════════════════════════════════

class MainWindow:

    POLL_MS = 50   # queue poll interval

    def __init__(self, root: tk.Tk, map_path: str = DEFAULT_MAP_PATH):
        self.root     = root
        self.map_path = map_path

        root.title('SAR Planner')
        root.configure(bg=BG_MAIN)
        root.minsize(1150, 720)

        # App state
        self._state    = AppState.IDLE
        self._selected = None   # selected paths from path_set
        self._cfg      = None   # MapConfig
        self._cells    = None   # all_cells frozenset
        self._last_dir = str(Path.home())

        # Thread communication
        self._queue: queue.Queue = queue.Queue()

        # tkinter variables
        self.altitude   = tk.StringVar(value='10.0')
        self.fov        = tk.StringVar(value='84.0')
        self.n_robots   = tk.StringVar(value='4')
        self.map_output = tk.StringVar(value=map_path)
        self.status_var = tk.StringVar(value='Ready.')

        self._drone_pil   = None
        self._drone_photo = None

        self._build()
        self._start_polling()

        # Fullscreen on startup
        try:
            self.root.state('zoomed')
        except tk.TclError:
            self.root.attributes('-zoomed', True)

        # Load existing map.yaml after window is rendered
        if os.path.exists(map_path):
            self.root.after(300, lambda: self._load_existing_map(map_path))

    # ── UI construction ───────────────────────────────────────────────────

    def _build(self):
        # Header
        hdr = tk.Frame(self.root, bg=BG_HEADER, height=46)
        hdr.pack(fill='x'); hdr.pack_propagate(False)
        tk.Label(hdr, text='SAR Planner',
                 bg=BG_HEADER, fg=FG_HEADER,
                 font=('Helvetica', 14, 'bold')).pack(side='left', padx=18, pady=10)
        if ROS2_OK:
            tk.Label(hdr, text='● ROS2',
                     bg=BG_HEADER, fg='#2ECC71',
                     font=('Helvetica', 9)).pack(side='right', padx=14)
        else:
            tk.Label(hdr, text='○ ROS2 unavailable',
                     bg=BG_HEADER, fg='#E74C3C',
                     font=('Helvetica', 9)).pack(side='right', padx=14)

        # Status bar
        sb = tk.Frame(self.root, bg='#34495E', height=26)
        sb.pack(fill='x', side='bottom'); sb.pack_propagate(False)
        tk.Label(sb, textvariable=self.status_var,
                 bg='#34495E', fg='#BDC3C7',
                 font=('Helvetica', 9), anchor='w').pack(fill='x', padx=10, pady=4)

        # Main area — PanedWindow for resizable split
        pane = tk.PanedWindow(self.root, orient='horizontal',
                              bg=BG_MAIN, sashwidth=6,
                              sashrelief='flat', bd=0)
        pane.pack(fill='both', expand=True, padx=6, pady=6)

        lf = tk.LabelFrame(pane, text='  Drone Image  ',
                            bg=BG_PANEL, font=('Helvetica', 10, 'bold'),
                            relief='groove', bd=1)
        rf = tk.LabelFrame(pane, text='  Mission Planner  ',
                            bg=BG_PANEL, font=('Helvetica', 10, 'bold'),
                            relief='groove', bd=1)
        pane.add(lf, minsize=280, stretch='always')
        pane.add(rf, minsize=500, stretch='always')

        self._build_left(lf)
        self._build_right(rf)

    def _build_left(self, p):
        # Image selector controls
        ctrl = tk.Frame(p, bg=BG_PANEL)
        ctrl.pack(fill='x', padx=10, pady=(8, 2))

        tk.Button(ctrl, text='Select Image',
                  bg='#2980B9', fg='white',
                  font=('Helvetica', 9, 'bold'),
                  relief='flat', padx=10, pady=5, cursor='hand2',
                  command=self._select_image).pack(side='left')

        for lbl, var, w in [('   Alt (m)', self.altitude, 6),
                             ('  H-FOV°',  self.fov,      6)]:
            tk.Label(ctrl, text=lbl, bg=BG_PANEL,
                     font=('Helvetica', 9)).pack(side='left')
            tk.Entry(ctrl, textvariable=var, width=w,
                     font=('Helvetica', 9)).pack(side='left', padx=(2, 0))

        self._lpath = tk.Label(p, text='No image selected',
                               bg=BG_PANEL, fg='#95A5A6',
                               font=('Helvetica', 8), anchor='w')
        self._lpath.pack(fill='x', padx=12, pady=(2, 2))

        # Full-height tab viewer: Drone Image / Draw Map / Digital Map / Path Map
        self._tabs = ResultTabs(p)
        self._tabs.pack(fill='both', expand=True, padx=8, pady=(0, 8))

    def _build_right(self, p):
        # ── Toolbar row 1: drawing controls ──────────────────────────────
        tb1 = tk.Frame(p, bg=BG_PANEL)
        tb1.pack(fill='x', padx=10, pady=(8, 0))

        tk.Button(tb1, text='Undo  Z',
                  bg='#7F8C8D', fg='white',
                  font=('Helvetica', 9), relief='flat',
                  padx=8, pady=4, cursor='hand2',
                  command=lambda: self._dc.undo()).pack(side='left')

        self._next_btn = tk.Button(tb1, text='Next  N',
                                    bg='#8E44AD', fg='white',
                                    font=('Helvetica', 9), relief='flat',
                                    padx=8, pady=4, cursor='hand2',
                                    state='disabled',
                                    command=lambda: self._dc.advance_obstacles())
        self._next_btn.pack(side='left', padx=(4, 10))

        tk.Frame(tb1, bg=BORDER, width=1, height=24).pack(side='left', padx=(0, 8), pady=2)

        tk.Label(tb1, text='Rotate:', bg=BG_PANEL,
                 font=('Helvetica', 9)).pack(side='left')
        tk.Button(tb1, text='↺ CCW',
                  bg='#1ABC9C', fg='white',
                  font=('Helvetica', 9), relief='flat',
                  padx=7, pady=4, cursor='hand2',
                  command=self._rotate_ccw).pack(side='left', padx=(4, 2))
        tk.Button(tb1, text='↻ CW',
                  bg='#1ABC9C', fg='white',
                  font=('Helvetica', 9), relief='flat',
                  padx=7, pady=4, cursor='hand2',
                  command=self._rotate_cw).pack(side='left', padx=(0, 4))
        self._rot_lbl = tk.Label(tb1, text='0°',
                                  bg=BG_PANEL, fg='#1ABC9C',
                                  font=('Helvetica', 9, 'bold'))
        self._rot_lbl.pack(side='left', padx=(0, 10))

        tk.Frame(tb1, bg=BORDER, width=1, height=24).pack(side='left', padx=(0, 8), pady=2)

        tk.Label(tb1, text='Robots', bg=BG_PANEL,
                 font=('Helvetica', 9)).pack(side='left')
        cb = ttk.Combobox(tb1, textvariable=self.n_robots,
                          values=['1', '2', '3', '4'],
                          width=3, state='readonly')
        cb.pack(side='left', padx=(2, 10))
        cb.bind('<<ComboboxSelected>>',
                lambda e: self._dc.set_robots(int(self.n_robots.get())))

        # ── Toolbar row 2: action buttons ────────────────────────────────
        tb2 = tk.Frame(p, bg=BG_PANEL)
        tb2.pack(fill='x', padx=10, pady=(4, 2))

        tk.Label(tb2, text='Output:', bg=BG_PANEL,
                 font=('Helvetica', 9)).pack(side='left')
        tk.Entry(tb2, textvariable=self.map_output, width=22,
                 font=('Helvetica', 8)).pack(side='left', padx=(2, 2))
        tk.Button(tb2, text='...', width=2, relief='flat', cursor='hand2',
                  command=self._pick_output).pack(side='left', padx=(0, 10))

        self._conv_btn = tk.Button(tb2, text='Convert',
                                    bg='#27AE60', fg='white',
                                    font=('Helvetica', 9, 'bold'),
                                    relief='flat', padx=12, pady=4,
                                    cursor='hand2', state='disabled',
                                    command=self._convert)
        self._conv_btn.pack(side='left', padx=(0, 4))

        self._gen_btn = tk.Button(tb2, text='Generate Paths',
                                   bg='#2980B9', fg='white',
                                   font=('Helvetica', 9, 'bold'),
                                   relief='flat', padx=12, pady=4,
                                   cursor='hand2', state='disabled',
                                   command=self._generate_paths)
        self._gen_btn.pack(side='left', padx=(0, 4))

        self._pub_btn = tk.Button(tb2, text='Publish',
                                   bg='#E74C3C', fg='white',
                                   font=('Helvetica', 9, 'bold'),
                                   relief='flat', padx=12, pady=4,
                                   cursor='hand2', state='disabled',
                                   command=self._publish)
        self._pub_btn.pack(side='left')

        # ── Step label ────────────────────────────────────────────────────
        self._step_lbl = tk.Label(p, text='Select an image to begin drawing',
                                   bg=BG_PANEL, fg='#2C3E50',
                                   font=('Helvetica', 9), anchor='w')
        self._step_lbl.pack(fill='x', padx=12, pady=(0, 2))

        # DrawCanvas: full height (result tabs moved to left panel)
        self._dc = DrawCanvas(p, on_state=self._on_dc_state)
        self._dc.pack(fill='both', expand=True, padx=10, pady=(0, 8))

        # Global key bindings
        self.root.bind('<Key-z>', lambda e: self._dc.undo())
        self.root.bind('<Key-Z>', lambda e: self._dc.undo())

    # ── Helpers ───────────────────────────────────────────────────────────

    def _ph(self, canvas, text):
        canvas.update_idletasks()
        w = canvas.winfo_width()  or 400
        h = canvas.winfo_height() or 300
        canvas.delete('all')
        canvas.create_text(w//2, h//2, text=text,
                           fill='#B2BEC3', font=('Helvetica', 12))

    def _setstatus(self, msg: str):
        self.status_var.set(msg)

    def _set_state(self, state: AppState):
        self._state = state
        self._update_buttons()

    def _update_buttons(self):
        s = self._state
        dc_done = self._dc.is_done()

        self._next_btn.config(
            state='normal' if self._dc.phase() == PH_OBSTACLES else 'disabled')
        self._conv_btn.config(
            state='normal' if dc_done else 'disabled')
        self._gen_btn.config(
            state='normal' if s in (AppState.MAP_SAVED,
                                    AppState.PATH_READY,
                                    AppState.PUBLISHED) else 'disabled')
        self._pub_btn.config(
            state='normal' if (s in (AppState.PATH_READY, AppState.PUBLISHED)
                               and ROS2_OK) else 'disabled')

    def _on_dc_state(self):
        """Called by DrawCanvas on every phase change."""
        self._step_lbl.config(text=self._dc.hint())
        self._update_buttons()
        self._rot_lbl.config(text=f'{self._dc.rotation_deg()}°')

    # ── Image selection ───────────────────────────────────────────────────

    def _select_image(self):
        try:
            alt = float(self.altitude.get())
            fov = float(self.fov.get())
        except ValueError:
            messagebox.showerror('Invalid input',
                                 'Altitude and H-FOV must be numbers.')
            return

        dlg  = ImagePickerDialog(self.root, start_dir=self._last_dir)
        path = dlg.result
        if not path:
            return

        self._last_dir = str(Path(path).parent)
        self._lpath.config(text=Path(path).name, fg='#2C3E50')

        # Reset result tabs (clear results 1-3, keep drone tab 0)
        self._tabs.clear_results()
        self._set_state(AppState.IDLE)
        self._selected = self._cfg = self._cells = None

        # Tab 0: drone image
        if PIL_OK:
            try:
                self._drone_pil = Image.open(path)
                self._tabs.set_image(0, self._drone_pil, switch=True)
            except Exception as e:
                pass

        # Right DrawCanvas
        try:
            self._dc.load(path, alt, fov, int(self.n_robots.get()))
            self._rot_lbl.config(text='0°')
            self._step_lbl.config(text=self._dc.hint())
            self._set_state(AppState.DRAWING)
            self._setstatus(
                f'Loaded: {Path(path).name}  |  '
                f'scale: {self._dc._img_scale * 100:.3f} cm/px  |  '
                f'ground: {self._dc._img_orig.size[0] * self._dc._img_scale:.1f} x '
                f'{self._dc._img_orig.size[1] * self._dc._img_scale:.1f} m')
        except Exception as e:
            messagebox.showerror('Load error', str(e))

    def _redisplay_drone(self):
        if not PIL_OK or self._drone_pil is None:
            return
        self._tabs.set_image(0, self._drone_pil, switch=False)

    # ── Rotation ──────────────────────────────────────────────────────────

    def _rotate_cw(self):
        if self._dc._img_orig is None:
            return
        self._dc.rotate_cw()
        self._rot_lbl.config(text=f'{self._dc.rotation_deg()}°')
        # Sync left preview
        if self._dc._working() is not None and PIL_OK:
            self._drone_pil = self._dc._working()
            self._redisplay_drone()
        self._setstatus(f'Rotated CW → {self._dc.rotation_deg()}°')

    def _rotate_ccw(self):
        if self._dc._img_orig is None:
            return
        self._dc.rotate_ccw()
        self._rot_lbl.config(text=f'{self._dc.rotation_deg()}°')
        if self._dc._working() is not None and PIL_OK:
            self._drone_pil = self._dc._working()
            self._redisplay_drone()
        self._setstatus(f'Rotated CCW → {self._dc.rotation_deg()}°')

    # ── Output path ───────────────────────────────────────────────────────

    def _pick_output(self):
        from tkinter import filedialog
        path = filedialog.asksaveasfilename(
            title='Save map.yaml as',
            defaultextension='.yaml',
            initialfile='map.yaml',
            filetypes=[('YAML', '*.yaml *.yml'), ('All files', '*.*')],
        )
        if path:
            self.map_output.set(path)

    # ── Convert ───────────────────────────────────────────────────────────

    def _convert(self):
        data = self._dc.map_data()
        if data is None:
            messagebox.showwarning('Not complete', 'Drawing steps not finished.')
            return

        out = self.map_output.get()
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out, 'w', encoding='utf-8') as f:
            yaml.dump(data, f, default_flow_style=False,
                      sort_keys=False, allow_unicode=True)

        self._setstatus(f'map.yaml saved: {out}')
        self._step_lbl.config(text='Map saved — press [Generate Paths]')

        # Tab 1: Draw Map snapshot
        snap = self._dc.get_draw_snapshot()
        if snap:
            self._tabs.set_image(1, snap, switch=False)

        # Tab 2: Digital Map render
        try:
            self._dc.update_idletasks()
            cw = max(self._tabs.winfo_width(),  400)
            ch = max(self._tabs.winfo_height(), 250)
            map_img = render_map(out, cw, ch)
            self._tabs.set_image(2, map_img)
        except Exception as e:
            self._setstatus(f'map.yaml saved — preview error: {e}')

        self._set_state(AppState.MAP_SAVED)

    # ── Generate Paths ────────────────────────────────────────────────────

    def _generate_paths(self):
        out = self.map_output.get()
        if not os.path.exists(out):
            messagebox.showerror('No map', f'map.yaml not found:\n{out}')
            return

        self._set_state(AppState.GENERATING)
        self._gen_btn.config(state='disabled', text='Generating...')
        self._step_lbl.config(text='Generating paths — please wait...')
        self._tabs.clear_paths()

        t = threading.Thread(target=self._worker_generate,
                             args=(out,), daemon=True)
        t.start()

    def _worker_generate(self, map_path: str):
        try:
            cfg       = MapConfig(map_path)
            path_sets = build_path_set(cfg)
            validate_path_set(path_sets, cfg)
            selected  = select_paths(path_sets, starts=cfg.starts, cfg=cfg)
            cells     = _build_map_cells(cfg)
            self._queue.put(('paths_ok', cfg, selected, cells, map_path))
        except Exception as e:
            self._queue.put(('paths_err', str(e)))

    # ── Publish ───────────────────────────────────────────────────────────

    def _publish(self):
        if not ROS2_OK or self._selected is None:
            return
        messagebox.showinfo(
            'Publishing',
            'Publishing GlobalPathWaypoints (TRANSIENT_LOCAL).\n'
            'Keep this window open — closing it stops the publisher.')
        global ros2_mgr
        if ros2_mgr is None:
            ros2_mgr = ROS2Manager()
        ros2_mgr.publish(
            self._selected,
            on_done=lambda s: self._queue.put(('pub_ok', s)),
        )

    # ── Load existing map ─────────────────────────────────────────────────

    def _load_existing_map(self, map_path: str):
        """Called on startup if map.yaml already exists."""
        try:
            self._dc.update_idletasks()
            self._tabs.update_idletasks()
            cw = max(self._tabs.winfo_width(),  400)
            ch = max(self._tabs.winfo_height(), 250)
            map_img = render_map(map_path, cw, ch)
            self._tabs.set_image(2, map_img)
            self._set_state(AppState.MAP_SAVED)
            self._step_lbl.config(
                text='Existing map.yaml loaded — press [Generate Paths] or start fresh')
            self._setstatus(f'Loaded existing map: {map_path}')
        except Exception as e:
            self._setstatus(f'Could not load existing map.yaml: {e}')

    # ── Queue polling ─────────────────────────────────────────────────────

    def _start_polling(self):
        self.root.after(self.POLL_MS, self._poll)

    def _poll(self):
        try:
            while True:
                msg = self._queue.get_nowait()
                self._handle_msg(msg)
        except queue.Empty:
            pass
        self.root.after(self.POLL_MS, self._poll)

    def _handle_msg(self, msg):
        kind = msg[0]

        if kind == 'paths_ok':
            _, cfg, selected, cells, map_path = msg
            self._cfg      = cfg
            self._selected = selected
            self._cells    = cells
            self._gen_btn.config(text='Generate Paths')
            self._set_state(AppState.PATH_READY)
            n_total = sum(len(p.waypoints) for p in selected.values())
            self._setstatus(
                f'Paths ready  |  {len(selected)} robots  |  '
                f'{n_total} total waypoints  |  grid {cfg.grid_step:.1f} m')
            self._step_lbl.config(
                text=f'Paths generated  —  press [Publish] to send'
                     + ('' if ROS2_OK else '  (ROS2 unavailable)'))

            # Render Path Map tab
            try:
                self._tabs.update_idletasks()
                cw = max(self._tabs.winfo_width(),  400)
                ch = max(self._tabs.winfo_height(), 250)
                path_img = render_paths(map_path, selected, cells, cfg, cw, ch)
                self._tabs.set_image(3, path_img)
            except Exception as e:
                self._setstatus(f'Paths ready — render error: {e}')

        elif kind == 'paths_err':
            self._gen_btn.config(text='Generate Paths')
            self._set_state(AppState.MAP_SAVED)
            messagebox.showerror('Path generation failed', msg[1])

        elif kind == 'pub_ok':
            self._set_state(AppState.PUBLISHED)
            self._setstatus(f'Published:\n{msg[1]}')
            self._step_lbl.config(text='Published — keep window open (TRANSIENT_LOCAL)')


# ═══════════════════════════════════════════════════════════════════════════════
# Entry point
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    if not PIL_OK:
        print('[ERROR] Pillow is required.')
        print('        pip install Pillow --break-system-packages')
        return

    import argparse
    parser = argparse.ArgumentParser(description='SAR Planner')
    parser.add_argument('--map', default=DEFAULT_MAP_PATH,
                        help='Path to existing map.yaml (optional)')
    args, _ = parser.parse_known_args()

    root = tk.Tk()
    app  = MainWindow(root, map_path=args.map)

    def on_close():
        global ros2_mgr
        if ros2_mgr is not None:
            try:
                ros2_mgr.shutdown()
            except Exception:
                pass
        elif ROS2_OK:
            try:
                import rclpy as _r
                _r.shutdown()
            except Exception:
                pass
        root.destroy()

    root.protocol('WM_DELETE_WINDOW', on_close)
    root.mainloop()


if __name__ == '__main__':
    main()
