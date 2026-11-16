param(
    [string]$BaseUrl = $env:VESSEL_BASE_URL,
    [ValidateRange(1, 300)][int]$WaitSeconds = 30,
    [string[]]$Files,
    [switch]$Format
)

$ErrorActionPreference = 'Stop'
$webroot = Join-Path $PSScriptRoot 'web'
Get-Command curl.exe -ErrorAction Stop | Out-Null
if ($Format -and $Files) { throw '-Format cannot be combined with -Files: formatting requires all assets.' }
if ($Files) {
    $selected = foreach ($relative in $Files) {
        $path = [IO.Path]::GetFullPath((Join-Path $webroot $relative))
        if (-not $path.StartsWith("$webroot\", [StringComparison]::OrdinalIgnoreCase)) {
            throw "Asset must be inside web: $relative"
        }
        Get-Item -LiteralPath $path -ErrorAction Stop
    }
    $assets = @($selected | Sort-Object FullName -Unique)
    if ($assets | Where-Object { $_.PSIsContainer }) { throw '-Files accepts files, not directories.' }
} else {
    $assets = @(Get-ChildItem -LiteralPath $webroot -Recurse -File)
}
if (-not $assets.Count) { throw 'No web assets selected; refusing deployment.' }

$candidates = @('http://192.168.11.222', 'http://192.168.4.1')
if ($BaseUrl) {
    $target = $null
    if (-not [uri]::TryCreate($BaseUrl, [UriKind]::Absolute, [ref]$target) -or
        $target.Scheme -notin @('http', 'https') -or $target.Query -or $target.Fragment) {
        throw 'BaseUrl must be an absolute HTTP/HTTPS URL without a query or fragment.'
    }
    $candidates = @($BaseUrl.TrimEnd('/'))
}

$download = [IO.Path]::GetTempFileName()
try {
    $base = $null
    $timer = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "=== WAIT FOR BOARD (up to $WaitSeconds seconds) ==="
    while (-not $base -and $timer.Elapsed.TotalSeconds -lt $WaitSeconds) {
        foreach ($candidate in $candidates) {
            $remaining = $WaitSeconds - $timer.Elapsed.TotalSeconds
            if ($remaining -le 0) { break }
            $timeout = [Math]::Min(3, $remaining).ToString('0.000', [Globalization.CultureInfo]::InvariantCulture)
            Write-Host "Checking $candidate/health ..."
            $code = & curl.exe --noproxy '*' --silent --show-error --connect-timeout 2 --max-time $timeout -o $download -w '%{http_code}' "$candidate/health"
            if ($LASTEXITCODE -eq 0 -and $code -eq '200') {
                $health = Get-Content -LiteralPath $download -Raw | ConvertFrom-Json
                if ($health.status -ne 'ok') { throw "Unexpected health response from $candidate." }
                $base = $candidate
                break
            }
            Write-Host "Not ready (curl exit=$LASTEXITCODE, HTTP=$code)."
        }
        if (-not $base) {
            $remainingMs = ($WaitSeconds - $timer.Elapsed.TotalSeconds) * 1000
            if ($remainingMs -gt 0) { Start-Sleep -Milliseconds ([int][Math]::Min(1000, $remainingMs)) }
        }
    }
    if (-not $base) {
        throw "Board unreachable after $WaitSeconds seconds. Use -BaseUrl with its actual DHCP address, or connect to Vessel-Motion at 192.168.4.1 if wireless is enabled. No assets were changed."
    }
    if ($Format) {
        Write-Host '=== FORMAT WEB PARTITION (explicitly requested) ==='
        $code = & curl.exe --noproxy '*' --silent --show-error --connect-timeout 3 --max-time 60 -X POST -o $download -w '%{http_code}' "$base/api/format"
        if ($LASTEXITCODE -ne 0 -or $code -ne '200') { throw "Web format failed (curl exit=$LASTEXITCODE, HTTP=$code)." }
        $response = Get-Content -LiteralPath $download -Raw | ConvertFrom-Json
        if ($response.formatted -ne $true) { throw 'Board did not confirm successful formatting.' }
    }
    $uploaded = 0; $unchanged = 0
    foreach ($asset in $assets) {
        $relative = $asset.FullName.Substring($webroot.Length + 1).Replace('\', '/')
        $urlPath = (($relative -split '/') | ForEach-Object { [uri]::EscapeDataString($_) }) -join '/'
        $hash = (Get-FileHash -LiteralPath $asset.FullName -Algorithm SHA256).Hash
        if (-not $Format) {
            $code = & curl.exe --noproxy '*' --silent --show-error --connect-timeout 3 --max-time 120 -o $download -w '%{http_code}' "$base/$urlPath"
            if ($LASTEXITCODE -ne 0 -or $code -notin @('200', '404')) {
                throw "Cannot compare $relative (curl exit=$LASTEXITCODE, HTTP=$code); upload stopped."
            }
            if ($code -eq '200' -and (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -eq $hash) {
                Write-Host "Unchanged: $relative"
                $unchanged++
                continue
            }
        }
        Write-Host "Uploading $relative ($($asset.Length) bytes)"
        $encoded = [uri]::EscapeDataString($relative)
        $code = & curl.exe --noproxy '*' --silent --show-error --connect-timeout 3 --max-time 120 -o $download -w '%{http_code}' --data-binary "@$($asset.FullName)" "$base/api/upload?path=$encoded"
        if ($LASTEXITCODE -ne 0 -or $code -ne '204') { throw "Upload failed for $relative (curl exit=$LASTEXITCODE, HTTP=$code)." }
        $code = & curl.exe --noproxy '*' --silent --show-error --connect-timeout 3 --max-time 120 -o $download -w '%{http_code}' "$base/$urlPath"
        if ($LASTEXITCODE -ne 0 -or $code -ne '200' -or
            (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $hash) {
            throw "SHA256 verification failed for $relative (curl exit=$LASTEXITCODE, HTTP=$code)."
        }
        $uploaded++
    }
    Write-Host "=== DONE: uploaded=$uploaded unchanged=$unchanged total=$($assets.Count) (SHA256 verified) ==="
} finally {
    Remove-Item -LiteralPath $download -Force -ErrorAction Stop
}
