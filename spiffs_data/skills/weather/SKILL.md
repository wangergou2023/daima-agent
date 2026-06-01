---
name: 天气
description: 通过 weather 工具获取 UAPI 实时天气、未来预报和逐小时天气（无需 API Key）。
---

# 天气

通过 weather 工具获取当前天气、未来几天预报或逐小时天气摘要。

## 何时使用
当用户询问天气、温度、降雨、预报等。

## 使用步骤
1. 使用 get_current_time 获取当前日期
2. 优先直接使用用户给的城市名；中文和英文一般都可用
3. 当前天气可调用 weather：{"location":"北京","type":"current"}
4. 多日预报可调用 weather：{"location":"北京","type":"forecast","days":3}
5. 逐小时天气可调用 weather：{"location":"北京","type":"hourly"}
6. 如果用户没有给地点，可以省略 location，让服务按 IP 自动定位

## 注意事项
- 如用户给了明确城市，优先按用户城市查询，不要默认用 IP 定位
- 如果用户明确问“未来几天”，要显式使用 forecast/days
- 如果用户问“今晚几点下雨”或“未来几个小时”，优先用 hourly
- 如果用户给的是行政区编码，也可用 adcode 查询

## 示例
用户："东京今天天气怎么样？"
→ get_current_time
→ weather {"location":"东京","type":"current"}
→ "东京：多云 8°C，体感 6°C，风速 10 km/h。"

用户："北京未来三天天气如何？"
→ get_current_time
→ weather {"location":"Beijing","type":"forecast","days":3}
→ "北京：晴 12°C，体感 10°C，风速 8 km/h。"

用户："我这边现在天气如何？"
→ get_current_time
→ weather {}
→ "当前位置：多云 20°C，湿度 24%，西南风 3级。"
