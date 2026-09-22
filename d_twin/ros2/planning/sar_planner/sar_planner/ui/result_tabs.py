"""
ui/result_tabs.py

Left-panel tab viewer with four tabs:
  0 - Drone Image : source aerial photo (set on image load)
  1 - Draw Map    : annotated drawing snapshot (set on Convert)
  2 - Digital Map : map.yaml vector render (set on Convert)
  3 - Path Map    : coverage path render (set on Generate Paths)

Each tab stores its PIL image independently.
Tab 0 never shows a checkmark (it is the source, not a result).
"""

import tkinter as tk
from typing import Optional

try:
    from PIL import Image, ImageTk
    PIL_OK = True
except ImportError:
    PIL_OK = False

BG_PANEL   = '#F5F6FA'
BG_CANVAS  = '#DFE6E9'
BORDER     = '#CCD6DD'
TAB_ACTIVE = '#2C3E50'
TAB_DONE   = '#27AE60'
TAB_IDLE   = '#95A5A6'
FG_WHITE   = '#FFFFFF'
FG_LIGHT   = '#ECEFF1'


class ResultTabs(tk.Frame):
    """Four-tab image viewer for the left panel."""

    TAB_NAMES = ['Drone Image', 'Draw Map', 'Digital Map', 'Path Map']
    # Indices of "result" tabs that show a checkmark when populated
    RESULT_TABS = {1, 2, 3}

    def __init__(self, parent, **kw):
        super().__init__(parent, bg=BG_PANEL, **kw)

        self._images  = [None, None, None, None]   # Optional PIL Image per tab
        self._tk_imgs = [None, None, None, None]   # Optional ImageTk per tab
        self._active  = 0

        self._build()

    # ── Build ──────────────────────────────────────────────────────────────

    def _build(self):
        # Tab bar
        bar = tk.Frame(self, bg=BG_PANEL)
        bar.pack(fill='x')

        self._btns = []
        for i, name in enumerate(self.TAB_NAMES):
            btn = tk.Button(
                bar,
                text=name,
                font=('Helvetica', 9),
                relief='flat', padx=12, pady=6,
                cursor='hand2',
                command=lambda idx=i: self._switch(idx),
            )
            btn.pack(side='left', padx=(0, 2))
            self._btns.append(btn)

        # Content canvas
        self._canvas = tk.Canvas(self, bg=BG_CANVAS,
                                 highlightthickness=1,
                                 highlightbackground=BORDER)
        self._canvas.pack(fill='both', expand=True)
        self._canvas.bind('<Configure>', lambda e: self._redisplay())

        self._update_styles()
        self._show_placeholder()

    # ── Public API ────────────────────────────────────────────────────────

    def set_image(self, tab_idx: int, pil_img: 'Image.Image',
                  switch: bool = True):
        """Store a PIL Image for the given tab, optionally switching to it."""
        if not 0 <= tab_idx < len(self._images):
            return
        self._images[tab_idx]  = pil_img
        self._tk_imgs[tab_idx] = None        # invalidate cached ImageTk
        self._update_styles()
        if switch:
            self._switch(tab_idx)
        elif tab_idx == self._active:
            # Already on this tab — redisplay in place
            self._redisplay()

    def clear_results(self):
        """Clear result tabs 1-3 (called when new image is selected)."""
        for i in self.RESULT_TABS:
            self._images[i]  = None
            self._tk_imgs[i] = None
        self._update_styles()
        if self._active in self.RESULT_TABS:
            self._switch(0)

    def clear_paths(self):
        """Clear only the Path Map tab (tab 3)."""
        self._images[3]  = None
        self._tk_imgs[3] = None
        self._update_styles()
        if self._active == 3:
            self._show_placeholder()

    def clear_all(self):
        """Clear all tabs (full reset)."""
        self._images  = [None, None, None, None]
        self._tk_imgs = [None, None, None, None]
        self._update_styles()
        self._switch(0)

    def active_tab(self) -> int:
        return self._active

    # ── Internal ──────────────────────────────────────────────────────────

    def _switch(self, idx: int):
        self._active = idx
        self._update_styles()
        if self._images[idx] is not None:
            self._redisplay()
        else:
            self._show_placeholder()

    def _update_styles(self):
        for i, btn in enumerate(self._btns):
            is_active   = (i == self._active)
            has_content = self._images[i] is not None
            is_result   = (i in self.RESULT_TABS)

            if is_active:
                bg = TAB_ACTIVE; fg = FG_WHITE
            elif has_content and is_result:
                bg = TAB_DONE;   fg = FG_WHITE
            else:
                bg = TAB_IDLE;   fg = FG_LIGHT

            label = self.TAB_NAMES[i]
            if has_content and is_result and not is_active:
                label += '  \u2713'

            btn.config(bg=bg, fg=fg, text=label)

    def _redisplay(self):
        img = self._images[self._active]
        if img is None:
            self._show_placeholder()
            return
        if not PIL_OK:
            return
        self._canvas.update_idletasks()
        cw = max(self._canvas.winfo_width(),  100)
        ch = max(self._canvas.winfo_height(), 100)
        disp = img.copy()
        disp.thumbnail((cw - 4, ch - 4), Image.LANCZOS)
        iw, ih = disp.size
        self._tk_imgs[self._active] = ImageTk.PhotoImage(disp)
        self._canvas.delete('all')
        self._canvas.create_image(cw // 2, ch // 2,
                                   anchor='center',
                                   image=self._tk_imgs[self._active])

    def _show_placeholder(self):
        self._canvas.update_idletasks()
        w = self._canvas.winfo_width()  or 400
        h = self._canvas.winfo_height() or 200
        self._canvas.delete('all')
        name = self.TAB_NAMES[self._active]
        texts = {
            0: 'Select a drone aerial photo',
            1: 'Complete drawing and press Convert',
            2: 'Complete drawing and press Convert',
            3: 'Press Generate Paths',
        }
        self._canvas.create_text(
            w // 2, h // 2,
            text=texts.get(self._active, f'No {name} yet'),
            fill='#B2BEC3',
            font=('Helvetica', 11))
