$ErrorActionPreference = "Stop"
$project = Split-Path -Parent $PSScriptRoot
$voiceKit = Resolve-Path (Join-Path $project "..\..")
$mock = Join-Path $voiceKit "tests\native_mock"
$output = Join-Path $PSScriptRoot "test_french_time_formatter.exe"

$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) {
    $candidates = @(
        "$env:USERPROFILE\.platformio\packages\toolchain-gccmingw32\bin\g++.exe",
        "$env:USERPROFILE\.platformio\packages\toolchain-gccmingw64\bin\g++.exe"
    )
    $compiler = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $compiler) { throw "Compilateur g++ introuvable." }

& $compiler -std=gnu++17 "-I$mock" "-I$(Join-Path $project 'include')" `
    (Join-Path $project "src\FrenchTimeFormatter.cpp") `
    (Join-Path $PSScriptRoot "test_french_time_formatter.cpp") `
    -o $output
if ($LASTEXITCODE -ne 0) { throw "Compilation des tests impossible." }

& $output
if ($LASTEXITCODE -ne 0) { throw "Les tests ont echoue." }
