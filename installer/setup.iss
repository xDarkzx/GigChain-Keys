; The Windows installer (Inno Setup 6), built by tools\package.ps1:
;   ISCC /J<build>\installer\branding.iss /DStageDir=<files> /DOutputDir=<dist> setup.iss
; Names come from branding.cmake (branding.iss); the files from the staged
; install (cmake --install + windeployqt).
;
; Like Audacity's and Reaper's: for everyone (Program Files, admin) or just
; this user; upgrades in place and keeps settings; closes the running app
; first; .gigchain setlists open with the app; uninstalls from Windows'
; Apps & Features and leaves settings and setlists.

#ifndef StageDir
  #error StageDir (the staged files) must be defined: run tools\package.ps1
#endif
#ifndef OutputDir
  #define OutputDir "dist"
#endif

[Setup]
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#Website}
AppSupportURL={#Website}/issues
AppUpdatesURL={#Website}/releases
AppCopyright={#Copyright}
AppComments={#Description}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup
; Everyone by default; the first page offers "only for me" (no admin), and
; so does the command line (/CURRENTUSER, for scripted installs).
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809 or later (what Qt 6 needs).
MinVersion=10.0.17763
LicenseFile=..\LICENSE
InfoBeforeFile={#InfoBefore}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName={#AppName}
WizardStyle=modern
WizardImageFile=wizard-side.bmp,wizard-side@2x.bmp
WizardSmallImageFile=wizard-small.bmp,wizard-small@2x.bmp
OutputDir={#OutputDir}
OutputBaseFilename={#SetupName}-{#AppVersion}-x64-setup
Compression=lzma2/ultra64
SolidCompression=yes
; The app running: asked to close (Restart Manager), found by its mutex.
AppMutex={#AppMutex}
CloseApplications=force
RestartApplications=no
SetupMutex={#SetupName}Setup
ChangesAssociations=yes
UsePreviousAppDir=yes
UsePreviousTasks=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Open .{#Extension} setlists with {#AppName}"; GroupDescription: "File types:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#ExeName}"; Comment: "{#Description}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Comment: "{#Description}"; Tasks: desktopicon

[Registry]
; Setlists (.gigchain) open with the app. HKA: the machine's classes for
; everyone, this user's for "only for me".
; (Another app may share .gigchain: its values stay; the keys go once empty.)
Root: HKA; Subkey: "Software\Classes\.{#Extension}"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.{#Extension}\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "{#AppName} setlist"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#ExeName}"",0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#ExeName}"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Applications\{#ExeName}\SupportedTypes"; ValueType: string; ValueName: ".{#Extension}"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#ExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
