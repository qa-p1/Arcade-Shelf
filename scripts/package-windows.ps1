param([string]$Build = "build", [string]$QtPrefix = $env:QT_ROOT_DIR, [string]$Version = "0.2.0", [string]$Output = "dist")
$ErrorActionPreference = "Stop"
$Source = Split-Path -Parent $PSScriptRoot
$Stage = Join-Path (Resolve-Path $Build) "package-windows"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
cmake --install $Build --config Release --prefix $Stage
if ($LASTEXITCODE -ne 0) { throw "CMake install failed" }
if (Test-Path "$Stage/bin") { Get-ChildItem "$Stage/bin" | Move-Item -Destination $Stage; Remove-Item "$Stage/bin" }
& "$QtPrefix/bin/windeployqt.exe" --release --qmldir "$Source/ui" --no-translations "$Stage/arcade-shelf.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
if (Test-Path "$QtPrefix/licenses") { Copy-Item -Recurse "$QtPrefix/licenses" "$Stage/share/arcade-shelf/licenses/Qt" }
New-Item -Force -ItemType Directory $Output | Out-Null
$OutputPath = (Resolve-Path $Output).Path
$env:QT_QPA_PLATFORM = "offscreen"
& "$Stage/arcade-shelf.exe" --version
if ($LASTEXITCODE -ne 0) { throw "Packaged executable smoke failed" }
$Makensis = (Get-Command makensis.exe -ErrorAction SilentlyContinue).Source
if (!$Makensis) { $Makensis = "${env:ProgramFiles(x86)}/NSIS/makensis.exe" }
$Architecture = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") { "arm64" } else { "x64" }
& $Makensis "/DVERSION=$Version" "/DSOURCE=$Source" "/DSTAGE=$Stage" "/DOUTPUT=$OutputPath/Arcade-Shelf-$Version-windows-$Architecture.exe" "$Source/packaging/windows/installer.nsi"
if ($LASTEXITCODE -ne 0) { throw "NSIS packaging failed" }
