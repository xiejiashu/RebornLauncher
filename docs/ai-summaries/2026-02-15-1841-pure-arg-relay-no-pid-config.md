# AI Summary

## 1) User request (quote)
> 行

## 2) What was done
- 按你的确认，把 RebornLauncher 的接力链路改成“纯传参”，去掉 PID 落盘依赖。
- 现在迁移/自更新都通过明确参数传递清理目标：`--cleanup-pid`、`--cleanup-path`、`--stage`。
- 保留 `GamePath` 配置（安装目录），不再写/读 PID 配置。

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - 新增接力参数结构与解析：
    - `RelaunchArgs`
    - `ParseRelaunchArgsFromCommandLine()`
    - `BuildRelaunchArgsString()`
  - `HandleCleanupRelay(...)` 改为仅使用传参清理旧进程/旧文件，不读配置 PID。
  - `LaunchProcess(...)` 改为统一拼接并传递 `cleanup-pid/path/stage` 参数。
  - 迁移与 canonical 回切拉起子进程时，显式传递当前 PID 与当前 exe 路径。
  - 保留旧版兼容：若仅有单一位置参数，仍按旧 cleanup-path 解析。
- `RebornLauncher/WorkThread.cpp`
  - 删除 PID 配置写入逻辑（`WriteLauncherPid` 等）。
  - 自更新拉起 `UpdateTemp.exe` 改为传参：
    - `--cleanup-pid=<current pid>`
    - `--cleanup-path="<current module path>"`
    - `--stage=selfupdate`
- `docs/agent-meta/hot-files-index.md`
  - 更新热文件计数。

## 4) Rationale
- 传参链路天然是一次性上下文，不会留下脏状态，避免“配置残留 PID 导致下次误判”。
- 通过 `cleanup-pid + cleanup-path` 双条件校验后再终止/删除，能降低误杀与误删风险。
- `stage` 参数为后续排障和链路扩展预留了明确语义。

## 5) Risks + rollback
- 风险：
  - 外部如果还有旧逻辑只传配置 PID、但不传 cleanup 参数，会无法触发清理；当前代码已保留“单位置 cleanup-path”的兼容路径。
  - 仍需依赖系统权限删除旧文件（已带重试，但权限不足时仍可能失败）。
- 回滚：
  - 执行 `git restore --source=HEAD~1 -- RebornLauncher/RebornLauncher.cpp RebornLauncher/WorkThread.cpp docs/agent-meta/hot-files-index.md`

## 6) Follow-ups/TODO (optional)
- 可再加 `--cleanup-create-time` 参数，进一步规避 PID 复用导致的边界误判。
