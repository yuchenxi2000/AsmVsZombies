$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $taskRoot 'build/loader-tests'
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$compiler = Join-Path $taskRoot 'MinGW/bin/clang++.exe'
$common = @('-m32', '-std=c++20', '-Wall', '-static', '-I', "$taskRoot/inc", '-I', "$taskRoot/loader", '-I', "$taskRoot/loader/tomlplusplus/include")
foreach ($fixture in @('modern', 'legacy', 'missing')) {
    $defines = @()
    if ($fixture -eq 'legacy') { $defines += '-DLEGACY_API' }
    if ($fixture -eq 'missing') { $defines += '-DMISSING_API' }
    & $compiler @common @defines '-shared' "$PSScriptRoot/fake_mod.cpp" '-o' "$testOutput/$fixture.dll"
    if ($LASTEXITCODE -ne 0) { throw "Failed compiling $fixture" }
}
& $compiler @common '-fexperimental-library' '-shared' "$PSScriptRoot/session_mod.cpp" "$taskRoot/bin/libavz.a" '-lgdi32' '-ldbghelp' '-lc++experimental' '-o' "$testOutput/session.dll"
if ($LASTEXITCODE -ne 0) { throw 'Failed compiling real AvZ session fixture; build avz first' }
& $compiler @common '-Wl,--image-base,0x400000,--disable-dynamicbase' "$PSScriptRoot/loader_tests.cpp" '-o' "$testOutput/loader_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Failed compiling loader tests' }
Push-Location $testOutput
try {
    foreach ($exitCase in @('--exit-uninitialized', '--exit-active')) {
        & "$testOutput/loader_tests.exe" $testOutput $exitCase
        if ($LASTEXITCODE -ne 0) { throw "Process exit test failed: $exitCase ($LASTEXITCODE)" }
    }
    & "$testOutput/loader_tests.exe" $testOutput
    if ($LASTEXITCODE -ne 0) { throw 'Loader tests failed' }
} finally {
    Pop-Location
}
