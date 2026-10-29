$ErrorActionPreference = 'Stop'
$knackPython = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
$knackRuntime = Join-Path $PSScriptRoot 'port\out\knack2-runtime.exe'
$knackExpected = 'a73cd77cbc018cf19441207d45ed84fb8744f5615797f9d88c381989af4a44ea'
if ((Get-FileHash -LiteralPath $knackRuntime -Algorithm SHA256).Hash.ToLowerInvariant() -ne $knackExpected) {
    throw 'Runtime differs from the verified Knack II artifact. No game was started.'
}
Push-Location -LiteralPath $PSScriptRoot
try {
    & $knackPython -B -m port.launcher.knack2 --real
    if ($LASTEXITCODE -ne 0) { throw "Launcher exited with code $LASTEXITCODE" }
} finally {
    Pop-Location
}
