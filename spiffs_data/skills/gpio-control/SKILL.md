---
name: GPIO 控制
description: 通过 /sys/class/gpio 文件系统控制 Linux GPIO 引脚。当用户需要控制 LED、读取按钮、驱动继电器或操作任何 GPIO 设备时使用。
---

# GPIO 控制

## 决策树

```
用户想要什么？
├─ 点亮/熄灭 LED      → 导出 → 设输出 → 写 1/0
├─ 让 LED 闪烁         → 导出 → 设输出 → 循环写 1/0（用 shell 循环）
├─ 读取按钮/传感器     → 导出 → 设输入 → cat 读值
├─ 查当前引脚状态      → cat /sys/class/gpio/gpio<N>/value
├─ 释放引脚/恢复默认    → echo <N> > /sys/class/gpio/unexport
└─ 不知道引脚号         → 先问用户或查芯片数据手册
```

## 使用步骤

所有操作用 `terminal` 工具执行。

### 导出引脚
```bash
echo <gpio_num> > /sys/class/gpio/export
```

### 设方向
```bash
echo out > /sys/class/gpio/gpio<gpio_num>/direction   # 输出（控制 LED/继电器）
echo in  > /sys/class/gpio/gpio<gpio_num>/direction   # 输入（读取传感器/按钮）
```

### 读写值
```bash
echo 1 > /sys/class/gpio/gpio<gpio_num>/value   # 高电平 / 开
echo 0 > /sys/class/gpio/gpio<gpio_num>/value   # 低电平 / 关
cat /sys/class/gpio/gpio<gpio_num>/value         # 读当前值（0 或 1）
```

### 释放引脚
```bash
echo <gpio_num> > /sys/class/gpio/unexport
```

## 示例

**点亮 LED（GPIO 17）：**
```
terminal {"command":"echo 17 > /sys/class/gpio/export","timeout":120}
terminal {"command":"echo out > /sys/class/gpio/gpio17/direction","timeout":120}
terminal {"command":"echo 1 > /sys/class/gpio/gpio17/value","timeout":120}
```

**让 LED 闪烁 5 次（GPIO 17，间隔 0.5s）：**
```
terminal {"command":"echo 17 > /sys/class/gpio/export && echo out > /sys/class/gpio/gpio17/direction && for i in $(seq 1 5); do echo 1 > /sys/class/gpio/gpio17/value; sleep 0.5; echo 0 > /sys/class/gpio/gpio17/value; sleep 0.5; done","timeout":120}
```

**读按钮（GPIO 18）：**
```
terminal {"command":"echo 18 > /sys/class/gpio/export && echo in > /sys/class/gpio/gpio18/direction && cat /sys/class/gpio/gpio18/value","timeout":120}
```

## 注意事项
- GPIO 编号用 BCM 编码，不是物理引脚号
- 需要 root 时用 sudo；Web 终端支持交互输入密码
- 某些系统用 libgpiod（gpioset/gpioget）替代 sysfs，先 `which gpioset` 确认
- 操作前确认引脚没被其他驱动占用
