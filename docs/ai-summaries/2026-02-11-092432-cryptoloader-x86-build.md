# AI Summary

## 1) User request (quote)
> "CryptoLoader 需要x86构建"

## 2) What was done
- 增加了“只构建 CryptoLoader”的 CMake 开关，避免为 x86 配置时强制引入 RebornLauncher/UpdateForge 的大量依赖。
- 为 Win32/x86 构建补齐了 Windows SDK 10.0.26100.0 所需的架构宏定义（`_X86_`），并为 x64 补齐（`_AMD64_`）。
- CryptoLoader 增加了 x86 专用输出目录（`bin_x86/...`），并添加静态断言防止误构建成 x64。
- 新增了 VS2022 的 CryptoLoader x86 构建脚本，可一键产出 `CryptoLoader.dll`（Debug/Release）。

## 3) Changes (paths + brief)
- `CMakeLists.txt`
  - 增加可选构建开关：`UPDATESUITE_BUILD_CRYPTOLOADER/REBRONLAUNCHER/UPDATEFORGE`。
  - detours 手动查找逻辑改为按 `VCPKG_INSTALLED_DIR` + `VCPKG_TARGET_TRIPLET` 精准定位，并设置 Debug/Release 导入库路径。
  - 为 VS2022/SDK 10.0.26100.0 添加架构宏：Win32 定义 `_X86_`、x64 定义 `_AMD64_`。
- `CryptoLoader/CMakeLists.txt`
  - Win32 时将 DLL/导入库输出到 `bin_x86/<Config>/`，避免覆盖 x64 输出。
- `CryptoLoader/dllmain.cpp`
  - 添加 `static_assert(sizeof(void*) == 4)`，防止误用 x64 构建（该 hook 地址为 32-bit 绝对地址）。
- `build_cryptoloader_vs2022_x86.bat`
  - 新增：配置并构建 Win32/x86 的 CryptoLoader（并将 detours 安装到 repo 内 `vcpkg_installed` 缓存）。

## 4) Rationale
- CryptoLoader 代码使用固定的 32-bit 绝对地址（`0x5081A080`），因此必须以 Win32/x86 生成。
- 通过 CMake 开关跳过其他子工程，可避免 x86 环境下为 RebornLauncher/UpdateForge 拉取并配置一大堆依赖，降低构建成本。
- Windows SDK 10.0.26100.0 的 `winnt.h` 对架构宏更敏感，显式定义可保证 Win32/x64 均能稳定编译。

## 5) Risks + rollback
- 风险：`_X86_`/`_AMD64_` 为全局编译定义，若未来引入 ARM64 等平台，需要按平台进一步细化。
- 回滚：还原以下文件即可撤销本次改动：
  - `CMakeLists.txt`
  - `CryptoLoader/CMakeLists.txt`
  - `CryptoLoader/dllmain.cpp`
  - `build_cryptoloader_vs2022_x86.bat`

## 6) Follow-ups / TODO (optional)
- 如果希望保持工作区干净，可考虑将 `bin_x86/`、`_builds_cryptoloader_x86/` 加入 `.gitignore`（本次未改动忽略规则）。
