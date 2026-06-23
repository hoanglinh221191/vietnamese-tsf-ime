; Inno Setup script for Neokey
; Download Inno Setup from https://jrsoftware.org/isinfo.php to compile this script.

#define MyAppName "Neokey"
#define MyAppVersion "0.1.6"
#define MyAppPublisher "Neokey Team"
#define MyAppExeName "neokey_config.exe"

[Setup]
AppId={{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={commonpf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableDirPage=no
DisableProgramGroupPage=no
OutputDir=dist
OutputBaseFilename=NeokeySetup
SetupIconFile=src\config-app\neokey.ico
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
AppMutex=Local\NeokeyConfigMutex

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
; Để hỗ trợ giao diện Tiếng Việt cho bộ cài:
; 1. Tải file Vietnamese.isl tại: https://jrsoftware.org/files/istrans/
; 2. Lưu vào thư mục "Languages" trong thư mục cài đặt của Inno Setup (ví dụ: C:\Program Files (x86)\Inno Setup 6\Languages)
; 3. Bỏ dấu chấm phẩy ở dòng dưới đây:
; Name: "vietnamese"; MessagesFile: "compiler:Languages\Vietnamese.isl"

[Files]
Source: "dist\Neokey\neokey_config.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "dist\Neokey\neokey.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 64bit
Source: "dist\Neokey\neokey32.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 32bit; Check: Is64BitInstallMode
Source: "dist\Neokey\neokey_shorthand.txt"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "dist\Neokey\VERSION"; DestDir: "{app}"; Flags: ignoreversion
Source: "src\config-app\neokey.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{app}\unins000.exe"
Name: "{userstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "-silent"

[Registry]
; Clean up registry settings and auto-start values on uninstall
Root: HKCU; Subkey: "Software\Neokey"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "Neokey"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Chạy {#MyAppName} sau khi cài đặt"; Flags: nowait postinstall skipifsilent

[Code]
// Import standard Windows TIP APIs from input.dll
function InstallLayoutOrTip(psz: String; dwFlags: DWORD): Boolean;
external 'InstallLayoutOrTip@input.dll stdcall delayload';

function UninstallLayoutOrTip(psz: String; dwFlags: DWORD): Boolean;
external 'UninstallLayoutOrTip@input.dll stdcall delayload';

function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;
  // Silently terminate neokey_config.exe if running to prevent file locks
  Exec('taskkill.exe', '/f /im neokey_config.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function InitializeUninstall(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;
  // Silently terminate neokey_config.exe if running to prevent file locks
  Exec('taskkill.exe', '/f /im neokey_config.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Register the Neokey TSF TIP profile
    // Format: <LangID>:<CLSID><ProfileGUID>
    // 042a = Vietnamese
    // CLSID: {A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}
    // Profile: {4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}
    if InstallLayoutOrTip('042a:{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}', 6) then
      Log('Successfully registered Neokey layout/tip')
    else
      Log('Failed to register Neokey layout/tip');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    // Unregister the Neokey TSF TIP profile
    UninstallLayoutOrTip('042a:{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}', 0);
  end;
end;
