; CodecSim Trial - Inno Setup Script
; Trial version: MP3 codec only

#ifndef MyAppVersion
  #define MyAppVersion "0.0.1"
#endif

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName=CodecSim Trial
AppVersion={#MyAppVersion}
AppPublisher=MouseSoft
AppPublisherURL=https://mousesoft.booth.pm/
AppSupportURL=https://mousesoft.booth.pm/
AppUpdatesURL=https://mousesoft.booth.pm/
DefaultDirName={commonpf}\CodecSim Trial
DefaultGroupName=CodecSim Trial
OutputBaseFilename=CodecSimTrial_Setup_{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile=trial_notice.txt
UninstallDisplayIcon={uninstallexe}
DisableProgramGroupPage=yes
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany=MouseSoft
VersionInfoProductName=CodecSim Trial
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Files]
; VST3 Plugin bundle
Source: "..\build-trial\out\CodecSim.vst3\*"; DestDir: "{commoncf}\VST3\CodecSim.vst3"; Flags: recursesubdirs ignoreversion

; README
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\Uninstall CodecSim Trial"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\CodecSim.vst3"
