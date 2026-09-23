$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $projectDir

python -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --windowed `
    --name "Ellisys_HCI_Bridge_v1.0.0" `
    "ellisys_hci_bridge.py"

Copy-Item -Force `
    "$projectDir\dist\Ellisys_HCI_Bridge_v1.0.0.exe" `
    "$projectDir\Ellisys_HCI_Bridge_v1.0.0.exe"

Write-Host "Built: $projectDir\Ellisys_HCI_Bridge_v1.0.0.exe"
