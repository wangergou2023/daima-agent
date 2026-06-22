# extensions/ — 预留目录

`extensions/` 不承担默认回合主链。`extensions_init()` 会执行，但默认空装配。

## OVERVIEW

当前定位：

- 保留一个明确的空入口
- 默认构建不在这里放任何主链逻辑
- 不作为核心架构事实来源

## CURRENT STRUCTURE

```text
extensions/
├── ext_init.c            # 空装配入口
├── ext_init.h            # init 声明
└── Makefile              # 默认只编译 ext_init.o
```

## WHAT IS ACTIVE BY DEFAULT

`extensions/ext_init.c` 当前默认不注册任何模块，`extensions/Makefile` 只编译 `ext_init.o`。

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| 默认装配 | `ext_init.c` | 空装配 |
| 默认 interview | `kernel/turn_pipeline.c` | 不在这里 |
| 默认 Ralph | `kernel/turn_finish.c` + `kernel/ralph.c` | 不在这里 |

## GUIDANCE

- 读框架先看 `kernel/`
- 新功能直接落 `kernel/`
- 不要把默认行为重新塞回 `extensions/`
