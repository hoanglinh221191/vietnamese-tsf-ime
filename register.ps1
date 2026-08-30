param(
    [switch]$Unregister,
    [switch]$Status,
    [switch]$RequireManifest,
    [switch]$RegisterElevatedOnly,
    [switch]$UnregisterElevatedOnly,
    [switch]$VerifyManifest,
    [switch]$SetDefault,
    [switch]$ConfigureCurrentUserOnly,
    [switch]$UnconfigureCurrentUserOnly
)

$ErrorActionPreference = "Stop"

function Get-Sha256Hex {
    param([string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha256.ComputeHash($stream)
        return [System.BitConverter]::ToString($bytes).Replace("-", "").ToLowerInvariant()
    } finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

$dllPath = Resolve-Path "$PSScriptRoot\build\neokey.dll" -ErrorAction SilentlyContinue
if ($null -eq $dllPath) {
    $dllPath = Resolve-Path "$PSScriptRoot\neokey.dll" -ErrorAction SilentlyContinue
}
if ($null -ne $dllPath) {
    $dllPath = $dllPath.Path
}

$dll32Path = Resolve-Path "$PSScriptRoot\build\neokey32.dll" -ErrorAction SilentlyContinue
if ($null -eq $dll32Path) {
    $dll32Path = Resolve-Path "$PSScriptRoot\neokey32.dll" -ErrorAction SilentlyContinue
}
if ($null -ne $dll32Path) {
    $dll32Path = $dll32Path.Path
}

if ($null -eq $dllPath -and $null -eq $dll32Path) {
    Write-Error "Could not find neokey.dll or neokey32.dll. Please compile the project first."
    exit 1
}

$configPath = Resolve-Path "$PSScriptRoot\build\neokey_config.exe" -ErrorAction SilentlyContinue
if ($null -eq $configPath) {
    $configPath = Resolve-Path "$PSScriptRoot\neokey_config.exe" -ErrorAction SilentlyContinue
}
if ($null -ne $configPath) {
    $configPath = $configPath.Path
}

$clsid = "{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}"
$profileGuid = "{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}"
$tipStr = "042A:$clsid$profileGuid"

function Get-PackageVersion {
    param([string]$Directory = $PSScriptRoot)

    $manifestPath = Join-Path $Directory "neokey_manifest.json"
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        try {
            $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            if (-not [string]::IsNullOrWhiteSpace([string]$manifest.version)) {
                return [string]$manifest.version
            }
        } catch {
        }
    }

    $versionPath = Join-Path $Directory "VERSION"
    if (Test-Path -LiteralPath $versionPath -PathType Leaf) {
        $version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
        if (-not [string]::IsNullOrWhiteSpace($version)) {
            return $version
        }
    }

    return "<unknown>"
}

function Is-Elevated {
    $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object System.Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-SafeManifestPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $false
    }
    $parts = $Path -split '[\\/]'
    foreach ($part in $parts) {
        if ($part -eq ".." -or $part -eq "") {
            return $false
        }
    }
    return $true
}

function Assert-ArtifactManifest {
    param(
        [switch]$Required
    )

    $manifestPath = Join-Path $PSScriptRoot "neokey_manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        if ($Required) {
            throw "Hash manifest missing: $manifestPath"
        }
        Write-Warning "Hash manifest not found; skipping release artifact verification."
        return
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.algorithm -ne "SHA256") {
        throw "Unsupported hash manifest format: $manifestPath"
    }

    $entries = @{}
    foreach ($entry in @($manifest.files)) {
        $relativePath = [string]$entry.path
        if (-not (Test-SafeManifestPath $relativePath)) {
            throw "Unsafe path in hash manifest: $relativePath"
        }
        $key = $relativePath.ToLowerInvariant()
        if ($entries.ContainsKey($key)) {
            throw "Duplicate path in hash manifest: $relativePath"
        }
        if ([string]$entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
            throw "Invalid SHA256 in hash manifest for: $relativePath"
        }
        if ([int64]$entry.bytes -lt 0) {
            throw "Invalid byte size in hash manifest for: $relativePath"
        }
        $entries[$key] = $entry
    }

    $requiredFiles = @(
        "neokey.dll",
        "neokey32.dll",
        "neokey_config.exe",
        "register.ps1",
        "install.bat",
        "uninstall.bat",
        "PORTABLE_RELEASE.md",
        "README.md",
        "README.vi.md",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "VERSION",
        "neokey_shorthand.txt"
    )
    foreach ($requiredFile in $requiredFiles) {
        $key = $requiredFile.ToLowerInvariant()
        if (-not $entries.ContainsKey($key)) {
            throw "Hash manifest does not include required file: $requiredFile"
        }
    }

    foreach ($entry in $entries.Values) {
        $relativePath = [string]$entry.path
        $path = Join-Path $PSScriptRoot $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required release file missing: $path"
        }

        $item = Get-Item -LiteralPath $path
        if ([int64]$entry.bytes -ne $item.Length) {
            throw "Size mismatch for $relativePath. Expected $($entry.bytes), got $($item.Length)."
        }

        $actualHash = Get-Sha256Hex -Path $path
        $expectedHash = ([string]$entry.sha256).ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "SHA256 mismatch for $relativePath."
        }
    }

    Write-Host "Release artifact hashes verified. Version: $(Get-PackageVersion $PSScriptRoot)"
}

function Invoke-Regsvr32 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath,
        [Parameter(Mandatory = $true)]
        [string]$DllFilePath,
        [Parameter(Mandatory = $true)]
        [ValidateSet("Register", "Unregister")]
        [string]$Operation,
        [Parameter(Mandatory = $true)]
        [string]$Architecture
    )

    $regsvrArguments = @()
    if ($Operation -eq "Unregister") {
        $regsvrArguments += "/u"
    }
    $regsvrArguments += "/s"
    # Start-Process joins ArgumentList into one command line. Keep the DLL path
    # quoted so spaces are passed as part of the single regsvr32 argument.
    $regsvrArguments += "`"$DllFilePath`""

    $process = Start-Process `
        -FilePath $ExecutablePath `
        -ArgumentList $regsvrArguments `
        -PassThru `
        -Wait
    Write-Host "$Architecture $Operation regsvr32 exit code: $($process.ExitCode)"
    if ($process.ExitCode -ne 0) {
        throw "$Architecture $Operation regsvr32 failed with exit code $($process.ExitCode)"
    }
}

function Invoke-DllRegistration {
    Assert-ArtifactManifest -Required:$RequireManifest

    Write-Host "Registering Neokey (in-place)..."
    $targetDir = Split-Path $dllPath -Parent
    $logPath = Join-Path $targetDir "register_elevated.log"
    Start-Transcript -Path $logPath -Force | Out-Null
    try {
        Write-Host "Target directory: $targetDir"
        Write-Host "DLL 64 path: $dllPath"
        Write-Host "DLL 32 path: $dll32Path"

        icacls "$targetDir" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /Q | Out-Null
        icacls "$targetDir" /grant "*S-1-15-2-2:(OI)(CI)(RX)" /Q | Out-Null
        icacls "$dllPath" /grant "*S-1-15-2-1:(RX)" /Q | Out-Null
        icacls "$dllPath" /grant "*S-1-15-2-2:(RX)" /Q | Out-Null
        if ($dll32Path) {
            icacls "$dll32Path" /grant "*S-1-15-2-1:(RX)" /Q | Out-Null
            icacls "$dll32Path" /grant "*S-1-15-2-2:(RX)" /Q | Out-Null
            Invoke-Regsvr32 `
                -ExecutablePath "C:\Windows\SysWOW64\regsvr32.exe" `
                -DllFilePath $dll32Path `
                -Operation Register `
                -Architecture "32-bit"
        }

        Invoke-Regsvr32 `
            -ExecutablePath "regsvr32.exe" `
            -DllFilePath $dllPath `
            -Operation Register `
            -Architecture "64-bit"
    } finally {
        Stop-Transcript | Out-Null
    }
}

function Invoke-DllUnregistration {
    Write-Host "Unregistering Neokey DLLs..."
    if ($dll32Path) {
        Invoke-Regsvr32 `
            -ExecutablePath "C:\Windows\SysWOW64\regsvr32.exe" `
            -DllFilePath $dll32Path `
            -Operation Unregister `
            -Architecture "32-bit"
    }
    if ($dllPath) {
        Invoke-Regsvr32 `
            -ExecutablePath "regsvr32.exe" `
            -DllFilePath $dllPath `
            -Operation Unregister `
            -Architecture "64-bit"
    }
}

function Test-RegistryKeyExists {
    param([string]$KeyPath)

    & reg.exe query $KeyPath /ve *> $null
    return ($LASTEXITCODE -eq 0)
}

function Get-RegistryDefaultValue {
    param([string]$KeyPath)

    try {
        $key = Get-Item -LiteralPath "Registry::$KeyPath" -ErrorAction Stop
        return [string]$key.GetValue("")
    } catch {
        return $null
    }
}

function Get-ManifestEntry {
    param(
        [string]$Directory,
        [string]$FileName
    )

    $manifestPath = Join-Path $Directory "neokey_manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        return $null
    }

    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        foreach ($entry in @($manifest.files)) {
            if ([string]::Equals([string]$entry.path, $FileName, [System.StringComparison]::OrdinalIgnoreCase)) {
                return $entry
            }
        }
    } catch {
        return $null
    }
    return $null
}

function Write-RegisteredFileStatus {
    param(
        [string]$Label,
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        Write-Host "$Label path: <not registered>"
        return
    }

    Write-Host "$Label path: $Path"
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Write-Host "$Label file: missing"
        return
    }

    $item = Get-Item -LiteralPath $Path
    $hash = Get-Sha256Hex -Path $Path
    Write-Host "$Label file: size=$($item.Length), modified=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), sha256=$hash"

    $entry = Get-ManifestEntry (Split-Path $Path -Parent) (Split-Path $Path -Leaf)
    if ($null -ne $entry) {
        $hashMatches = ([string]$entry.sha256).ToLowerInvariant() -eq $hash
        $sizeMatches = [int64]$entry.bytes -eq $item.Length
        Write-Host "$Label manifest: version=$(Get-PackageVersion (Split-Path $Path -Parent)), hash_match=$hashMatches, size_match=$sizeMatches"
    } else {
        Write-Host "$Label manifest: <not found>"
    }
}

function Get-DefaultInputMethodTip {
    $current = Get-WinDefaultInputMethodOverride
    if ($null -eq $current) {
        return $null
    }
    return [string]$current.InputMethodTip
}

function Set-NeokeyAsDefaultInputMethod {
    Write-Host "Setting Neokey as the default input method for the current Windows user..."
    Set-WinDefaultInputMethodOverride -InputTip $tipStr
    $currentTip = Get-DefaultInputMethodTip
    if (-not [string]::Equals($currentTip, $tipStr, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Windows did not retain Neokey as the default input method override."
    }
    Write-Host "Neokey is now the default input method override for this user."
    Write-Host "The setting takes effect for new sign-in sessions and remains after reboot."
}

function Add-NeokeyToUserLanguageList {
    Write-Host "Adding TIP to user language list..."
    $list = Get-WinUserLanguageList
    $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" } | Select-Object -First 1
    if ($null -eq $viLang) {
        Write-Host "Vietnamese language not found in user settings. Adding vi-VN..."
        $viObj = New-WinUserLanguageList -Language "vi-VN"
        $list.Add($viObj[0])
        $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" } | Select-Object -First 1
    }

    if (-not ($viLang.InputMethodTips -contains $tipStr)) {
        $viLang.InputMethodTips.Add($tipStr)
        Set-WinUserLanguageList $list -Force
        Write-Host "Successfully added TIP to user language list."
    } else {
        Write-Host "TIP is already in user language list."
    }
}

function Initialize-NeokeyUserData {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        Write-Warning "LOCALAPPDATA is unavailable; shorthand data will use the package fallback path."
        return
    }

    try {
        $dataDirectory = Join-Path $env:LOCALAPPDATA "Neokey"
        $shorthandPath = Join-Path $dataDirectory "neokey_shorthand.txt"
        $legacyShorthandPath = Join-Path $PSScriptRoot "neokey_shorthand.txt"
        New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null

        if (-not (Test-Path -LiteralPath $shorthandPath -PathType Leaf) -and
            (Test-Path -LiteralPath $legacyShorthandPath -PathType Leaf)) {
            Copy-Item -LiteralPath $legacyShorthandPath -Destination $shorthandPath
            Write-Host "Migrated shorthand data to the current user profile."
        }

        $directoryInfo = New-Object System.IO.DirectoryInfo($dataDirectory)
        $acl = $directoryInfo.GetAccessControl()
        foreach ($sidValue in @("S-1-15-2-1", "S-1-15-2-2")) {
            $sid = New-Object System.Security.Principal.SecurityIdentifier($sidValue)
            $rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
                $sid,
                "ReadAndExecute",
                "ContainerInherit,ObjectInherit",
                "None",
                "Allow")
            $acl.SetAccessRule($rule)
        }
        $directoryInfo.SetAccessControl($acl)
    } catch {
        Write-Warning "Could not initialize the per-user shorthand folder: $_"
    }
}

function Initialize-NeokeyUserSettings {
    Initialize-NeokeyUserData

    $keyPath = "HKCU:\Software\Neokey"
    if (-not (Test-Path $keyPath)) {
        New-Item -Path "HKCU:\Software" -Name "Neokey" -Force | Out-Null
    }

    $existing = Get-ItemProperty -Path $keyPath -Name "InputMethod" -ErrorAction SilentlyContinue
    if ($null -eq $existing) {
        Write-Host "Initializing default InputMethod to VNI (2)..."
        New-ItemProperty -Path $keyPath -Name "InputMethod" -Value 2 -PropertyType DWord -Force | Out-Null
    }

    Write-Host "Granting AppContainer read access to $keyPath..."
    $registryKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
        "Software\Neokey", $true)
    if ($null -eq $registryKey) {
        throw "Could not open HKCU:\Software\Neokey for ACL update."
    }
    try {
        $acl = $registryKey.GetAccessControl()
        $sid = New-Object System.Security.Principal.SecurityIdentifier("S-1-15-2-1")
        $rule = New-Object System.Security.AccessControl.RegistryAccessRule(
            $sid,
            "ReadKey",
            "ContainerInherit,ObjectInherit",
            "None",
            "Allow")
        $acl.SetAccessRule($rule)
        $registryKey.SetAccessControl($acl)
    } finally {
        $registryKey.Dispose()
    }
    Write-Host "AppContainer read access granted successfully."
}

function Remove-NeokeyFromUserLanguageList {
    Write-Host "Removing TIP from user language list..."
    if ([string]::Equals((Get-DefaultInputMethodTip), $tipStr, [System.StringComparison]::OrdinalIgnoreCase)) {
        Set-WinDefaultInputMethodOverride
        Write-Host "Removed Neokey as the default input method override."
    }

    $list = Get-WinUserLanguageList
    $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" } | Select-Object -First 1
    if ($null -eq $viLang) {
        Write-Host "Vietnamese language is not in the user language list."
        return
    }

    $toRemove = @($viLang.InputMethodTips | Where-Object { $_ -eq $tipStr })
    if ($toRemove.Count -eq 0) {
        Write-Host "TIP was not in user language list."
        return
    }

    foreach ($item in $toRemove) {
        [void]$viLang.InputMethodTips.Remove($item)
    }
    Set-WinUserLanguageList $list -Force
    Write-Host "Successfully removed TIP from user language list."
}

function Configure-NeokeyCurrentUser {
    Add-NeokeyToUserLanguageList
    Initialize-NeokeyUserSettings
    if ($SetDefault) {
        Set-NeokeyAsDefaultInputMethod
    }
}

function Unconfigure-NeokeyCurrentUser {
    Remove-NeokeyFromUserLanguageList
    Remove-ItemProperty `
        -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
        -Name "Neokey" `
        -ErrorAction SilentlyContinue
}

if ($ConfigureCurrentUserOnly -and $UnconfigureCurrentUserOnly) {
    throw "ConfigureCurrentUserOnly and UnconfigureCurrentUserOnly cannot be used together."
}
if ($RegisterElevatedOnly -and $UnregisterElevatedOnly) {
    throw "RegisterElevatedOnly and UnregisterElevatedOnly cannot be used together."
}
if ($Unregister -and ($RegisterElevatedOnly -or $UnregisterElevatedOnly)) {
    throw "Unregister cannot be combined with an elevated-only mode."
}

if ($ConfigureCurrentUserOnly) {
    Assert-ArtifactManifest -Required:$RequireManifest
    Configure-NeokeyCurrentUser
    exit 0
}

if ($UnconfigureCurrentUserOnly) {
    Unconfigure-NeokeyCurrentUser
    exit 0
}

if ($RegisterElevatedOnly) {
    if (-not (Is-Elevated)) {
        Write-Error "RegisterElevatedOnly requires Administrator privileges."
        exit 1
    }
    Invoke-DllRegistration
    Write-Host "DLLs registered successfully in-place."
    exit 0
}

if ($UnregisterElevatedOnly) {
    if (-not (Is-Elevated)) {
        Write-Error "UnregisterElevatedOnly requires Administrator privileges."
        exit 1
    }
    Invoke-DllUnregistration
    Write-Host "DLLs unregistered successfully."
    exit 0
}

if ($VerifyManifest) {
    Assert-ArtifactManifest -Required
    exit 0
}

if ($Status) {
    Write-Host "Checking registration status..."
    Write-Host "This package version: $(Get-PackageVersion $PSScriptRoot)"
    $key64Hklm = "HKEY_LOCAL_MACHINE\Software\Classes\CLSID\$clsid\InprocServer32"
    $key64Hkcu = "HKEY_CURRENT_USER\Software\Classes\CLSID\$clsid\InprocServer32"
    $key32Hklm = "HKEY_LOCAL_MACHINE\Software\Classes\Wow6432Node\CLSID\$clsid\InprocServer32"
    $key32Hkcu = "HKEY_CURRENT_USER\Software\Classes\Wow6432Node\CLSID\$clsid\InprocServer32"

    $comReg64 = Test-RegistryKeyExists "HKLM\Software\Classes\CLSID\$clsid"
    if (-not $comReg64) {
        $comReg64 = Test-RegistryKeyExists "HKCU\Software\Classes\CLSID\$clsid"
    }
    Write-Host "64-bit COM DLL Registered: $comReg64"
    Write-RegisteredFileStatus "64-bit HKLM" (Get-RegistryDefaultValue $key64Hklm)
    Write-RegisteredFileStatus "64-bit HKCU" (Get-RegistryDefaultValue $key64Hkcu)

    $comReg32 = $false
    if ([Environment]::Is64BitOperatingSystem) {
        $comReg32 = Test-RegistryKeyExists "HKLM\Software\Classes\Wow6432Node\CLSID\$clsid"
        if (-not $comReg32) {
            $comReg32 = Test-RegistryKeyExists "HKCU\Software\Classes\Wow6432Node\CLSID\$clsid"
        }
        Write-Host "32-bit COM DLL Registered: $comReg32"
        Write-RegisteredFileStatus "32-bit HKLM" (Get-RegistryDefaultValue $key32Hklm)
        Write-RegisteredFileStatus "32-bit HKCU" (Get-RegistryDefaultValue $key32Hkcu)
    }
    
    $langList = Get-WinUserLanguageList
    $viLang = $langList | Where-Object { $_.LanguageTag -like "vi*" }
    $inUserList = $null -ne $viLang -and $viLang.InputMethodTips -contains $tipStr
    Write-Host "TIP in User Language List: $inUserList"
    $defaultInputTip = Get-DefaultInputMethodTip
    if ([string]::IsNullOrWhiteSpace($defaultInputTip)) {
        Write-Host "Default Input Method TIP: <dynamic Windows selection>"
    } else {
        Write-Host "Default Input Method TIP: $defaultInputTip"
    }
    $isDefault = [string]::Equals($defaultInputTip, $tipStr, [System.StringComparison]::OrdinalIgnoreCase)
    Write-Host "Neokey is Default Input Method: $isDefault"
    exit 0
}

if ((Is-Elevated) -and -not $RegisterElevatedOnly -and -not $UnregisterElevatedOnly) {
    Write-Warning "This script is running elevated. Language list and default input changes apply to the elevated user account. Run install.bat normally so it elevates only DLL registration for your desktop account."
}

if ($Unregister) {
    Write-Host "Unregistering Neokey..."

    # 1. Unregister DLL COM and TSF system-wide (requires elevation). Do this
    # before changing the current user's settings so a denied UAC prompt or a
    # regsvr32 failure leaves the user's working configuration intact.
    if (-not (Is-Elevated)) {
        Write-Host "Requesting Administrator privileges to unregister DLL..."
        $elevatedArguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -UnregisterElevatedOnly"
        $process = Start-Process `
            -FilePath "powershell.exe" `
            -ArgumentList $elevatedArguments `
            -Verb RunAs `
            -PassThru `
            -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLLs unregistered successfully."
        } else {
            throw "Failed to unregister DLLs. Exit code: $($process.ExitCode)"
        }
    } else {
        Invoke-DllUnregistration
        Write-Host "DLLs unregistered successfully."
    }

    # 2. Remove TIP/autostart only after system unregistration succeeded.
    Unconfigure-NeokeyCurrentUser
} else {
    # 1. Register DLL COM and TSF system-wide (requires elevation)
    if (-not (Is-Elevated)) {
        Write-Host "Requesting Administrator privileges to register DLL..."
        $args = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -RegisterElevatedOnly"
        if ($RequireManifest) {
            $args += " -RequireManifest"
        }

        $process = Start-Process powershell.exe -ArgumentList $args -Verb RunAs -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLLs registered successfully in-place."
        } else {
            Write-Error "Failed to register DLLs. Exit code: $($process.ExitCode)"
        }
    } else {
        Invoke-DllRegistration
        Write-Host "DLLs registered successfully in-place."
    }

    # 2. Configure the current desktop user after system registration.
    Configure-NeokeyCurrentUser
    if (-not $SetDefault) {
        Write-Host "Tip: rerun with -SetDefault to make Neokey the default input method after sign-in or reboot."
    }
}
