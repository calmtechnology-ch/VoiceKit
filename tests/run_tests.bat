@echo off
setlocal
echo ========================================================
echo   Compilation et Execution des tests VoxCore VoiceKit
echo ========================================================
set GCC="C:\Users\phall\.platformio\packages\toolchain-gccmingw32\bin\g++.exe"

%GCC% -std=c++14 -O2 -static -Iinclude -Isrc -Itests/native_mock -Itests -Iexamples/OrientationClockVoice/include tests/native_mock/cJSON.c src/audio/AudioTraceLog.cpp src/audio/LiveAudioPlayer.cpp src/gemini/GeminiLiveClient.cpp src/hal/M5AudioOutput.cpp src/VoxCoreVoice.cpp tests/test_voicekit_all.cpp -o tests/test_voicekit_all.exe

if %ERRORLEVEL% NEQ 0 (
    echo [ERREUR] Echec de la compilation des tests.
    exit /b %ERRORLEVEL%
)

tests\test_voicekit_all.exe
exit /b %ERRORLEVEL%
