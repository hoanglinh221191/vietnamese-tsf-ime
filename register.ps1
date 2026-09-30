param(
    [switch]$Unregister,
    [switch]$Status,
    [switch]$RequireManifest,
    [switch]$RegisterElevatedOnly,
    [switch]$UnregisterElevatedOnly,
    [switch]$VerifyManifest,
    [switch]$SetDefault,
    [switch]$ConfigureCurrentUserOnly,
    [switch]$UnconfigureCurrentUserOnly,
    # Do not register the second copy of Neokey under English (US).
    #
    # Both copies type identical Vietnamese - same DLL, same settings - and only
    # the label Windows puts on the input differs. That label decides what
    # applications which pick a font by input language do, CorelDRAW and Word
    # among them: under Vietnamese they reach for whatever font is set for that
    # language and drop the one the user chose, under English they keep it. So
    # both are registered by default and Win+Space moves between them.
    #
    # Vietnamese remains the default input either way - it leads the input order
    # and holds the default-method override - so the extra entry costs a machine
    # that never touches it nothing but a stop in the Win+Space cycle. This
    # switch is for someone who does not want even that.
    [switch]$NoEnglishProfile,
    # Leave the shorthand file this user typed. Everything else Neokey put on
    # the machine still goes: settings, logs, registrations, the startup entry.
    [switch]$KeepUserData
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

# -LiteralPath throughout: the folder is whatever the user extracted the zip
# to, and a name like "Neokey [0.1.18]" is a wildcard pattern to -Path that
# matches nothing, so the install used to stop at "Could not find neokey.dll"
# with the file right there.
$dllPath = Resolve-Path -LiteralPath "$PSScriptRoot\build\neokey.dll" -ErrorAction SilentlyContinue
if ($null -eq $dllPath) {
    $dllPath = Resolve-Path -LiteralPath "$PSScriptRoot\neokey.dll" -ErrorAction SilentlyContinue
}
if ($null -ne $dllPath) {
    $dllPath = $dllPath.Path
}

$dll32Path = Resolve-Path -LiteralPath "$PSScriptRoot\build\neokey32.dll" -ErrorAction SilentlyContinue
if ($null -eq $dll32Path) {
    $dll32Path = Resolve-Path -LiteralPath "$PSScriptRoot\neokey32.dll" -ErrorAction SilentlyContinue
}
if ($null -ne $dll32Path) {
    $dll32Path = $dll32Path.Path
}

if ($null -eq $dllPath -and $null -eq $dll32Path) {
    # Removing Neokey does not need the DLLs: without them there is nothing to
    # ask to unregister, and the sweep takes the registration away instead. An
    # antivirus that quarantined them must not leave an uninstall that cannot
    # start.
    if ($Unregister -or $UnregisterElevatedOnly -or $UnconfigureCurrentUserOnly) {
        Write-Warning "neokey.dll and neokey32.dll are not in this folder; removing what is registered without them."
    } else {
        Write-Error "Could not find neokey.dll or neokey32.dll. Please compile the project first."
        exit 1
    }
}

$configPath = Resolve-Path -LiteralPath "$PSScriptRoot\build\neokey_config.exe" -ErrorAction SilentlyContinue
if ($null -eq $configPath) {
    $configPath = Resolve-Path -LiteralPath "$PSScriptRoot\neokey_config.exe" -ErrorAction SilentlyContinue
}
if ($null -ne $configPath) {
    $configPath = $configPath.Path
}

$clsid = "{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}"
$profileGuid = "{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}"
$tipStr = "042A:$clsid$profileGuid"
# The same service filed under English. A TIP is identified by language as well
# as by class and profile, so these two strings name two entries backed by one
# DLL rather than two installations.
$tipStrEnglish = "0409:$clsid$profileGuid"
$registerEnglish = -not $NoEnglishProfile

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

    # What each failure means for the person holding the folder. A bare "SHA256
    # mismatch" was all 0.1.18 said, and the usual cause is not a damaged
    # download: extracting a new zip over a folder Neokey is running from.
    # Windows will not replace a DLL or program that is in use, the extractor
    # skips it, and the folder ends up holding two versions.
    $mixedVersionsHint = "The folder holds files from two versions of Neokey. This happens when a new zip is extracted over a folder Neokey is still running from: Windows keeps the files that are in use. Extract the zip into a new, empty folder and run install.bat there, or run uninstall.bat, restart Windows, and extract again."
    $missingFileHint = "Extract the whole zip again into a new, empty folder. If the file disappears again, an antivirus program is removing it."
    foreach ($entry in $entries.Values) {
        $relativePath = [string]$entry.path
        $path = Join-Path $PSScriptRoot $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required release file missing: $path. $missingFileHint"
        }

        $item = Get-Item -LiteralPath $path
        if ([int64]$entry.bytes -ne $item.Length) {
            throw "Size mismatch for $relativePath. Expected $($entry.bytes), got $($item.Length). $mixedVersionsHint"
        }

        $actualHash = Get-Sha256Hex -Path $path
        $expectedHash = ([string]$entry.sha256).ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "SHA256 mismatch for $relativePath. $mixedVersionsHint"
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

function Set-NeokeyProfilePreference {
    param(
        [bool]$EnableEnglish,
        [string]$KeyPath = "HKCU:\Software\Neokey"
    )

    if (-not (Test-Path -LiteralPath $KeyPath)) {
        New-Item -Path $KeyPath -Force | Out-Null
    }
    Set-ItemProperty -LiteralPath $KeyPath -Name "RegisterEnglishProfile" `
        -Value ([int]$EnableEnglish) -Type DWord -Force
}

function Invoke-DllRegistration {
    Assert-ArtifactManifest -Required:$RequireManifest

    # Set the choice before the DLL reads it, including on upgrades from a
    # previous opt-out. A failed write must not silently register fewer profiles.
    Set-NeokeyProfilePreference -EnableEnglish $registerEnglish

    if ($registerEnglish) {
        Write-Host "Registering Neokey under both Vietnamese and English (US)..."
    } else {
        Write-Host "Registering Neokey under Vietnamese only (in-place)..."
    }
    # Kept by the caller's transcript: see Invoke-ElevatedStep.
    $targetDir = Split-Path $dllPath -Parent
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
}

# The Administrator step runs in a window of its own that closes when it ends,
# so what it says is kept in a file beside the DLLs - the one folder both that
# window and the one that asked for it can reach, whichever account approved
# the prompt - and shown by the window that asked when the step fails.
function Get-ElevatedLogPath {
    param([string]$Name)
    $folder = if ($dllPath) { Split-Path $dllPath -Parent } else { $PSScriptRoot }
    return Join-Path $folder $Name
}

function Get-UnregisterLogPath {
    return Get-ElevatedLogPath "unregister_elevated.log"
}

function Get-RegisterLogPath {
    return Get-ElevatedLogPath "register_elevated.log"
}

function Write-ElevatedLog {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    Write-Host "What the Administrator step reported ($Path):"
    Get-Content -LiteralPath $Path |
        Where-Object { $_ -notmatch '^\*{10,}' -and $_ -notmatch '^(Start|End) time|^Username|^RunAs User|^Configuration Name|^Machine|^Host Application|^Process ID|^PS\w+Version|^BuildVersion|^CLRVersion|^WSManStackVersion|^SerializationVersion|^Transcript started|^Windows PowerShell transcript' } |
        ForEach-Object { Write-Host "  $_" }
}

# Runs the Administrator step's work under its transcript and turns what it
# throws into an exit code, so a failure reaches the window that asked for it
# with its reason instead of as a bare number.
function Invoke-ElevatedStep {
    param(
        [string]$LogPath,
        [scriptblock]$Step,
        [string]$SuccessMessage
    )
    $transcribing = $false
    try {
        Start-Transcript -Path $LogPath -Force | Out-Null
        $transcribing = $true
    } catch {
        Write-Verbose "Elevated transcript: $_"
    }
    $exitCode = 0
    try {
        # To the screen, not the return value: the caller reads a number back.
        & $Step | Out-Host
        Write-Host $SuccessMessage
    } catch {
        Write-Host "ERROR: $($_.Exception.Message)"
        $exitCode = 1
    } finally {
        if ($transcribing) {
            Stop-Transcript | Out-Null
        }
    }
    return $exitCode
}

# Every application that takes typing loads neokey.dll from the folder it was
# registered in, for as long as Neokey stays installed. A portable folder in one
# of these places installs without complaint and breaks afterwards - when the
# temporary folder is emptied, the drive is unplugged, or the network is not
# there at sign-in - so the install says so before it writes anything.
#
# Returns $null, or what is wrong and whether the install has to stop. The
# folders to compare against are parameters so this can be checked without
# being on such a drive.
function Get-InstallLocationProblem {
    param(
        [string]$Directory,
        [string[]]$TempDirectories = @(),
        [string[]]$OneDriveDirectories = @(),
        [string]$DriveType = ""
    )

    if ([string]::IsNullOrWhiteSpace($Directory)) {
        return $null
    }
    $full = $Directory.TrimEnd('\') + '\'

    function Test-Under {
        param([string]$Path, [string[]]$Roots)
        foreach ($root in $Roots) {
            if ([string]::IsNullOrWhiteSpace($root)) {
                continue
            }
            $prefix = $root.TrimEnd('\') + '\'
            if ($Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
        return $false
    }

    if ($full.StartsWith('\\') -or $DriveType -eq "Network") {
        return [pscustomobject]@{
            Kind = "Network"
            Blocking = $true
            Message = "This folder is on a network drive ($Directory). Every app you type in would load Neokey over the network, and the Administrator step cannot see drives mapped for your account. Copy the whole folder to this computer, for example to C:\Neokey, and run install.bat from there."
        }
    }

    # Explorer's "open the zip and double-click", WinRAR and 7-Zip each unpack
    # what they run into a folder of their own under TEMP and delete it again.
    # Checked by name as well as by place, since TEMP can be spelled in 8.3 form.
    foreach ($segment in $full.TrimEnd('\').Split('\')) {
        if ($segment -match '^(Temp\d+_|Rar\$(EX|DI)|7z[OE][0-9A-Fa-f]{6,}$)') {
            return [pscustomobject]@{
                Kind = "Archive"
                Blocking = $true
                Message = "install.bat is running from inside the zip file ($Directory). Windows unpacked it into a temporary folder that it deletes later, and Neokey would go with it. Right-click the zip, choose Extract All, and run install.bat from the extracted folder."
            }
        }
    }

    if (Test-Under $full $TempDirectories) {
        return [pscustomobject]@{
            Kind = "Temp"
            Blocking = $true
            Message = "This folder is inside the temporary folder ($Directory), which Windows and cleanup tools empty. Move the whole folder somewhere it can stay, for example to C:\Neokey, and run install.bat from there."
        }
    }

    if ($DriveType -eq "Removable") {
        return [pscustomobject]@{
            Kind = "Removable"
            Blocking = $false
            Message = "This folder is on a removable drive ($Directory). Neokey stops working in every app while that drive is unplugged. A folder on this computer, for example C:\Neokey, avoids that."
        }
    }

    if (Test-Under $full $OneDriveDirectories) {
        return [pscustomobject]@{
            Kind = "OneDrive"
            Blocking = $false
            Message = "This folder is inside OneDrive ($Directory). Neokey's program files are being set to 'Always keep on this device' so OneDrive cannot turn them into online-only copies. A folder outside OneDrive, for example C:\Neokey, avoids the question."
        }
    }

    return $null
}

function Assert-InstallLocation {
    $driveType = ""
    try {
        $root = [System.IO.Path]::GetPathRoot($PSScriptRoot)
        if (-not [string]::IsNullOrWhiteSpace($root) -and -not $root.StartsWith('\\')) {
            $driveType = [string](New-Object System.IO.DriveInfo($root)).DriveType
        }
    } catch {
        Write-Verbose "Drive type of ${PSScriptRoot}: $_"
    }

    $tempDirectories = @($env:TEMP, $env:TMP, [System.IO.Path]::GetTempPath())
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $tempDirectories += (Join-Path $env:LOCALAPPDATA "Temp")
    }
    if (-not [string]::IsNullOrWhiteSpace($env:SystemRoot)) {
        $tempDirectories += (Join-Path $env:SystemRoot "Temp")
    }

    $problem = Get-InstallLocationProblem `
        -Directory $PSScriptRoot `
        -TempDirectories $tempDirectories `
        -OneDriveDirectories @($env:OneDrive, $env:OneDriveConsumer, $env:OneDriveCommercial) `
        -DriveType $driveType
    if ($null -eq $problem) {
        return
    }
    if ($problem.Blocking) {
        throw $problem.Message
    }
    Write-Warning $problem.Message
    if ($problem.Kind -eq "OneDrive") {
        foreach ($file in @($dllPath, $dll32Path, $configPath)) {
            if ([string]::IsNullOrWhiteSpace($file)) {
                continue
            }
            # +P is OneDrive's "Always keep on this device", -U clears
            # "online-only". The DLLs are what every app loads; the program is
            # what starts at sign-in, possibly before OneDrive is running.
            & attrib.exe +P -U "$file" 2>&1 | Out-Null
        }
    }
}

# Files that came out of a zip downloaded with a browser carry its "from the
# internet" mark. For neokey_config.exe that means a SmartScreen or "Open File -
# Security Warning" prompt each time Windows starts it from the startup entry,
# and when this script starts it below. Only files whose hashes the manifest has
# just vouched for are cleared, and only when the person ran the install.
function Clear-DownloadedMark {
    foreach ($file in @($dllPath, $dll32Path, $configPath)) {
        if ([string]::IsNullOrWhiteSpace($file)) {
            continue
        }
        try {
            Unblock-File -LiteralPath $file -ErrorAction Stop
        } catch {
            Write-Verbose "Unblock ${file}: $_"
        }
    }
}

# The installer closes the tray and opens it again after an upgrade; a portable
# install did neither, so the icon did not appear until the next sign-in and an
# older copy - from another folder, or a version ago - kept running meanwhile.
# The new one has to be the one left running: a second copy started with
# -silent sees the first and exits.
function Start-NeokeyTrayApp {
    if ($null -eq $configPath) {
        return
    }
    if (Is-Elevated) {
        # Started from here it would run as the elevated account, which is not
        # the one typing. The startup entry brings it up at the next sign-in.
        Write-Host "The Neokey tray app will start at the next sign-in."
        return
    }
    Stop-NeokeyTrayApp
    try {
        # -WorkingDirectory is resolved as a wildcard pattern (-FilePath is
        # not), so a folder named "Neokey [0.1.18]" has to be escaped.
        $workingDirectory = [System.Management.Automation.WildcardPattern]::Escape(
            (Split-Path $configPath -Parent))
        Start-Process -FilePath $configPath -ArgumentList "-silent" `
            -WorkingDirectory $workingDirectory | Out-Null
        Write-Host "Started the Neokey tray app."
    } catch {
        Write-Warning "Could not start the Neokey tray app; it will start at the next sign-in. $_"
    }
}

function Invoke-DllUnregistration {
    # Something that can be run again. A DLL refuses when Windows no longer has
    # its profile - removed by the installer's uninstaller, or by an uninstall
    # that stopped halfway - and 0.1.18's refused that way every time, so the
    # first refusal stopping everything left an uninstall that could never
    # finish. Each DLL is asked in turn, the keys are swept either way, and what
    # is left on the machine decides.
    Write-Host "Unregistering Neokey DLLs..."
    $failures = @()
    $dlls = @(
        @{ Path = $dll32Path; Exe = "C:\Windows\SysWOW64\regsvr32.exe"; Arch = "32-bit" },
        @{ Path = $dllPath; Exe = "regsvr32.exe"; Arch = "64-bit" }
    )
    foreach ($dll in $dlls) {
        if (-not $dll.Path) {
            continue
        }
        try {
            Invoke-Regsvr32 `
                -ExecutablePath $dll.Exe `
                -DllFilePath $dll.Path `
                -Operation Unregister `
                -Architecture $dll.Arch
        } catch {
            $failures += $_.Exception.Message
            Write-Warning $_.Exception.Message
        }
    }
    Remove-NeokeyMachineRegistryResidue

    $remaining = @(Get-NeokeyMachineRegistrationKeys | Where-Object {
        Test-Path -LiteralPath $_
    })
    if ($remaining.Count -gt 0) {
        $details = @($failures) + @("still registered: $($remaining -join ', ')")
        throw "Neokey is still registered. $($details -join '; ')"
    }
    if ($failures.Count -gt 0) {
        Write-Host "regsvr32 reported a problem, but nothing of Neokey's registration is left on this machine."
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

# What an input list starts on. Windows answers this in two steps: an override
# names one input method outright, and without one the first language in the
# list wins. Both the signed-in user and the sign-in screen keep their own copy
# of that pair, which is how a machine can be configured perfectly and still
# come back on the wrong keyboard - the screen you unlock at has its own
# settings, and its choice carries into the session.
#
# Pure on purpose: the two registry locations are read by the caller, so this
# can be tested without a sign-in screen to look at.
function Test-InputListResolvesToNeokey {
    param(
        [string]$OverrideTip,
        [string[]]$Languages,
        [string]$NeokeyTip,
        [string]$NeokeyLanguageTag
    )

    if (-not [string]::IsNullOrWhiteSpace($OverrideTip)) {
        return [string]::Equals(
            $OverrideTip, $NeokeyTip,
            [System.StringComparison]::OrdinalIgnoreCase)
    }
    if ($null -ne $Languages -and @($Languages).Count -gt 0) {
        # "vi" and "vi-VN" are the same language to Windows and to the rest of
        # this script, which matches it as vi* throughout.
        $first = [string]@($Languages)[0]
        return [string]::Equals(
                   $first, $NeokeyLanguageTag,
                   [System.StringComparison]::OrdinalIgnoreCase) -or
               $first.StartsWith(
                   "$NeokeyLanguageTag-",
                   [System.StringComparison]::OrdinalIgnoreCase)
    }
    return $false
}

# The sign-in screen's copy of the same pair, read from the account Windows
# shows it under. Absent values are reported as absent rather than guessed at:
# no override there is exactly the case that lets the first language win.
function Get-SignInScreenInputState {
    $path = "Registry::HKEY_USERS\.DEFAULT\Control Panel\International\User Profile"
    $state = [pscustomobject]@{
        Present = $false
        OverrideTip = ""
        Languages = @()
    }
    if (-not (Test-Path -LiteralPath $path)) {
        return $state
    }
    $props = Get-ItemProperty -LiteralPath $path -ErrorAction SilentlyContinue
    if ($null -eq $props) {
        return $state
    }
    $state.Present = $true
    if ($props.PSObject.Properties.Name -contains "InputMethodOverride") {
        $state.OverrideTip = [string]$props.InputMethodOverride
    }
    if ($props.PSObject.Properties.Name -contains "Languages") {
        $state.Languages = @($props.Languages)
    }
    return $state
}

function Activate-NeokeyInCurrentSession {
    try {
        $typeName = "Win32InputNativeActivator"
        $type = [System.Type]::GetType($typeName)
        if ($null -eq $type) {
            $code = @"
using System;
using System.Runtime.InteropServices;

public static class Win32InputNativeActivator {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr ActivateKeyboardLayout(IntPtr hkl, uint Flags);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SystemParametersInfo(uint uiAction, uint uiParam, ref IntPtr pvParam, uint fWinIni);

    public const uint WM_INPUTLANGCHANGEREQUEST = 0x0050;
    public const uint KLF_ACTIVATE = 0x00000001;
    public const uint KLF_SETFORPROCESS = 0x00000100;
    public const uint SPI_SETDEFAULTINPUTLANG = 0x005A;
    public const uint SPIF_SENDCHANGE = 0x0002;
    public static readonly IntPtr HWND_BROADCAST = new IntPtr(0xffff);
}
"@
            Add-Type -TypeDefinition $code -ErrorAction SilentlyContinue
        }

        # 0x0409042a: Vietnamese (0x042A) with US layout substitute (0x0409)
        $hkl = [IntPtr]0x0409042a
        $hklRef = $hkl

        # SPIF_UPDATEINIFILE is deliberately not passed. It makes Windows
        # rewrite HKCU\Keyboard Layout\Preload so this HKL lands in slot 1,
        # but it does not touch CTF's own copy of that order in
        # CTF\SortOrder\Language. The two lists then disagree, Windows
        # re-resolves them at the next sign-in, and the session can come back
        # on a different language's IME. Set-NeokeyInputOrder writes both
        # lists together instead; this call only affects the live session.
        [Win32InputNativeActivator]::SystemParametersInfo(
            [Win32InputNativeActivator]::SPI_SETDEFAULTINPUTLANG,
            0,
            [ref]$hklRef,
            [Win32InputNativeActivator]::SPIF_SENDCHANGE) | Out-Null

        [Win32InputNativeActivator]::PostMessage(
            [Win32InputNativeActivator]::HWND_BROADCAST,
            [Win32InputNativeActivator]::WM_INPUTLANGCHANGEREQUEST,
            [IntPtr]::Zero,
            $hkl) | Out-Null

        [Win32InputNativeActivator]::ActivateKeyboardLayout(
            $hkl,
            [Win32InputNativeActivator]::KLF_ACTIVATE -bor [Win32InputNativeActivator]::KLF_SETFORPROCESS) | Out-Null

        Write-Host "Activated Neokey as active keyboard layout in the current desktop session."
    } catch {
        Write-Verbose "Session activation note: $_"
    }
}

function Get-InputListOrder {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }

    $key = Get-Item -LiteralPath $Path
    $entries = foreach ($name in $key.GetValueNames()) {
        if ([string]::IsNullOrEmpty($name)) {
            continue
        }
        $index = 0
        $parsed = [int]::TryParse(
            $name,
            [System.Globalization.NumberStyles]::HexNumber,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$index)
        if (-not $parsed) {
            continue
        }
        [pscustomobject]@{
            Name  = $name
            Index = $index
            Value = ([string]$key.GetValue($name)).Trim().ToLowerInvariant()
        }
    }

    return @($entries | Sort-Object Index)
}

function Set-InputListOrder {
    param(
        [string]$Path,
        [string[]]$Values,
        [ValidateSet("Preload", "Ctf")]
        [string]$Style
    )

    $key = Get-Item -LiteralPath $Path
    $existing = @($key.GetValueNames() | Where-Object { -not [string]::IsNullOrEmpty($_) })

    $written = @()
    for ($i = 0; $i -lt $Values.Count; $i++) {
        # Preload numbers its entries from 1, CTF from 0 with 8 hex digits.
        $name = if ($Style -eq "Preload") { [string]($i + 1) } else { "{0:x8}" -f $i }
        Set-ItemProperty -LiteralPath $Path -Name $name -Value $Values[$i] -Force
        $written += $name
    }

    # Deduplicating can shorten the list, which would otherwise leave a stale
    # trailing entry behind and reintroduce the disagreement being fixed here.
    foreach ($stale in $existing) {
        if ($stale -notin $written) {
            Remove-ItemProperty -LiteralPath $Path -Name $stale -ErrorAction SilentlyContinue
        }
    }
}

function Set-NeokeyInputOrder {
    # Windows keeps the input list in two places that have to agree: the legacy
    # Win32 list in HKCU\Keyboard Layout\Preload, and CTF's own copy of the same
    # order in HKCU\Software\Microsoft\CTF\SortOrder\Language. Writing only one
    # of them - which both SPI_SETDEFAULTINPUTLANG and the old
    # Set-ItemProperty -Name "1" call did - leaves them out of step, and Windows
    # then re-resolves the active profile at the next sign-in. On a machine that
    # also has a third input language installed, that can hand the session to
    # the wrong IME entirely.
    #
    # Only the *input* order is touched. Microsoft Store apps - Notepad, Clock,
    # Calculator - resolve their interface language against the preferred
    # languages list (HKCU\Control Panel\International\User Profile\Languages)
    # and PreferredUILanguages, and neither is written here or anywhere else in
    # this script. Putting Vietnamese first in the input order does not put it
    # first in the language list.
    $vietnameseId = "0000042a"

    $targets = @(
        @{ Path = "HKCU:\Keyboard Layout\Preload"; Style = "Preload" },
        @{ Path = "HKCU:\Software\Microsoft\CTF\SortOrder\Language"; Style = "Ctf" }
    )

    foreach ($target in $targets) {
        try {
            $current = @(Get-InputListOrder -Path $target.Path)
            if ($current.Count -eq 0) {
                # No list yet means Windows has not built one for this user, and
                # inventing one here would only fight whatever it writes later.
                continue
            }

            $ordered = @($vietnameseId)
            foreach ($entry in $current) {
                if ($entry.Value -ne $vietnameseId -and $entry.Value -notin $ordered) {
                    $ordered += $entry.Value
                }
            }

            $before = @($current | ForEach-Object { $_.Value })
            if (($before -join ",") -eq ($ordered -join ",")) {
                continue
            }

            Set-InputListOrder -Path $target.Path -Values $ordered -Style $target.Style
            Write-Verbose "Input order in $($target.Path): $($before -join ', ') -> $($ordered -join ', ')"
        } catch {
            Write-Verbose "Input order update for $($target.Path): $_"
        }
    }

    # Read both lists back rather than assuming the writes landed: a list that
    # was missing is skipped above, and CTF can rewrite its copy underneath us.
    $preloadNow = @(Get-InputListOrder -Path $targets[0].Path | ForEach-Object { $_.Value })
    $ctfNow = @(Get-InputListOrder -Path $targets[1].Path | ForEach-Object { $_.Value })
    $agree = ($preloadNow -join ",") -eq ($ctfNow -join ",")
    if ($agree -and $preloadNow.Count -gt 0 -and $preloadNow[0] -eq $vietnameseId) {
        Write-Host "Neokey is first in both the Win32 and CTF input lists."
    } else {
        Write-Warning "Could not put Neokey first in both input lists. Win32: $($preloadNow -join ', '). CTF: $($ctfNow -join ', ')."
    }
}

function Set-NeokeyAutoStart {
    $runPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"

    if ($null -eq $configPath) {
        Write-Warning "neokey_config.exe was not found next to this script, so Neokey was not added to Windows startup."
        return
    }

    # Byte-for-byte the value the config app writes for its own "Start with
    # Windows" option, so the two agree and that setting reads back as on. The
    # -silent switch is what makes the app come up as a tray icon only: the tray
    # icon is added from WM_CREATE regardless, and -silent suppresses the
    # WM_USER_SHOW_SETTINGS post that would otherwise open the settings window.
    $command = "`"$configPath`" -silent"

    try {
        if (-not (Test-Path -LiteralPath $runPath)) {
            New-Item -Path $runPath -Force | Out-Null
        }
        Set-ItemProperty -LiteralPath $runPath -Name "Neokey" -Value $command -Force
        Write-Host "Neokey will start with Windows, minimised to the tray: $command"
    } catch {
        Write-Warning "Could not add Neokey to Windows startup: $_"
    }
}

# What this user's Vietnamese input looked like before Neokey first changed it.
#
# Each value has three states. Absent means nothing was ever recorded, which is
# what an install from a build older than this one leaves behind. An empty
# string means "there was nothing there". Anything else is what has to be put
# back. The uninstaller reads them so it can reverse what the installer did
# instead of guessing, which is how machines that never had Vietnamese ended up
# with Microsoft's Vietnamese keyboard after removing Neokey.
$script:neokeySettingsPath = "HKCU:\Software\Neokey"
$script:preNeokeyTipsValue = "PreviousVietnameseInputMethods"
$script:preNeokeyLanguageAddedValue = "AddedVietnameseLanguage"
$script:preNeokeySubstituteValue = "PreviousVietnameseLayoutSubstitute"

function Get-NeokeySettingValue {
    param([string]$Name)

    $property = Get-ItemProperty `
        -Path $script:neokeySettingsPath `
        -Name $Name `
        -ErrorAction SilentlyContinue
    if ($null -eq $property) {
        return $null
    }
    return $property.$Name
}

function Set-NeokeySettingValue {
    param(
        [string]$Name,
        $Value,
        [string]$Type = "String"
    )

    if (-not (Test-Path -LiteralPath $script:neokeySettingsPath)) {
        New-Item -Path "HKCU:\Software" -Name "Neokey" -Force | Out-Null
    }
    New-ItemProperty `
        -Path $script:neokeySettingsPath `
        -Name $Name `
        -Value $Value `
        -PropertyType $Type `
        -Force | Out-Null
}

function Test-ShouldRecordPreNeokeyState {
    param(
        [bool]$AddedLanguage,
        [string[]]$ReplacedInputMethods,
        [bool]$AlreadyRecorded
    )

    # The first run that touches Vietnamese is the only one that can see what
    # was there. A repair or upgrade run changes nothing - the language is
    # already in Neokey's shape - and recording "there was nothing here" for it
    # would tell the uninstaller the machine arrived that way.
    if ($AlreadyRecorded) {
        return $false
    }
    return ($AddedLanguage -or @($ReplacedInputMethods).Count -gt 0)
}

function Save-PreNeokeyVietnameseState {
    param(
        [bool]$AddedLanguage,
        [string[]]$ReplacedInputMethods
    )

    $alreadyRecorded = $null -ne (Get-NeokeySettingValue $script:preNeokeyTipsValue)
    if (-not (Test-ShouldRecordPreNeokeyState `
            -AddedLanguage $AddedLanguage `
            -ReplacedInputMethods $ReplacedInputMethods `
            -AlreadyRecorded $alreadyRecorded)) {
        return
    }

    try {
        Set-NeokeySettingValue `
            -Name $script:preNeokeyTipsValue `
            -Value ((@($ReplacedInputMethods)) -join ";")
        Set-NeokeySettingValue `
            -Name $script:preNeokeyLanguageAddedValue `
            -Value ([int][bool]$AddedLanguage) `
            -Type "DWord"
        Write-Verbose "Recorded the pre-Neokey Vietnamese state for uninstall."
    } catch {
        Write-Warning "Could not record what to restore on uninstall: $_"
    }
}

function Test-ShouldRecordLayoutSubstitute {
    param(
        [object]$AlreadyRecorded,
        [string]$ExistingValue,
        [string]$ValueAboutToBeWritten
    )

    if ($null -ne $AlreadyRecorded) {
        return $false
    }
    # A value that already reads the same as the one about to be written cannot
    # be attributed to anyone. Neokey writes exactly this, and so do other
    # Vietnamese IMEs. Recording nothing leaves it alone at uninstall, which is
    # the safe answer whichever of the two put it there.
    return $ExistingValue -ne $ValueAboutToBeWritten
}

function Clear-PreNeokeyVietnameseState {
    foreach ($name in @(
            $script:preNeokeyTipsValue,
            $script:preNeokeyLanguageAddedValue,
            $script:preNeokeySubstituteValue)) {
        Remove-ItemProperty `
            -Path $script:neokeySettingsPath `
            -Name $name `
            -ErrorAction SilentlyContinue
    }
}

function Get-VietnameseCleanupAction {
    param(
        [string[]]$RemainingInputMethods,
        [object]$RecordedInputMethods,
        [bool]$AddedLanguage,
        [bool]$IsOnlyLanguage,
        [bool]$IsDisplayLanguage
    )

    # $null means nothing was recorded; an empty array means the record says
    # there was nothing to keep. The two lead to different answers below, so
    # they are kept apart rather than both collapsing to "empty".
    $recorded = $null
    if ($null -ne $RecordedInputMethods) {
        $recorded = @($RecordedInputMethods |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    }

    if ($null -ne $recorded -and $recorded.Count -gt 0) {
        return [pscustomobject]@{ Action = "RestoreRecorded"; InputMethods = $recorded }
    }

    if (@($RemainingInputMethods).Count -gt 0) {
        # Something else is filed under Vietnamese - another IME, or a keyboard
        # added after Neokey. Not ours to remove.
        return [pscustomobject]@{ Action = "Leave"; InputMethods = @() }
    }

    # Vietnamese would be left with no input method at all, which is not a state
    # to hand back to Windows. Either the language goes or something fills it.
    if ($IsOnlyLanguage -or $IsDisplayLanguage) {
        return [pscustomobject]@{ Action = "InstallStockKeyboard"; InputMethods = @() }
    }
    if ($AddedLanguage) {
        return [pscustomobject]@{ Action = "RemoveLanguage"; InputMethods = @() }
    }
    if ($null -eq $recorded) {
        # Installed by a build that recorded nothing, so whether this machine had
        # Vietnamese beforehand is no longer knowable. Removing the language is
        # the recoverable mistake - Settings puts it back in one click - while
        # adding Microsoft's Vietnamese keyboard to a machine that never had it
        # is the one users report, because it turns the number row into tone
        # marks.
        return [pscustomobject]@{ Action = "RemoveLanguage"; InputMethods = @() }
    }
    return [pscustomobject]@{ Action = "InstallStockKeyboard"; InputMethods = @() }
}

function Restore-VietnameseLayoutSubstitute {
    $recorded = Get-NeokeySettingValue $script:preNeokeySubstituteValue
    if ($null -eq $recorded) {
        # Nothing recorded, so leave it alone. Pointing the Vietnamese layout at
        # the US one is also what other Vietnamese IMEs write, and removing
        # theirs would hand their users the number-row tone marks this is meant
        # to prevent. With Vietnamese gone from the language list the value is
        # never consulted anyway.
        return
    }

    $substPath = "HKCU:\Keyboard Layout\Substitutes"
    try {
        if ([string]::IsNullOrEmpty([string]$recorded)) {
            Remove-ItemProperty `
                -Path $substPath `
                -Name "0000042a" `
                -ErrorAction SilentlyContinue
            Write-Host "Removed the Vietnamese layout substitute Neokey added."
        } else {
            Set-ItemProperty `
                -Path $substPath `
                -Name "0000042a" `
                -Value ([string]$recorded) `
                -Force
            Write-Host "Restored the Vietnamese layout substitute to $recorded."
        }
    } catch {
        Write-Verbose "Vietnamese layout substitute restore: $_"
    }
}

function Set-NeokeyAsDefaultInputMethod {
    Write-Host "Setting Neokey as the default input method for the current Windows user..."

    # The order of the preferred languages list is deliberately left alone.
    #
    # That list is what Microsoft Store apps resolve their UI language against -
    # the current Notepad, Clock and Calculator are all Store apps - and it is
    # controlled independently of the Windows display language. An earlier
    # version of this script moved Vietnamese to the top here, which silently
    # turned those apps Vietnamese for every user who installed Neokey, did not
    # revert on uninstall, and was never needed: the default input method is set
    # by Set-WinDefaultInputMethodOverride below, which does not care about the
    # order of that list.
    #
    # Nothing here writes Set-WinUILanguageOverride either. It only existed to
    # protect the display language from the reordering above.

    # Warn - but do not silently change anything - if Vietnamese is already at
    # the top while Windows itself is running in another language. That is the
    # state the old script left behind, and it is what makes Store apps
    # Vietnamese.
    try {
        $list = Get-WinUserLanguageList
        if ($list.Count -gt 1 -and $list[0].LanguageTag -like "vi*") {
            $uiCulture = (Get-UICulture).Name
            if ($uiCulture -notlike "vi*") {
                Write-Warning "Vietnamese is first in your Preferred languages list while Windows itself runs in $uiCulture."
                Write-Warning "That is what makes Microsoft Store apps (Notepad, Clock, Calculator) show a Vietnamese interface."
                Write-Warning "To put them back: Settings > Time and language > Language and region, then drag $uiCulture above Vietnamese."
                Write-Warning "This does not affect Neokey: the default keyboard is set separately, just below."
            }
        }
    } catch {
        Write-Verbose "Could not inspect the preferred languages list: $_"
    }

    # 1. Set modern Windows 10/11 Input Method Override
    Set-WinDefaultInputMethodOverride -InputTip $tipStr
    $currentTip = Get-DefaultInputMethodTip
    if (-not [string]::Equals($currentTip, $tipStr, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Windows did not retain Neokey as the default input method override."
    }

    # 2. Ensure the Win32 and CTF input lists agree and lead with Neokey
    Set-NeokeyInputOrder

    # 3. Point the legacy Vietnamese layout at the US physical layout
    try {
        $substPath = "HKCU:\Keyboard Layout\Substitutes"
        if (-not (Test-Path $substPath)) {
            New-Item -Path "HKCU:\Keyboard Layout" -Name "Substitutes" -Force | Out-Null
        }
        $previousSubstitute = ""
        $existingSubstitute = Get-ItemProperty `
            -Path $substPath `
            -Name "0000042a" `
            -ErrorAction SilentlyContinue
        if ($null -ne $existingSubstitute) {
            $previousSubstitute = [string]$existingSubstitute."0000042a"
        }
        if (Test-ShouldRecordLayoutSubstitute `
                -AlreadyRecorded (Get-NeokeySettingValue $script:preNeokeySubstituteValue) `
                -ExistingValue $previousSubstitute `
                -ValueAboutToBeWritten "00000409") {
            Set-NeokeySettingValue `
                -Name $script:preNeokeySubstituteValue `
                -Value $previousSubstitute
        }
        Set-ItemProperty -Path $substPath -Name "0000042a" -Value "00000409" -Force
    } catch {
        Write-Verbose "Substitutes registry update: $_"
    }

    # 4. Activate immediately in the current running desktop session
    Activate-NeokeyInCurrentSession

    Write-Host "Neokey is now the default input method override for this user."
    Write-Host "The setting takes effect immediately, for new sign-in sessions, and remains after reboot."
}

function Add-NeokeyToUserLanguageList {
    Write-Host "Adding TIP to user language list..."
    $list = Get-WinUserLanguageList
    $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" } | Select-Object -First 1
    $addedVietnamese = $false
    if ($null -eq $viLang) {
        Write-Host "Vietnamese language not found in user settings. Adding vi-VN..."
        $viObj = New-WinUserLanguageList -Language "vi-VN"
        $list.Add($viObj[0])
        $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" } | Select-Object -First 1
        $addedVietnamese = $true
    }

    # Remove redundant legacy Microsoft Vietnamese keyboards so only Neokey remains under vi
    $changed = $false
    $legacyTips = @($viLang.InputMethodTips | Where-Object { $_ -ne $tipStr })
    foreach ($lt in $legacyTips) {
        $viLang.InputMethodTips.Remove($lt) | Out-Null
        $changed = $true
        Write-Host "Removed redundant built-in Vietnamese keyboard: $lt"
    }

    # Both of those are changes to somebody else's machine, and only this run
    # can still see what was there. Write it down before going any further.
    Save-PreNeokeyVietnameseState `
        -AddedLanguage $addedVietnamese `
        -ReplacedInputMethods $legacyTips

    if (-not ($viLang.InputMethodTips -contains $tipStr)) {
        $viLang.InputMethodTips.Add($tipStr)
        $changed = $true
        Write-Host "Successfully added TIP to user language list."
    } else {
        Write-Host "TIP is already in user language list."
    }

    # The English copy shares its language with the US keyboard, and that
    # keyboard stays. It is the only way to type what this service does not
    # handle, and the only way back if the service ever fails to load - which is
    # why the pruning above is confined to Vietnamese.
    $enLang = $list | Where-Object { $_.LanguageTag -like "en*" } | Select-Object -First 1
    if ($registerEnglish) {
        if ($null -eq $enLang) {
            Write-Host "English not found in user settings. Adding en-US..."
            $enObj = New-WinUserLanguageList -Language "en-US"
            $list.Add($enObj[0])
            $enLang = $list | Where-Object { $_.LanguageTag -like "en*" } | Select-Object -First 1
        }
        if (-not ($enLang.InputMethodTips -contains $tipStrEnglish)) {
            $enLang.InputMethodTips.Add($tipStrEnglish)
            $changed = $true
            Write-Host "Added the English copy of Neokey to the user language list."
        } else {
            Write-Host "The English copy of Neokey is already in the user language list."
        }
    } elseif ($null -ne $enLang -and ($enLang.InputMethodTips -contains $tipStrEnglish)) {
        [void]$enLang.InputMethodTips.Remove($tipStrEnglish)
        $changed = $true
        Write-Host "Removed the English copy of Neokey from the user language list."
    }

    if ($changed) {
        Set-WinUserLanguageList $list -Force
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
    $changed = $false

    # Each copy is looked for on its own. A machine may carry the English one
    # and not the Vietnamese, or the other way round, and stopping at whichever
    # is already gone would leave the other one listed with nothing behind it.
    $viLang = $list | Where-Object { $_.LanguageTag -like "vi*" } | Select-Object -First 1
    if ($null -ne $viLang) {
        $removedOurs = $false
        foreach ($item in @($viLang.InputMethodTips | Where-Object { $_ -eq $tipStr })) {
            [void]$viLang.InputMethodTips.Remove($item)
            $removedOurs = $true
            $changed = $true
        }

        if ($removedOurs) {
            $recordedRaw = Get-NeokeySettingValue $script:preNeokeyTipsValue
            $recordedTips = $null
            if ($null -ne $recordedRaw) {
                $recordedTips = @((([string]$recordedRaw) -split ";") |
                    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
            }

            $plan = Get-VietnameseCleanupAction `
                -RemainingInputMethods @($viLang.InputMethodTips) `
                -RecordedInputMethods $recordedTips `
                -AddedLanguage ([int](Get-NeokeySettingValue $script:preNeokeyLanguageAddedValue) -eq 1) `
                -IsOnlyLanguage ($list.Count -le 1) `
                -IsDisplayLanguage ((Get-UICulture).Name -like "vi*")

            switch ($plan.Action) {
                "RestoreRecorded" {
                    foreach ($tip in $plan.InputMethods) {
                        if (-not ($viLang.InputMethodTips -contains $tip)) {
                            $viLang.InputMethodTips.Add($tip)
                        }
                    }
                    Write-Host "Restored the Vietnamese keyboards that were there before Neokey: $($plan.InputMethods -join ', ')."
                }
                "RemoveLanguage" {
                    # By position, not by Remove($viLang): the entry came out of
                    # a pipeline, and matching it back against the list would
                    # rest on how the object it was wrapped in compares.
                    for ($i = $list.Count - 1; $i -ge 0; $i--) {
                        if ($list[$i].LanguageTag -like "vi*") {
                            $list.RemoveAt($i)
                        }
                    }
                    Write-Host "Removed the Vietnamese language entry, which Neokey was the only thing using."
                    Write-Host "If you want it back: Settings > Time and language > Language and region > Add a language."
                }
                "InstallStockKeyboard" {
                    $viLang.InputMethodTips.Add("042A:0000042a")
                    Write-Host "Vietnamese would have been left with no keyboard, so the built-in one takes Neokey's place."
                }
                default { }
            }
        }
    }

    $enLang = $list | Where-Object { $_.LanguageTag -like "en*" } | Select-Object -First 1
    if ($null -ne $enLang) {
        foreach ($item in @($enLang.InputMethodTips | Where-Object { $_ -eq $tipStrEnglish })) {
            [void]$enLang.InputMethodTips.Remove($item)
            $changed = $true
        }
        if ($enLang.InputMethodTips.Count -eq 0) {
            $enLang.InputMethodTips.Add("0409:00000409")
            $changed = $true
        }
    }

    if (-not $changed) {
        Write-Host "TIP was not in user language list."
        return
    }
    Set-WinUserLanguageList $list -Force
    Write-Host "Successfully removed TIP from user language list."
}

function Configure-NeokeyCurrentUser {
    # Setup registers DLLs in the elevated account, then calls this in the
    # original desktop account. Keep that user's preference/status in sync too.
    Set-NeokeyProfilePreference -EnableEnglish $registerEnglish
    Add-NeokeyToUserLanguageList
    Initialize-NeokeyUserSettings
    if ($SetDefault) {
        Set-NeokeyAsDefaultInputMethod
        # Unconfigure-NeokeyCurrentUser has always removed this value, but
        # nothing ever created it, so the tray app never came back after a
        # sign-in even though -SetDefault promises the setup survives one.
        Set-NeokeyAutoStart
    }
}

# Everything Neokey leaves on a machine, outside the program folder itself.
#
# One list, read by both uninstallers - the portable script and the installer's
# uninstall step - so a value added to one cannot be forgotten by the other, and
# so -Status can say what a failed uninstall left behind.
function Get-NeokeyResidueTargets {
    # The package directory is a parameter rather than $PSScriptRoot read from
    # inside, so this list can be built - and checked - without being the script.
    param([string]$PackageDirectory = $PSScriptRoot)

    $targets = @(
        [pscustomobject]@{
            Kind = "RegistryKey"
            Path = "HKCU:\Software\Neokey"
            Label = "settings"
            UserData = $false
        },
        [pscustomobject]@{
            Kind = "RegistryValue"
            Path = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
            Name = "Neokey"
            Label = "start with Windows"
            UserData = $false
        }
    )

    # The shorthand file is the one thing here the user typed themselves, so it
    # is the one thing -KeepUserData spares.
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $targets += [pscustomobject]@{
            Kind = "Directory"
            Path = (Join-Path $env:LOCALAPPDATA "Neokey")
            Label = "shorthand data"
            UserData = $true
        }
    }

    # The log is written to whatever GetTempPath() returns in each process that
    # loads the DLL, so more than one of these can exist on the same machine.
    $logDirectories = @($env:TEMP, "C:\Temp")
    if (-not [string]::IsNullOrWhiteSpace($env:SystemRoot)) {
        $logDirectories += (Join-Path $env:SystemRoot "Temp")
    }
    foreach ($directory in $logDirectories) {
        if ([string]::IsNullOrWhiteSpace($directory)) {
            continue
        }
        $targets += [pscustomobject]@{
            Kind = "File"
            Path = (Join-Path $directory "neokey.log")
            Label = "log"
            UserData = $false
        }
    }

    # Written by the elevated half of registration, next to the DLL it registered.
    if (-not [string]::IsNullOrWhiteSpace($PackageDirectory)) {
        $targets += [pscustomobject]@{
            Kind = "File"
            Path = (Join-Path $PackageDirectory "register_elevated.log")
            Label = "registration log"
            UserData = $false
        }
    }

    return $targets
}

function Test-NeokeyResidueTargetPresent {
    param([object]$Target)

    switch ($Target.Kind) {
        "RegistryKey" { return Test-Path -LiteralPath $Target.Path }
        "RegistryValue" {
            $property = Get-ItemProperty `
                -Path $Target.Path `
                -Name $Target.Name `
                -ErrorAction SilentlyContinue
            return $null -ne $property
        }
        default { return Test-Path -LiteralPath $Target.Path }
    }
}

function Stop-NeokeyTrayApp {
    # The tray app is what writes settings back, so it has to be gone before the
    # settings key is removed - otherwise it recreates the key on the next
    # change and the uninstall looks like it did nothing. Asking first, killing
    # second: WM_CLOSE runs its own teardown, which takes the icon out of the
    # notification area instead of leaving a dead one until the mouse hits it.
    try {
        if ($null -eq [System.Type]::GetType("NeokeyTrayWindowCloser")) {
            $code = @"
using System;
using System.Runtime.InteropServices;

public static class NeokeyTrayWindowCloser {
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string className, string windowName);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    public const uint WM_CLOSE = 0x0010;
}
"@
            Add-Type -TypeDefinition $code -ErrorAction SilentlyContinue
        }

        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            $hwnd = [NeokeyTrayWindowCloser]::FindWindowW("NeokeyTrayWindowClass", $null)
            if ($hwnd -eq [IntPtr]::Zero) {
                break
            }
            [NeokeyTrayWindowCloser]::PostMessageW(
                $hwnd,
                [NeokeyTrayWindowCloser]::WM_CLOSE,
                [IntPtr]::Zero,
                [IntPtr]::Zero) | Out-Null
            Start-Sleep -Milliseconds 150
        }
    } catch {
        Write-Verbose "Tray shutdown request: $_"
    }

    $remaining = @(Get-Process -Name "neokey_config" -ErrorAction SilentlyContinue)
    if ($remaining.Count -eq 0) {
        return
    }
    Write-Host "Closing the Neokey tray app..."
    foreach ($process in $remaining) {
        try {
            Stop-Process -Id $process.Id -Force -ErrorAction Stop
            $process.WaitForExit(5000) | Out-Null
        } catch {
            Write-Warning "Could not close the Neokey tray app (pid $($process.Id)): $_"
        }
    }
}

function Remove-NeokeyResidue {
    param([switch]$KeepUserData)

    Stop-NeokeyTrayApp

    foreach ($target in Get-NeokeyResidueTargets) {
        if ($KeepUserData -and $target.UserData) {
            if (Test-NeokeyResidueTargetPresent $target) {
                Write-Host "Kept $($target.Label): $($target.Path)"
            }
            continue
        }
        if (-not (Test-NeokeyResidueTargetPresent $target)) {
            continue
        }

        try {
            switch ($target.Kind) {
                "RegistryKey" {
                    Remove-Item -LiteralPath $target.Path -Recurse -Force -ErrorAction Stop
                }
                "RegistryValue" {
                    Remove-ItemProperty `
                        -Path $target.Path `
                        -Name $target.Name `
                        -Force `
                        -ErrorAction Stop
                }
                "Directory" {
                    Remove-Item -LiteralPath $target.Path -Recurse -Force -ErrorAction Stop
                }
                default {
                    Remove-Item -LiteralPath $target.Path -Force -ErrorAction Stop
                }
            }
            Write-Host "Removed $($target.Label): $($target.Path)"
        } catch {
            # A log still open in an application that has not been restarted is
            # the ordinary case here, and it is not a reason to fail the
            # uninstall. Say which file, so it can be deleted by hand.
            Write-Warning "Could not remove $($target.Label) at $($target.Path): $($_.Exception.Message)"
        }
    }
}

function Get-NeokeyMachineRegistrationKeys {
    return @(
        "HKLM:\SOFTWARE\Classes\CLSID\$clsid",
        "HKLM:\SOFTWARE\Classes\Wow6432Node\CLSID\$clsid",
        "HKCU:\SOFTWARE\Classes\CLSID\$clsid",
        "HKCU:\SOFTWARE\Classes\Wow6432Node\CLSID\$clsid",
        "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$clsid",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP\$clsid"
    )
}

function Remove-NeokeyMachineRegistryResidue {
    # Swept after regsvr32 /u, never before: unregistration is what asks Windows
    # to retract the profile, and deleting the keys underneath it first would
    # leave CTF holding a profile it can no longer describe. Anything still here
    # afterwards is a leftover, and leftovers are what make a reinstall behave
    # like the old version.
    foreach ($key in Get-NeokeyMachineRegistrationKeys) {
        if (-not (Test-Path -LiteralPath $key)) {
            continue
        }
        try {
            Remove-Item -LiteralPath $key -Recurse -Force -ErrorAction Stop
            Write-Host "Removed leftover registration key: $key"
        } catch {
            Write-Warning "Could not remove $key : $($_.Exception.Message)"
        }
    }
}

function Unconfigure-NeokeyCurrentUser {
    Remove-NeokeyFromUserLanguageList
    Restore-VietnameseLayoutSubstitute
    # Both of those read the record out of the settings key, which is about to
    # go. Clearing it separately is not redundant: if removing the key fails -
    # a permission, an open handle - the record must not survive to be replayed
    # against a later install.
    Clear-PreNeokeyVietnameseState

    # An upgrade does not come through here. The installer updates in place and
    # says so on its own wizard page, so settings and shorthand data survive a
    # version change; this path runs only when somebody is removing Neokey.
    Remove-NeokeyResidue -KeepUserData:$KeepUserData
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
    # Setup's own step, run in the program folder after Inno has written it.
    # That folder is not the portable package the manifest describes: it has
    # no install.bat, uninstall.bat or PORTABLE_RELEASE.md, README.md is
    # README.en.md there, and the shorthand file is the user's own when one was
    # already in place. Checked against that manifest, this step failed on
    # every install from 0.1.10 on, and Inno does not read the exit code of a
    # [Run] entry, so the user was never configured: no default input, no US
    # layout under Neokey, no startup entry. Inno verifies its own payload as
    # it extracts it; there is nothing left for the manifest to add here.
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
    # This runs in a window of its own that closes when it ends, so what it
    # says is kept for the window that asked; see Get-ElevatedLogPath.
    $exitCode = Invoke-ElevatedStep `
        -LogPath (Get-RegisterLogPath) `
        -Step { Invoke-DllRegistration } `
        -SuccessMessage "DLLs registered successfully in-place."
    exit $exitCode
}

if ($UnregisterElevatedOnly) {
    if (-not (Is-Elevated)) {
        Write-Error "UnregisterElevatedOnly requires Administrator privileges."
        exit 1
    }
    $exitCode = Invoke-ElevatedStep `
        -LogPath (Get-UnregisterLogPath) `
        -Step { Invoke-DllUnregistration } `
        -SuccessMessage "DLLs unregistered successfully."
    exit $exitCode
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

    # Reported from what is on the machine, not from the switches this run was
    # given, so a -Status run says what is on the machine either way.
    $enLangStatus = $langList | Where-Object { $_.LanguageTag -like "en*" }
    $englishListed = $null -ne $enLangStatus -and
        $enLangStatus.InputMethodTips -contains $tipStrEnglish
    $englishRequested = (Get-ItemProperty -Path "HKCU:\Software\Neokey" `
        -Name "RegisterEnglishProfile" -ErrorAction SilentlyContinue).RegisterEnglishProfile
    Write-Host "English copy registered: $([bool]$englishRequested)"
    Write-Host "English copy in User Language List: $englishListed"
    $defaultInputTip = Get-DefaultInputMethodTip
    if ([string]::IsNullOrWhiteSpace($defaultInputTip)) {
        Write-Host "Default Input Method TIP: <dynamic Windows selection>"
    } else {
        Write-Host "Default Input Method TIP: $defaultInputTip"
    }
    $isDefault = [string]::Equals($defaultInputTip, $tipStr, [System.StringComparison]::OrdinalIgnoreCase)
    Write-Host "Neokey is Default Input Method: $isDefault"

    # The two input lists disagreeing is what lets a sign-in come back on the
    # wrong IME, so report it rather than making it something only a registry
    # editor can see.
    $preloadOrder = @(Get-InputListOrder -Path "HKCU:\Keyboard Layout\Preload" | ForEach-Object { $_.Value })
    $ctfOrder = @(Get-InputListOrder -Path "HKCU:\Software\Microsoft\CTF\SortOrder\Language" | ForEach-Object { $_.Value })
    Write-Host "Win32 input order (Preload): $($preloadOrder -join ', ')"
    Write-Host "CTF input order (SortOrder): $($ctfOrder -join ', ')"
    $ordersAgree = ($preloadOrder -join ",") -eq ($ctfOrder -join ",")
    Write-Host "Input orders agree: $ordersAgree"
    if (-not $ordersAgree) {
        Write-Warning "The Win32 and CTF input lists disagree. Windows re-resolves them at sign-in and may activate another language's IME. Rerun with -SetDefault to fix."
    }

    # The two lists above are the ones this script writes. Neither of them is
    # what Windows asks first: it asks the language list, and it asks it twice -
    # once for the signed-in user and once for the screen you unlock at. A
    # machine can be correct in every line above and still come back on the US
    # keyboard every morning, because waking from sleep goes through that
    # screen and its choice carries into the session. Reported here because
    # nothing else on this machine says it out loud.
    $userLanguageOrder = @($langList | ForEach-Object { $_.LanguageTag })
    Write-Host "Language order (user): $($userLanguageOrder -join ', ')"
    $userResolves = Test-InputListResolvesToNeokey `
        -OverrideTip $defaultInputTip `
        -Languages $userLanguageOrder `
        -NeokeyTip $tipStr `
        -NeokeyLanguageTag "vi"
    Write-Host "User session starts on Neokey: $userResolves"
    if (-not $userResolves) {
        Write-Warning "Your own session does not start on Neokey. Rerun with -SetDefault to fix."
    }

    $signIn = Get-SignInScreenInputState
    if (-not $signIn.Present) {
        Write-Host "Sign-in screen input settings: <not readable>"
    } else {
        $signInOverride = if ([string]::IsNullOrWhiteSpace($signIn.OverrideTip)) {
            "<none>"
        } else {
            $signIn.OverrideTip
        }
        Write-Host "Sign-in screen override: $signInOverride"
        Write-Host "Sign-in screen language order: $($signIn.Languages -join ', ')"
        $signInResolves = Test-InputListResolvesToNeokey `
            -OverrideTip $signIn.OverrideTip `
            -Languages $signIn.Languages `
            -NeokeyTip $tipStr `
            -NeokeyLanguageTag "vi"
        Write-Host "Sign-in screen starts on Neokey: $signInResolves"
        if ($userResolves -and -not $signInResolves) {
            Write-Warning "The sign-in screen starts on another input method, and unlocking carries that choice into the session - so waking the machine from sleep comes back on the wrong keyboard even though your own settings are right. Fix it in Settings > Time & language > Language & region > Administrative language settings > Copy settings, ticking 'Welcome screen and system accounts'."
        }
    }

    # Which physical layout the Vietnamese language is bound to. Without the
    # substitute, Windows binds 0x042a to its own Vietnamese layout, where the
    # number row types tone marks - so digits stop reaching the IME as digits
    # and VNI cannot be typed at all. It is the one thing that decides this, and
    # a machine reporting it wrong is diagnosable from here rather than only by
    # asking the user to press keys and describe what came out.
    $substituteValue = (Get-ItemProperty `
        -Path "HKCU:\Keyboard Layout\Substitutes" `
        -Name "0000042a" `
        -ErrorAction SilentlyContinue)."0000042a"
    if ([string]::IsNullOrWhiteSpace($substituteValue)) {
        Write-Host "Vietnamese layout substitute: <not set>"
        Write-Warning "Without the substitute Windows may bind the Vietnamese physical layout, where the number row types tone marks and VNI cannot be typed. Rerun with -SetDefault to fix."
    } else {
        Write-Host "Vietnamese layout substitute: $substituteValue"
        if ($substituteValue -ne "00000409") {
            Write-Warning "The Vietnamese language is bound to layout $substituteValue rather than the US layout 00000409. Rerun with -SetDefault to fix."
        }
    }

    # Whether this install can put Vietnamese back the way it found it. Worth
    # asking for by name in a support thread: an uninstall that leaves a
    # Vietnamese keyboard behind is one that had nothing recorded to restore.
    $recordedTips = Get-NeokeySettingValue $script:preNeokeyTipsValue
    if ($null -eq $recordedTips) {
        Write-Host "Vietnamese state before Neokey: not recorded (installed by an earlier build)"
    } elseif ([string]::IsNullOrEmpty([string]$recordedTips)) {
        Write-Host "Vietnamese state before Neokey: no keyboards were filed under it"
    } else {
        Write-Host "Vietnamese state before Neokey: $recordedTips"
    }
    $addedByNeokey = [int](Get-NeokeySettingValue $script:preNeokeyLanguageAddedValue) -eq 1
    Write-Host "Vietnamese language entry added by Neokey: $addedByNeokey"

    # What Neokey has on this machine outside its own folder. On a working
    # install this reads as a list of what is in use; run after an uninstall,
    # the same list is what the uninstall failed to remove.
    Write-Host "Machine footprint:"
    foreach ($target in Get-NeokeyResidueTargets) {
        $present = Test-NeokeyResidueTargetPresent $target
        $name = if ($target.Kind -eq "RegistryValue") {
            "$($target.Path)\$($target.Name)"
        } else {
            $target.Path
        }
        Write-Host ("  [{0}] {1} ({2})" -f $(if ($present) { "x" } else { " " }), $name, $target.Label)
    }

    $autoStartValue = (Get-ItemProperty `
        -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
        -Name "Neokey" `
        -ErrorAction SilentlyContinue).Neokey
    if ([string]::IsNullOrWhiteSpace($autoStartValue)) {
        Write-Host "Starts with Windows: False"
    } else {
        Write-Host "Starts with Windows: True ($autoStartValue)"
    }
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
        $unregisterLog = Get-UnregisterLogPath
        Remove-Item -LiteralPath $unregisterLog -Force -ErrorAction SilentlyContinue
        $elevatedArguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -UnregisterElevatedOnly"
        $process = Start-Process `
            -FilePath "powershell.exe" `
            -ArgumentList $elevatedArguments `
            -Verb RunAs `
            -PassThru `
            -Wait
        if ($process.ExitCode -eq 0) {
            Remove-Item -LiteralPath $unregisterLog -Force -ErrorAction SilentlyContinue
            Write-Host "DLLs unregistered successfully."
        } else {
            # The elevated window has closed by now, and with it everything it
            # said. Its record is the only way to tell "the DLL could not be
            # loaded" from "Windows refused" in a report.
            Write-ElevatedLog $unregisterLog
            throw "Failed to unregister DLLs. Exit code: $($process.ExitCode)"
        }
    } else {
        Invoke-DllUnregistration
        Write-Host "DLLs unregistered successfully."
    }

    # 2. Remove TIP/autostart only after system unregistration succeeded.
    Unconfigure-NeokeyCurrentUser
} else {
    # 0. Refuse a folder the DLLs cannot keep being loaded from, before
    # anything is written anywhere.
    Assert-InstallLocation

    # 1. Register DLL COM and TSF system-wide (requires elevation)
    if (-not (Is-Elevated)) {
        Write-Host "Requesting Administrator privileges to register DLL..."
        # Removed first so a step that dies before it can write its own record
        # - a script the elevated account cannot reach - does not have an old
        # one shown in its place.
        $registerLog = Get-RegisterLogPath
        Remove-Item -LiteralPath $registerLog -Force -ErrorAction SilentlyContinue
        $args = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -RegisterElevatedOnly"
        if ($RequireManifest) {
            $args += " -RequireManifest"
        }
        # The elevated run is the one that calls regsvr32, and the DLL decides
        # there whether to file a second profile. A switch that stops at this
        # boundary would be accepted, reported, and silently do nothing.
        if ($NoEnglishProfile) {
            $args += " -NoEnglishProfile"
        }

        $process = Start-Process powershell.exe -ArgumentList $args -Verb RunAs -PassThru -Wait
        if ($process.ExitCode -eq 0) {
            Write-Host "DLLs registered successfully in-place."
        } else {
            Write-ElevatedLog $registerLog
            throw "Failed to register DLLs. Exit code: $($process.ExitCode)"
        }
    } else {
        Invoke-DllRegistration
        Write-Host "DLLs registered successfully in-place."
    }

    # 2. Configure the current desktop user after system registration.
    Configure-NeokeyCurrentUser
    if ($SetDefault) {
        # 3. What install.bat promises: the tray icon now, not at the next
        # sign-in. The installer does this itself and passes
        # -ConfigureCurrentUserOnly, so it never gets here.
        Clear-DownloadedMark
        Start-NeokeyTrayApp
    } else {
        Write-Host "Tip: rerun with -SetDefault to make Neokey the default input method after sign-in or reboot."
    }
}
