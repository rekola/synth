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
ChangesEnvironment=yes

[Tasks]
Name: "addtopath"; Description: "Add synth to the &PATH, so that it can be run from any terminal"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; The program runs in a console window until it has a window of its own.
Name: "{group}\synth (play the welcome song)"; Filename: "{app}\synth.exe"; Parameters: "--headless --autoplay"; WorkingDir: "{app}"
Name: "{group}\synth (list audio and MIDI devices)"; Filename: "{cmd}"; Parameters: "/k ""{app}\synth.exe"" --list-devices"; WorkingDir: "{app}"
Name: "{group}\Songs"; Filename: "{app}\share\synth\songs"
Name: "{group}\Licences"; Filename: "{app}\THIRD_PARTY_LICENSES.md"

[Registry]
; The PATH of all users for an all-users install, else the user's own.
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addtopath; Check: IsAdminInstallMode and NeedsAddPath('system')
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addtopath; Check: (not IsAdminInstallMode) and NeedsAddPath('user')

[Code]
const
  SystemEnvironment = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  UserEnvironment = 'Environment';

function EnvironmentRoot(Scope: string): Integer;
begin
  if Scope = 'system' then Result := HKLM else Result := HKCU;
end;

function EnvironmentKey(Scope: string): string;
begin
  if Scope = 'system' then Result := SystemEnvironment else Result := UserEnvironment;
end;

// Whether the install directory is missing from that PATH.
function NeedsAddPath(Scope: string): Boolean;
var
  Current: string;
begin
  if not RegQueryStringValue(EnvironmentRoot(Scope), EnvironmentKey(Scope), 'Path', Current) then
    Result := True
  else
    Result := Pos(';' + Uppercase(ExpandConstant('{app}')) + ';', ';' + Uppercase(Current) + ';') = 0;
end;

procedure RemoveFromPath(Scope: string);
var
  Current, Entry: string;
  Start: Integer;
begin
  if not RegQueryStringValue(EnvironmentRoot(Scope), EnvironmentKey(Scope), 'Path', Current) then
    Exit;
  Entry := ExpandConstant('{app}');
  Start := Pos(';' + Uppercase(Entry) + ';', ';' + Uppercase(Current) + ';');
  if Start = 0 then
    Exit;
  // The entry and the separator after it; Start is where the entry begins in Current.
  Delete(Current, Start, Length(Entry) + 1);
  if (Length(Current) > 0) and (Current[Length(Current)] = ';') then
    SetLength(Current, Length(Current) - 1);
  RegWriteExpandStringValue(EnvironmentRoot(Scope), EnvironmentKey(Scope), 'Path', Current);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveFromPath('user');
    if IsAdminInstallMode then
      RemoveFromPath('system');
  end;
end;
