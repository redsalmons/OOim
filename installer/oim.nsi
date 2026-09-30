; OceanTalk (零海) Windows installer — Modern UI 2 wizard
; Build: "C:\Program Files (x86)\NSIS\makensis.exe" installer\oim.nsi
; Input:  build\windows\arm64\runner\* (produced by run_app.ps1)
; Output: build\windows\arm64\OceanTalk-Setup-1.0.0.exe
; NOTE: this file MUST be saved as UTF-8 **with BOM** — without the BOM NSIS
; decodes it as ANSI (GBK) and the 零海 shortcut/wizard text turns mojibake.

Unicode true
!define APP_NAME      "OceanTalk"
!define APP_NAME_ZH   "零海 OceanTalk"
!define APP_EXE       "oceantalk.exe"
!define APP_VERSION   "1.0.0"
!define APP_PUBLISHER "OceanTalk"
!define BUILD_DIR     "..\build\windows\arm64\runner"
!define OUT_FILE      "..\build\windows\arm64\OceanTalk-Setup-${APP_VERSION}.exe"

!include "MUI2.nsh"

Name "${APP_NAME_ZH}"
OutFile "${OUT_FILE}"
!define MUI_ICON   "..\windows\runner\resources\app_icon.ico"
!define MUI_UNICON "..\windows\runner\resources\app_icon.ico"

; Per-user install: no UAC prompt required.
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"

SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

; ---------------------------------------------------------------------------
; Wizard pages: Welcome -> Directory (install path) -> Install -> Finish
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
Section "Install" SEC01
  ; Kill a running instance so locked binaries can be replaced.
  nsExec::ExecToLog 'taskkill /F /IM ${APP_EXE}'
  Pop $0

  SetOutPath "$INSTDIR"
  File /r "${BUILD_DIR}\*.*"

  ; Shortcuts
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut  "$SMPROGRAMS\${APP_NAME}\${APP_NAME_ZH}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut  "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"      "$INSTDIR\uninstall.exe"
  CreateShortcut  "$DESKTOP\${APP_NAME_ZH}.lnk" "$INSTDIR\${APP_EXE}"

  ; Add/Remove Programs entry
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "DisplayName"     "${APP_NAME_ZH}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "DisplayIcon"     "$INSTDIR\${APP_EXE}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "DisplayVersion"  "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
      "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; ---------------------------------------------------------------------------
Section "Uninstall"
  nsExec::ExecToLog 'taskkill /F /IM ${APP_EXE}'
  Pop $0

  ; Remove installed files (keeps user data in %APPDATA%\com.example\OceanTalk).
  RMDir /r "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME_ZH}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME_ZH}.lnk"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
SectionEnd
