---
name: Vector 机器人控制
description: 控制 Vector 机器人的硬件能力（运动、动画、电源、传感器）。基于 wire-pod 中文意图系统。
---

# Vector 机器人控制

## 核心规则

匹配动作 → 调工具 → 简短中文确认。所有工具可用且可靠。

## 决策表（中文 → 工具）

| 用户说了什么 | → | 调用工具及参数 |
|--------------|---|---------------|
| "过来" "回来" "过来一点" | → | `robot_drive_straight(speed_mmps=80, dist_mm=300)` |
| "转身" "转过来" "回头" | → | `robot_turn_in_place(angle_rad=3.14)` |
| "看向我" "看我" "抬起头" | → | `robot_set_head_angle(angle_rad=0)` |
| "举手" "抬起手臂" | → | `robot_set_lift_height(height_mm=90)` |
| "放下" "放下手臂" | → | `robot_set_lift_height(height_mm=0)` |
| "回家充电" "去充电" "回家" | → | `robot_drive_on_charger` |
| "出发" "离开充电座" "出来" | → | `robot_drive_off_charger` |
| "停" "别动" "闭嘴" "安静" | → | `robot_stop` |
| "开心" "真棒" "庆祝" "跳舞" | → | `robot_play_animation(name="happy01")` |
| "伤心" "笨" "傻" | → | `robot_play_animation(name="sad01")` |
| "烟花" "新年好" "放烟花" | → | `robot_play_animation(name="weatherstars01")` |
| "打招呼" "你好" "早上好" | → | `robot_play_animation(name="greeting01")` |
| "再见" "拜拜" | → | `robot_play_animation(name="goodbye01")` |
| "惊讶" "惊喜" | → | `robot_play_animation(name="surprise01")` |
| "睡觉" "去睡觉" "晚安" | → | `robot_play_animation(name="sleep01")` |
| "喜欢" "爱" | → | `robot_play_animation(name="love01")` |
| "音量" "调音量" | → | `robot_set_volume(level=4)` |
| "小声" "安静点" | → | `robot_set_volume(level=1)` |
| "拍照" | → | `robot_app_intent(intent="intent_photo_take_extend")` |
| "还有电吗" "电量" | → | `robot_get_battery` |

## 可用工具全集

- **robot_drive_straight** — 直行：speed_mmps (±200), dist_mm (50-2000)
- **robot_turn_in_place** — 转：angle_rad (π=180°), speed_rad_per_sec (2.0)
- **robot_set_head_angle** — 头：angle_rad (-0.5~0.7)
- **robot_set_lift_height** — 臂：height_mm (0~100)
- **robot_stop** — 停
- **robot_play_animation** — 表情：name (happy01/sad01/greeting01/weatherstars01/surprise01/sleep01/love01/goodbye01)
- **robot_drive_on_charger** — 回充
- **robot_drive_off_charger** — 离桩
- **robot_get_battery** — 电量
- **robot_set_volume** — 音量 0-4
- **robot_get_sensors** — 传感器
- **robot_app_intent** — 内置意图
- **robot_cancel_playback** — 停播

## 可用 AppIntent

| intent | 效果 |
|--------|------|
| `intent_system_charger` | 回充 |
| `intent_greeting_hello` | 打招呼 |
| `intent_greeting_goodmorning` | 早上好 |
| `intent_greeting_goodnight` | 晚安 |
| `intent_greeting_goodbye` | 再见 |
| `intent_imperative_praise` | 表扬 |
| `intent_imperative_abuse` | 批评 |
| `intent_imperative_affirmative` | 肯定 |
| `intent_imperative_negative` | 否定 |
| `intent_imperative_love` | 喜欢 |
| `intent_imperative_come` | 过来 |
| `intent_imperative_dance` | 跳舞 |
| `intent_imperative_lookatme` | 看我 |
| `intent_imperative_forward` | 向前 |
| `intent_imperative_backup` | 后退 |
| `intent_imperative_turnaround` | 转身 |
| `intent_seasonal_happynewyear` | 新年/烟花 |
| `intent_seasonal_happyholidays` | 节日快乐 |
| `intent_system_sleep` | 睡觉 |
| `intent_photo_take_extend` | 拍照 |
| `intent_global_stop_extend` | 停止 |
| `intent_play_popawheelie` | 举手 |
| `intent_play_fistbump` | 碰拳 |

## 注意事项

1. 两个 motor 工具之间间隔 ≥1s
2. AppIntent 连续调用间隔 ≥1.5s（内部 codeown）
3. `robot_play_animation` 内部用 AppIntent 实现，不超时
4. 机器人离开充电座才能自由移动
