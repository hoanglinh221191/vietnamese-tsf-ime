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

; A prerelease version such as 0.1.15-dev is a valid name for a build, but not
; a valid VERSIONINFO number - that field is four integers and nothing else. The
; numeric part goes there and the whole string goes in the text fields beside it,
; which is what Windows shows and what the packaging script reads back.
#if Pos("-", MyAppVersion) > 0
  #define MyNumericVersion Copy(MyAppVersion, 1, Pos("-", MyAppVersion) - 1)
#else
  #define MyNumericVersion MyAppVersion
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
; No prompt to close anything. The tray is closed and reopened by [Code]; the
; DLLs live inside every app that types, so asking to close those would list
; half the desktop, and a DLL left for the next restart kept serving the old
; version to every app opened in the meantime. [Code] moves a loaded DLL aside
; instead, which Windows allows, and the new one takes its name at once.
CloseApplications=no
RestartApplications=no
VersionInfoVersion={#MyNumericVersion}
VersionInfoTextVersion={#MyAppVersion}
VersionInfoProductTextVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Setup
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyNumericVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "vietnamese"; MessagesFile: "compiler:Languages\Vietnamese.isl"

[CustomMessages]
english.ConfigShortcut=Neokey Settings
english.UninstallShortcut=Uninstall Neokey
english.OpenConfig=Open Neokey settings
english.RunInTray=Run Neokey in the system tray
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
english.FinishNotice=Neokey installation is complete.%n%nApps opened from now on use the new version. Apps that were already open keep the previous one until you close and reopen them.
vietnamese.ConfigShortcut=Cấu hình Neokey
vietnamese.UninstallShortcut=Gỡ cài đặt Neokey
vietnamese.OpenConfig=Mở cấu hình Neokey
vietnamese.RunInTray=Chạy Neokey dưới khay hệ thống
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
vietnamese.FinishNotice=Neokey đã được cài đặt.%n%nỨng dụng mở từ bây giờ sẽ dùng bản mới. Ứng dụng đang mở sẵn vẫn dùng bản cũ cho đến khi được đóng và mở lại.

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
; No -RequireManifest: {app} holds only part of the portable package, so the
; check failed on every install from 0.1.10 on and, since Inno ignores this
; entry's exit code, the user was silently left unconfigured.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\register.ps1"" -ConfigureCurrentUserOnly -SetDefault"; WorkingDir: "{app}"; StatusMsg: "{cm:SettingDefault}"; Flags: runhidden runasoriginaluser waituntilterminated
; Neokey belongs in the tray, not in a window. This box is ticked, so a new
; install and an update both end there with nothing opened, and it runs on a
; silent install too. Waiting until it is idle means its tray window exists by
; the time the entry below asks that window to show the settings.
Filename: "{app}\{#MyAppExeName}"; Parameters: "-silent"; Description: "{cm:RunInTray}"; WorkingDir: "{app}"; Flags: postinstall waituntilidle runasoriginaluser
; Unticked: the settings open after an install only when asked for.
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:OpenConfig}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent unchecked runasoriginaluser

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\register.ps1"" -UnconfigureCurrentUserOnly"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "NeokeyUserCleanup"

[UninstallDelete]
; The shorthand file ships with onlyifdoesntexist, so a machine that already had
; one never recorded it as installed and Inno will not take it away. Without
; these the program folder outlives the uninstall holding one stray file.
Type: files; Name: "{app}\neokey_shorthand.txt"
Type: files; Name: "{app}\register_elevated.log"
; Binaries an update moved aside while an app still had them loaded. Each was
; also handed to the next restart when it was moved, so one still in use here
; goes then.
Type: files; Name: "{app}\*.old"
; A copy queued to be moved in at the next restart. With it gone that move
; does nothing, instead of putting a DLL back after the uninstall.
Type: files; Name: "{app}\*.pending"
Type: dirifempty; Name: "{app}"

[Code]
const
  CurrentVersion = '{#MyAppVersion}';
  IMEClassId = '{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}_is1';
  TrayWindowClass = 'NeokeyTrayWindowClass';
  TrayMutex = 'Local\NeokeyConfigMutex';
  TrayCloseTimeoutMs = 5000;
  WM_CLOSE = $0010;

var
  InstalledVersion: String;
  InstallMode: String;
  TrayRunning: Boolean;
  InstallSucceeded: Boolean;
  MovedAsideFrom: TArrayOfString;
  MovedAsideTo: TArrayOfString;

function IsTrayRunning(): Boolean;
begin
  Result := (FindWindowByClassName(TrayWindowClass) <> 0) or
            CheckForMutexes(TrayMutex);
end;

function WaitForTrayToExit(TimeoutMs: Integer): Boolean;
var
  Waited: Integer;
begin
  Waited := 0;
  while IsTrayRunning() and (Waited < TimeoutMs) do
  begin
    Sleep(100);
    Waited := Waited + 100;
  end;
  Result := not IsTrayRunning();
end;

// Asks the tray to quit the way its own Exit item does - its window goes, and
// the tray icon with it - and waits for the process to be gone, since its file
// is about to be replaced. A tray that has not gone in time is ended.
procedure CloseTray();
var
  Wnd: HWND;
  ResultCode: Integer;
begin
  Wnd := FindWindowByClassName(TrayWindowClass);
  if Wnd <> 0 then
    PostMessage(Wnd, WM_CLOSE, 0, 0);
  if WaitForTrayToExit(TrayCloseTimeoutMs) then
    Exit;
  Log('Neokey tray did not close in time; ending it');
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#MyAppExeName}', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
  WaitForTrayToExit(2000);
end;

// Windows will not overwrite a DLL that an app has loaded, but it will rename
// it. The loaded copy moves aside under a new name, the app that loaded it
// carries on with it, and the new file takes the old name at once - so every
// app opened after this update gets the new version, not only those opened
// after the next restart.
procedure MoveAside(const Name: String);
var
  Path: String;
  Aside: String;
  N: Integer;
begin
  Path := ExpandConstant('{app}\' + Name);
  if not FileExists(Path) then
    Exit;
  Aside := Path + '.' + GetDateTimeString('yyyymmddhhnnss', #0, #0) + '.old';
  if not RenameFile(Path, Aside) then
  begin
    Log('Could not move aside ' + Path + '; it is replaced at the next restart');
    Exit;
  end;
  N := GetArrayLength(MovedAsideFrom);
  SetArrayLength(MovedAsideFrom, N + 1);
  SetArrayLength(MovedAsideTo, N + 1);
  MovedAsideFrom[N] := Path;
  MovedAsideTo[N] := Aside;
end;

// A copy nothing has loaded any more is deleted now; one still in use goes at
// the next restart.
procedure DiscardMovedAside();
var
  I: Integer;
begin
  for I := 0 to GetArrayLength(MovedAsideTo) - 1 do
    if not DeleteFile(MovedAsideTo[I]) then
      RestartReplace(MovedAsideTo[I], '');
end;

// Every earlier installer left a loaded DLL to the next restart: it put the
// new copy beside it and queued a move over it. Explorer always has the DLL
// loaded, so every earlier update did this. If the machine has not restarted
// since, that move is still queued and would put the older build back over
// this one. Windows runs the moves in order, so one more queued after it puts
// this version back on top. The shared queue is only read and appended to,
// never rewritten.
procedure OutlastPendingReplacement(const Name: String);
var
  Pending: String;
  Path: String;
  Duplicate: String;
begin
  if not RegQueryMultiStringValue(HKLM,
      'SYSTEM\CurrentControlSet\Control\Session Manager',
      'PendingFileRenameOperations', Pending) then
    Exit;
  Path := ExpandConstant('{app}\' + Name);
  if Pos(Lowercase(Path) + #0, Lowercase(Pending) + #0) = 0 then
    Exit;
  Duplicate := Path + '.' + GetDateTimeString('yyyymmddhhnnss', #0, #0) + '.pending';
  if FileCopy(Path, Duplicate, False) then
    RestartReplace(Duplicate, Path)
  else
    Log('Could not queue ' + Path + ' behind an earlier pending replacement');
end;

// Copies left by earlier updates, from apps that have since closed.
procedure DeleteOldCopies();
var
  FindRec: TFindRec;
  Dir: String;
begin
  Dir := ExpandConstant('{app}\');
  if FindFirst(Dir + '*.old', FindRec) then
  try
    repeat
      DeleteFile(Dir + FindRec.Name);
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

// An install that did not finish must not leave Neokey without its files: a
// binary moved aside is put back wherever nothing took its place.
procedure RestoreMovedAside();
var
  I: Integer;
begin
  for I := 0 to GetArrayLength(MovedAsideFrom) - 1 do
    if (not FileExists(MovedAsideFrom[I])) and FileExists(MovedAsideTo[I]) then
      RenameFile(MovedAsideTo[I], MovedAsideFrom[I]);
end;

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

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  TrayRunning := IsTrayRunning();
  if TrayRunning then
    CloseTray();
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    DeleteOldCopies();
    MoveAside('{#MyAppExeName}');
    MoveAside('neokey.dll');
    MoveAside('neokey32.dll');
  end
  else if CurStep = ssPostInstall then
  begin
    InstallSucceeded := True;
    OutlastPendingReplacement('{#MyAppExeName}');
    OutlastPendingReplacement('neokey.dll');
    OutlastPendingReplacement('neokey32.dll');
    DiscardMovedAside();
  end;
end;

procedure DeinitializeSetup();
var
  ResultCode: Integer;
begin
  if InstallSucceeded then
    Exit;
  RestoreMovedAside();
  // [Run] brings the tray back only after a finished install.
  if TrayRunning then
    ExecAsOriginalUser(ExpandConstant('{app}\{#MyAppExeName}'), '-silent',
                       ExpandConstant('{app}'), SW_SHOWNORMAL, ewNoWait,
                       ResultCode);
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  if IsTrayRunning() then
    CloseTray();
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
