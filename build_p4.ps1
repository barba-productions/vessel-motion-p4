$ErrorActionPreference = 'Continue'
Set-Location "c:\Users\barba\OneDrive\Documents\___Arduino\Project 28. Vessel Motion P4"
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'idf_monitor|esp_idf_monitor' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1' | Out-Null
Write-Host "=== BUILD ==="
& idf.py build 2>&1 | Tee-Object -FilePath "$env:TEMP\p4_build.log" | Select-String -Pattern 'Project build complete|ninja failed|FAILED|error:|\.c:' | Select-Object -First 30
Write-Host "=== BUILD DONE ==="
