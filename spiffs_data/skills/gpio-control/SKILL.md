---
name: GPIO 控制
description: 当用户需要控制 LED、读取按钮、驱动继电器或操作 Linux GPIO 设备时使用。
---

# GPIO 控制

通过 Linux `/sys/class/gpio` 文件系统读写 GPIO。所有操作都使用 `terminal` 执行。

## 何时使用

- 用户要点亮、熄灭或闪烁 LED。
- 用户要读取按钮、传感器或 GPIO 电平。
- 用户要驱动继电器或释放 GPIO 引脚。

## 使用步骤

1. 确认 GPIO 编号；不知道编号时先询问用户或查硬件资料。
2. 导出引脚：`echo <gpio_num> > /sys/class/gpio/export`。
3. 设置方向：输出用 `echo out > /sys/class/gpio/gpio<gpio_num>/direction`，输入用 `echo in > .../direction`。
4. 读写值：输出写 `1/0`，输入读取 `value`。
5. 完成后按需释放：`echo <gpio_num> > /sys/class/gpio/unexport`。

## 工具与路径

- 常用工具：`terminal`。
- GPIO 根路径：`/sys/class/gpio`。
- 若系统提供 `gpioset`/`gpioget`，可先用 `which gpioset` 判断是否改用 libgpiod。

## 输出要求

- 执行前说明将操作哪个 GPIO 和方向。
- 执行后说明当前状态或读取值。

## 注意事项

- GPIO 编号通常是 SoC 编号，不是物理引脚号。
- 需要 root 权限时用合适的提权方式。
- 操作前确认引脚未被其他驱动占用。
- 写错引脚可能影响硬件，编号不明确时不要猜。
