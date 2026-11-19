@echo off
call "%~dp0_build_variant.bat" en-openai "EN + OpenAI" %*
set "RC=%ERRORLEVEL%"
echo %cmdcmdline% | find /i "/c """ >nul && pause
exit /b %RC%
