#!/usr/bin/env python3
"""Kconfig 工具 - 支持 menuconfig / defconfig / genconfig"""

import sys, os, re

ROOT = os.path.dirname(os.path.abspath(__file__))
KCONFIG = os.path.join(ROOT, "daima/Kconfig")
DOTCONFIG = os.path.join(ROOT, ".config")
HEADER = os.path.join(ROOT, "daima/core/config.h")
DEFCONFIG = os.path.join(ROOT, "daima/defconfig")

def cmd_menuconfig():
    import kconfiglib
    from kconfiglib import Kconfig
    k = Kconfig(KCONFIG)
    if os.path.exists(DOTCONFIG):
        k.load_config(DOTCONFIG, replace=False)
    kconfiglib.standard_kconfig.warn_override_no_value = False
    k.warn = False
    # disable verbose warnings
    from kconfiglib import standard_kconfig
    kconfiglib.standard_kconfig.__dict__["_conf"] = lambda *a, **kw: None

    # Save on exit
    try:
        kconfiglib.menuconfig(k)
    finally:
        k.write_config(DOTCONFIG)
        cmd_genconfig()
        print(f"[OK] .config saved, {HEADER} updated")

def cmd_genconfig():
    from kconfiglib import Kconfig
    k = Kconfig(KCONFIG)
    if os.path.exists(DOTCONFIG):
        k.load_config(DOTCONFIG, replace=False)

    if not os.path.exists(HEADER):
        print(f"[ERROR] {HEADER} not found, need base config.h")
        return

    text = open(HEADER).read()
    
    for sym in sorted(k.defined_syms, key=lambda s: s.name):
        name = sym.name
        val = "1" if sym.str_value == "y" else "0"
        pattern = rf'#ifndef {name}\n#define {name} \d+\n#endif'
        repl = f'#ifndef {name}\n#define {name} {val}\n#endif'
        if re.search(pattern, text):
            text = re.sub(pattern, repl, text)
    
    with open(HEADER, "w") as f:
        f.write(text)
    print(f"[OK] {HEADER}")

def cmd_defconfig():
    from kconfiglib import Kconfig
    k = Kconfig(KCONFIG)
    if os.path.exists(DEFCONFIG):
        k.load_config(DEFCONFIG, replace=False)
    else:
        k.load_config()  # use Kconfig defaults
    k.write_config(DOTCONFIG)
    cmd_genconfig()
    print(f"[OK] defconfig applied")

def cmd_list():
    from kconfiglib import Kconfig
    k = Kconfig(KCONFIG)
    for sym in sorted(k.defined_syms, key=lambda s: s.name):
        on = "on" if sym.str_value == "y" else "off"
        print(f"  [{on:>3}] {sym.name:<35} {k.syms.get(sym.name)}")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    {"menuconfig": cmd_menuconfig, "defconfig": cmd_defconfig,
     "genconfig": cmd_genconfig, "list": cmd_list}.get(cmd, cmd_list)()
