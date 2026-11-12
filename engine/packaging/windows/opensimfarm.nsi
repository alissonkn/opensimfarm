; OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
; Licensed under the GNU General Public License version 3 (see LICENSE.txt).
;
; Windows installer (NSIS 3, built by tools/package.sh). Installs for the current user only (no administrator
; rights needed) into %LOCALAPPDATA%\Programs\OpenSimFarm, finds the player's own SimFarm folder (the game's own
; search, run from the installer) or lets the player choose it, and remembers it where the game looks
; (%APPDATA%\OpenSimFarm\gamepath.txt). Nothing of the original game is changed (a setup disc's
; packed files are unpacked by the game into its own folder).
;
; makensis -DVERSION=x.y.z -DSTAGE=<folder with opensimfarm.exe README.md LICENSE.txt, and opensimfarm.ico for
;          the installer's own icon (built into the installer; the installed shortcuts use the icon inside the exe)>
;          -DOUTFILE=<installer.exe> opensimfarm.nsi
;
; The installer uses no plugins (they are unpacked as DLLs at run time, which Windows' Smart App Control may block):
; no welcome or finish page, the folder search runs with ExecWait.

Unicode true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!define APP "OpenSimFarm"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenSimFarm"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\OpenSimFarm"
InstallDirRegKey HKCU "Software\OpenSimFarm" "InstallDir"
RequestExecutionLevel user
BrandingText "${APP} ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "OpenSimFarm developers, GPL-3.0-or-later"

Var GameDir
Var Searched

!define MUI_ICON "${STAGE}\opensimfarm.ico"
!define MUI_UNICON "${STAGE}\opensimfarm.ico"
!define MUI_ABORTWARNING

!define MUI_LICENSEPAGE_TEXT_TOP "${APP} ${VERSION} is an open re-implementation of SimFarm. It contains none of the original game's files: it plays with your own copy of SimFarm."
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY

; the SimFarm folder: a second directory page on $GameDir
!define MUI_PAGE_HEADER_TEXT "Your SimFarm folder"
!define MUI_PAGE_HEADER_SUBTEXT "Where your own copy of SimFarm is."
!define MUI_DIRECTORYPAGE_TEXT_TOP "${APP} reads the game files from your SimFarm folder (the one that holds MEGADATA.IDX and MEGADATA.EEA), or the SimFarm for Windows CD. It only reads them; nothing there is changed.$\r$\n$\r$\nThe folder below was found on this computer. If it is wrong or empty, click Browse and choose the right one. You can also leave it empty: the game then asks when it starts."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "SimFarm folder"
!define MUI_DIRECTORYPAGE_VARIABLE $GameDir
!define MUI_DIRECTORYPAGE_VERIFYONLEAVE
!define MUI_PAGE_CUSTOMFUNCTION_PRE GamePagePre
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE GamePageLeave
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; $0 without the line ends (CR, LF) at its end
Function TrimLineEnd
  ${Do}
    StrCpy $3 $0 1 -1
    ${If} $3 == "$\n"
    ${OrIf} $3 == "$\r"
      StrCpy $0 $0 -1
    ${Else}
      ${ExitDo}
    ${EndIf}
  ${Loop}
FunctionEnd

; true when $0 holds the game files, or is SimFarm for Windows' setup disc (its files packed: MEGADATA.ID_ and
; MEGADATA.EE_; the game unpacks them into its own folder when it starts)
Function IsGameFolder
  ${If} ${FileExists} "$0\MEGADATA.IDX"
  ${AndIf} ${FileExists} "$0\MEGADATA.EEA"
    StrCpy $1 1
  ${ElseIf} ${FileExists} "$0\MEGADATA.ID_"
  ${AndIf} ${FileExists} "$0\MEGADATA.EE_"
  ${AndIf} ${FileExists} "$0\SETUP.INF"
    StrCpy $1 1
  ${Else}
    StrCpy $1 0
  ${EndIf}
FunctionEnd

Function GamePagePre
  ${If} $Searched == 1
    Return
  ${EndIf}
  StrCpy $Searched 1
  ; a folder remembered by an earlier installation or by the game
  ClearErrors
  FileOpen $2 "$APPDATA\OpenSimFarm\gamepath.txt" r
  ${IfNot} ${Errors}
    FileRead $2 $0
    FileClose $2
    Call TrimLineEnd
    Call IsGameFolder
    ${If} $1 == 1
      StrCpy $GameDir $0
      Return
    ${EndIf}
  ${EndIf}
  ; the game's own search of the usual places (drive roots, the user's folders), run from a temporary copy
  InitPluginsDir
  SetOutPath "$PLUGINSDIR\find"
  File "${STAGE}\opensimfarm.exe"
  ExecWait '"$PLUGINSDIR\find\opensimfarm.exe" --findgameto "$PLUGINSDIR\found.txt"' $2
  SetOutPath "$TEMP"
  ClearErrors
  FileOpen $3 "$PLUGINSDIR\found.txt" r
  ${IfNot} ${Errors}
    FileRead $3 $0
    FileClose $3
  ${Else}
    StrCpy $2 1
  ${EndIf}
  ${If} $2 == 0
    Call TrimLineEnd
    Call IsGameFolder
    ${If} $1 == 1
      StrCpy $GameDir $0
    ${EndIf}
  ${EndIf}
FunctionEnd

Function GamePageLeave
  ${If} $GameDir == ""
    Return
  ${EndIf}
  StrCpy $0 $GameDir
  Call IsGameFolder
  ${If} $1 == 1
    Return
  ${EndIf}
  ; the folder dialog may have added a name to the chosen folder: try the folder above it too
  ${GetParent} "$GameDir" $0
  Call IsGameFolder
  ${If} $1 == 1
    StrCpy $GameDir $0
    Return
  ${EndIf}
  ; the folder above the SimFarm folder: look one level down
  FindFirst $2 $3 "$GameDir\*"
  ${DoWhile} $3 != ""
    ${If} $3 != "."
    ${AndIf} $3 != ".."
      StrCpy $0 "$GameDir\$3"
      Call IsGameFolder
      ${If} $1 == 1
        FindClose $2
        StrCpy $GameDir $0
        Return
      ${EndIf}
    ${EndIf}
    FindNext $2 $3
  ${Loop}
  FindClose $2
  MessageBox MB_YESNO|MB_ICONEXCLAMATION "This folder does not hold SimFarm's files (MEGADATA.IDX and MEGADATA.EEA):$\r$\n$GameDir$\r$\n$\r$\nChoose another folder? (No: go on without one; the game asks when it starts.)" IDNO skip
  Abort
skip:
  StrCpy $GameDir ""
FunctionEnd

Section "${APP}" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${STAGE}\opensimfarm.exe"
  Delete "$INSTDIR\SDL2.dll" ; versions before 0.6.2 had SDL2 beside the program
  File "${STAGE}\README.md"
  File "${STAGE}\LICENSE.txt"
  ; remember the SimFarm folder where the game looks for it
  ${If} $GameDir != ""
    CreateDirectory "$APPDATA\OpenSimFarm"
    FileOpen $0 "$APPDATA\OpenSimFarm\gamepath.txt" w
    FileWrite $0 "$GameDir$\r$\n"
    FileClose $0
  ${EndIf}
  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\opensimfarm.exe" "" "$INSTDIR\opensimfarm.exe" 0
  ; the same game, but it first lets the player choose the SimFarm folder again
  CreateShortcut "$SMPROGRAMS\${APP} - choose SimFarm folder.lnk" "$INSTDIR\opensimfarm.exe" "--reconfigure" "$INSTDIR\opensimfarm.exe" 0
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\OpenSimFarm" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "OpenSimFarm developers"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\opensimfarm.exe,0"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTKEY}" "URLInfoAbout" "https://github.com/alissonkn/opensimfarm"
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" 5000
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\opensimfarm.exe" "" "$INSTDIR\opensimfarm.exe" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The game, the README and the licence."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "A shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; the finish page needs a plugin: ask instead
Function .onInstSuccess
  IfSilent done
  MessageBox MB_YESNO|MB_ICONQUESTION "${APP} is installed. Start it now?" IDNO done
  Exec '"$INSTDIR\opensimfarm.exe"'
done:
FunctionEnd

; the player's saves and settings (%APPDATA%\OpenSimFarm) are kept
Section "Uninstall"
  Delete "$INSTDIR\opensimfarm.exe"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP} - choose SimFarm folder.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "Software\OpenSimFarm"
SectionEnd
