param(
    [switch]$Unregister,
    [switch]$Status,
    [switch]$RequireManifest,
    [switch]$RegisterElevatedOnly,
    [switch]$VerifyManifest
)

$ErrorActionPreference = "Stop"

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
        $entries[$relativePath.ToLowerInvariant()] = $entry
    }

    foreach ($requiredFile in @("neokey.dll", "neokey32.dll", "neokey_config.exe")) {
        $key = $requiredFile.ToLowerInvariant()
        if (-not $entries.ContainsKey($key)) {
            throw "Hash manifest does not include required file: $requiredFile"
        }

        $entry = $entries[$key]
        $path = Join-Path $PSScriptRoot $requiredFile
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required release file missing: $path"
        }

        $item = Get-Item -LiteralPath $path
        if ([int64]$entry.bytes -ne $item.Length) {
            throw "Size mismatch for $requiredFile. Expected $($entry.bytes), got $($item.Length)."
        }

        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        $expectedHash = ([string]$entry.sha256).ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "SHA256 mismatch for $requiredFile."
        }
    }

    Write-Host "Release artifact hashes verified. Version: $(Get-PackageVersion $PSScriptRoot)"
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
            $process32 = Start-Process C:\Windows\SysWOW64\regsvr32.exe -ArgumentList "/s", "`"$dll32Path`"" -PassThru -Wait
            Write-Host "32-bit regsvr32 exit code: $($process32.ExitCode)"
            if ($process32.ExitCode -ne 0) {
                throw "32-bit regsvr32 failed with exit code $($process32.ExitCode)"
            }
        }

        $process64 = Start-Process regsvr32.exe -ArgumentList "/s", "`"$dllPath`"" -PassThru -Wait
        Write-Host "64-bit regsvr32 exit code: $($process64.ExitCode)"
        if ($process64.ExitCode -ne 0) {
            throw "64-bit regsvr32 failed with exit code $($process64.ExitCode)"
        }
    } finally {
        Stop-Transcript | Out-Null
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
    $hash = Get-FileHash -LiteralPath $Path -Algorithm SHA256
    Write-Host "$Label file: size=$($item.Length), modified=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')), sha256=$($hash.Hash.ToLowerInvariant())"

    $entry = Get-ManifestEntry (Split-Path $Path -Parent) (Split-Path $Path -Leaf)
    if ($null -ne $entry) {
        $hashMatches = ([string]$entry.sha256).ToLowerInvariant() -eq $hash.Hash.ToLowerInvariant()
        $sizeMatches = [int64]$entry.bytes -eq $item.Length
        Write-Host "$Label manifest: version=$(Get-PackageVersion (Split-Path $Path -Parent)), hash_match=$hashMatches, size_match=$sizeMatches"
    } else {
        Write-Host "$Label manifest: <not found>"
    }
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
    exit 0
}

if ($Unregister) {
    Write-Host "Unregistering Neokey..."
    
    # 1. Remove TIP from current user's language list (non-elevated)
    Write-Host "Removing TIP from user language list..."
    $list = Get-WinUserLanguageList
    $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" }
    if ($null -ne $viLang) {
        $toRemove = @($viLang.InputMethodTips | Where-Object { $_ -eq $tipStr })
        if ($toRemove.Count -gt 0) {
            foreach ($item in $toRemove) {
                [void]$viLang.InputMethodTips.Remove($item)
            }
            try {
                Set-WinUserLanguageList $list -Force
                Write-Host "Successfully removed TIP from user language list."
            } catch {
                Write-Warning "Failed to update user language list: $_"
            }
        } else {
            Write-Host "TIP was not in user language list."
        }
    }

    # 2. Unregister DLL COM and TSF system-wide (requires elevation)
    if (-not (Is-Elevated)) {
        Write-Host "Requesting Administrator privileges to unregister DLL..."
        $cmd = "& { " +
               "if ('$dll32Path') { Start-Process C:\Windows\SysWOW64\regsvr32.exe -ArgumentList '/u', '/s', '$dll32Path' -Wait; } " +
               "Start-Process regsvr32.exe -ArgumentList '/u', '/s', '$dllPath' -Wait " +
               "}"
        $process = Start-Process powershell.exe -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", "`"$cmd`"" -Verb RunAs -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLLs unregistered successfully."
        } else {
            Write-Error "Failed to unregister DLLs. Exit code: $($process.ExitCode)"
        }
    } else {
        if ($dll32Path) {
            Start-Process C:\Windows\SysWOW64\regsvr32.exe -ArgumentList "/u", "/s", "`"$dll32Path`"" -PassThru -Wait
        }
        Start-Process regsvr32.exe -ArgumentList "/u", "/s", "`"$dllPath`"" -PassThru -Wait
        Write-Host "DLLs unregistered successfully."
    }
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

    # 2. Add TIP to current user's language list (non-elevated)
    Write-Host "Adding TIP to user language list..."
    $list = Get-WinUserLanguageList
    $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" }
    if ($null -eq $viLang) {
        Write-Host "Vietnamese language not found in user settings. Adding vi-VN..."
        $viObj = New-WinUserLanguageList -Language "vi-VN"
        $list.Add($viObj[0])
        $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" }
    }
    
    if (-not ($viLang.InputMethodTips -contains $tipStr)) {
        $viLang.InputMethodTips.Add($tipStr)
        try {
            Set-WinUserLanguageList $list -Force
            Write-Host "Successfully added TIP to user language list."
        } catch {
            Write-Warning "Failed to update user language list: $_"
        }
    } else {
        Write-Host "TIP is already in user language list."
    }
}
