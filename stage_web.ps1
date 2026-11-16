param(
    [string]$Port = 'COM3',
    [ValidateRange(9600, 2000000)][int]$Baud = 460800,
    [string]$BaseUrl = $env:VESSEL_BASE_URL,
    [ValidateRange(1, 300)][int]$WaitSeconds = 60,
    [string]$IdfProfile = $env:VESSEL_IDF_PROFILE,
    [switch]$Format
)

$ErrorActionPreference = 'Stop'
& "$PSScriptRoot\flash_p4.ps1" -Port $Port -Baud $Baud -IdfProfile $IdfProfile
& "$PSScriptRoot\upload_web.ps1" -BaseUrl $BaseUrl -WaitSeconds $WaitSeconds -Format:$Format
