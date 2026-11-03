param()
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw '请安装 Visual Studio 2022 C++ 桌面开发工具。' }
$msbuild = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw '未找到 Visual Studio 2022 MSBuild 和 C++ 工具链。' }
& $msbuild (Join-Path $projectDirectory 'CommandTray.sln') /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /nologo
if ($LASTEXITCODE -ne 0) { throw "编译失败，退出码：$LASTEXITCODE" }
$packageDirectory = Join-Path $projectDirectory 'build\package\CommandTray-x64'
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectDirectory 'build\x64\Release\CommandTray.exe') -Destination $packageDirectory -Force
# 始终使用源码中的示例，避免将本地运行配置打进发布包。
Copy-Item -LiteralPath (Join-Path $projectDirectory 'src\tray.ini') -Destination $packageDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectDirectory 'README.md') -Destination $packageDirectory -Force
$distDirectory = Join-Path $projectDirectory 'dist'
New-Item -ItemType Directory -Path $distDirectory -Force | Out-Null
$archive = Join-Path $distDirectory 'CommandTray-windows-x64.zip'
Compress-Archive -LiteralPath (Join-Path $packageDirectory 'CommandTray.exe'), (Join-Path $packageDirectory 'tray.ini'), (Join-Path $packageDirectory 'README.md') -DestinationPath $archive -Force
Write-Output "发布包：$archive"
