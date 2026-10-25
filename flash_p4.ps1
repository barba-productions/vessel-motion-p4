$ErrorActionPreference = 'Continue'
Set-Location "c:\Users\barba\OneDrive\Documents\___Arduino\Project 28. Vessel Motion P4"
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1' | Out-Null
Write-Host "=== FLASH ==="
& idf.py -p COM3 flash 2>&1 | Select-String -Pattern 'Hash of data verified|Hard resetting|Failed|error' | Select-Object -First 15
Write-Host "=== FLASH DONE ==="
Start-Sleep -Seconds 8
Write-Host "=== LIST ==="
try { (Invoke-WebRequest -Uri 'http://192.168.11.222/api/list' -UseBasicParsing -TimeoutSec 10).Content } catch { Write-Host "LIST FAILED: $_" }
Write-Host "=== LIST DONE ==="
