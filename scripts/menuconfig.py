#!/usr/bin/env python3
"""简易 menuconfig - 文本菜单, 不需要 ncurses"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KCONFIG = os.path.join(ROOT, "daima", "Kconfig")
DOTCONFIG = os.path.join(ROOT, ".config")

from kconfiglib import Kconfig

def show_menu():
    k = Kconfig(KCONFIG)
    if os.path.exists(DOTCONFIG):
        k.load_config(DOTCONFIG, replace=False)
    
    syms = sorted(k.defined_syms, key=lambda s: s.name)
    
    while True:
        print("\033[2J\033[H")  # clear screen
        print("  daima-agent Configuration")
        print("  " + "=" * 50)
        print()
        
        for i, sym in enumerate(syms):
            val = sym.str_value
            mark = "[*]" if val == "y" else "[ ]"
            # Get prompt from the menu node
            prompt = ""
            for node in sym.nodes:
                if node.prompt:
                    prompt = str(node.prompt[0])
                    break
            print(f"  {i:2d}. {mark} {sym.name:<32} {prompt}")
        
        print()
        print("  t. toggle-all    s. save+exit    q. quit (no save)")
        print("  Enter number to toggle")
        print()
        cmd = input("  > ").strip()
        
        if cmd == 'q':
            break
        elif cmd == 's':
            k.write_config(DOTCONFIG)
            print(f"  [OK] {DOTCONFIG}")
            os.system(f"cd {ROOT} && python3 kconfig.py genconfig")
            break
        elif cmd == 't':
            all_on = all(s.str_value == 'y' for s in syms)
            new_val = "n" if all_on else "y"
            for s in syms:
                s.set_value(new_val)
        elif cmd.isdigit():
            idx = int(cmd)
            if 0 <= idx < len(syms):
                sym = syms[idx]
                new_val = "n" if sym.str_value == "y" else "y"
                sym.set_value(new_val)

if __name__ == "__main__":
    show_menu()
