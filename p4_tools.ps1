function Initialize-P4Environment {
    param([string]$IdfProfile = $env:VESSEL_IDF_PROFILE)

    if (-not $IdfProfile) {
        $settings = Get-Content (Join-Path $PSScriptRoot '.vscode\settings.json') -Raw | ConvertFrom-Json
        $setup = $settings.'idf.currentSetup'
        $version = [regex]::Match($setup, 'v(\d+\.\d+\.\d+)').Groups[1].Value
        if (-not $version) { throw 'Cannot determine ESP-IDF version from idf.currentSetup. Supply -IdfProfile.' }
        $IdfProfile = "C:\Espressif\tools\Microsoft.v$version.PowerShell_profile.ps1"
    }
    if (-not (Test-Path -LiteralPath $IdfProfile -PathType Leaf)) {
        throw "ESP-IDF environment script not found: $IdfProfile. Supply -IdfProfile or VESSEL_IDF_PROFILE."
    }
    . $IdfProfile | Out-Host
    $script:P4Python = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
    $script:P4Idf = Join-Path $env:IDF_PATH 'tools\idf.py'
    foreach ($path in @($script:P4Python, $script:P4Idf)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing ESP-IDF tool: $path" }
    }
}

function Invoke-P4Idf {
    param([string[]]$Arguments)

    if (-not $script:P4Idf) { throw 'Initialize-P4Environment must be called first.' }
    Push-Location $PSScriptRoot
    try {
        & $script:P4Python $script:P4Idf @Arguments | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "ESP-IDF command failed (exit $LASTEXITCODE): idf.py $($Arguments -join ' ')"
        }
    } finally {
        Pop-Location
    }
}
