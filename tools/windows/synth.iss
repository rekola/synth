; Inno Setup script for the Windows installer. Built by
; .github/workflows/release-windows.yml with
;   ISCC /DStageDir=<dir from stage.sh> /DAppVersion=<version> /DOutputDir=<dir> synth.iss

#ifndef StageDir
  #error StageDir must name the directory stage.sh filled
#endif
#ifndef AppVersion
  #define AppVersion "0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6E3C2F4A-5B0D-4C7E-9A41-2D8F0B1C3E57}
AppName=synth
AppVersion={#AppVersion}
AppPublisher=Mikael Rekola
DefaultDirName={autopf}\synth
DefaultGroupName=synth
; Everything lives next to synth.exe, so a per-user install works as well.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#StageDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=synth-{#AppVersion}-windows-x86_64-setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=synth
DisableProgramGroupPage=yes

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; The program runs in a console window until it has a window of its own.
Name: "{group}\synth (play the welcome song)"; Filename: "{app}\synth.exe"; Parameters: "--headless --autoplay"; WorkingDir: "{app}"
Name: "{group}\synth (list audio and MIDI devices)"; Filename: "{cmd}"; Parameters: "/k ""{app}\synth.exe"" --list-devices"; WorkingDir: "{app}"
Name: "{group}\Songs"; Filename: "{app}\share\synth\songs"
Name: "{group}\Licences"; Filename: "{app}\THIRD_PARTY_LICENSES.md"
