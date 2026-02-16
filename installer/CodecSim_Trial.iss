; CodecSim Trial - Inno Setup Script
; Trial version: MP3 codec only

[Setup]
AppName=CodecSim Trial
AppVersion=1.0.0
AppPublisher=MouseSoft
AppPublisherURL=https://mousesoft.booth.pm/
AppSupportURL=https://mousesoft.booth.pm/
AppUpdatesURL=https://mousesoft.booth.pm/
DefaultDirName={commonpf}\CodecSim Trial
DefaultGroupName=CodecSim Trial
OutputBaseFilename=CodecSimTrial_Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile=trial_notice.txt
UninstallDisplayIcon={uninstallexe}
DisableProgramGroupPage=yes

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
