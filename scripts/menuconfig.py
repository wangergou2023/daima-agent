#!/usr/bin/env python3
"""menuconfig - 内核风格 ncurses 配置界面"""

import curses, os
from kconfiglib import Kconfig

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KCONFIG = os.path.join(ROOT, "Kconfig")
DOTCONFIG = os.path.join(ROOT, ".config")

def load_config():
    k = Kconfig(KCONFIG)
    if os.path.exists(DOTCONFIG):
        k.load_config(DOTCONFIG, replace=False)
    return k, sorted(k.defined_syms, key=lambda s: s.name)

def save(k):
    k.write_config(DOTCONFIG)
    os.system(f"cd {ROOT} && python3 scripts/kconfig.py genconfig > /dev/null 2>&1")

def draw(stdscr, syms, cursor, msg):
    stdscr.clear()
    h, w = stdscr.getmaxyx()
    stdscr.addstr(0, 2, "daima-agent Configuration", curses.A_BOLD)
    stdscr.addstr(1, 2, "\u2500" * (w - 4))
    visible = h - 5
    start = max(0, cursor - visible // 2)
    end = min(len(syms), start + visible)
    for i in range(start, end):
        y = i - start + 3
        s = syms[i]
        mark = "[*]" if s.str_value == "y" else "[ ]"
        prompt = ""
        for n in s.nodes:
            if n.prompt: prompt = str(n.prompt[0]); break
        attr = curses.A_REVERSE if i == cursor else curses.A_NORMAL
        stdscr.addstr(y, 2, f" {mark} {s.name:<35} {prompt[:w-42]}", attr)
    stdscr.addstr(h - 2, 2, msg)
    stdscr.addstr(h - 1, 2, " \u2191\u2193:nav  Space:toggle  t:all  s:save  q:quit")
    stdscr.refresh()

def main(stdscr):
    curses.curs_set(0)
    k, syms = load_config()
    cursor, msg = 0, ""
    while True:
        draw(stdscr, syms, cursor, msg)
        key = stdscr.getch()
        msg = ""
        if key in (ord('q'), 27): break
        elif key in (ord('j'), curses.KEY_DOWN): cursor = min(cursor + 1, len(syms) - 1)
        elif key in (ord('k'), curses.KEY_UP): cursor = max(cursor - 1, 0)
        elif key in (ord(' '), 10):
            s = syms[cursor]
            s.set_value("n" if s.str_value == "y" else "y")
            msg = f"  {s.name} = {s.str_value}"
        elif key == ord('t'):
            all_on = all(s.str_value == 'y' for s in syms)
            for s in syms: s.set_value("n" if all_on else "y")
            msg = "  toggle-all"
        elif key == ord('s'):
            save(k)
            msg = "  [OK] .config + autoconf.h saved"

    curses.endwin()
    print("  .config saved")

curses.wrapper(main)
