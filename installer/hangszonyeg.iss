; Hangszőnyeg Windows-telepítő (Inno Setup 6)
#define AppName "Hangszőnyeg"
#define AppVersion "1.0.0"

[Setup]
AppId={{6B3E2C1A-9D47-4F0B-8C55-2A1E7D3F9B10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Bordás Máté
DefaultDirName={autopf}\Hangszonyeg
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=Hangszonyeg-telepito
UninstallDisplayIcon={app}\Hangszonyeg.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "hu"; MessagesFile: "compiler:Languages\Hungarian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\dist\Hangszonyeg\Hangszonyeg.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\Hangszonyeg\OLVASSEL.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Hangszonyeg.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Hangszonyeg.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Hangszonyeg.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
