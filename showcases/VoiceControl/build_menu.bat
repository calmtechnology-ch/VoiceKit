@echo off
setlocal
chcp 65001 >nul 2>&1

:menu
cls
echo ========================================================
echo   VoiceControl - Compilation et Televersement CoreS3
echo ========================================================
echo.
echo   1. FR + Gemini  ^(compiler + televerser^)
echo   2. FR + OpenAI  ^(compiler + televerser^)
echo   3. EN + Gemini  ^(compiler + televerser^)
echo   4. EN + OpenAI  ^(compiler + televerser^)
echo   5. Compiler les 4 variantes ^(sans televersement^)
echo   0. Quitter
echo.
set "CHOICE="
set /p "CHOICE=Votre choix [0-5] : "

if "%CHOICE%"=="1" call "%~dp0_build_variant.bat" fr-gemini "FR + Gemini" %* & pause & goto :menu
if "%CHOICE%"=="2" call "%~dp0_build_variant.bat" fr-openai "FR + OpenAI" %* & pause & goto :menu
if "%CHOICE%"=="3" call "%~dp0_build_variant.bat" en-gemini "EN + Gemini" %* & pause & goto :menu
if "%CHOICE%"=="4" call "%~dp0_build_variant.bat" en-openai "EN + OpenAI" %* & pause & goto :menu
if "%CHOICE%"=="5" call "%~dp0build_all.bat" & pause & goto :menu
if "%CHOICE%"=="0" exit /b 0

echo.
echo [INFO] Choix invalide.
timeout /t 2 >nul
goto :menu
