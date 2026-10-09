Unicode true
!include "MUI2.nsh"
!ifndef VERSION
  !error "Pass /DVERSION=..."
!endif
!ifndef STAGE
  !error "Pass /DSTAGE=..."
!endif
!ifndef OUTPUT
  !error "Pass /DOUTPUT=..."
!endif
Name "Arcade Shelf"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\Arcade Shelf"
InstallDirRegKey HKCU "Software\Arcade\Shelf" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Arcade Shelf"
VIAddVersionKey "FileDescription" "Arcade Shelf per-user installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Arcade contributors"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "${SOURCE}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Section "Arcade Shelf"
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Arcade\Shelf" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf" "DisplayName" "Arcade Shelf"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf" "UninstallString" '$"$INSTDIR\Uninstall.exe$"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf" "QuietUninstallString" '$"$INSTDIR\Uninstall.exe$" /S'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\Arcade"
  CreateShortcut "$SMPROGRAMS\Arcade\Arcade Shelf.lnk" "$INSTDIR\arcade-shelf.exe"
SectionEnd
Section "Uninstall"
  SetShellVarContext current
  ExecWait '$"$INSTDIR\arcade-shelf.exe$" --quit'
  Delete "$SMPROGRAMS\Arcade\Arcade Shelf.lnk"
  Delete "$LOCALAPPDATA\Arcade\apps\arcade.shelf.json"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Arcade Shelf"
  DeleteRegKey HKCU "Software\Arcade\Shelf"
  # User collections and original files are intentionally retained.
  RMDir /r "$INSTDIR"
SectionEnd
