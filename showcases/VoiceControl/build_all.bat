@echo off
setlocal
chcp 65001 >nul 2>&1

echo ========================================================
echo   VoiceControl - Compilation des 4 variantes
echo ========================================================

call "%~dp0_build_variant.bat" fr-gemini "FR + Gemini" --build-only || goto :fail
call "%~dp0_build_variant.bat" fr-openai "FR + OpenAI" --build-only || goto :fail
call "%~dp0_build_variant.bat" en-gemini "EN + Gemini" --build-only || goto :fail
call "%~dp0_build_variant.bat" en-openai "EN + OpenAI" --build-only || goto :fail

echo.
echo ========================================================
echo   [OK] Les 4 variantes ont ete compilees avec succes.
echo ========================================================
echo %cmdcmdline% | find /i "/c """ >nul && pause
exit /b 0

:fail
echo.
echo [ERREUR] La compilation d'une variante a echoue.
echo %cmdcmdline% | find /i "/c """ >nul && pause
exit /b 1
