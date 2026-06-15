#!/usr/bin/env python3
"""menuconfig - wraps kconfiglib's full-featured menuconfig (mconf-compatible)"""
import os, sys, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KCONFIG = os.path.join(ROOT, "Kconfig")
DOTCONFIG = os.path.join(ROOT, ".config")

# 找到 kconfiglib 的 menuconfig.py
kconfiglib_menuconfig = None
for p in sys.path:
    candidate = os.path.join(p, "menuconfig.py")
    if os.path.exists(candidate) and "site-packages" in candidate:
        kconfiglib_menuconfig = candidate
        break

if not kconfiglib_menuconfig:
    print("ERROR: kconfiglib menuconfig.py not found. Install: pip3 install kconfiglib")
    sys.exit(1)

os.chdir(ROOT)
ret = subprocess.call([sys.executable, kconfiglib_menuconfig, KCONFIG],
                      env={**os.environ, "MENUCONFIG_STYLE": "aquatic"})

if ret == 0:
    subprocess.call([sys.executable, "scripts/kconfig.py", "genconfig"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("  autoconf.h updated")
