@echo off
REM Build Windows installer using NSIS
REM Requires NSIS to be installed and in PATH

echo Building Windows installer for VEX TM Remote Display Server...

REM Check if NSIS is available
where makensis >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: NSIS (makensis) not found in PATH
    echo Please install NSIS from https://nsis.sourceforge.io/
    echo and ensure makensis.exe is in your PATH
    exit /b 1
)

REM Build the installer
makensis installer.nsi

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Installer built successfully!
    echo Output: VEXTMRemoteDisplayServer-1.0.0-Setup.exe
) else (
    echo.
    echo ERROR: Failed to build installer
    exit /b 1
)

