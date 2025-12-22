@echo off
setlocal EnableDelayedExpansion

echo ============================================================
echo                  ZuesEngine Bootstrap Script
echo ============================================================
echo.
echo This script will check and install prerequisites, then
echo configure the project so you can build it in your IDE.
echo.

:: Check for admin rights (needed for some installations)
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo NOTE: Running without admin rights. Some installations may require elevation.
    echo.
)

:: ============================================================
:: Check CMake
:: ============================================================
echo [1/4] Checking CMake...
where cmake >nul 2>&1
if %errorLevel% neq 0 (
    echo       CMake not found in PATH.
    echo.
    echo       Please install CMake from: https://cmake.org/download/
    echo       Make sure to add CMake to your PATH during installation.
    echo.
    echo       After installing, close and reopen this terminal, then run this script again.
    echo.
    set CMAKE_MISSING=1
) else (
    for /f "tokens=3" %%v in ('cmake --version ^| findstr /i "cmake version"') do (
        echo       Found CMake version %%v
    )
)

:: ============================================================
:: Check MSVC / Visual Studio
:: ============================================================
echo.
echo [2/4] Checking Visual Studio / MSVC...

set FOUND_VS=0

:: Check for VS 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2022 Community
    set FOUND_VS=1
    set VS_YEAR=2022
    set VS_EDITION=Community
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2022 Professional
    set FOUND_VS=1
    set VS_YEAR=2022
    set VS_EDITION=Professional
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2022 Enterprise
    set FOUND_VS=1
    set VS_YEAR=2022
    set VS_EDITION=Enterprise
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2022 Build Tools
    set FOUND_VS=1
    set VS_YEAR=2022
    set VS_EDITION=BuildTools
)

:: Check for VS 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2019 Community
    set FOUND_VS=1
    set VS_YEAR=2019
    set VS_EDITION=Community
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2019 Professional
    set FOUND_VS=1
    set VS_YEAR=2019
    set VS_EDITION=Professional
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2019 Enterprise
    set FOUND_VS=1
    set VS_YEAR=2019
    set VS_EDITION=Enterprise
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    echo       Found Visual Studio 2019 Build Tools
    set FOUND_VS=1
    set VS_YEAR=2019
    set VS_EDITION=BuildTools
)

if %FOUND_VS% equ 0 (
    echo       Visual Studio / MSVC not found.
    echo.
    echo       Please install Visual Studio Build Tools from:
    echo       https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    echo.
    echo       Make sure to install the "Desktop development with C++" workload.
    echo.
    set MSVC_MISSING=1
)

:: ============================================================
:: Check for missing prerequisites
:: ============================================================
echo.
if defined CMAKE_MISSING (
    echo ERROR: CMake is required but not installed.
    goto :prerequisites_missing
)
if defined MSVC_MISSING (
    echo ERROR: Visual Studio / MSVC is required but not installed.
    goto :prerequisites_missing
)

echo [3/4] All prerequisites found!
echo.

:: ============================================================
:: Configure the project
:: ============================================================
echo [4/4] Configuring project...
echo.

:: Create build directory
if not exist "build" mkdir build

:: Configure with CMake
echo Running: cmake -B build -G "Visual Studio 17 2022" -A x64
cmake -B build -G "Visual Studio 17 2022" -A x64

if %errorLevel% neq 0 (
    echo.
    echo ERROR: CMake configuration failed!
    echo.
    echo If you're using Visual Studio 2019, try:
    echo   cmake -B build -G "Visual Studio 16 2019" -A x64
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo                    Bootstrap Complete!
echo ============================================================
echo.
echo The project has been configured. You can now:
echo.
echo   1. Open in Visual Studio:
echo      build\ZuesEngine.sln
echo.
echo   2. Open in CLion:
echo      Open the root CMakeLists.txt as a project
echo.
echo   3. Build from command line:
echo      cmake --build build --config Release
echo.
echo Output will be in: bin\ZuesEngine\
echo.
pause
exit /b 0

:prerequisites_missing
echo.
echo ============================================================
echo              Prerequisites Installation Required
echo ============================================================
echo.
echo Please install the missing prerequisites listed above.
echo After installation, run this script again.
echo.
echo Quick links:
echo   CMake:         https://cmake.org/download/
echo   Build Tools:   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
echo.
pause
exit /b 1
