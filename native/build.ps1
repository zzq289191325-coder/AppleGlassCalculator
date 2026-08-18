param(
    [Parameter(Mandatory = $true)]
    [string]$ToolchainRoot,
    [string]$OutputDirectory = "..\artifacts\native-win-x64"
)

$ErrorActionPreference = "Stop"
$compiler = Join-Path $ToolchainRoot "bin\x86_64-w64-mingw32-clang++.exe"
$windres = Join-Path $ToolchainRoot "bin\x86_64-w64-mingw32-windres.exe"
if (!(Test-Path $compiler) -or !(Test-Path $windres)) {
    throw "LLVM-MinGW compiler was not found under $ToolchainRoot"
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$output = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot $OutputDirectory))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$resourceObject = Join-Path $output "app-res.o"
$executable = Join-Path $output "Calculator.exe"

& $windres (Join-Path $scriptRoot "app.rc") -O coff -o $resourceObject
if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed" }

& $compiler (Join-Path $scriptRoot "main.cpp") $resourceObject `
    "-std=c++20" "-Os" "-flto" "-fuse-ld=lld" "-ffunction-sections" "-fdata-sections" `
    "-fno-exceptions" "-fno-rtti" "-municode" "-mwindows" "-static" `
    "-Wl,--gc-sections" "-Wl,--strip-all" "-Wl,--subsystem,windows" `
    "-lgdiplus" "-lshell32" "-lole32" "-luuid" "-luser32" "-lgdi32" `
    "-o" $executable
if ($LASTEXITCODE -ne 0) { throw "Native build failed" }

Get-Item $executable | Select-Object FullName, Length
