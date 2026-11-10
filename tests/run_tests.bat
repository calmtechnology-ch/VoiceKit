@echo off
setlocal
echo ========================================================
echo   Compilation et Execution des tests VoxCore VoiceKit
echo ========================================================

rem Resolution dynamique et reproductible du compilateur C++
if defined CXX (
    set "GCC=%CXX%"
) else if defined GCC (
    set "GCC=%GCC%"
) else (
    where g++ >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        set "GCC=g++"
    ) else if exist "%USERPROFILE%\.platformio\packages\toolchain-gccmingw32\bin\g++.exe" (
        set "GCC=%USERPROFILE%\.platformio\packages\toolchain-gccmingw32\bin\g++.exe"
    ) else (
        echo [ERREUR] Compilateur g++ introuvable dans PATH ni dans .platformio.
        exit /b 1
    )
)

%GCC% -std=gnu++17 -O2 -static -Isrc -Itests/native_mock -Itests -Iexamples/OrientationClockVoice/include tests/native_mock/cJSON.c src/core/AudioTraceLog.cpp src/core/AudioResampler.cpp src/core/LiveAudioPlayer.cpp src/core/GeminiLiveClient.cpp src/core/GeminiVoiceProvider.cpp src/core/OpenAIRealtimeClient.cpp src/core/LiveSessionController.cpp src/core/LiveAudioStreamer.cpp src/core/ConversationController.cpp src/core/WiFiManager.cpp src/core/ContinuousListeningController.cpp src/core/WakeWordAudioBridge.cpp src/core/WakeWordManager.cpp src/core/WakeWordConversationController.cpp src/core/tools/ToolRegistry.cpp src/core/voiceux/TextGate.cpp src/core/voiceux/VoiceIntentRouter.cpp src/core/voiceux/ReplyMemory.cpp src/core/voiceux/VoiceUxController.cpp src/core/voiceux/VoiceUxLiveBridge.cpp src/hal/M5AudioOutput.cpp src/hal/M5AudioInput.cpp src/VoxCoreVoice.cpp src/VoiceKit.cpp tests/test_voicekit_all.cpp -o tests/test_voicekit_all.exe

if %ERRORLEVEL% NEQ 0 (
    echo [ERREUR] Echec de la compilation des tests.
    exit /b %ERRORLEVEL%
)

tests\test_voicekit_all.exe
exit /b %ERRORLEVEL%
