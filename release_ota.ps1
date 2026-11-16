param(
    [Parameter(Mandatory=$true)][string]$Version,
    [switch]$SkipBuild,
    [switch]$NoPush,
    [string]$IdfProfile = $env:VESSEL_IDF_PROFILE,
    [string]$OtaRepo
)
# OTA release workflow for Vessel Motion P4.
# Bumps the version, builds, packages the firmware binary into the local
# clone of the public OTA repo and publishes it (commit + push).
# Usage: powershell -File release_ota.ps1 -Version 1.0.1 [-SkipBuild] [-NoPush]
$ErrorActionPreference = 'Stop'
$proj = $PSScriptRoot
if (-not $OtaRepo) {
    $OtaRepo = Join-Path (Split-Path $proj -Parent) 'vessel-motion-p4-OTA'
}
$otaRepo = [IO.Path]::GetFullPath($OtaRepo)
$otaBase = 'https://raw.githubusercontent.com/barba-productions/vessel-motion-p4-OTA/main/stable'

if (-not (Test-Path -LiteralPath (Join-Path $otaRepo '.git'))) {
    throw "OTA repository not found: $otaRepo"
}
Push-Location $proj
try {

Write-Host "=== BUMP VERSION $Version ==="
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be major.minor.patch' }
$globals = Get-Content main\globals.h -Raw
if (-not [regex]::IsMatch($globals, '#define FIRMWARE_VERSION "[^"]+"')) {
    throw 'FIRMWARE_VERSION definition missing from globals.h.'
}
if ($SkipBuild) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $proj 'build\vessel_motion_p4.bin'))
    # ESP-IDF image: 24-byte header + 8-byte first segment header + app descriptor.
    if ($bytes.Length -lt 80 -or $bytes[0] -ne 0xE9 -or
        [BitConverter]::ToUInt32($bytes, 32) -ne 2882360370) { throw 'Invalid ESP-IDF application binary.' }
    $binaryVersion = [Text.Encoding]::UTF8.GetString($bytes, 48, 32).TrimEnd([char]0)
    if ($binaryVersion -ne $Version) {
        throw "SkipBuild refused: binary version is $binaryVersion, requested version is $Version."
    }
}
$globals = [regex]::Replace($globals, '#define FIRMWARE_VERSION "[^"]+"', "#define FIRMWARE_VERSION `"$Version`"")
[IO.File]::WriteAllText((Join-Path $proj 'main\globals.h'), $globals, (New-Object Text.UTF8Encoding($false)))

if (-not $SkipBuild) {
    Write-Host "=== BUILD ==="
    & "$proj\build_p4.ps1" -IdfProfile $IdfProfile
}

Write-Host "=== PACKAGE ==="
$bin = "$proj\build\vessel_motion_p4.bin"
$sha = (Get-FileHash $bin -Algorithm SHA256).Hash.ToLower()
$size = (Get-Item $bin).Length
New-Item -ItemType Directory -Force "$otaRepo\stable" | Out-Null
Copy-Item $bin "$otaRepo\stable\vessel_motion_p4.bin" -Force
$manifest = [ordered]@{
    schema = 1
    product = [regex]::Match($globals, '#define FIRMWARE_PRODUCT "([^"]+)"').Groups[1].Value
    architecture = 'esp32p4'
    version = $Version
    size = $size
    sha256 = $sha
    url = "$otaBase/vessel_motion_p4.bin"
}
$manifest | ConvertTo-Json | Set-Content "$otaRepo\stable\manifest.json"
Write-Host "manifest: version=$Version size=$size sha256=$($sha.Substring(0,12))..."

if (-not $NoPush) {
    Write-Host "=== PUBLISH OTA REPO ==="
    Set-Location $otaRepo
    git add -- stable/vessel_motion_p4.bin stable/manifest.json
    if ($LASTEXITCODE -ne 0) { throw 'git add failed' }
    git commit --only -m "OTA release $Version" -m "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>" -- stable/vessel_motion_p4.bin stable/manifest.json
    if ($LASTEXITCODE -ne 0) { throw 'git commit failed' }
    git push
    if ($LASTEXITCODE -ne 0) { throw 'git push failed' }
    Write-Host "OTA $Version published to vessel-motion-p4-OTA."
} else {
    Write-Host "NoPush set - OTA repo updated locally only ($otaRepo)."
}
Write-Host "=== DONE ==="
} finally {
    Pop-Location
}
