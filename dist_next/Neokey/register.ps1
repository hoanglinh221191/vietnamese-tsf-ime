param(
    [switch]$Unregister,
    [switch]$Status
)

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

function Is-Elevated {
    $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object System.Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

if ($Status) {
    Write-Host "Checking registration status..."
    $regQuery = Start-Process reg.exe -ArgumentList "query", "HKLM\Software\Classes\CLSID\$clsid", "/ve" -NoNewWindow -PassThru -Wait
    $comReg64 = ($regQuery.ExitCode -eq 0)
    if (-not $comReg64) {
        $regQuery = Start-Process reg.exe -ArgumentList "query", "HKCU\Software\Classes\CLSID\$clsid", "/ve" -NoNewWindow -PassThru -Wait
        $comReg64 = ($regQuery.ExitCode -eq 0)
    }
    Write-Host "64-bit COM DLL Registered: $comReg64"

    $comReg32 = $false
    if ([Environment]::Is64BitOperatingSystem) {
        $regQuery32 = Start-Process reg.exe -ArgumentList "query", "HKLM\Software\Classes\Wow6432Node\CLSID\$clsid", "/ve" -NoNewWindow -PassThru -Wait
        $comReg32 = ($regQuery32.ExitCode -eq 0)
        if (-not $comReg32) {
            $regQuery32 = Start-Process reg.exe -ArgumentList "query", "HKCU\Software\Classes\Wow6432Node\CLSID\$clsid", "/ve" -NoNewWindow -PassThru -Wait
            $comReg32 = ($regQuery32.ExitCode -eq 0)
        }
        Write-Host "32-bit COM DLL Registered: $comReg32"
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
    Write-Host "Registering Neokey (in-place)..."
    $targetDir = Split-Path $dllPath -Parent

    # 1. Register DLL COM and TSF system-wide (requires elevation)
    if (-not (Is-Elevated)) {
        Write-Host "Requesting Administrator privileges to register DLL..."
        $logPath = Join-Path $targetDir "register_elevated.log"
        $cmd = "& { " +
               "Start-Transcript -Path '$logPath' -Force; " +
               "Write-Host 'Target directory: $targetDir'; " +
               "Write-Host 'DLL 64 path: $dllPath'; " +
               "Write-Host 'DLL 32 path: $dll32Path'; " +
               "icacls '$targetDir' /grant '*S-1-15-2-1:(OI)(CI)(RX)' /Q; " +
               "icacls '$targetDir' /grant '*S-1-15-2-2:(OI)(CI)(RX)' /Q; " +
               "icacls '$dllPath' /grant '*S-1-15-2-1:(RX)' /Q; " +
               "icacls '$dllPath' /grant '*S-1-15-2-2:(RX)' /Q; " +
               "if ('$dll32Path') { " +
               "  icacls '$dll32Path' /grant '*S-1-15-2-1:(RX)' /Q; " +
               "  icacls '$dll32Path' /grant '*S-1-15-2-2:(RX)' /Q; " +
               "  `$p32 = Start-Process C:\Windows\SysWOW64\regsvr32.exe -ArgumentList '/s', '$dll32Path' -Wait -PassThru; " +
               "  Write-Host '32-bit regsvr32 exit code:' `$p32.ExitCode; " +
               "} " +
               "`$p64 = Start-Process regsvr32.exe -ArgumentList '/s', '$dllPath' -Wait -PassThru; " +
               "Write-Host '64-bit regsvr32 exit code:' `$p64.ExitCode; " +
               "Stop-Transcript; " +
               "}"
        
        $process = Start-Process powershell.exe -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", "`"$cmd`"" -Verb RunAs -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLLs registered successfully in-place."
        } else {
            Write-Error "Failed to register DLLs. Exit code: $($process.ExitCode)"
        }
    } else {
        icacls "$targetDir" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /Q | Out-Null
        icacls "$targetDir" /grant "*S-1-15-2-2:(OI)(CI)(RX)" /Q | Out-Null
        icacls "$dllPath" /grant "*S-1-15-2-1:(RX)" /Q | Out-Null
        icacls "$dllPath" /grant "*S-1-15-2-2:(RX)" /Q | Out-Null
        if ($dll32Path) {
            icacls "$dll32Path" /grant "*S-1-15-2-1:(RX)" /Q | Out-Null
            icacls "$dll32Path" /grant "*S-1-15-2-2:(RX)" /Q | Out-Null
            Start-Process C:\Windows\SysWOW64\regsvr32.exe -ArgumentList "/s", "`"$dll32Path`"" -PassThru -Wait | Out-Null
        }

        $process = Start-Process regsvr32.exe -ArgumentList "/s", "`"$dllPath`"" -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLLs registered successfully in-place."
        } else {
            Write-Error "Failed to register DLLs. Exit code: $($process.ExitCode)"
        }
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
