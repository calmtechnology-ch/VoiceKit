@echo off
call "%~dp0_build_variant.bat" fr-openai "FR + OpenAI" %*
set "RC=%ERRORLEVEL%"
echo %cmdcmdline% | find /i "/c """ >nul && pause
exit /b %RC%
