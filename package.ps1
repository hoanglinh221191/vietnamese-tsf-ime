param(
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$Zip,
    [string]$DistRoot = "$PSScriptRoot\dist"
)

$ErrorActionPreference = "Stop"

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

function Write-HashManifest {
    param(
        [string]$Directory,
        [string[]]$FileNames,
        [string]$Version
    )

    $manifestFiles = @()
    foreach ($fileName in $FileNames) {
        $path = Join-Path $Directory $fileName
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Cannot hash missing release file: $path"
        }

        $item = Get-Item -LiteralPath $path
        $hash = Get-FileHash -LiteralPath $path -Algorithm SHA256
        $manifestFiles += [ordered]@{
            path = $fileName
            sha256 = $hash.Hash.ToLowerInvariant()
            bytes = $item.Length
        }
    }

    $manifest = [ordered]@{
        schema = 1
        product = "Neokey"
        version = $Version
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
$packageDir = Join-Path $DistRoot "Neokey"
$zipPath = Join-Path $DistRoot "Neokey-portable.zip"
$zipStagingRoot = Join-Path $DistRoot "Neokey_zip_staging"

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
        $testExe = Join-Path $buildDir "core_tests.exe"
        if (-not (Test-Path -LiteralPath $testExe -PathType Leaf)) {
            throw "Test executable missing: $testExe"
        }
        & $testExe
        if ($LASTEXITCODE -ne 0) {
            throw "core_tests.exe failed with exit code $LASTEXITCODE"
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
    Copy-RequiredFile (Join-Path $repoRoot "LICENSE") $stagingDir
    Copy-RequiredFile (Join-Path $repoRoot "VERSION") $stagingDir

    $shorthandSource = Join-Path $buildDir "neokey_shorthand.txt"
    if (-not (Test-Path -LiteralPath $shorthandSource -PathType Leaf)) {
        $shorthandSource = Join-Path $repoRoot "neokey_shorthand.txt"
    }
    if (Test-Path -LiteralPath $shorthandSource -PathType Leaf) {
        Copy-Item -LiteralPath $shorthandSource -Destination $stagingDir -Force
    } else {
        Set-Content -LiteralPath (Join-Path $stagingDir "neokey_shorthand.txt") -Value "# shortcut=expanded text" -Encoding UTF8
    }

    Write-HashManifest $stagingDir @("neokey.dll", "neokey32.dll", "neokey_config.exe") $releaseVersion
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

Write-Host ""
Write-Host "Portable release ready: Neokey $releaseVersion"
Write-Host "  $packageDir"
if ($Zip) {
    Write-Host "  $zipPath"
}
