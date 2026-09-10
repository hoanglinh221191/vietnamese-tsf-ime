; Neokey Windows installer. Build through package.bat -Installer so the
; version and package paths are supplied from VERSION and DistRoot.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef MyPackageDir
  #define MyPackageDir "dist\Neokey"
#endif
#ifndef MyOutputDir
  #define MyOutputDir "dist"
#endif

#define MyAppName "Neokey"
#define MyAppPublisher "Neokey"
#define MyAppExeName "neokey_config.exe"
#define MyAppUrl "https://github.com/hoanglinh221191/vietnamese-tsf-ime"

[Setup]
AppId={{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppUrl}
AppSupportURL={#MyAppUrl}/issues
AppUpdatesURL={#MyAppUrl}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableWelcomePage=no
DisableDirPage=auto
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
OutputDir={#MyOutputDir}
OutputBaseFilename=NeokeySetup
SetupIconFile=src\config-app\neokey.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no
AppMutex=Local\NeokeyConfigMutex
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Setup
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "vietnamese"; MessagesFile: "compiler:Languages\Vietnamese.isl"

[CustomMessages]
english.ConfigShortcut=Neokey Settings
english.UninstallShortcut=Uninstall Neokey
english.OpenConfig=Open Neokey settings
english.SettingDefault=Making Neokey the default input method...
english.InstallTitle=Install Neokey
english.InstallBody=Setup will register VIE-Neokey and ENG-Neokey %1, with VIE-Neokey as the default input method.%n%nApprove the Administrator prompt when Windows asks.
english.UpdateTitle=Update Neokey %1
english.UpdateBody=Neokey %1 is installed.%n%nSetup will update to %2, preserve settings and shorthand data, and keep Neokey as the default input method.
english.RepairTitle=Repair Neokey %1
english.RepairBody=Neokey %1 is already installed.%n%nSetup will reinstall the program files without deleting settings.
english.DowngradeError=Neokey %1 is installed, which is newer than this %2 installer.%n%nDownload the latest release instead of downgrading.
english.InstallButton=&Install
english.UpdateButton=&Update
english.RepairButton=&Repair
english.FinishNotice=Neokey installation is complete.%n%nClose and reopen every app that was running during installation so it can load the new input method. Restart Windows after installing or updating Neokey to ensure the text service is fully reloaded.
vietnamese.ConfigShortcut=Cấu hình Neokey
vietnamese.UninstallShortcut=Gỡ cài đặt Neokey
vietnamese.OpenConfig=Mở cấu hình Neokey
vietnamese.SettingDefault=Đang đặt Neokey làm bộ gõ mặc định...
vietnamese.InstallTitle=Cài đặt Neokey
vietnamese.InstallBody=Bộ cài sẽ đăng ký VIE-Neokey và ENG-Neokey %1, đặt VIE-Neokey làm bộ gõ mặc định.%n%nBạn chỉ cần chấp nhận yêu cầu quyền Quản trị viên.
vietnamese.UpdateTitle=Cập nhật Neokey %1
vietnamese.UpdateBody=Đã tìm thấy Neokey %1.%n%nBộ cài sẽ cập nhật lên %2, giữ nguyên cấu hình và dữ liệu gõ tắt, đồng thời tiếp tục đặt Neokey làm bộ gõ mặc định.
vietnamese.RepairTitle=Sửa chữa Neokey %1
vietnamese.RepairBody=Neokey %1 đã được cài đặt.%n%nBộ cài sẽ cài lại các tệp chương trình mà không xóa cấu hình.
vietnamese.DowngradeError=Máy đang có Neokey %1, mới hơn bộ cài %2.%n%nHãy tải bản mới nhất thay vì hạ phiên bản.
vietnamese.InstallButton=&Cài đặt
vietnamese.UpdateButton=&Cập nhật
vietnamese.RepairButton=&Cài lại
vietnamese.FinishNotice=Neokey đã được cài đặt.%n%nHãy đóng và mở lại mọi ứng dụng đang chạy để chúng nạp bộ gõ mới. Khởi động lại Windows sau khi cài đặt hoặc cập nhật Neokey để bảo đảm dịch vụ nhập liệu được nạp lại đầy đủ.

[Files]
Source: "{#MyPackageDir}\neokey_config.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#MyPackageDir}\neokey.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit
Source: "{#MyPackageDir}\neokey32.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 32bit
Source: "{#MyPackageDir}\neokey_manifest.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyPackageDir}\register.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyPackageDir}\VERSION"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyPackageDir}\README.md"; DestDir: "{app}"; DestName: "README.en.md"; Flags: ignoreversion
Source: "{#MyPackageDir}\README.vi.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyPackageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyPackageDir}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyPackageDir}\neokey_shorthand.txt"; DestDir: "{app}"; Flags: onlyifdoesntexist

[Icons]
Name: "{group}\{cm:ConfigShortcut}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallShortcut}"; Filename: "{uninstallexe}"

[Registry]
; Inno processes registry entries before regserver. Override a previous
; portable -NoEnglishProfile choice in the account registering the DLLs.
; The original desktop account is configured separately by [Run].
Root: HKCU64; Subkey: "Software\Neokey"; ValueType: dword; ValueName: "RegisterEnglishProfile"; ValueData: "1"
Root: HKCU32; Subkey: "Software\Neokey"; ValueType: dword; ValueName: "RegisterEnglishProfile"; ValueData: "1"

[Run]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\register.ps1"" -ConfigureCurrentUserOnly -RequireManifest -SetDefault"; WorkingDir: "{app}"; StatusMsg: "{cm:SettingDefault}"; Flags: runhidden runasoriginaluser waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:OpenConfig}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\register.ps1"" -UnconfigureCurrentUserOnly"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "NeokeyUserCleanup"

[UninstallDelete]
; The shorthand file ships with onlyifdoesntexist, so a machine that already had
; one never recorded it as installed and Inno will not take it away. Without
; these the program folder outlives the uninstall holding one stray file.
Type: files; Name: "{app}\neokey_shorthand.txt"
Type: files; Name: "{app}\register_elevated.log"
Type: dirifempty; Name: "{app}"

[Code]
const
  CurrentVersion = '{#MyAppVersion}';
  IMEClassId = '{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}_is1';

var
  InstalledVersion: String;
  InstallMode: String;

function FindInstalledVersion(var Version: String): Boolean;
begin
  Result := RegQueryStringValue(HKLM64, UninstallKey, 'DisplayVersion', Version);
  if not Result then
    Result := RegQueryStringValue(HKLM32, UninstallKey, 'DisplayVersion', Version);
  if not Result then
    Result := RegQueryStringValue(HKCU64, UninstallKey, 'DisplayVersion', Version);
  if not Result then
    Result := RegQueryStringValue(HKCU32, UninstallKey, 'DisplayVersion', Version);
end;

function NormalizeVersionForCompare(const Value: String): String;
var
  I: Integer;
  DotCount: Integer;
  SuffixPos: Integer;
begin
  Result := Trim(Value);
  SuffixPos := Pos('-', Result);
  if SuffixPos > 0 then
    Delete(Result, SuffixPos, Length(Result) - SuffixPos + 1);

  DotCount := 0;
  for I := 1 to Length(Result) do
    if Result[I] = '.' then
      DotCount := DotCount + 1;
  while DotCount < 3 do
  begin
    Result := Result + '.0';
    DotCount := DotCount + 1;
  end;
end;

function TryCompareVersions(const Left, Right: String; var Comparison: Integer): Boolean;
var
  LeftVersion: Int64;
  RightVersion: Int64;
begin
  Result :=
    StrToVersion(NormalizeVersionForCompare(Left), LeftVersion) and
    StrToVersion(NormalizeVersionForCompare(Right), RightVersion);
  if Result then
    Comparison := ComparePackedVersion(LeftVersion, RightVersion);
end;

function InitializeSetup(): Boolean;
var
  Comparison: Integer;
begin
  Result := True;
  InstallMode := 'install';
  InstalledVersion := '';

  if not FindInstalledVersion(InstalledVersion) then
    Exit;

  if not TryCompareVersions(InstalledVersion, CurrentVersion, Comparison) then
  begin
    InstallMode := 'update';
    Exit;
  end;

  if Comparison > 0 then
  begin
    MsgBox(
      Format(CustomMessage('DowngradeError'), [InstalledVersion, CurrentVersion]),
      mbCriticalError,
      MB_OK);
    Result := False;
    Exit;
  end;

  if Comparison = 0 then
    InstallMode := 'repair'
  else
    InstallMode := 'update';
end;

procedure InitializeWizard();
begin
  if InstallMode = 'update' then
  begin
    WizardForm.Caption := Format(CustomMessage('UpdateTitle'), [CurrentVersion]);
    WizardForm.WelcomeLabel1.Caption := Format(CustomMessage('UpdateTitle'), [CurrentVersion]);
    WizardForm.WelcomeLabel2.Caption := Format(CustomMessage('UpdateBody'), [InstalledVersion, CurrentVersion]);
  end
  else if InstallMode = 'repair' then
  begin
    WizardForm.Caption := Format(CustomMessage('RepairTitle'), [CurrentVersion]);
    WizardForm.WelcomeLabel1.Caption := Format(CustomMessage('RepairTitle'), [CurrentVersion]);
    WizardForm.WelcomeLabel2.Caption := Format(CustomMessage('RepairBody'), [CurrentVersion]);
  end
  else
  begin
    WizardForm.WelcomeLabel1.Caption := CustomMessage('InstallTitle');
    WizardForm.WelcomeLabel2.Caption := Format(CustomMessage('InstallBody'), [CurrentVersion]);
  end;
end;

procedure RemoveLeftoverRegistrationKeys();
var
  Paths: array[0..1] of String;
  I: Integer;
begin
  Paths[0] := 'SOFTWARE\Classes\CLSID\' + IMEClassId;
  Paths[1] := 'SOFTWARE\Microsoft\CTF\TIP\' + IMEClassId;
  for I := 0 to GetArrayLength(Paths) - 1 do
  begin
    RegDeleteKeyIncludingSubkeys(HKLM64, Paths[I]);
    RegDeleteKeyIncludingSubkeys(HKLM32, Paths[I]);
    RegDeleteKeyIncludingSubkeys(HKCU64, Paths[I]);
    RegDeleteKeyIncludingSubkeys(HKCU32, Paths[I]);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Only after Inno has unregistered the DLLs, which is what asks Windows to
  // retract the profile. Deleting these first would leave CTF holding a profile
  // it can no longer describe. Anything still here by now is a leftover, and a
  // leftover is what makes a later install behave like the version before it.
  if CurUninstallStep = usPostUninstall then
    RemoveLeftoverRegistrationKeys();
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpReady then
  begin
    if InstallMode = 'update' then
      WizardForm.NextButton.Caption := CustomMessage('UpdateButton')
    else if InstallMode = 'repair' then
      WizardForm.NextButton.Caption := CustomMessage('RepairButton')
    else
      WizardForm.NextButton.Caption := CustomMessage('InstallButton');
  end
  else if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption := CustomMessage('FinishNotice');
end;
