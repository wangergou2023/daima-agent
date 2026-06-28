# Changelog

## 0.1.0-alpha - 2026-06-28

初版发布，重点是把 `daima-agent` 推到一个可真实运行、可安装、可观察的多 subagent alpha 状态。

### Added

- 真实多 subagent 并发编排基础能力
- Web 端多 subagent 基础展示：
  - tabs
  - detail
  - timeline
  - blocker
- `session_events` 双游标回放：
  - `after_seq`
  - `after_visible_revision`
- 基于 `~/.agent-data/spiffs_data/workspace/opencode` 的自检仓库准备与运行日志探测
- macOS 兼容的开发/安装启动链路

### Changed

- session-first restore / replay 基础链路已经可用
- child session / coordinator projection 已开始收敛为统一展示来源
- parent wake 已具备基础 defer / retry / dedupe / consumed-watermark 语义

### Known Limitations

- 还没有收敛成 `opencode sessions.events(after)` 那种单一 durable event stream
- 部分展示链路仍在继续从旧 `task.output` 收敛到 `child_session` 作为唯一真相源
- `parent wake` 语义仍未完全追平 `oh-my-openagent`
