# AI Summary

## 1) User request (quote)
> 帮我看下 RebornLauncher 项目中。把没用到的素材从res中踢除。缩减体积。还有看下第一次运行时的逻辑是不是正确。... 然后你检查下现在RebornLauncher的整个流程链路，会不会有问题。加强一点检查。。。有问题的话修复它。

## 2) What was done
- 审查并修复了 `RebornLauncher` 首次运行迁移与自更新接力逻辑。
- 清理了 `res` 中已不再被代码使用的 PNG 资源，并停止将整套 `Texture` 目录拷贝到产物目录。
- 本地编译验证 `RebornLauncher`（Release）通过。

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - 重构启动前置流程：
    - 新增基于 `launcher.ini` 的配置读写（`%LOCALAPPDATA%/MapleFireReborn/launcher.ini`）。
    - 首次运行目录迁移：优先历史配置目录，否则按 `D/E/F/G/C` 选择目标目录，含磁盘可用空间检查与回退。
    - 迁移后重启并清理原路径文件，创建桌面快捷方式。
    - 自更新接力链路强化：通过命令行清理参数 + pid/path 校验执行旧进程终止与旧文件删除；`UpdateTemp.exe` 自动回切为 `RebornLauncher.exe`。
    - 修复此前 `lpCmdLine` 判空逻辑缺陷（避免无参数时误进入清理分支）。
    - 修复工作目录来源：改为模块所在目录（而非当前 shell cwd）。
- `RebornLauncher/WorkThread.cpp`
  - 自更新触发前写入 pid 改为同一 `launcher.ini`，与主进程读取保持一致。
  - 自更新拉起 `UpdateTemp.exe` 时，清理参数改为带引号，避免路径含空格时参数截断。
- `RebornLauncher/CMakeLists.txt`
  - 删除 `POST_BUILD` 对 `Texture/` 的整目录复制，减少产物体积。
- `RebornLauncher/RebornLauncher.rc`
  - 删除未使用资源引用：`IDB_UI1` 与全部 `IDB_LSL_*`。
- `RebornLauncher/resource.h`
  - 移除对应未使用资源 ID 定义并更新 `_APS_NEXT_RESOURCE_VALUE`。
- Deleted files:
  - `RebornLauncher/Texture/UI/Background.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/jump.0.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/move.0.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/move.1.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/move.2.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/move.3.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/move.4.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/move.5.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/move.6.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/stand.0.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/stand.1.png`
  - `RebornLauncher/Texture/GreenWaterSpirit/stand.2.png`
- `docs/agent-meta/hot-files-index.md`
  - 按本次访问更新计数，保持 Top 15。

## 4) Rationale
- 旧逻辑存在几处关键风险：
  - “是否已在安装目录”判断过宽，下载目录等场景会被误判为已安装，导致不迁移。
  - `lpCmdLine` 仅判指针非空，正常无参启动也可能进入清理分支，存在误杀风险。
  - pid 存储读写通道不统一（Win.ini vs 配置文件）导致更新链路可能失配。
  - 目录迁移参数与目标路径组合存在一致性问题。
- 本次改动统一了状态存储与链路约束，明确了迁移与自更新交接条件，避免误判/误删/误杀。
- 删除未使用资源并停止运行时复制整目录，直接缩减包体与输出目录体积。

## 5) Risks + rollback
- 风险：
  - 首次迁移默认最小可用空间阈值设置为 2GB，若实际需求高于该阈值，仍可能迁移到较紧张磁盘。
  - 资源清理后，若未来重新启用旧动画逻辑，需要恢复对应资源与 rc 定义。
- 回滚：
  - 执行 `git restore --source=HEAD~1 -- RebornLauncher/RebornLauncher.cpp RebornLauncher/WorkThread.cpp RebornLauncher/CMakeLists.txt RebornLauncher/RebornLauncher.rc RebornLauncher/resource.h`
  - 执行 `git checkout -- RebornLauncher/Texture/UI/Background.png RebornLauncher/Texture/GreenWaterSpirit/*`

## 6) Follow-ups/TODO (optional)
- 建议后续把迁移磁盘阈值改成可配置项（bootstrap/config），避免硬编码。
- 可补一组集成测试脚本覆盖：首次运行迁移、更新自替换、异常恢复（删不掉旧文件/无写权限）。
