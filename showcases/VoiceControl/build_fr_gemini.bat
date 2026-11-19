@echo off
call "%~dp0_build_variant.bat" fr-gemini "FR + Gemini" %*
set "RC=%ERRORLEVEL%"
if "%~1"=="" echo %cmdcmdline% | find /i "/c """"" >nul && pause
exit /b %RC%
