# Build Zues inside the zues-toolchain Docker image (clang-p2996 + cmake + ninja).
# Usage:
#   .\tools\zues-build.ps1                     # default preset: clang-reflection
#   .\tools\zues-build.ps1 -Preset debug       # any other preset
#   .\tools\zues-build.ps1 -Clean              # nuke the build dir first

[CmdletBinding()]
param(
    [string]$Preset = "clang-reflection",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."

# 1. Docker daemon up?
& docker info *>$null
if ($LASTEXITCODE -ne 0) {
    Write-Error "Docker daemon not reachable. Start Docker Desktop and try again."
    exit 1
}

# 2. Toolchain image present? Build if missing.
$img = docker images -q zues-toolchain:latest 2>$null
if (-not $img) {
    Write-Host "[zues] zues-toolchain image not found; building..."
    & docker build -t zues-toolchain:latest -f "$PSScriptRoot\Dockerfile.zues-toolchain" $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { Write-Error "image build failed"; exit 1 }
}

$cleanCmd = if ($Clean) { "rm -rf build/$Preset && " } else { "" }

Write-Host "[zues] building preset '$Preset' in container..."
& docker run --rm `
    -v "${root}:/work" `
    -w /work `
    zues-toolchain:latest `
    bash -c "${cleanCmd}cmake --preset $Preset && cmake --build build/$Preset"

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "[zues] done. Artifacts: $root\build\$Preset\bin\"
