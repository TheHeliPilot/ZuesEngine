# Run a built binary inside the zues-toolchain container (Linux ELF).
# Usage:
#   .\tools\zues-run.ps1                                  # runs editor under clang-reflection
#   .\tools\zues-run.ps1 -Binary mygame -Preset debug

[CmdletBinding()]
param(
    [string]$Preset = "clang-reflection",
    [string]$Binary = "editor"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."

& docker run --rm `
    -v "${root}:/work" `
    -w "/work/build/$Preset/bin" `
    zues-toolchain:latest `
    "./$Binary"
