"""
ui/image_picker.py

Custom image file picker dialog.
Vertical scroll + mousewheel + thumbnail preview.
"""

import os
import tkinter as tk
from pathlib import Path
from tkinter import ttk

try:
    from PIL import Image, ImageTk
    PIL_OK = True
except ImportError:
    PIL_OK = False

IMG_EXTS = {'.jpg', '.jpeg', '.png', '.bmp', '.tiff', '.tif', '.webp'}

BG_HEADER = '#2C3E50'
BG_PANEL  = '#F5F6FA'
BG_MAIN   = '#ECEFF1'
FG_HEADER = '#FFFFFF'
BORDER    = '#CCD6DD'


class ImagePickerDialog(tk.Toplevel):
    """
    Custom file picker with:
    - Vertical scrolling file list + mouse wheel
    - Thumbnail preview panel
    - Quick-access shortcuts (Home, Desktop, Pictures, Downloads)
    """

    def __init__(self, parent, start_dir: str = None):
        super().__init__(parent)
        self.result = None
        self._thumb = None
        self._items = []   # list of (display_name, Path, is_dir)

        self.cwd = Path(start_dir or Path.home())

        self.title('Select Drone Image')
        self.configure(bg=BG_MAIN)
        self.resizable(True, True)
        self.minsize(760, 500)
        self.geometry('860x560')

        self._build()
        self._cd(self.cwd)

        self.transient(parent)
        self.grab_set()
        self.focus_set()
        self.wait_window()

    # ── Build ──────────────────────────────────────────────────────────────

    def _build(self):
        # Header / nav bar
        nav = tk.Frame(self, bg=BG_HEADER, height=44)
        nav.pack(fill='x'); nav.pack_propagate(False)

        tk.Button(nav, text='↑  Up',
                  bg='#34495E', fg=FG_HEADER,
                  font=('Helvetica', 9), relief='flat',
                  padx=10, pady=4, cursor='hand2',
                  command=self._up).pack(side='left', padx=(8, 4), pady=6)

        self._path_var = tk.StringVar()
        pe = tk.Entry(nav, textvariable=self._path_var,
                      bg='#34495E', fg='#ECF0F1',
                      font=('Helvetica', 9), relief='flat',
                      insertbackground='white', bd=4)
        pe.pack(side='left', fill='x', expand=True, pady=8, padx=(0, 8))
        pe.bind('<Return>', lambda e: self._cd(Path(self._path_var.get())))

        # Quick-access bar
        qa = tk.Frame(self, bg='#ECF0F1', height=34)
        qa.pack(fill='x'); qa.pack_propagate(False)

        tk.Label(qa, text='Quick:', bg='#ECF0F1', fg='#7F8C8D',
                 font=('Helvetica', 8)).pack(side='left', padx=(10, 4), pady=6)

        shortcuts = [
            ('Home',      Path.home()),
            ('Desktop',   Path.home() / 'Desktop'),
            ('Pictures',  Path.home() / 'Pictures'),
            ('Downloads', Path.home() / 'Downloads'),
        ]
        for label, path in shortcuts:
            if path.exists():
                tk.Button(qa, text=label,
                          bg='#D5DBDB', fg='#2C3E50',
                          font=('Helvetica', 8), relief='flat',
                          padx=8, pady=2, cursor='hand2',
                          command=lambda p=path: self._cd(p),
                          ).pack(side='left', padx=2, pady=4)

        # Content area
        content = tk.Frame(self, bg=BG_MAIN)
        content.pack(fill='both', expand=True)

        # File list (left ~60%)
        lf = tk.Frame(content, bg=BG_PANEL, bd=0)
        lf.pack(side='left', fill='both', expand=True)

        self._lb = tk.Listbox(
            lf,
            bg=BG_PANEL, fg='#2C3E50',
            selectbackground='#2980B9', selectforeground='white',
            font=('Helvetica', 10),
            relief='flat', bd=0, highlightthickness=0,
            activestyle='none',
        )
        vsb = tk.Scrollbar(lf, orient='vertical', command=self._lb.yview)
        self._lb.config(yscrollcommand=vsb.set)
        vsb.pack(side='right', fill='y')
        self._lb.pack(fill='both', expand=True)

        # Mouse-wheel (Linux + Windows)
        for w in (self._lb, self, content, lf):
            w.bind('<Button-4>',   lambda e: self._lb.yview_scroll(-3, 'units'))
            w.bind('<Button-5>',   lambda e: self._lb.yview_scroll( 3, 'units'))
            w.bind('<MouseWheel>', lambda e: self._lb.yview_scroll(
                                       -1 * (e.delta // 120), 'units'))

        self._lb.bind('<Double-Button-1>', self._dbl)
        self._lb.bind('<<ListboxSelect>>', self._sel)
        self._lb.bind('<Return>',          self._dbl)

        # Divider
        tk.Frame(content, bg=BORDER, width=1).pack(side='left', fill='y')

        # Preview panel (right, fixed 260px)
        pv = tk.Frame(content, bg='#2C3E50', width=260)
        pv.pack(side='right', fill='y'); pv.pack_propagate(False)

        self._pvc = tk.Canvas(pv, bg='#2C3E50', highlightthickness=0)
        self._pvc.pack(fill='both', expand=True, padx=10, pady=(10, 4))

        self._pvl = tk.Label(pv, text='', bg='#2C3E50', fg='#BDC3C7',
                             font=('Helvetica', 8), justify='center')
        self._pvl.pack(pady=(0, 10))

        # Bottom bar
        bot = tk.Frame(self, bg='#E8EAED', height=52)
        bot.pack(fill='x', side='bottom'); bot.pack_propagate(False)

        self._open_btn = tk.Button(bot, text='Open',
                                   bg='#2980B9', fg='white',
                                   font=('Helvetica', 9, 'bold'),
                                   relief='flat', padx=18, pady=6,
                                   state='disabled', cursor='hand2',
                                   command=self._open)
        self._open_btn.pack(side='right', padx=10, pady=10)

        tk.Button(bot, text='Cancel',
                  bg='#95A5A6', fg='white',
                  font=('Helvetica', 9), relief='flat',
                  padx=12, pady=6, cursor='hand2',
                  command=self.destroy).pack(side='right', padx=4, pady=10)

        self._sel_lbl = tk.Label(bot, text='',
                                 bg='#E8EAED', fg='#2C3E50',
                                 font=('Helvetica', 9), anchor='w')
        self._sel_lbl.pack(side='left', padx=12, pady=10)

    # ── Navigation ────────────────────────────────────────────────────────

    def _cd(self, path):
        path = Path(path)
        if not path.is_dir():
            return
        self.cwd = path
        self._path_var.set(str(path))
        self._lb.delete(0, 'end')
        self._items = []

        if path != path.parent:
            self._items.append(('..', path.parent, True))
            self._lb.insert('end', '   📁   ..')

        try:
            entries = sorted(path.iterdir(),
                             key=lambda p: (not p.is_dir(), p.name.lower()))
        except PermissionError:
            entries = []

        for entry in entries:
            if entry.is_dir() and not entry.name.startswith('.'):
                self._items.append((entry.name, entry, True))
                self._lb.insert('end', f'   📁   {entry.name}')
            elif entry.suffix.lower() in IMG_EXTS:
                self._items.append((entry.name, entry, False))
                self._lb.insert('end', f'   🖼   {entry.name}')

        self._open_btn.config(state='disabled')
        self._sel_lbl.config(text='')
        self._pvc.delete('all')
        self._pvl.config(text='')

    def _up(self):
        self._cd(self.cwd.parent)

    # ── List interaction ──────────────────────────────────────────────────

    def _sel(self, _event=None):
        s = self._lb.curselection()
        if not s or s[0] >= len(self._items):
            return
        name, path, is_dir = self._items[s[0]]
        if is_dir:
            self._open_btn.config(state='disabled')
            return
        if path.is_file():
            self._open_btn.config(state='normal')
            self._sel_lbl.config(text=name)
            self._show_thumb(path)

    def _dbl(self, _event=None):
        s = self._lb.curselection()
        if not s or s[0] >= len(self._items):
            return
        name, path, is_dir = self._items[s[0]]
        if is_dir:
            self._cd(path)
        else:
            self._open()

    def _show_thumb(self, path):
        if not PIL_OK:
            return
        try:
            img = Image.open(path)
            ow, oh = img.size
            self._pvc.update_idletasks()
            cw = max(self._pvc.winfo_width(),  200)
            ch = max(self._pvc.winfo_height(), 160)
            thumb = img.copy()
            thumb.thumbnail((cw - 6, ch - 6), Image.LANCZOS)
            self._thumb = ImageTk.PhotoImage(thumb)
            self._pvc.delete('all')
            self._pvc.create_image(cw // 2, ch // 2,
                                    anchor='center', image=self._thumb)
            kb = path.stat().st_size // 1024
            self._pvl.config(text=f'{ow} × {oh} px\n{kb:,} KB')
        except Exception:
            pass

    def _open(self):
        s = self._lb.curselection()
        if not s or s[0] >= len(self._items):
            return
        name, path, is_dir = self._items[s[0]]
        if not is_dir and path.is_file():
            self.result = str(path)
            self.destroy()
