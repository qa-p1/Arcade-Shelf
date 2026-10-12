param([string]$Version, [string]$Output = "dist")
$ErrorActionPreference = "Stop"
$Root = Join-Path $env:RUNNER_TEMP "shelf-installer-smoke"
$env:ARCADE_HOME = Join-Path $Root "arcade"
$env:ARCADE_SHELF_HOME = Join-Path $Root "profile"
$env:ARCADE_NO_INSTALL_PROMPT = "1"
$InstallDir = Join-Path $Root "installed"
$Installer = Get-ChildItem "$Output/Arcade-Shelf-$Version-windows-*.exe" | Select-Object -First 1
if (!$Installer) { throw "Shelf installer missing" }
New-Item -ItemType Directory -Force $env:ARCADE_SHELF_HOME | Out-Null
Set-Content (Join-Path $env:ARCADE_SHELF_HOME "keep.txt") "retain settings and content"
try {
    $Process = Start-Process -FilePath $Installer.FullName -ArgumentList "/S", "/D=$InstallDir" -Wait -PassThru
    if ($Process.ExitCode -ne 0) { throw "Silent install failed: $($Process.ExitCode)" }
    $ReceiptPath = Join-Path $env:ARCADE_HOME "installs/arcade.shelf.json"
    $Receipt = Get-Content $ReceiptPath -Raw | ConvertFrom-Json
    if ($Receipt.id -ne "arcade.shelf" -or $Receipt.method -ne "windows-installer" -or $Receipt.version -ne $Version) {
        throw "Incorrect installation receipt"
    }
    if (!(Test-Path $Receipt.integration.cli) -or !(Test-Path $Receipt.integration.desktopEntry)) {
        throw "CLI shim or Start menu shortcut missing"
    }
    & "$InstallDir/arcade-shelf.exe" --integration-status
    if ($LASTEXITCODE -ne 0) { throw "Installed integration status failed" }
    $Process = Start-Process -FilePath "$InstallDir/uninstall.exe" -ArgumentList "/S", "_?=$InstallDir" -Wait -PassThru
    if ($Process.ExitCode -ne 0) { throw "Silent uninstall failed: $($Process.ExitCode)" }
    if ((Test-Path $ReceiptPath) -or (Test-Path $Receipt.integration.cli) -or (Test-Path $Receipt.integration.desktopEntry)) {
        throw "Receipt or integration remained after uninstall"
    }
    if (!(Test-Path (Join-Path $env:ARCADE_SHELF_HOME "keep.txt"))) { throw "Uninstall removed app data" }
    Write-Host "Windows silent install/uninstall, receipt, shim and data preservation passed (CI only)."
} finally {
    Remove-Item Env:ARCADE_HOME, Env:ARCADE_SHELF_HOME, Env:ARCADE_NO_INSTALL_PROMPT -ErrorAction SilentlyContinue
}
