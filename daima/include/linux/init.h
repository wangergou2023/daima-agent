#pragma once

typedef int (*initcall_t)(void);

#define __init
#define __exit

#define pure_initcall(fn)     __initcall(fn, 0)
#define core_initcall(fn)     __initcall(fn, 1)
#define postcore_initcall(fn) __initcall(fn, 2)
#define arch_initcall(fn)     __initcall(fn, 3)
#define subsys_initcall(fn)   __initcall(fn, 4)
#define fs_initcall(fn)       __initcall(fn, 5)
#define device_initcall(fn)   __initcall(fn, 6)
#define late_initcall(fn)     __initcall(fn, 7)

#define __initcall(fn, level) \
    static initcall_t __initcall_##fn __attribute__((used)) \
        __attribute__((section(".initcall" #level ".init"))) = fn

struct daima_driver;
int daima_driver_probe(struct daima_driver *drv);
void do_basic_setup(void);
