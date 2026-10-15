$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$output = Join-Path $env:TEMP 'smart_helmet_safety_tests.exe'
$includes = @(
    "$PSScriptRoot/stubs", "$repoRoot/shared/protocol",
    "$repoRoot/helmet_node/main", "$repoRoot/helmet_node/components/gps_driver/include",
    "$repoRoot/bike_node/components/bike_state/include",
    "$repoRoot/helmet_node/components/system_state/include",
    "$repoRoot/helmet_node/components/event_manager/include",
    "$repoRoot/helmet_node/components/helmet_io/include"
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
& gcc @flags @sources -o $output
if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }

$rfidOutput = Join-Path $env:TEMP 'smart_helmet_rfid_tests.exe'
& gcc @flags "$PSScriptRoot/test_rfid_allowlist.c" -o $rfidOutput
if ($LASTEXITCODE -ne 0) { throw 'RFID host compilation failed' }
& $rfidOutput
if ($LASTEXITCODE -ne 0) { throw 'RFID host tests failed' }

$idfRoot = $env:IDF_PATH
if (-not $idfRoot) { $idfRoot = 'D:/esp/v5.5.4/esp-idf' }
$jsonRoot = Join-Path $idfRoot 'components/json/cJSON'
$cloudOutput = Join-Path $env:TEMP 'smart_helmet_cloud_tests.exe'
$cloudFlags = $flags + @('-DHOST_CLOUD_TEST=1', "-I$jsonRoot",
    "-I$repoRoot/helmet_node/components/cloud_client/include",
    "-I$repoRoot/helmet_node/components/wifi_manager/include")
& gcc @cloudFlags "$PSScriptRoot/test_cloud.c" "$jsonRoot/cJSON.c" `
    "$repoRoot/helmet_node/components/system_state/system_state.c" -o $cloudOutput
if ($LASTEXITCODE -ne 0) { throw 'Cloud host compilation failed' }
& $cloudOutput
if ($LASTEXITCODE -ne 0) { throw 'Cloud host tests failed' }
