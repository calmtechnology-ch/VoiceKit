@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

if "%~1"=="" (
    echo [ERREUR] Usage interne : _build_variant.bat ^<env^> "^<label^>" [--build-only] [COMx]
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "ENV_NAME=%~1"
set "VARIANT_LABEL=%~2"
set "BUILD_ONLY=0"
set "UPLOAD_PORT="

:parse_args
shift
shift
:arg_loop
if "%~1"=="" goto :args_done
if /i "%~1"=="--build-only" (
    set "BUILD_ONLY=1"
) else if /i "%~1"=="-b" (
    set "BUILD_ONLY=1"
) else (
    set "UPLOAD_PORT=%~1"
)
shift
goto :arg_loop
:args_done

rem Resolution de PlatformIO CLI (PATH ou installation standard)
where.exe pio >nul 2>&1
if not errorlevel 1 (
    set "PIO=pio"
) else if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" (
    set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
) else (
    echo [ERREUR] PlatformIO CLI ^(pio^) introuvable dans PATH ni dans "%USERPROFILE%\.platformio\penv\Scripts\".
    exit /b 1
)

pushd "%SCRIPT_DIR%" >nul

echo ========================================================
echo   VoiceControl - %VARIANT_LABEL% ^(%ENV_NAME%^)
echo ========================================================

if "%BUILD_ONLY%"=="1" (
    echo [INFO] Mode : Compilation uniquement
    "%PIO%" run -e "%ENV_NAME%"
) else if defined UPLOAD_PORT (
    echo [INFO] Mode : Compilation + Televersement sur %UPLOAD_PORT%
    "%PIO%" run -e "%ENV_NAME%" -t upload --upload-port "%UPLOAD_PORT%"
) else (
    echo [INFO] Mode : Compilation + Televersement ^(port auto^)
    "%PIO%" run -e "%ENV_NAME%" -t upload
)

set "EXIT_CODE=%ERRORLEVEL%"
popd >nul

if not "%EXIT_CODE%"=="0" (
    echo.
    echo [ERREUR] Echec pour la variante %VARIANT_LABEL% ^(code %EXIT_CODE%^).
    exit /b %EXIT_CODE%
)

echo.
echo [OK] Variante %VARIANT_LABEL% terminee avec succes.
exit /b 0
