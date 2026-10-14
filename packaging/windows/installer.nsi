!include "MUI2.nsh"
!ifndef VERSION
  !error "Pass /DVERSION=..."
!endif
!ifndef SOURCE
  !error "Pass /DSOURCE=..."
!endif
!ifndef STAGE
  !error "Pass /DSTAGE=..."
!endif
!ifndef OUTPUT
  !error "Pass /DOUTPUT=..."
!endif
!ifndef UNINSTALL_FILES
  !error "Pass /DUNINSTALL_FILES=... (generated from STAGE)"
!endif

!define ARCADE_ID "arcade.shelf"
!define ARCADE_NAME "Arcade Shelf"
!define ARCADE_VERSION "${VERSION}"
!define ARCADE_EXE "arcade-shelf.exe"
!define ARCADE_CLI "arcade-shelf"
!define ARCADE_START_AT_LOGIN_DEFAULT 0
!define ARCADE_HOMEPAGE "https://github.com/qa-p1/Arcade-catalog/tree/main/docs"
!include "${SOURCE}\vendor\arcade-link\shared\packaging\windows\arcade.nsh"
!insertmacro ArcadeSetup

OutFile "${OUTPUT}"
InstallDirRegKey HKCU "Software\Arcade\Shelf" "InstallDir"
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Arcade Shelf"
VIAddVersionKey "FileDescription" "Arcade Shelf per-user installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Arcade contributors"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "${SOURCE}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!insertmacro ArcadeTasks

Function .onInit
  !insertmacro ArcadeInitTasks
FunctionEnd

Section "Arcade Shelf" ArcadeShelfApplication
  SectionIn RO
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  WriteRegStr HKCU "Software\Arcade\Shelf" "InstallDir" "$INSTDIR"
  !insertmacro ArcadeInstall
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  ExecWait '$"$INSTDIR\arcade-shelf.exe$" --quit'
  !insertmacro ArcadeUninstall
  ${If} $ArcadeExitCode == 0
    DeleteRegKey HKCU "Software\Arcade\Shelf"
    ; Only delete the staged payload, then its empty directories, deepest first.
    ; A custom install directory may contain unrelated files or user data.
    !include "${UNINSTALL_FILES}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
  ${Else}
    ; Exit 10 means a successor owns the receipt. Keep payload and install
    ; metadata: that successor may live inside the old custom directory.
    DetailPrint "A successor owns this installation; leaving application files intact."
  ${EndIf}
SectionEnd
