@echo off
setlocal

echo ============================================================
echo                  ZuesEngine Build Script
echo ============================================================
echo.

:: Parse arguments
set BUILD_TYPE=Release
set CLEAN_BUILD=0

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="debug" set BUILD_TYPE=Debug
if /i "%~1"=="release" set BUILD_TYPE=Release
if /i "%~1"=="clean" set CLEAN_BUILD=1
shift
goto :parse_args
:done_parsing

:: Clean build if requested
if %CLEAN_BUILD% equ 1 (
    echo Cleaning build directory...
    if exist "build" rmdir /s /q build
    if exist "bin\ZuesEngine" rmdir /s /q bin\ZuesEngine
    echo.
)

:: Check if build directory exists, if not run bootstrap
if not exist "build" (
    echo Build directory not found. Running bootstrap...
    call bootstrap.bat
    if %errorLevel% neq 0 exit /b 1
)

:: Build
echo Building %BUILD_TYPE% configuration...
echo.
cmake --build build --config %BUILD_TYPE% --parallel

if %errorLevel% neq 0 (
    echo.
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ============================================================
echo                    Build Complete!
echo ============================================================
echo.
echo Output location: bin\ZuesEngine\
echo.
echo Executables:
if exist "bin\ZuesEngine\ZuesEditor.exe" echo   - ZuesEditor.exe
if exist "bin\ZuesEngine\ZuesLauncher.exe" echo   - ZuesLauncher.exe
echo.

pause
