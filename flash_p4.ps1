param(
    [string]$Port = 'COM3',
    [ValidateRange(9600, 2000000)][int]$Baud = 460800,
    [string]$IdfProfile = $env:VESSEL_IDF_PROFILE,
    [switch]$Monitor,
    [switch]$ListWeb,
    [string]$BaseUrl = $env:VESSEL_BASE_URL
)

$ErrorActionPreference = 'Stop'
if (-not $Port.Trim()) { throw 'Port must not be empty.' }
if ($ListWeb -and -not $BaseUrl) { throw '-ListWeb requires -BaseUrl or VESSEL_BASE_URL.' }
if ($ListWeb) {
    $target = $null
    if (-not [uri]::TryCreate($BaseUrl, [UriKind]::Absolute, [ref]$target) -or
        $target.Scheme -notin @('http', 'https')) { throw 'BaseUrl must be an absolute HTTP/HTTPS URL.' }
    Get-Command curl.exe -ErrorAction Stop | Out-Null
}
. "$PSScriptRoot\p4_tools.ps1"
Initialize-P4Environment -IdfProfile $IdfProfile
Write-Host 'Close the serial monitor before flashing; this script does not terminate processes.'
# Build separately first so a failed build can never proceed to flashing.
Invoke-P4Idf -Arguments @('build')
Write-Host '=== FLASH ==='
Invoke-P4Idf -Arguments @('-p', $Port, '-b', "$Baud", 'flash')
Write-Host '=== FLASH COMPLETE ==='
if ($ListWeb) {
    & curl.exe --noproxy '*' --fail-with-body --show-error --connect-timeout 3 --max-time 15 "$($BaseUrl.TrimEnd('/'))/api/list"
    if ($LASTEXITCODE -ne 0) { throw "Flash succeeded, but web listing failed (curl exit $LASTEXITCODE)." }
}
if ($Monitor) { Invoke-P4Idf -Arguments @('-p', $Port, 'monitor') }
