$ErrorActionPreference = 'Continue'
# Uploads the P4's web/ folder to the device's LittleFS 'web' partition over HTTP.
# Standalone: run after any flash, or wire into the ESP-IDF fire button via
# the idf.postFlashTask setting in .vscode/settings.json.
$proj = 'c:\Users\barba\OneDrive\Documents\___Arduino\Project 28. Vessel Motion P4'
$webroot = "$proj\web"
$base = 'http://192.168.11.222'

Write-Host "=== WAIT FOR BOARD ==="
$up = $false
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Seconds 2
    try { $h = Invoke-WebRequest -Uri "$base/health" -UseBasicParsing -TimeoutSec 3; if ($h.StatusCode -eq 200) { $up = $true; break } } catch {}
}
if (-not $up) { Write-Host "BOARD NOT UP ($base)"; exit 1 }
Write-Host "BOARD UP"

Write-Host "=== FORMAT WEB PARTITION ==="
try { $r = Invoke-WebRequest -Uri "$base/api/format" -Method Post -UseBasicParsing -TimeoutSec 30; Write-Host $r.Content } catch { Write-Host "FORMAT FAILED: $_"; exit 1 }
Start-Sleep -Seconds 2

Write-Host "=== UPLOAD ==="
$files = Get-ChildItem $webroot -Recurse -File
$ok = 0; $fail = 0
foreach ($f in $files) {
    $rel = $f.FullName.Substring($webroot.Length + 1).Replace('\', '/')
    $enc = [uri]::EscapeDataString($rel)
    $resp = & curl.exe -s -o NUL -w "%{http_code}" --data-binary "@$($f.FullName)" "$base/api/upload?path=$enc"
    if ($resp -eq '204') { $ok++ } else { $fail++; Write-Host "UPLOAD FAIL $rel -> $resp" }
}
Write-Host "uploaded ok=$ok fail=$fail total=$($files.Count)"

Write-Host "=== VERIFY ==="
Start-Sleep -Seconds 2
$vok = 0; $vfail = 0
foreach ($f in $files) {
    $rel = $f.FullName.Substring($webroot.Length + 1).Replace('\', '/')
    $srcLen = $f.Length
    try {
        $g = Invoke-WebRequest -Uri "$base/$rel" -UseBasicParsing -TimeoutSec 15
        $gotLen = $g.RawContentLength
        if ($gotLen -eq $srcLen) { $vok++ } else { $vfail++; Write-Host "VERIFY FAIL $rel src=$srcLen got=$gotLen" }
    } catch { $vfail++; Write-Host "VERIFY ERR $rel $_" }
}
Write-Host "verify ok=$vok fail=$vfail"
Write-Host "=== DONE ==="
