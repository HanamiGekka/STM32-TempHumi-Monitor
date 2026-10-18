; This wrapper installs reviewed portable bytes; it never configures or programs a board.
#ifndef ProductVersion
  #error ProductVersion must come from the verified release identity
#endif

[Setup]
AppId={{3CF43736-ED17-4989-B1C6-E6D143B82CC8}
AppName=TempHumi Monitor
AppVersion={#ProductVersion}
AppVerName=TempHumi Monitor {#ProductVersion}
AppPublisher=TempHumi Monitor contributors
VersionInfoVersion={#NumericVersion}
VersionInfoProductVersion={#NumericVersion}
VersionInfoProductTextVersion={#ProductVersion}
DefaultDirName={code:DefaultInstallDirectory}
DefaultGroupName=TempHumi Monitor
DisableProgramGroupPage=yes
AllowNoIcons=no
DisableDirPage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDirectory}
OutputBaseFilename=TempHumi_{#ProductVersion}_Setup
SetupIconFile={#ProjectRoot}\UIDesktop\resources\temphumi.ico
UninstallDisplayIcon={app}\TempHumi.exe
LicenseFile={#ProjectRoot}\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
AlwaysRestart=no
Uninstallable=yes
SignedUninstaller=no
SetupMutex=TempHumiMonitorSetup

[Tasks]
Name: desktopicon; Description: Create a &desktop shortcut; Flags: unchecked

[Files]
#include PayloadInclude

[Icons]
Name: "{group}\TempHumi Monitor"; Filename: "{app}\TempHumi.exe"; WorkingDir: "{app}"; IconFilename: "{app}\TempHumi.exe"
Name: "{group}\Uninstall TempHumi Monitor"; Filename: "{uninstallexe}"
Name: "{autodesktop}\TempHumi Monitor"; Filename: "{app}\TempHumi.exe"; WorkingDir: "{app}"; IconFilename: "{app}\TempHumi.exe"; Tasks: desktopicon

; No Run, UninstallDelete or application-settings Registry section: startup is explicit,
; and uninstallation removes only files registered by this installer, retaining user data.
[Code]
function DefaultInstallDirectory(Param: String): String;
begin
  if DirExists('E:\Program Files') then
    Result := 'E:\Program Files\TempHumi Monitor'
  else
    Result := ExpandConstant('{localappdata}\Programs\TempHumi Monitor');
end;
