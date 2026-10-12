# 开发与验证

[文档目录](README.md) · [上一篇：生命周期管理](03_lifecycle.md)

## 源码导航

| 文件 | 主要职责 |
| --- | --- |
| [`loader/avzloader.cpp`](../../loader/avzloader.cpp) | 游戏主循环／绘制钩子、用户上下文、目录扫描时机、加载器导出、进程驻留 |
| [`loader/mod_manager.h`](../../loader/mod_manager.h) | 配置解析、模块登记、所有权、回调保护、控制状态同步、安全卸载 |
| [`loader/directory_monitor.h`](../../loader/directory_monitor.h) | 异步目录通知、变化代数、监控取消与句柄释放 |
| [`inc/avz_mod.h`](../../inc/avz_mod.h) | DLL 导出声明、模组取得加载器接口的逻辑 |
| [`inc/avz_mod_lifecycle.h`](../../inc/avz_mod_lifecycle.h) | 生命周期状态位、独立于游戏汇编的会话判定规则 |
| [`src/avz_mod.cpp`](../../src/avz_mod.cpp) | 模组逻辑／管理／绘制导出，回调退出后的延迟维护 |
| [`src/avz_script.cpp`](../../src/avz_script.cpp) | 脚本初始化、会话结束、停止确认、同步等待取消 |
| [`src/avz_state_hook.cpp`](../../src/avz_state_hook.cpp) | 钩子执行和业务许可检查 |
| [`src/avz_tick_runner.cpp`](../../src/avz_tick_runner.cpp) | 帧队列调度和句柄状态查询 |
| [`src/avz_coroutine.cpp`](../../src/avz_coroutine.cpp) | 挂起协程登记、会话／停止清理 |
| [`src/avz_time_queue.cpp`](../../src/avz_time_queue.cpp) | 时间操作队列和执行前的业务许可检查 |
| [`src/avz_painter.cpp`](../../src/avz_painter.cpp) | 绘制队列、初始化保护和会话清理 |
| [`src/avz_game_controllor.cpp`](../../src/avz_game_controllor.cpp) | 高级暂停、跳帧、窗口刷新及模组控制转发 |
| [`src/avz_hook.cpp`](../../src/avz_hook.cpp) | 脚本 DLL 的附加／分离、独立注入回退、进程退出保护 |
| [`src/avz_seh.cpp`](../../src/avz_seh.cpp) | 异常报告、调用栈与 `crash.txt` 输出 |

接口约定见[模组 API](02_mod_api.md)，行为约定见[生命周期管理](03_lifecycle.md)。修改导出名、状态位、所有权或停止确认条件时，需要同步更新这些文档与对应测试。

## 构建环境

项目使用 Windows、32 位 LLVM-MinGW 和 C++20。仓库的 `MinGW/bin` 提供编译器，CMake 构建还需要可用的生成器及相应构建工具。下面以 `MinGW Makefiles` 和 `mingw32-make.exe` 为例；运行前把构建工具加入 `PATH`。这套命令不要求将编译器本身加入 `PATH`。

从仓库根目录打开 PowerShell，首次配置一个独立的构建目录：

```powershell
$compilerDir = (Resolve-Path .\MinGW\bin).Path.Replace('\', '/')
$makeProgram = (Get-Command mingw32-make.exe -ErrorAction Stop).Source.Replace('\', '/')

cmake -S . -B build/avzloader -G "MinGW Makefiles" `
    "-DCMAKE_C_COMPILER=$compilerDir/clang.exe" `
    "-DCMAKE_CXX_COMPILER=$compilerDir/clang++.exe" `
    "-DCMAKE_MAKE_PROGRAM=$makeProgram"
```

若已有使用相同编译器和生成器的构建目录，可以直接复用；切换工具链或生成器时使用新的构建目录，避免旧缓存混用。

### 构建静态库和运行组件

只验证框架静态库时：

```powershell
cmake --build build/avzloader --target avz -j 8
```

需要在游戏中验证新实现时，构建加载器、工具和所有示例模组：

```powershell
cmake --build build/avzloader `
    --target avz avzloader lazy autogarden pe12 advpause injector instloader -j 8
```

当前 CMake 将结果输出到仓库的 `bin`：静态库为 `libavz.a`，加载器为 `avzloader.dll`，四个示例模组为 `lazy.dll`、`autogarden.dll`、`pe12.dll`、`advpause.dll`。安装器构建目标和可执行文件名为 `instloader`／`instloader.exe`；发行包的安装步骤和命名以仓库 README 为准。

AvZ 被静态链接到每个脚本 DLL。只更新 `avzloader.dll` 或 `libavz.a`，不会改变游戏中已加载模组包含的框架代码；修改 `src`／`inc` 后，需要重新编译相关脚本 DLL，再替换游戏使用的版本。

普通验证显式指定以上目标。项目的 `PackageAvz` 是默认构建中的打包目标，会删除 `bin/*.dll.a` 并写入 `release/env2/nightly.zip`；准备 GitHub Release 时再按发布流程执行打包。开发测试和发布打包需要分别确认产物。

### 打包 AvZLoader 发布版本

加载器使用独立版本信息 [`loader/version.json`](../../loader/version.json) 和更新日志 [`CHANGELOG_LOADER.md`](../../CHANGELOG_LOADER.md)，不修改上游 `metadata.json` 的 AvZ 版本编号或 `release/version.txt` 的版本索引。

生成加载器发布包时，显式构建新目标：

```powershell
cmake -S . -B build/avzloader
cmake --build build/avzloader --target PackageAvzLoader -j 8
```

此目标依赖静态库、加载器、安装器、注入器和四个示例模组。编译完成后执行 [`loader/package_release.ps1`](../../loader/package_release.ps1)，生成 `dist/avzloader/AvZLoader-2.0.0.zip` 和同名 `.sha256` 校验文件。`dist` 已加入仓库 `.gitignore`，发布产物只上传 GitHub Release，不提交到 Git，也不放入上游的 `release` 目录。也可以在已经构建完所有组件后单独运行该脚本；单独运行不会重新编译。

发布包使用明确的二进制名单，不会包含 `bin` 中旧的注入测试 DLL 或导入库；包含源码、头文件、静态库、示例配置、文档、测试和许可证。包内 `SHA256SUMS.txt` 记录每个随包文件的哈希，压缩包外的 `.sha256` 校验整个 ZIP。文件快照保留在 `build/loader-release`，便于核查。

发布包不包含游戏程序或 `MinGW` 编译器。只使用预编译模组时无需额外编译器；编写脚本或重新构建源码时，需要原有 AvZ 开发环境中的 LLVM-MinGW。更新时先关闭游戏，再替换运行组件。

发布前运行自动测试，检查 ZIP 内的版本信息、二进制哈希和文档链接，并按游戏内验证步骤确认行为。GitHub Release 使用加载器的独立版本号；上游 AvZ **2.9.2** 作为基础库版本说明。

## 自动回归测试

先构建当前 `avz`，再从仓库根目录运行：

```powershell
& .\tests\run_loader_tests.ps1
```

脚本使用仓库的 32 位编译器，构建三种协议测试 DLL、链接真实 `libavz.a` 的会话测试 DLL，以及测试宿主。生成文件放在 `build/loader-tests`，不把 DLL 注入真实游戏。

如果 PowerShell 执行策略阻止运行脚本，可在允许的开发环境中用一次性进程参数执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_loader_tests.ps1
```

脚本遇到编译失败或非零测试退出码会抛出错误。成功运行会看到未初始化 DLL 的进程退出测试、活动 DLL 的进程退出测试，以及最后一行 `Loader lifecycle, DLL ownership, reentrant unload, config and watcher tests passed`。

### 测试文件与覆盖范围

| 测试文件 | 职责 |
| --- | --- |
| [`tests/fake_mod.cpp`](../../tests/fake_mod.cpp) | 现代协议、旧协议和缺少必需导出的 DLL，提供可控计数和停止确认 |
| [`tests/session_mod.cpp`](../../tests/session_mod.cpp) | 链接真实 AvZ，创建帧运行器、时间队列、挂起协程和状态钩；进程退出路径使用生产 `DllMain` 实现 |
| [`tests/loader_tests.cpp`](../../tests/loader_tests.cpp) | 会话规则、真实 Windows DLL 加卸载、管理器重入、配置和目录监控验证 |
| [`tests/run_loader_tests.ps1`](../../tests/run_loader_tests.ps1) | 编译测试文件、运行两种独立退出进程和主测试 |

| 场景 | 检查内容 |
| --- | --- |
| 会话身份 | 选卡进入战斗保留会话；用户变更、对象替换、战斗回到选卡、时钟回退结束会话；重新运行模式控制下一局 |
| 筛选与控制权 | 不匹配时业务／绘制停用，管理仍运行；释放该模组的暂停和不刷新窗口请求 |
| 配置更新 | 无效配置保留最后有效限制；关卡限制不能匹配主菜单遗留的关卡 ID |
| 目录快照 | 扫描失败保留模组；成功空目录触发目录模组卸载；不影响 injector 所有权 |
| 调度重入 | 模组回调内部重入扫描和停止确认，外层调用返回前仍不释放 DLL 或破坏记录 |
| 停止确认 | 多次管理仍不能强制卸载尚未确认清理的 DLL，收到确认后在安全位置释放 |
| 协议兼容 | 缺少生命周期协议的 DLL 保持等待；缺少必需导出的 DLL 被拒绝 |
| 目录通知 | 连续变化被保留；停止事件可取消异步目录读取并结束监控线程 |
| 真实会话清理 | 筛选停用保留同局协程；会话结束清理时间队列、三种会话帧队列和协程；保留 `AFTER_INJECT` 帧运行 |
| 最终停止 | 清空全部帧队列与协程，包括没有有效会话的情况；退出钩子不重复执行 |
| 业务钩中途停用 | 前一个业务钩关闭许可后，后续业务钩不执行 |
| 初始化前绘制 | 模拟 3D 接口，在首次逻辑初始化前调用全部绘制入口，不访问设备或帧队列；初始化后仍能正常绘制；重加载后再次受保护 |
| 默认帧句柄 | 初始化前安全查询／停止，初始化后默认句柄不影响其他活动运行器 |
| 进程退出 | 未初始化／活动 DLL 驻留，游戏基础指针已清空；真实 `ExitProcess` 路径执行 CRT 析构，并检查退出保护已建立、没有调用脚本清理钩 |

### 自动测试的边界

测试宿主使用数据缓冲区模拟游戏对象，并在自己的固定地址映像中保留 AvZ 所需的地址范围。绘制测试使用模拟的 Direct3D 接口。它们检查真实框架 C++ 逻辑和 Windows 模块寿命，但不执行 PvZ 二进制中的选卡、主循环、战斗、绘制等内部函数。

因此测试通过不能替代实际游戏验证。特别是同步选卡取消后的游戏表现、不同游戏修改版的内存布局、下一轮和读档时的对象变化，以及多个脚本操作同一游戏状态的冲突，都需要在目标游戏中确认。

## 游戏内验证步骤

关闭游戏后替换本次构建的组件，检查测试脚本确实使用新版本 DLL 和同名配置。保留原文件备份；准备一个可重复使用的测试存档，然后记录每组操作的界面、用户、关卡和结果。

| 操作 | 预期结果 |
| --- | --- |
| 不匹配关卡启动游戏，并保持模组驻留后退出 | 模组不执行业务；从未初始化的 DLL 退出时无析构崩溃 |
| 匹配关卡运行中修改 TOML 为不匹配，再改回 | 同局暂停／恢复业务，不重跑 `AScript`，不因配置单独触发清理钩 |
| 停用后返回菜单，再重新进入关卡 | 停用期间仍清理旧会话；下一局是否运行遵循重新载入模式 |
| 选卡时移走 DLL | 框架等待在可取消边界退出，调用栈退回后完成卸载 |
| 选卡时卸载，在准备战斗字幕期间重新加载 | 新 DLL 不因首次逻辑初始化尚未发生就访问绘制队列 |
| 高级暂停或不刷新窗口时停用／卸载所有者 | 释放该所有者的控制请求，游戏恢复相应状态 |
| 跳帧期间停用／卸载 | 停用模组不再贡献跳帧条件；其他匹配模组仍能工作 |
| 多个模组同时运行，反复加卸载其中一个 | 其他模组保留自己的有效会话；没有旧回调调用到已卸载 DLL |
| 无尽下一轮／战斗后重新选卡 | 旧队列清理，新会话按模式初始化，不重复沿用上一轮操作 |
| 切换用户、重开或读档使时钟回退 | 结束旧会话，避免持有旧对象或旧时序；按条件重新运行 |
| 写入无效配置，再修复；另测删除配置 | 无效配置保留旧限制，修复后更新，删除后取消配置限制 |
| 活动模组驻留时退出游戏 | 进程退出保护生效，无队列／日志器析构访问异常 |

同步初始化取消与“已完成初始化的同局停用”是不同路径，应分别记录。也应测试 `NONE`、`MAIN_UI`、`MAIN_UI_OR_FIGHT_UI` 三种模式，不只验证持续挂机的示例。

建议验证记录至少包括 DLL 文件哈希、加载器版本、配置内容、目标游戏版本、具体操作顺序、发生的界面和日志。记录结果时区分“自动测试通过”“实际游戏检查通过”和“尚未验证”。

## 常见调试输出

加载器目前向标准输出／错误流写诊断。`loader/avzloader.cpp` 的 `DEBUG` 宏默认注释；需要观察这些输出时，可在调试构建中启用其控制台逻辑。当前没有默认持久化的模组管理日志文件。

| 输出前缀 | 含义与检查方向 |
| --- | --- |
| `Load mod:` | DLL 已登记；不表示 `AScript` 已开始 |
| `Register injected mod:` | 外部注入 DLL 已加入调度表 |
| `Unload pending: ... waiting for cleanup` | 已请求卸载，但尚未获得可安全释放的确认；检查未返回的回调和同步等待 |
| `Unload pending: ... recompile with the lifecycle protocol` | 缺少完整生命周期导出；重新编译模组 |
| `Unload mod:` | 已通过卸载条件，准备调用 `FreeLibrary` |
| `Keeping previous config for` | 新配置解析／校验失败，旧配置继续生效 |
| `Cannot load mod configuration:` | 首次配置无效，当前 DLL 暂不加载 |
| `Cannot scan mods; retaining previous snapshot:` | 目录访问失败，保留原记录 |
| `Cannot load` | `LoadLibraryW` 失败；输出附有 Windows 错误码，检查位数、路径和依赖 |
| `Missing mod API:` | DLL 缺少八个必需导出中的一个或多个 |
| `Mod cleanup failed; keeping DLL loaded` | 框架清理抛出异常；保留 DLL，避免释放仍可能被引用的代码 |
| `Mod API failed; retaining DLL:` | 导出调用抛出异常；加载器禁用业务并保留模块 |

清理失败后不会自动无限重跑作者清理，退出请求也不能作为清理成功的证明。先保存诊断信息，再排查故障；不要通过删除记录或超时强制 `FreeLibrary` 掩盖失败。

## 崩溃定位

框架的异常报告会尝试写 `crash.txt`，通常位于游戏的启动工作目录。保存日志时同时保存产生崩溃的 DLL；重新编译后地址会改变，不能直接拿新 DLL 的符号解释旧日志。

1. 记录异常地址、模块名、寄存器、完整调用栈和当时的操作顺序。
2. 比较游戏实际使用的 DLL 与构建产物的 SHA256，确认分析的是同一二进制。
3. 取得模块实际加载基址。计算 `RVA = 异常地址 - 实际加载基址`，再用 DLL 的节表、首选基址和符号定位函数。
4. 若使用日志的 `Logical Address`，其段内偏移还需加上对应节的虚拟地址，不能直接当成 RVA。
5. 根据反汇编和寄存器确认实际访问对象，检查是否是未初始化对象、旧会话指针、已销毁队列或已卸载代码，而不是仅凭报告中的 DLL 名称判断某个脚本有错。

例如在 PowerShell 中保存本次构建的符号和反汇编：

```powershell
Get-FileHash -Algorithm SHA256 .\bin\advpause.dll
& .\MinGW\bin\llvm-nm.exe -n .\bin\advpause.dll |
    Set-Content -Encoding utf8 .\build\advpause-symbols.txt
& .\MinGW\bin\llvm-objdump.exe -d -C .\bin\advpause.dll |
    Set-Content -Encoding utf8 .\build\advpause-disassembly.txt
```

构建产物若已剥离符号或没有源码调试信息，需要保留未剥离版本、映射文件或带调试信息的复现构建。`_execute_onexit_table`、`LdrShutdownProcess`、`ExitProcess` 等帧提示异常发生在进程退出／CRT 析构路径，应同时检查正常卸载和进程终止是否走了不同分支。

定位到框架 C++ 时序问题后，优先补能够复现相同状态的自动测试。涉及游戏内部函数和调用约定时，再结合目标游戏版本的反汇编与动态调试确认；代码注释或其他版本反编译中的函数地址只能作为线索。

[文档目录](README.md) · [上一篇：生命周期管理](03_lifecycle.md)
