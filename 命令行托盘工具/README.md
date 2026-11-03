# 命令行托盘工具

Windows 原生 C++ 托盘工具。它以管理员权限运行，启动后读取旁边的 `tray.ini`，自动启动目标程序并默认隐藏界面。

## 获取程序

从 [dist/CommandTray-windows-x64.zip](dist/CommandTray-windows-x64.zip) 获取 Windows x64 发布包，完整解压后编辑旁边的 `tray.ini`，再运行 `CommandTray.exe`。程序清单使用 `requireAdministrator`，Windows 会在启动时显示 UAC 提示。发布包采用静态 C++ 运行库，无需另外安装 Visual C++ 运行库。

## 工程结构

- `CommandTray.sln`、`CommandTray.vcxproj`：Visual Studio 主工程。
- `src/`：C++ 源码、头文件、图标、清单、资源及示例配置。
- [docs/SPEC.md](docs/SPEC.md)：工具规格说明。
- `tests/src/`、`tests/scripts/`：测试代码和执行脚本。
- `scripts/`：图标转换和打包脚本。
- `build/`：本地编译、测试及打包临时输出，不纳入版本管理。
- `dist/`：纳入版本管理的 ZIP 发布包。

## 构建与打包

需要 Visual Studio 2022，安装“使用 C++ 的桌面开发”和 Windows SDK。打开 `CommandTray.sln`，选择 `x64`、`Debug` 或 `Release` 后生成。编译输出在 `build/x64/<配置>/`，可执行程序和配置文件放在同一目录。

也可在工具目录下用 PowerShell 一键编译 Release 并打包：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
```

脚本自动查找 VS 2022 的 MSBuild，重新编译 x64 Release，生成 `dist/CommandTray-windows-x64.zip`。包内包含 `CommandTray.exe`、源码中的示例 `tray.ini` 和本 README；重新打包会覆盖同名 ZIP。打包不会使用本地运行时修改的配置。

程序和托盘图标均由 `src/tray.png` 转换出的多尺寸 `src/tray.ico` 提供，通过 `src/app.rc` 嵌入 EXE；运行时无需外部图片。修改原图后运行 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/convert-icon.ps1` 重新生成图标，再构建。

## 测试

在工具目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/scripts/test.ps1
```

测试工程为 `CommandTray.Tests.vcxproj`，输出保存在 `build/tests/`。自动测试覆盖路径处理、窗口显隐和兼容模式停止主进程；不自动注册登录任务。UAC、完整托盘菜单和登录自启需在 Windows 桌面手动验证。

## 配置

编辑 `tray.ini`：

```ini
[program]
path=tools\my-server.exe
arguments=--port 8080 --name "测试服务"
working_directory=
hide_on_start=true
manage_process_tree=true
```

`path` 和 `working_directory` 支持 `%变量%`，普通相对路径以 `CommandTray.exe` 所在目录为基准。`working_directory` 留空时使用目标程序目录。参数原样交给 Windows 命令行解析；需要包含空格的参数请使用双引号。

`hide_on_start=true` 时每次启动或重启目标后隐藏其窗口，设为 `false` 则显示，托盘菜单仍可随时打开或隐藏。开机自启不写入配置，程序会自行检查当前用户的计划任务是否存在且启用；通过托盘菜单启用或关闭。

`manage_process_tree=true` 时工具使用 Job Object 管理并在退出时结束目标进程树。Win7 上的 OpenSSH/Cygwin `sshd.exe` 如果 SFTP 子系统异常，可设为 `false`，让 SSHD 自行管理子进程。此模式下重启、退出仍会终止主进程，但不会主动结束其子进程；显示、隐藏可控制主进程窗口及共享控制台。

## 托盘菜单

- 双击图标或“打开界面”显示目标窗口；“隐藏界面”恢复隐藏。
- “启动”仅在目标停止时可用；“重启”停止目标、重新读取配置并启动；是否结束子进程由 `manage_process_tree` 决定。
- “开机自启（登录时）”创建或删除当前用户的最高权限计划任务，默认关闭。任务按程序路径和用户 SID 隔离；兼容旧任务时仅处理属于当前用户的任务。
- “退出并停止程序”停止目标并删除托盘图标；兼容模式保留子进程。

配置错误会弹窗提示。开机时 Explorer 尚未就绪导致托盘添加失败时，每两秒重试，成功后停止重试；目标程序照常启动。Explorer 重启后托盘图标会自动恢复。重新构建时只在输出目录缺少 `tray.ini` 时复制示例，不覆盖已有配置。

## 安全边界

工具会以管理员权限运行，因此只应使用可信目录中的配置文件和目标程序。配置不执行命令拼接之外的额外脚本；参数交给目标程序自身解析。
