[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer vswhere.exe was not found. Install Visual Studio 2022 with Desktop development with C++.'
}
$msbuild = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe'
if ($LASTEXITCODE -ne 0 -or -not $msbuild) {
    throw 'Visual Studio 2022 MSBuild with C++ tools was not found.'
}
$msbuild = @($msbuild)[0]
& $msbuild (Join-Path $projectDirectory 'CommandTray.Tests.vcxproj') /nologo /m /verbosity:minimal "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw "Test build failed with exit code $LASTEXITCODE."
}
$testExecutable = Join-Path $projectDirectory "build/tests/x64/$Configuration/CommandTray.Tests.exe"
& $testExecutable
if ($LASTEXITCODE -ne 0) {
    throw "Tests failed with exit code $LASTEXITCODE."
}
