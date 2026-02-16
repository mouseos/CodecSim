; CodecSim Full Version Installer
; Inno Setup Script
; Publisher: MouseSoft
; https://mousesoft.booth.pm/

#define MyAppName "CodecSim"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.1"
#endif
#define MyAppVersionNum Copy(MyAppVersion, 1, Pos("-", MyAppVersion + "-") - 1)
#define MyAppPublisher "MouseSoft"
#define MyAppURL "https://mousesoft.booth.pm/"
#define MyAppExeName "CodecSim.exe"

[Setup]
AppId={{E8F3A1B2-5C7D-4E9F-B6A0-1D2E3F4A5B6C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={commonpf}\CodecSim
DefaultGroupName={#MyAppName}
OutputBaseFilename=CodecSim_Setup_{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
LZMANumBlockThreads=4
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputDir=..\build\installer
SetupIconFile=compiler:SetupClassicIcon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern
VersionInfoVersion={#MyAppVersionNum}
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersionNum}

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Types]
Name: "full"; Description: "Full Installation"
Name: "compact"; Description: "VST3 Plugin Only"
Name: "custom"; Description: "Custom Installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 Plugin"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone Application"; Types: full custom

[Files]
; VST3 Plugin - entire .vst3 bundle folder
Source: "..\build\out\CodecSim.vst3\*"; DestDir: "{commoncf}\VST3\CodecSim.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone Application
Source: "..\build\out\CodecSim.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\build\out\ffmpeg.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\build\out\LICENSE-ffmpeg.txt"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

; README
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Components: standalone
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\CodecSim.vst3"

[Code]
function InitializeSetup(): Boolean;
var
  PreviousVersion: String;
  MsgResult: Integer;
begin
  Result := True;

  if RegQueryStringValue(HKLM,
    'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{{E8F3A1B2-5C7D-4E9F-B6A0-1D2E3F4A5B6C}_is1',
    'DisplayVersion', PreviousVersion) then
  begin
    if PreviousVersion = '{#MyAppVersion}' then
    begin
      MsgResult := MsgBox(
        '{#MyAppName} ' + PreviousVersion + ' is already installed.' + #13#10 +
        'Do you want to reinstall it?',
        mbConfirmation, MB_YESNO);
      Result := (MsgResult = IDYES);
    end
    else
    begin
      MsgResult := MsgBox(
        '{#MyAppName} ' + PreviousVersion + ' is currently installed.' + #13#10 +
        'Do you want to upgrade to version {#MyAppVersion}?',
        mbConfirmation, MB_YESNO);
      Result := (MsgResult = IDYES);
    end;
  end;
end;
