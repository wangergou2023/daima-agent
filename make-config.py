#!/usr/bin/env python3
"""轻量 make config - 读取 daima/Kconfig 生成/展示编译开关"""
import sys, os

KCONFIG = os.path.join(os.path.dirname(__file__), "daima/Kconfig")
HEADER  = os.path.join(os.path.dirname(__file__), "daima/include/autoconf.h")

def parse_kconfig():
    configs = {}
    current = None
    with open(KCONFIG) as f:
        for line in f:
            line = line.strip()
            if line.startswith("config "):
                current = line.split()[1]
                configs[current] = {"bool": False, "default": "y", "help": ""}
            elif line.startswith("bool ") and current:
                configs[current]["bool"] = True
            elif line.startswith("default ") and current:
                configs[current]["default"] = line.split()[1]
            elif line.startswith("help") and current:
                pass  # help follows
            elif current:
                configs[current]["help"] += line + "\n"
    return configs

def cmd_list():
    configs = parse_kconfig()
    for name, cfg in sorted(configs.items()):
        default = "on" if cfg["default"] == "y" else "off"
        print(f"  [{default:>3}] {name:<35} {cfg['help'].split(chr(10))[0]}")

def cmd_show():
    configs = parse_kconfig()
    for name, cfg in sorted(configs.items()):
        desc = cfg['help'].strip().replace('\n', '\n    ')
        print(f"config {name}")
        print(f"  default: {cfg['default']}")
        print(f"  {desc}")
        print()

def cmd_gen():
    configs = parse_kconfig()
    lines = []
    lines.append("/* 自动生成, 来自 daima/Kconfig */")
    lines.append("#pragma once")
    lines.append("")
    for name, cfg in sorted(configs.items()):
        val = "1" if cfg["default"] == "y" else "0"
        lines.append(f"#ifndef {name}")
        lines.append(f"#define {name} {val}")
        lines.append(f"#endif")
        lines.append("")
    
    with open(HEADER, "w") as f:
        f.write("\n".join(lines))
    print(f"[OK] {HEADER}")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    {"list": cmd_list, "show": cmd_show, "gen": cmd_gen}.get(cmd, cmd_list)()
