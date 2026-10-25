$ErrorActionPreference = 'Continue'
$proj = 'c:\Users\barba\OneDrive\Documents\___Arduino\Project 28. Vessel Motion P4'
$webroot = 'c:\Users\barba\OneDrive\Documents\___Arduino\Project 28. Vessel Motion P4\web'
$base = 'http://192.168.11.222'
Set-Location $proj

Write-Host "=== BUILD ==="
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1' | Out-Null
$buildOut = & idf.py build 2>&1 | Out-String
if ($buildOut -match 'Project build complete') { Write-Host "BUILD OK" } else { Write-Host "BUILD FAILED"; $buildOut | Select-String -Pattern 'error:' | Select-Object -First 10; exit 1 }

Write-Host "=== FLASH ==="
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
$flashOut = & idf.py -p COM3 flash 2>&1 | Out-String
if ($flashOut -match 'Hash of data verified') { Write-Host "FLASH OK" } else { Write-Host "FLASH FAILED"; exit 1 }

& "$proj\upload_web.ps1"
