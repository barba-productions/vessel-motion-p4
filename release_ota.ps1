param(
    [Parameter(Mandatory=$true)][string]$Version,
    [switch]$SkipBuild,
    [switch]$NoPush
)
# OTA release workflow for Vessel Motion P4.
# Bumps the version, builds, packages the firmware binary into the local
# clone of the public OTA repo and publishes it (commit + push).
# Usage: powershell -File release_ota.ps1 -Version 1.0.1 [-SkipBuild] [-NoPush]
$ErrorActionPreference = 'Stop'
$proj = 'c:\Users\barba\OneDrive\Documents\___Arduino\Project 28. Vessel Motion P4'
$otaRepo = 'c:\Users\barba\OneDrive\Documents\___Arduino\vessel-motion-p4-OTA'
$otaBase = 'https://raw.githubusercontent.com/barba-productions/vessel-motion-p4-OTA/main/stable'

Set-Location $proj

Write-Host "=== BUMP VERSION $Version ==="
$cmake = Get-Content CMakeLists.txt -Raw
$cmake = [regex]::Replace($cmake, 'project\(vessel_motion_p4 VERSION [0-9.]+\)', "project(vessel_motion_p4 VERSION $Version)")
Set-Content CMakeLists.txt $cmake -NoNewline
$ws = Get-Content main\webserver.c -Raw
$ws = [regex]::Replace($ws, '#define P4_VERSION "[0-9a-zA-Z.\-]+"', "#define P4_VERSION `"$Version`"")
Set-Content main\webserver.c $ws -NoNewline

if (-not $SkipBuild) {
    Write-Host "=== BUILD ==="
    Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    . 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1' | Out-Null
    $out = & idf.py build 2>&1 | Out-String
    if ($out -match 'Project build complete') { Write-Host "BUILD OK" } else { Write-Host "BUILD FAILED"; exit 1 }
}

Write-Host "=== PACKAGE ==="
$bin = "$proj\build\vessel_motion_p4.bin"
$sha = (Get-FileHash $bin -Algorithm SHA256).Hash.ToLower()
$size = (Get-Item $bin).Length
New-Item -ItemType Directory -Force "$otaRepo\stable" | Out-Null
Copy-Item $bin "$otaRepo\stable\vessel_motion_p4.bin" -Force
$manifest = [ordered]@{
    schema = 1
    product = 'vessel-motion-p4'
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
    git add -A
    git commit -m "OTA release $Version" 2>&1 | Select-Object -Last 1
    git push 2>&1 | Select-Object -Last 1
    Write-Host "OTA $Version published to vessel-motion-p4-OTA."
} else {
    Write-Host "NoPush set - OTA repo updated locally only ($otaRepo)."
}
Write-Host "=== DONE ==="
