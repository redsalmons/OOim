; OceanTalk installer - build once per arch:
;   ISCC.exe /DArch=x64   tools\installer.iss
;   ISCC.exe /DArch=arm64 tools\installer.iss

#ifndef Arch
  #define Arch "x64"
#endif
#if Arch == "x64"
  #define AllowedArchs "x64compatible"
  #define InstallMode "x64"
#else
  #define AllowedArchs "arm64"
  #define InstallMode "arm64"
#endif

#define AppName "OceanTalk"
#define AppVersion "1.0.0"
#define AppPublisher "com.example"
#define AppExe "oceantalk.exe"
#define SourceDir "..\build\windows\" + Arch + "\runner"

[Setup]
AppId={{8F2A1B3E-5C4D-4E6F-9A0B-1C2D3E4F5A6B}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=..\installer_output
OutputBaseFilename={#AppName}-setup-{#Arch}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed={#AllowedArchs}
ArchitecturesInstallIn64BitMode={#InstallMode}
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\{#AppExe}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
