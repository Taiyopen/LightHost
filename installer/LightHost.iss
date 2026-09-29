#define MyAppName "Light Host"
#define MyAppVersion "1.5.0"
#define MyAppPublisher "LightHost"
#define MyAppExeName "Light Host.exe"

[Setup]
AppId={{E7C4A91B-3F52-4D8E-B6A1-9C2E8F4D7B30}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppCopyright=Copyright (C) 2015-2026 LightHost
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\license
OutputDir=..\dist
OutputBaseFilename=LightHost-{#MyAppVersion}-Setup
SetupIconFile=..\build-new\LightHost_artefacts\JuceLibraryCode\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Light Host Setup
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "chinesetraditional"; MessagesFile: "ChineseTraditional.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build-new\LightHost_artefacts\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "redist\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "redist\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "redist\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\license"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
; App 內自動更新是用 /SILENT 執行，裝完要自己重新開啟（postinstall 預設以原本的使用者身分執行，不會帶系統管理員權限）
Filename: "{app}\{#MyAppExeName}"; Flags: nowait postinstall skipifnotsilent
