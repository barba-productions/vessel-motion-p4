param([string]$IdfProfile = $env:VESSEL_IDF_PROFILE)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\p4_tools.ps1"
Initialize-P4Environment -IdfProfile $IdfProfile
Write-Host '=== BUILD ==='
Invoke-P4Idf -Arguments @('build')
Write-Host '=== BUILD COMPLETE ==='
