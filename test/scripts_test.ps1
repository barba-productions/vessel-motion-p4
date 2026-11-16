$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path ([IO.Path]::GetTempPath()) ("vessel-script-test-" + [guid]::NewGuid().ToString('N'))
$server = $null

function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}

function Run-Helper([string]$Command, [bool]$Success) {
    & powershell -NoProfile -ExecutionPolicy Bypass -Command $Command | Out-Host
    Assert-True (($LASTEXITCODE -eq 0) -eq $Success) "Unexpected exit code $LASTEXITCODE for $Command"
}

try {
    New-Item -ItemType Directory -Path "$fixture\web", "$fixture\tools", "$fixture\.vscode" -Force | Out-Null
    Copy-Item "$repo\*.ps1" $fixture
    Copy-Item "$repo\.vscode\settings.json" "$fixture\.vscode"
    [IO.File]::WriteAllText("$fixture\web\a.css", 'same')
    [IO.File]::WriteAllText("$fixture\web\b.js", 'new!')
    $idfLog = "$fixture\idf.log"
    $profile = "$fixture\mock-profile.ps1"
    @"
`$env:IDF_PATH = '$fixture'
`$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v5.5.5\venv'
"@ | Set-Content $profile
    @'
import os, sys
with open(os.environ["P4_TEST_LOG"], "a") as log:
    log.write(" ".join(sys.argv[1:]) + "\n")
fail = os.environ.get("P4_TEST_FAIL")
sys.exit(9 if (fail == "1" and "build" in sys.argv) or (fail == "flash" and "flash" in sys.argv) else 0)
'@ | Set-Content "$fixture\tools\idf.py"
    $env:P4_TEST_LOG = $idfLog
    $env:P4_TEST_FAIL = '1'
    Run-Helper "& '$fixture\build_p4.ps1' -IdfProfile '$profile'" $false
    Run-Helper "& '$fixture\flash_p4.ps1' -IdfProfile '$profile'" $false
    Run-Helper "& '$fixture\stage_web.ps1' -IdfProfile '$profile' -BaseUrl http://127.0.0.1:1" $false
    Assert-True (-not ((Get-Content $idfLog -Raw) -match 'flash')) 'A failed build must never flash'
    $env:P4_TEST_FAIL = '0'
    Run-Helper "& '$fixture\flash_p4.ps1' -IdfProfile '$profile' -Port COM99 -Baud 921600" $true
    Assert-True ((Get-Content $idfLog -Raw) -match '-p COM99 -b 921600 flash') 'Port/baud must reach IDF'
    $serverCode = @'
const http=require("http"),fs=require("fs"),path=require("path");
const root=process.argv[2],portFile=path.join(root,"port.txt"),log=path.join(root,"http.log");
const assets=new Map([["a.css",Buffer.from("same")],["b.js",Buffer.from("old!")]]);
http.createServer((req,res)=>{
  const url=new URL(req.url,"http://localhost");
  fs.appendFileSync(log,req.method+" "+url.pathname+"\n");
  if(url.pathname==="/health"){res.setHeader("Content-Type","application/json");res.end('{"status":"ok"}');return;}
  if(url.pathname==="/api/format"){assets.clear();res.end('{"formatted":true}');return;}
  if(url.pathname==="/api/upload"){
    const chunks=[];req.on("data",d=>chunks.push(d));req.on("end",()=>{assets.set(url.searchParams.get("path"),Buffer.concat(chunks));res.writeHead(204);res.end();});return;
  }
  const name=decodeURIComponent(url.pathname.slice(1));
  if(assets.has(name)){res.end(fs.existsSync(path.join(root,"corrupt"))?Buffer.from("bad!"):assets.get(name));}
  else{res.writeHead(404);res.end();}
}).listen(0,"127.0.0.1",function(){fs.writeFileSync(portFile,String(this.address().port));});
'@
    [IO.File]::WriteAllText("$fixture\server.js", $serverCode)
    $server = Start-Process -FilePath (Get-Command node).Source -ArgumentList @("`"$fixture\server.js`"", "`"$fixture`"") -PassThru -NoNewWindow
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path "$fixture\port.txt") -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    Assert-True (Test-Path "$fixture\port.txt") 'Mock HTTP server readiness'
    $base = "http://127.0.0.1:$(Get-Content "$fixture\port.txt")"
    $upload = "& '$fixture\upload_web.ps1' -BaseUrl $base"
    Run-Helper $upload $true
    $requests = Get-Content "$fixture\http.log"
    Assert-True (@($requests | Where-Object { $_ -eq 'POST /api/upload' }).Count -eq 1) 'Only changed asset uploaded'
    Assert-True (-not ($requests -contains 'POST /api/format')) 'Default upload must not format'
    Run-Helper "$upload -Files @('a.css','b.js')" $true
    Assert-True (@((Get-Content "$fixture\http.log") | Where-Object { $_ -eq 'POST /api/upload' }).Count -eq 1) 'Second run skips identical assets'
    Run-Helper "$upload -Format -Files @('a.css')" $false
    Run-Helper "$upload -Files @('..\p4_tools.ps1')" $false
    Run-Helper "$upload -Format" $true
    Assert-True ((Get-Content "$fixture\http.log") -contains 'POST /api/format') 'Explicit format invokes format endpoint'
    [IO.File]::WriteAllText("$fixture\corrupt", '')
    Run-Helper "$upload -Files @('b.js')" $false
    Run-Helper "& '$fixture\upload_web.ps1' -BaseUrl http://127.0.0.1:1 -WaitSeconds 1 -Files @('a.css')" $false
    # SkipBuild must reject a mismatched image before changing globals or packaging.
    New-Item -ItemType Directory -Path "$fixture\main", "$fixture\build", "$fixture\ota\.git" -Force | Out-Null
    Copy-Item "$repo\main\globals.h" "$fixture\main"
    $before = (Get-FileHash "$fixture\main\globals.h").Hash
    $bytes = New-Object byte[] 80
    $bytes[0] = 0xE9
    [BitConverter]::GetBytes([uint32]2882360370).CopyTo($bytes, 32)
    [Text.Encoding]::UTF8.GetBytes('0.0.1').CopyTo($bytes, 48)
    [IO.File]::WriteAllBytes("$fixture\build\vessel_motion_p4.bin", $bytes)
    Run-Helper "& '$fixture\release_ota.ps1' -Version 9.9.9 -SkipBuild -NoPush -OtaRepo '$fixture\ota'" $false
    Assert-True ((Get-FileHash "$fixture\main\globals.h").Hash -eq $before) 'Mismatched release must not change firmware version'
    Assert-True (-not (Test-Path "$fixture\ota\stable")) 'Mismatched release must not package'
    Run-Helper "& '$fixture\release_ota.ps1' -Version 0.0.1 -SkipBuild -NoPush -OtaRepo '$fixture\ota'" $true
    $manifest = Get-Content "$fixture\ota\stable\manifest.json" -Raw | ConvertFrom-Json
    Assert-True ($manifest.version -eq '0.0.1') 'Release manifest matches image version'
    Assert-True ($manifest.sha256 -eq (Get-FileHash "$fixture\ota\stable\vessel_motion_p4.bin").Hash.ToLower()) 'Release manifest SHA256 matches image'
    New-Item -ItemType Directory -Path "$fixture\default-project", "$fixture\vessel-motion-p4-OTA\.git" -Force | Out-Null
    Copy-Item "$fixture\release_ota.ps1" "$fixture\default-project"
    Copy-Item "$fixture\main", "$fixture\build" "$fixture\default-project" -Recurse
    Run-Helper "& powershell -NoProfile -ExecutionPolicy Bypass -File '$fixture\default-project\release_ota.ps1' -Version 0.0.1 -SkipBuild -NoPush" $true
    Assert-True (Test-Path "$fixture\vessel-motion-p4-OTA\stable\manifest.json") 'File invocation resolves the default sibling OTA repository'
    $env:P4_TEST_FAIL = '1'
    Run-Helper "& '$fixture\release_ota.ps1' -Version 0.0.2 -NoPush -OtaRepo '$fixture\ota' -IdfProfile '$profile'" $false
    Assert-True ((Get-Content "$fixture\ota\stable\manifest.json" -Raw | ConvertFrom-Json).version -eq '0.0.1') 'Failed release build must not replace packaged assets'
    $env:P4_TEST_FAIL = 'flash'
    [IO.File]::WriteAllText("$fixture\upload_web.ps1", "[IO.File]::WriteAllText('$fixture\upload-called', 'unexpected')")
    Run-Helper "& '$fixture\stage_web.ps1' -IdfProfile '$profile'" $false
    Assert-True (-not (Test-Path "$fixture\upload-called")) 'A failed flash must never upload'
    Write-Host 'PASS deployment helpers: failure gates, port/baud, selective uploads, SHA256, explicit format and release validation'
} finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Remove-Item Env:\P4_TEST_LOG, Env:\P4_TEST_FAIL -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
