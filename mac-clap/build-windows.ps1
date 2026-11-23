$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build\windows"

cmake -S $Root -B $BuildDir -A x64
cmake --build $BuildDir --config Release --parallel

$TestExe = Join-Path $BuildDir "Release\host_test.exe"
$Clap = Join-Path $BuildDir "clap\Release\TB-MPC-3-P-0.clap"
if (-not (Test-Path $Clap)) {
    $Clap = Join-Path $BuildDir "clap\TB-MPC-3-P-0.clap"
}

& $TestExe $Clap
Write-Host "Built TB-MPC-3-P-0.clap! Copy it to: C:\Program Files\Common Files\CLAP\"
