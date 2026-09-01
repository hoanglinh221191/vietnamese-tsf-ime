param(
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$Zip,
    [switch]$Installer,
    [switch]$Arm64Preview,
    [string]$InnoCompiler,
    [string]$DistRoot = "$PSScriptRoot\dist"
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

function Get-PeMachine {
    param([string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $reader = New-Object System.IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Not a PE file: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt ($stream.Length - 6)) {
            throw "Invalid PE header offset: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Invalid PE signature: $Path"
        }
        return $reader.ReadUInt16()
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Run-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "==> $Name"
    & $Action
}

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required file missing: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Read-ReleaseVersion {
    param([string]$RepoRoot)

    $versionPath = Join-Path $RepoRoot "VERSION"
    if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
        throw "VERSION file missing: $versionPath"
    }

    $version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
    if ([string]::IsNullOrWhiteSpace($version)) {
        throw "VERSION file is empty: $versionPath"
    }
    if ($version -notmatch '^\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$') {
        throw "VERSION must be semantic version style, for example 0.1.0. Got: $version"
    }

    return $version
}

function Resolve-InnoCompiler {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "Inno Setup compiler not found: $RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $fromPath = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($null -ne $fromPath) {
        return $fromPath.Source
    }

    $uninstallRoots = @(
        "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*"
    )
    $registeredInstallations = Get-ItemProperty $uninstallRoots -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like "Inno Setup*" -and -not [string]::IsNullOrWhiteSpace($_.InstallLocation) } |
        Sort-Object DisplayVersion -Descending
    foreach ($installation in $registeredInstallations) {
        $candidate = Join-Path $installation.InstallLocation "ISCC.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "Inno Setup compiler was not found. Install JRSoftware.InnoSetup or pass -InnoCompiler <path>."
}

function Write-HashManifest {
    param(
        [string]$Directory,
        [string[]]$FileNames,
        [string]$Version,
        [string]$Product = "Neokey",
        [string]$Architecture = "windows-x64-x86"
    )

    $manifestFiles = @()
    foreach ($fileName in $FileNames) {
        $path = Join-Path $Directory $fileName
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Cannot hash missing release file: $path"
        }

        $item = Get-Item -LiteralPath $path
        $hash = Get-Sha256Hex -Path $path
        $manifestFiles += [ordered]@{
            path = $fileName
            sha256 = $hash
            bytes = $item.Length
        }
    }

    $manifest = [ordered]@{
        schema = 1
        product = $Product
        version = $Version
        architecture = $Architecture
        algorithm = "SHA256"
        generated_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        files = $manifestFiles
    }

    $manifestPath = Join-Path $Directory "neokey_manifest.json"
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

$repoRoot = $PSScriptRoot
$releaseVersion = Read-ReleaseVersion $repoRoot
$buildDir = Join-Path $repoRoot "build\package"
$arm64BuildDir = Join-Path $repoRoot "build\package-arm64"
$packageDir = Join-Path $DistRoot "Neokey"
$zipPath = Join-Path $DistRoot "Neokey-portable.zip"
$zipStagingRoot = Join-Path $DistRoot "Neokey_zip_staging"
$installerPath = Join-Path $DistRoot "NeokeySetup.exe"
$arm64ZipPath = Join-Path $DistRoot "Neokey-arm64-preview.zip"
$arm64ZipStagingRoot = Join-Path $DistRoot "Neokey_arm64_zip_staging"

Run-Step "Validate registration script safety" {
    & (Join-Path $repoRoot "tests\register_script_tests.ps1")
}

if (-not $SkipBuild) {
    Run-Step "Build x64/x86 MSVC artifacts" {
        $previousOutDir = $env:OUT_DIR
        $env:OUT_DIR = "build\package"
        try {
            & (Join-Path $repoRoot "build.bat")
            if ($LASTEXITCODE -ne 0) {
                throw "build.bat failed with exit code $LASTEXITCODE"
            }
        } finally {
            if ($null -eq $previousOutDir) {
                Remove-Item Env:\OUT_DIR -ErrorAction SilentlyContinue
            } else {
                $env:OUT_DIR = $previousOutDir
            }
        }
    }
}

if (-not $SkipTests) {
    Run-Step "Run core regression tests" {
        foreach ($testName in @("core_tests.exe", "core_tests32.exe")) {
            $testExe = Join-Path $buildDir $testName
            if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
                throw "Test executable missing: $testExe"
            }
            & $testExe
            if ($LASTEXITCODE -ne 0) {
                throw "$testName failed with exit code $LASTEXITCODE"
            }
        }
    }
}

$stagingDir = Join-Path $DistRoot "Neokey_staging"

Run-Step "Create clean staging folder" {
    if (Test-Path -LiteralPath $stagingDir) {
        Remove-Item -LiteralPath $stagingDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

    Copy-RequiredFile (Join-Path $buildDir "neokey.dll") $stagingDir
    Copy-RequiredFile (Join-Path $buildDir "neokey32.dll") $stagingDir
    Copy-RequiredFile (Join-Path $buildDir "neokey_config.exe") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "register.ps1") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "install.bat") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "uninstall.bat") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "PORTABLE_RELEASE.md") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "PORTABLE_README.md") (Join-Path $stagingDir "README.md")
    Copy-RequiredFile (Join-Path $repoRoot "PORTABLE_README.vi.md") (Join-Path $stagingDir "README.vi.md")
    Copy-RequiredFile (Join-Path $repoRoot "LICENSE") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "VERSION") $stagingDir

    $shorthandSource = Join-Path $buildDir "neokey_shorthand.txt"
    if (-not (Test-Path -LiteralPath $shorthandSource -PathType Leaf)) {
        $shorthandSource = Join-Path $repoRoot "neokey_shorthand.txt"
    }
    if (Test-Path -LiteralPath $shorthandSource -PathType Leaf) {
        Copy-Item -LiteralPath $shorthandSource -Destination $stagingDir -Force
    } else {
        Set-Content -LiteralPath (Join-Path $stagingDir "neokey_shorthand.txt") -Value @(
            "# shortcut=expanded text"
            "# Dynamic variables: DATE, TIME, WEEKDAY, UUID, NEWLINE, TAB, CLIPBOARD, SELECTION, CURSOR"
            "# dday=Today is {{DD/MM/YYYY}}"
            "# xhello=Hello {{CLIPBOARD}},"
            "# stamp={{DATE}} {{TIME}} - {{UUID}}"
            "# wrap=[{{SELECTION}}]{{CURSOR}}"
            "# clip={{CLIPBOARD|TRIM}}"
        ) -Encoding UTF8
    }

    Write-HashManifest $stagingDir @(
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
    ) $releaseVersion
}

if ($Zip) {
    Run-Step "Create zip archive" {
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        if (Test-Path -LiteralPath $zipStagingRoot) {
            Remove-Item -LiteralPath $zipStagingRoot -Recurse -Force
        }

        try {
            $zipPackageDir = Join-Path $zipStagingRoot "Neokey"
            New-Item -ItemType Directory -Path $zipPackageDir -Force | Out-Null
            Get-ChildItem -Path $stagingDir -File | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $zipPackageDir -Force
            }
            Compress-Archive -Path $zipPackageDir -DestinationPath $zipPath -Force
        } finally {
            if (Test-Path -LiteralPath $zipStagingRoot) {
                Remove-Item -LiteralPath $zipStagingRoot -Recurse -Force
            }
        }
    }
}

Run-Step "Update active package folder" {
    if (-not (Test-Path -LiteralPath $packageDir)) {
        New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    }

    # Clean up non-locked files first
    Get-ChildItem -Path $packageDir -File | ForEach-Object {
        $file = $_.FullName
        if ($_.Name -notmatch '\.old$') {
            try {
                Remove-Item -LiteralPath $file -Force
            } catch {
                # Locked. Rename to a unique name to allow overwrite
                $uniqueId = [Guid]::NewGuid().Guid.SubString(0,8)
                try {
                    Rename-Item -LiteralPath $file -NewName "$($_.Name).$uniqueId.old" -Force
                } catch {
                    Write-Warning "Failed to rename locked file: $file. $_"
                }
            }
        }
    }

    # Copy files from staging to packageDir
    Get-ChildItem -Path $stagingDir -File | ForEach-Object {
        $destFile = Join-Path $packageDir $_.Name
        Copy-Item -LiteralPath $_.FullName -Destination $destFile -Force
    }

    # Try to clean up any remaining .old files
    Get-ChildItem -Path $packageDir -File | Where-Object { $_.Name -match '\.old$' } | ForEach-Object {
        try { Remove-Item -LiteralPath $_.FullName -Force } catch {}
    }

    # Remove staging folder
    if (Test-Path -LiteralPath $stagingDir) {
        Remove-Item -LiteralPath $stagingDir -Recurse -Force
    }
}

if ($Arm64Preview) {
    Run-Step "Build ARM64 preview artifacts" {
        $previousOutDir = $env:OUT_DIR
        $env:OUT_DIR = "build\package-arm64"
        try {
            & (Join-Path $repoRoot "build.bat") arm64
            if ($LASTEXITCODE -ne 0) {
                throw "build.bat arm64 failed with exit code $LASTEXITCODE"
            }
        } finally {
            if ($null -eq $previousOutDir) {
                Remove-Item Env:\OUT_DIR -ErrorAction SilentlyContinue
            } else {
                $env:OUT_DIR = $previousOutDir
            }
        }
    }

    Run-Step "Verify ARM64 PE machine type" {
        foreach ($fileName in @(
            "neokey_arm64.dll",
            "neokey_config_arm64.exe",
            "core_tests_arm64.exe"
        )) {
            $path = Join-Path $arm64BuildDir $fileName
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "ARM64 preview artifact missing: $path"
            }
            $machine = Get-PeMachine $path
            if ($machine -ne 0xAA64) {
                throw ("ARM64 PE machine mismatch for {0}. Expected 0xAA64, got 0x{1:X4}." -f $fileName, $machine)
            }
            Write-Host "$fileName PE machine: 0xAA64"
        }
    }

    Run-Step "Create ARM64 preview archive" {
        if (Test-Path -LiteralPath $arm64ZipPath) {
            Remove-Item -LiteralPath $arm64ZipPath -Force
        }
        if (Test-Path -LiteralPath $arm64ZipStagingRoot) {
            Remove-Item -LiteralPath $arm64ZipStagingRoot -Recurse -Force
        }

        try {
            $arm64PackageDir = Join-Path $arm64ZipStagingRoot "Neokey-arm64-preview"
            New-Item -ItemType Directory -Path $arm64PackageDir -Force | Out-Null
            Copy-RequiredFile (Join-Path $arm64BuildDir "neokey_arm64.dll") $arm64PackageDir
            Copy-RequiredFile `
                (Join-Path $arm64BuildDir "neokey_config_arm64.exe") `
                (Join-Path $arm64PackageDir "neokey_config.exe")
            Copy-RequiredFile (Join-Path $arm64BuildDir "core_tests_arm64.exe") $arm64PackageDir
            Copy-RequiredFile (Join-Path $repoRoot "ARM64_PREVIEW.md") $arm64PackageDir
            Copy-RequiredFile (Join-Path $repoRoot "LICENSE") $arm64PackageDir
            Copy-RequiredFile (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") $arm64PackageDir
            Copy-RequiredFile (Join-Path $repoRoot "VERSION") $arm64PackageDir

            Write-HashManifest $arm64PackageDir @(
                "neokey_arm64.dll",
                "neokey_config.exe",
                "core_tests_arm64.exe",
                "ARM64_PREVIEW.md",
                "LICENSE",
                "THIRD_PARTY_NOTICES.md",
                "VERSION"
            ) $releaseVersion "Neokey ARM64 Preview" "windows-arm64-preview"

            Compress-Archive -Path $arm64PackageDir -DestinationPath $arm64ZipPath -Force
        } finally {
            if (Test-Path -LiteralPath $arm64ZipStagingRoot) {
                Remove-Item -LiteralPath $arm64ZipStagingRoot -Recurse -Force
            }
        }
    }
}

if ($Installer) {
    Run-Step "Create Windows installer" {
        $compiler = Resolve-InnoCompiler $InnoCompiler
        $setupScript = Join-Path $repoRoot "setup.iss"
        if (-not (Test-Path -LiteralPath $setupScript -PathType Leaf)) {
            throw "Inno Setup script missing: $setupScript"
        }
        if (Test-Path -LiteralPath $installerPath -PathType Leaf) {
            Remove-Item -LiteralPath $installerPath -Force
        }

        $versionArg = "/DMyAppVersion=$releaseVersion"
        $packageArg = '/DMyPackageDir="' + $packageDir + '"'
        $outputArg = '/DMyOutputDir="' + $DistRoot + '"'
        & $compiler $versionArg $packageArg $outputArg $setupScript
        if ($LASTEXITCODE -ne 0) {
            throw "ISCC.exe failed with exit code $LASTEXITCODE"
        }
        if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
            throw "Installer output missing: $installerPath"
        }

        $productVersion = (Get-Item -LiteralPath $installerPath).VersionInfo.ProductVersion.Trim()
        if ($productVersion -ne $releaseVersion) {
            throw "Installer version mismatch. Expected $releaseVersion, got $productVersion."
        }
    }
}

Write-Host ""
Write-Host "Portable release ready: Neokey $releaseVersion"
Write-Host "  $packageDir"
if ($Zip) {
    Write-Host "  $zipPath"
}
if ($Installer) {
    Write-Host "  $installerPath"
}
if ($Arm64Preview) {
    Write-Host "  $arm64ZipPath"
}
