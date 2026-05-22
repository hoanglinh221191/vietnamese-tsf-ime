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

$repoRoot = $PSScriptRoot
$buildDir = Join-Path $repoRoot "build\package"
$packageDir = Join-Path $DistRoot "Neokey"
$zipPath = Join-Path $DistRoot "Neokey-portable.zip"

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

Run-Step "Create clean portable folder" {
    if (Test-Path -LiteralPath $packageDir) {
        Remove-Item -LiteralPath $packageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

    Copy-RequiredFile (Join-Path $buildDir "neokey.dll") $packageDir
    Copy-RequiredFile (Join-Path $buildDir "neokey32.dll") $packageDir
    Copy-RequiredFile (Join-Path $buildDir "neokey_config.exe") $packageDir
    Copy-RequiredFile (Join-Path $repoRoot "register.ps1") $packageDir
    Copy-RequiredFile (Join-Path $repoRoot "install.bat") $packageDir
    Copy-RequiredFile (Join-Path $repoRoot "uninstall.bat") $packageDir
    Copy-RequiredFile (Join-Path $repoRoot "PORTABLE_RELEASE.md") $packageDir

    $shorthandSource = Join-Path $buildDir "neokey_shorthand.txt"
    if (-not (Test-Path -LiteralPath $shorthandSource -PathType Leaf)) {
        $shorthandSource = Join-Path $repoRoot "neokey_shorthand.txt"
    }
    if (Test-Path -LiteralPath $shorthandSource -PathType Leaf) {
        Copy-Item -LiteralPath $shorthandSource -Destination $packageDir -Force
    } else {
        Set-Content -LiteralPath (Join-Path $packageDir "neokey_shorthand.txt") -Value "# shortcut=expanded text" -Encoding UTF8
    }
}

if ($Zip) {
    Run-Step "Create zip archive" {
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        Compress-Archive -Path $packageDir -DestinationPath $zipPath -Force
    }
}

Write-Host ""
Write-Host "Portable release ready:"
Write-Host "  $packageDir"
if ($Zip) {
    Write-Host "  $zipPath"
}
