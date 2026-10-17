param([int]$Jobs = 6)
$ErrorActionPreference = 'Stop'
if ($Jobs -lt 1) { throw 'Jobs must be positive.' }
$toolchain = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { Join-Path $env:LOCALAPPDATA 'knack2-toolchain\msys64' }
$bash = Join-Path $toolchain 'usr\bin\bash.exe'
if (!(Test-Path -LiteralPath $bash)) { throw "MSYS2 CLANG64 missing: $toolchain. See README.md." }
$names = @('MSYSTEM', 'MSYS2_ROOT', 'PATH', 'TLG_LTO', 'TLG_BUILD_JOBS')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:MSYSTEM = 'CLANG64'
    $env:MSYS2_ROOT = $toolchain.Replace('\', '/')
    $env:PATH = "$toolchain\clang64\bin;$toolchain\usr\bin;$env:PATH"
    $env:TLG_LTO = 'OFF'
    $env:TLG_BUILD_JOBS = "$Jobs"
    $script = (Join-Path $PSScriptRoot 'port\build.sh').Replace('\', '/')
    # Windows PowerShell 5 turns redirected native stderr warnings into errors.
    $ErrorActionPreference = 'Continue'
    & $bash --noprofile --norc $script
    $buildExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($buildExit -ne 0) { throw "Build failed: exit $buildExit. See port/out/gpu-build.log." }
    & $bash -lc 'pacman -Q' | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'port\out\toolchain-packages.txt') -Encoding UTF8
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
