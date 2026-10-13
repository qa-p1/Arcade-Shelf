param([string]$Version, [string]$Output = "dist")
$ErrorActionPreference = "Stop"
if (!$env:RUNNER_TEMP) { throw "Run this only in the Windows CI runner" }
$Root = Join-Path $env:RUNNER_TEMP ("shelf-installer-smoke-" + [guid]::NewGuid().ToString("N"))
$env:ARCADE_HOME = Join-Path $Root "arcade"
$env:ARCADE_SHELF_HOME = Join-Path $Root "profile"
$env:ARCADE_NO_INSTALL_PROMPT = "1"
$InstallDir = Join-Path $Root "custom install"
$Installer = Get-ChildItem "$Output/Arcade-Shelf-$Version-windows-*.exe" | Select-Object -First 1
if (!$Installer) { throw "Shelf installer missing" }
New-Item -ItemType Directory -Force $env:ARCADE_SHELF_HOME | Out-Null
Set-Content (Join-Path $env:ARCADE_SHELF_HOME "keep.txt") "retain settings and content"
$UnrelatedFiles = @(
    (Join-Path $InstallDir "unrelated.txt"),
    (Join-Path $InstallDir "share/arcade-shelf/licenses/user-note.txt")
)
foreach ($Path in $UnrelatedFiles) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    Set-Content -LiteralPath $Path "pre-existing user file"
}

function Install-Shelf([string]$Directory) {
    # NSIS consumes /D= through the end of the command line, including spaces.
    $Process = Start-Process -FilePath $Installer.FullName -ArgumentList "/S", "/D=$Directory" -Wait -PassThru
    if ($Process.ExitCode -ne 0) { throw "Silent install failed: $($Process.ExitCode)" }
}

function Uninstall-Shelf([string]$Directory, [string]$Name) {
    # Run a copy with _?= last/unquoted to wait for completion while allowing
    # the original uninstall.exe to be deleted (Windows locks running exes).
    $Executable = Join-Path $Root $Name
    Copy-Item -LiteralPath (Join-Path $Directory "uninstall.exe") -Destination $Executable
    $Process = Start-Process -FilePath $Executable -ArgumentList "/S", "_?=$Directory" -Wait -PassThru
    if ($Process.ExitCode -ne 0) { throw "Silent uninstall failed: $($Process.ExitCode)" }
}

try {
    Install-Shelf $InstallDir
    $Payload = @(Get-ChildItem -LiteralPath $InstallDir -Recurse -File -Force |
        Where-Object { $_.FullName -notin $UnrelatedFiles } | Select-Object -ExpandProperty FullName)
    if ($Payload.Count -lt 3) { throw "Installed payload inventory is empty" }
    $ReceiptPath = Join-Path $env:ARCADE_HOME "installs/arcade.shelf.json"
    $Receipt = Get-Content $ReceiptPath -Raw | ConvertFrom-Json
    if ($Receipt.id -ne "arcade.shelf" -or $Receipt.method -ne "windows-installer" -or $Receipt.version -ne $Version) {
        throw "Incorrect installation receipt"
    }
    if (!(Test-Path $Receipt.integration.cli) -or !(Test-Path $Receipt.integration.desktopEntry)) {
        throw "CLI shim or Start menu shortcut missing"
    }
    # Shelf is a GUI-subsystem executable: piping its output makes PowerShell
    # wait for it and set $LASTEXITCODE.
    $Status = & "$InstallDir/arcade-shelf.exe" --integration-status | Out-String
    Write-Host $Status
    if ($LASTEXITCODE -ne 0) { throw "Installed integration status failed ($LASTEXITCODE)" }
    if (($Status | ConvertFrom-Json).receipt.id -ne "arcade.shelf") { throw "Integration status has no Shelf receipt" }
    Uninstall-Shelf $InstallDir "uninstall-primary.exe"
    if ((Test-Path $ReceiptPath) -or (Test-Path $Receipt.integration.cli) -or (Test-Path $Receipt.integration.desktopEntry)) {
        throw "Receipt or integration remained after uninstall"
    }
    foreach ($Path in $Payload) {
        if (Test-Path -LiteralPath $Path) { throw "Shelf payload remained after uninstall: $Path" }
    }
    foreach ($Path in $UnrelatedFiles) {
        if (!(Test-Path -LiteralPath $Path) -or (Get-Content -LiteralPath $Path -Raw).Trim() -ne "pre-existing user file") {
            throw "Uninstall damaged an unrelated file: $Path"
        }
    }
    if (!(Test-Path (Join-Path $env:ARCADE_SHELF_HOME "keep.txt"))) { throw "Uninstall removed app data" }

    # A successor inside the old custom directory must survive the old
    # uninstaller's exit-10 path, including its shared receipt and metadata.
    Install-Shelf $InstallDir
    $SuccessorDir = Join-Path $InstallDir "successor install"
    Install-Shelf $SuccessorDir
    $SuccessorReceiptText = Get-Content -LiteralPath $ReceiptPath -Raw
    $Successor = $SuccessorReceiptText | ConvertFrom-Json
    $SuccessorPayload = @(Get-ChildItem -LiteralPath $SuccessorDir -Recurse -File -Force | Get-FileHash -Algorithm SHA256)
    Uninstall-Shelf $InstallDir "uninstall-old.exe"
    if ((Get-Content -LiteralPath $ReceiptPath -Raw) -cne $SuccessorReceiptText) { throw "Old uninstall changed the successor receipt" }
    foreach ($File in $SuccessorPayload) {
        if (!(Test-Path -LiteralPath $File.Path) -or (Get-FileHash -LiteralPath $File.Path -Algorithm SHA256).Hash -ne $File.Hash) {
            throw "Old uninstall damaged successor payload: $($File.Path)"
        }
    }
    foreach ($Path in @($Successor.integration.cli, $Successor.integration.desktopEntry)) {
        if (!(Test-Path -LiteralPath $Path)) { throw "Old uninstall removed successor integration: $Path" }
    }
    $Metadata = Get-ItemProperty -LiteralPath "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\arcade.shelf"
    if ($Metadata.InstallLocation -ne $SuccessorDir) { throw "Old uninstall removed successor Apps and features metadata" }
    $InstallLocation = Get-ItemProperty -LiteralPath "HKCU:\Software\Arcade\Shelf"
    if ($InstallLocation.InstallDir -ne $SuccessorDir) { throw "Old uninstall removed successor install location" }
    Uninstall-Shelf $SuccessorDir "uninstall-successor.exe"
    foreach ($Path in @($ReceiptPath, $Successor.path, $Successor.integration.cli, $Successor.integration.desktopEntry)) {
        if (Test-Path -LiteralPath $Path) { throw "Successor uninstall retained $Path" }
    }
    foreach ($Path in $UnrelatedFiles) {
        if ((Get-Content -LiteralPath $Path -Raw).Trim() -ne "pre-existing user file") { throw "Successor lifecycle damaged $Path" }
    }
    if ((Get-Content -LiteralPath (Join-Path $env:ARCADE_SHELF_HOME "keep.txt") -Raw).Trim() -ne "retain settings and content") {
        throw "Successor lifecycle damaged app data"
    }
    Write-Host "Windows silent lifecycle, custom-directory payload cleanup, unrelated files and successor protection passed (CI only)."
} finally {
    Remove-Item Env:ARCADE_HOME, Env:ARCADE_SHELF_HOME, Env:ARCADE_NO_INSTALL_PROMPT -ErrorAction SilentlyContinue
}
