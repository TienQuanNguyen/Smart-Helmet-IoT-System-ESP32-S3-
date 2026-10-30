$ErrorActionPreference = 'Stop'
# ESP-IDF prepends tool directories whose DLLs can shadow MSYS2 GCC's DLLs.
# gcc.exe can still print its version while its cc1.exe child fails to load.
$hostCompiler = (Get-Command gcc -CommandType Application -ErrorAction Stop |
    Select-Object -First 1).Source
$hostCompilerDir = Split-Path -Parent $hostCompiler
$originalHostTestPath = $env:PATH
try {
    $env:PATH = $hostCompilerDir + [IO.Path]::PathSeparator + $originalHostTestPath
    Write-Host "Host compiler: $hostCompiler"
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
    $output = Join-Path $env:TEMP 'smart_helmet_safety_tests.exe'
    $includes = @(
        "$PSScriptRoot/stubs", "$repoRoot/shared/protocol",
        "$repoRoot/helmet_node/main", "$repoRoot/helmet_node/components/gps_driver/include",
        "$repoRoot/bike_node/components/bike_state/include",
        "$repoRoot/helmet_node/components/system_state/include",
        "$repoRoot/helmet_node/components/event_manager/include",
        "$repoRoot/helmet_node/components/helmet_io/include",
        "$repoRoot/helmet_node/components/power_manager/include"
    )
    $sources = @(
        "$PSScriptRoot/test_safety.c",
        "$repoRoot/helmet_node/components/gps_driver/gps_driver.c",
        "$repoRoot/bike_node/components/bike_state/bike_state.c",
        "$repoRoot/helmet_node/components/system_state/system_state.c",
        "$repoRoot/helmet_node/components/helmet_io/helmet_io.c"
    )
    $flags = @('-std=c11', '-Wall', '-Wextra', '-Werror')
    foreach ($include in $includes) { $flags += "-I$include" }
    & $hostCompiler @flags @sources -o $output
    if ($LASTEXITCODE -ne 0) { throw "Host compilation failed (exit=$LASTEXITCODE; compiler=$hostCompiler). See compiler diagnostics above." }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }

    $rfidOutput = Join-Path $env:TEMP 'smart_helmet_rfid_tests.exe'
    & $hostCompiler @flags "$PSScriptRoot/test_rfid_allowlist.c" -o $rfidOutput
    if ($LASTEXITCODE -ne 0) { throw "RFID host compilation failed (exit=$LASTEXITCODE; compiler=$hostCompiler). See compiler diagnostics above." }
    & $rfidOutput
    if ($LASTEXITCODE -ne 0) { throw 'RFID host tests failed' }

    $idfRoot = $env:IDF_PATH
    if (-not $idfRoot) { $idfRoot = 'D:/esp/v5.5.4/esp-idf' }
    $jsonRoot = Join-Path $idfRoot 'components/json/cJSON'
    $cloudOutput = Join-Path $env:TEMP 'smart_helmet_cloud_tests.exe'
    $cloudFlags = $flags + @('-DHOST_CLOUD_TEST=1', "-I$jsonRoot",
        "-I$repoRoot/helmet_node/components/cloud_client/include",
        "-I$repoRoot/helmet_node/components/wifi_manager/include")
    & $hostCompiler @cloudFlags "$PSScriptRoot/test_cloud.c" "$jsonRoot/cJSON.c" "$repoRoot/helmet_node/components/power_manager/power_model.c" "$repoRoot/helmet_node/components/power_manager/power_json.c" `
        "$repoRoot/helmet_node/components/system_state/system_state.c" -o $cloudOutput
    if ($LASTEXITCODE -ne 0) { throw "Cloud host compilation failed (exit=$LASTEXITCODE; compiler=$hostCompiler). See compiler diagnostics above." }
    & $cloudOutput
    if ($LASTEXITCODE -ne 0) { throw 'Cloud host tests failed' }

    $firebaseOutput = Join-Path $env:TEMP 'smart_helmet_firebase_tests.exe'
    & $hostCompiler @cloudFlags '-DHOST_FIREBASE_TEST=1' "$PSScriptRoot/test_cloud.c" "$jsonRoot/cJSON.c" "$repoRoot/helmet_node/components/power_manager/power_model.c" "$repoRoot/helmet_node/components/power_manager/power_json.c" `
        "$repoRoot/helmet_node/components/system_state/system_state.c" -o $firebaseOutput
    if ($LASTEXITCODE -ne 0) { throw "Firebase host compilation failed (exit=$LASTEXITCODE; compiler=$hostCompiler). See compiler diagnostics above." }
    & $firebaseOutput
    if ($LASTEXITCODE -ne 0) { throw 'Firebase host tests failed' }
    $powerOutput = Join-Path $env:TEMP 'smart_helmet_power_tests.exe'
    & $hostCompiler @flags "-I$jsonRoot" "$PSScriptRoot/test_power.c" `
        "$repoRoot/helmet_node/components/power_manager/power_model.c" `
        "$repoRoot/helmet_node/components/power_manager/power_json.c" `
        "$repoRoot/helmet_node/components/system_state/system_state.c" `
        "$jsonRoot/cJSON.c" -o $powerOutput
    if ($LASTEXITCODE -ne 0) { throw 'Power host compilation failed' }
    & $powerOutput
    if ($LASTEXITCODE -ne 0) { throw 'Power host tests failed' }
    $taskOutput = Join-Path $env:TEMP 'smart_helmet_power_task_tests.exe'
    & $hostCompiler @flags '-DHOST_CLOUD_TEST=1' '-DHOST_FIREBASE_TEST=1' "$PSScriptRoot/test_power_task.c" `
        "$repoRoot/helmet_node/components/power_manager/power_model.c" -o $taskOutput
    if ($LASTEXITCODE -ne 0) { throw 'Power task host compilation failed' }
    & $taskOutput
    if ($LASTEXITCODE -ne 0) { throw 'Power task host tests failed' }
} finally {
    # Also restore when the script is invoked directly/dot-sourced or throws.
    $env:PATH = $originalHostTestPath
}
