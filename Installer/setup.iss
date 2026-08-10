; Open Salamander Inno Setup Script
; This installer maintains all features from the previous custom installer

#define MyAppName "Open Salamander"
#define MyAppVersion "6.0"
#define MyAppPublisher "Taskscape Ltd"
#define MyAppURL "https://www.opensalamander.com/"
#define MyAppExeName "salamand.exe"

; The product major is release-managed; the build number remains supplied by CI.
#ifndef BuildNumber
  #define BuildNumber "0"
#endif

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
AppId={{F4A1E7D3-8E5C-4B2A-9F6E-3D7C8A5B9E2F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}.{#BuildNumber}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=LICENSE
; SetupIconFile=..\src\setup\res\setup.ico
OutputBaseFilename=OpenSalamander_{#MyAppVersion}.{#BuildNumber}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; 64-bit only installer
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Minimum Windows version
MinVersion=10.0.17763
; Uninstaller settings
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.LaunchAfterInstall=Launch {#MyAppName} after installation

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Main executables
Source: "{#SourcePath}\salamand.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\salmon.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\salbroker.exe"; DestDir: "{app}"; Flags: ignoreversion

; Shell extensions live under utils and are registered by Salamander on first run.
Source: "{#SourcePath}\utils\salextx64.dll"; DestDir: "{app}\utils"; Flags: ignoreversion
Source: "{#SourcePath}\utils\salextx86.dll"; DestDir: "{app}\utils"; Flags: ignoreversion

; Utility executables (optional - only if present)
Source: "{#SourcePath}\salopen.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\salspawn.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\tserver.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\sfx7zip.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\zip2sfx.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\translator.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\salpvenv.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\fcremote.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourcePath}\7zwrapper.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; License file
Source: "{#SourcePath}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

; Build info for traceability
Source: "{#SourcePath}\build_info.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Language files for main application
Source: "{#SourcePath}\lang\*.slg"; DestDir: "{app}\lang"; Flags: ignoreversion

; Toolbar icons
Source: "{#SourcePath}\toolbars\*.svg"; DestDir: "{app}\toolbars"; Flags: ignoreversion

; Convert tables (character encoding tables)
Source: "{#SourcePath}\convert\centeuro\*"; DestDir: "{app}\convert\centeuro"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourcePath}\convert\cyrillic\*"; DestDir: "{app}\convert\cyrillic"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourcePath}\convert\westeuro\*"; DestDir: "{app}\convert\westeuro"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; Plugins - each plugin in its own directory with optional language files
Source: "{#SourcePath}\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchAfterInstall}"; Flags: nowait postinstall skipifsilent

[Registry]
; Add application to App Paths for easier command-line launching
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\salamand.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\salamand.exe"; ValueType: string; ValueName: "Path"; ValueData: "{app}"

[UninstallDelete]
; Clean up any files created during runtime
Type: filesandordirs; Name: "{app}\Temporary"
Type: dirifempty; Name: "{app}\plugins"
Type: dirifempty; Name: "{app}\lang"
Type: dirifempty; Name: "{app}\toolbars"
Type: dirifempty; Name: "{app}\convert"
Type: dirifempty; Name: "{app}\utils"
Type: dirifempty; Name: "{app}"

[Code]
// Check if Open Salamander is currently running and offer to close it
function InitializeSetup(): Boolean;
begin
  Result := True;
  
  // Check if salamand.exe is running (uses process list mutex from src/tasklist.cpp)
  while CheckForMutexes('TaskscapeLtdSalamander3bProcessListMutex') do
  begin
    if MsgBox('Open Salamander is currently running. Please close it before continuing installation.' + #13#10 + #13#10 + 
              'Click OK to retry or Cancel to exit setup.', mbError, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

// Check if uninstalling while application is running
function InitializeUninstall(): Boolean;
begin
  Result := True;
  
  if CheckForMutexes('TaskscapeLtdSalamander3bProcessListMutex') then
  begin
    MsgBox('Open Salamander is currently running. Please close it before uninstalling.', mbError, MB_OK);
    Result := False;
  end;
end;

// Cleanup old installation if upgrading from custom installer
procedure CurStepChanged(CurStep: TSetupStep);
var
  OldUninstallString: String;
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
  begin
    // Check for old custom uninstaller (remove.exe)
    if FileExists(ExpandConstant('{app}\remove.exe')) then
    begin
      // Run the old uninstaller silently
      if MsgBox('A previous version of Open Salamander was detected. Would you like to uninstall it first?', 
                mbConfirmation, MB_YESNO) = IDYES then
      begin
        Exec(ExpandConstant('{app}\remove.exe'), '/S', '', SW_SHOW, ewWaitUntilTerminated, ResultCode);
      end;
    end;
  end;
end;
