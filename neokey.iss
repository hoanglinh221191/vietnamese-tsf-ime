; Inno Setup script for Neokey (Vietnamese TSF IME)
; Compile using Inno Setup Compiler (ISCC.exe)

#define MyAppName "Neokey"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Neokey Team"
#define MyAppExeName "neokey_config.exe"
#define MyAppDllName "neokey.dll"

[Setup]
AppId={{5E47481F-78BD-4AC9-BB66-2244E4C690F4}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputBaseFilename=neokey_setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "build\neokey.dll"; DestDir: "{app}"; Flags: regserver restartreplace sharedfile
Source: "build\neokey_config.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName} Configuration"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName} Configuration"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Run]
; Grant AppContainer capabilities permissions to the directory and DLL for sandbox security (Edge, Chrome, etc.)
Filename: "icacls.exe"; Parameters: """{app}"" /grant *S-1-15-2-1:(OI)(CI)(RX) /Q"; Flags: runhidden
Filename: "icacls.exe"; Parameters: """{app}"" /grant *S-1-15-2-2:(OI)(CI)(RX) /Q"; Flags: runhidden
Filename: "icacls.exe"; Parameters: """{app}\{#MyAppDllName}"" /grant *S-1-15-2-1:(RX) /Q"; Flags: runhidden
Filename: "icacls.exe"; Parameters: """{app}\{#MyAppDllName}"" /grant *S-1-15-2-2:(RX) /Q"; Flags: runhidden

; Add TIP to current user's input method settings via PowerShell
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command \"$list = Get-WinUserLanguageList; $vi = $list | Where-Object { $_.LanguageTag -like 'vi*' }; if (-not $vi) { $list.Add((New-WinUserLanguageList -Language 'vi-VN')[0]); $vi = $list | Where-Object { $_.LanguageTag -like 'vi*' } }; if (-not ($vi.InputMethodTips -contains '042a:{{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}{{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}')) { $vi.InputMethodTips.Add('042a:{{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}{{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}'); Set-WinUserLanguageList $list -Force }\""; Flags: runhidden

; Launch configuration tool after installation finishes
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Clean up TIP from user's input method list before unregistering
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command \"$list = Get-WinUserLanguageList; $vi = $list | Where-Object { $_.LanguageTag -like 'vi*' }; if ($vi) { $vi.InputMethodTips.Remove('042a:{{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}{{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}'); Set-WinUserLanguageList $list -Force }\""; Flags: runhidden

; Unregister DLL (handled automatically by regserver flag, but explicitly run here for safety)
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\{#MyAppDllName}"""; Flags: runhidden
