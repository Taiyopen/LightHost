#define MyAppName "Light Host"
#define MyAppVersion "1.8.0"
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
; 降噪：推論引擎、DeepFilterNet 與模型檔（與 CMakeLists.txt 的 LIGHTHOST_RUNTIME_DLLS／LIGHTHOST_MODEL_FILES 同步）
Source: "..\build-new\LightHost_artefacts\Release\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-new\LightHost_artefacts\Release\df.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-new\LightHost_artefacts\Release\models\*"; DestDir: "{app}\models"; Flags: ignoreversion
; 第三方授權
Source: "..\third_party\rnnoise\COPYING"; DestDir: "{app}\licenses"; DestName: "RNNoise.txt"; Flags: ignoreversion
Source: "..\third_party\onnxruntime\LICENSE"; DestDir: "{app}\licenses"; DestName: "ONNXRuntime.txt"; Flags: ignoreversion
Source: "..\third_party\onnxruntime\ThirdPartyNotices.txt"; DestDir: "{app}\licenses"; DestName: "ONNXRuntime-ThirdPartyNotices.txt"; Flags: ignoreversion
Source: "..\third_party\deepfilternet\LICENSE-MIT"; DestDir: "{app}\licenses"; DestName: "DeepFilterNet.txt"; Flags: ignoreversion
Source: "..\third_party\fastenhancer\LICENSE"; DestDir: "{app}\licenses"; DestName: "FastEnhancer.txt"; Flags: ignoreversion
Source: "..\third_party\gtcrn\LICENSE"; DestDir: "{app}\licenses"; DestName: "GTCRN.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
; App 內自動更新是用 /SILENT 執行，裝完要自己重新開啟（postinstall 預設以原本的使用者身分執行，不會帶系統管理員權限）
Filename: "{app}\{#MyAppExeName}"; Flags: nowait postinstall skipifnotsilent

[Code]
// 「Start with Windows」是 App 自己寫進登錄檔的，解除安裝時一併清掉
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Light Host');
end;
