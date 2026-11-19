@echo off
call "%~dp0_build_variant.bat" en-gemini "EN + Gemini" %*
set "RC=%ERRORLEVEL%"
if "%~1"=="" echo %cmdcmdline% | find /i "/c """"" >nul && pause
exit /b %RC%
