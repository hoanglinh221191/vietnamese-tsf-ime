param(
    [switch]$Unregister,
    [switch]$Status
)

$dllPath = Resolve-Path "$PSScriptRoot\build\neokey.dll" -ErrorAction SilentlyContinue
if ($null -eq $dllPath) {
    Write-Error "Could not find build\neokey.dll. Please compile the project first."
    exit 1
}
$dllPath = $dllPath.Path

$targetDir = "C:\neokey"
$targetDllPath = "$targetDir\neokey.dll"

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
    $comReg = Test-Path "HKCU:\Software\Classes\CLSID\$clsid"
    if (-not $comReg) {
        $comReg = Test-Path "HKLM:\Software\Classes\CLSID\$clsid"
    }
    Write-Host "COM DLL Registered: $comReg"
    
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
        Write-Host "Requesting Administrator privileges to unregister and clean up DLL..."
        $cmd = "& { " +
               "Start-Process regsvr32.exe -ArgumentList '/u', '/s', '$targetDllPath' -Wait; " +
               "try { Remove-Item -Path '$targetDir' -Recurse -Force -ErrorAction Stop } " +
               "catch { " +
                   "Get-ChildItem '$targetDir' -Filter 'neokey.dll*' | Where-Object { `$_.Name -notlike '*.old.*' } | " +
                   "ForEach-Object { try { Rename-Item -Path `$_.FullName -NewName (`$_.Name + '.old.' + [Guid]::NewGuid().ToString().Substring(0,8)) -Force } catch {} } " +
               "}" +
               "}"
        $process = Start-Process powershell.exe -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", "`"$cmd`"" -Verb RunAs -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLL unregistered and cleaned up successfully."
        } else {
            Write-Error "Failed to unregister DLL. Exit code: $($process.ExitCode)"
        }
    } else {
        Start-Process regsvr32.exe -ArgumentList "/u", "/s", "`"$targetDllPath`"" -PassThru -Wait
        try {
            Remove-Item -Path $targetDir -Recurse -Force -ErrorAction Stop
        } catch {
            Get-ChildItem $targetDir -Filter "neokey.dll*" | Where-Object { $_.Name -notlike "*.old.*" } |
            ForEach-Object { try { Rename-Item -Path $_.FullName -NewName ($_.Name + ".old." + [Guid]::NewGuid().ToString().Substring(0,8)) -Force } catch {} }
        }
    }
} else {
    Write-Host "Registering Neokey..."

    # 1. Register DLL COM and TSF system-wide (requires elevation)
    if (-not (Is-Elevated)) {
        Write-Host "Requesting Administrator privileges to deploy and register DLL..."
        $cmd = "& { " +
               "New-Item -ItemType Directory -Path '$targetDir' -Force | Out-Null; " +
               "icacls '$targetDir' /grant '*S-1-15-2-1:(OI)(CI)(RX)' /Q | Out-Null; " +
               "icacls '$targetDir' /grant '*S-1-15-2-2:(OI)(CI)(RX)' /Q | Out-Null; " +
               "try { Copy-Item -Path '$dllPath' -Destination '$targetDllPath' -Force -ErrorAction Stop } " +
               "catch { " +
                   "Rename-Item -Path '$targetDllPath' -NewName ('neokey.dll.old.' + [Guid]::NewGuid().ToString().Substring(0,8)) -Force; " +
                   "Copy-Item -Path '$dllPath' -Destination '$targetDllPath' -Force " +
               "}; " +
               "icacls '$targetDllPath' /grant '*S-1-15-2-1:(RX)' /Q | Out-Null; " +
               "icacls '$targetDllPath' /grant '*S-1-15-2-2:(RX)' /Q | Out-Null; " +
               "Start-Process regsvr32.exe -ArgumentList '/s', '$targetDllPath' -Wait " +
               "}"
        
        $process = Start-Process powershell.exe -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", "`"$cmd`"" -Verb RunAs -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLL deployed and registered successfully at $targetDllPath."
        } else {
            Write-Error "Failed to deploy and register DLL. Exit code: $($process.ExitCode)"
        }
    } else {
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        icacls $targetDir /grant "*S-1-15-2-1:(OI)(CI)(RX)" /Q | Out-Null
        icacls $targetDir /grant "*S-1-15-2-2:(OI)(CI)(RX)" /Q | Out-Null
        try {
            Copy-Item -Path $dllPath -Destination $targetDllPath -Force -ErrorAction Stop
        } catch {
            Rename-Item -Path $targetDllPath -NewName ("neokey.dll.old." + [Guid]::NewGuid().ToString().Substring(0,8)) -Force
            Copy-Item -Path $dllPath -Destination $targetDllPath -Force
        }
        icacls $targetDllPath /grant "*S-1-15-2-1:(RX)" /Q | Out-Null
        icacls $targetDllPath /grant "*S-1-15-2-2:(RX)" /Q | Out-Null
        $process = Start-Process regsvr32.exe -ArgumentList "/s", "`"$targetDllPath`"" -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLL deployed and registered successfully at $targetDllPath."
        } else {
            Write-Error "Failed to register DLL. Exit code: $($process.ExitCode)"
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
