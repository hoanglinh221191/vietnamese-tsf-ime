param(
    [string]$RegisterScript,
    [string]$PackageScript
)

$ErrorActionPreference = "Stop"
$passed = 0

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw "FAILED: $Message"
    }
    $script:passed++
}

if ([string]::IsNullOrWhiteSpace($RegisterScript)) {
    $RegisterScript = Join-Path (Split-Path $PSScriptRoot -Parent) "register.ps1"
}
$RegisterScript = (Resolve-Path -LiteralPath $RegisterScript).Path
$source = Get-Content -LiteralPath $RegisterScript -Raw
if ([string]::IsNullOrWhiteSpace($PackageScript)) {
    $PackageScript = Join-Path (Split-Path $PSScriptRoot -Parent) "package.ps1"
}
$PackageScript = (Resolve-Path -LiteralPath $PackageScript).Path
$packageSource = Get-Content -LiteralPath $PackageScript -Raw

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $RegisterScript,
    [ref]$tokens,
    [ref]$parseErrors
)
Assert-True ($parseErrors.Count -eq 0) "register.ps1 must parse without errors"

$manifestPayloadFiles = @(
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
foreach ($payloadFile in $manifestPayloadFiles) {
    Assert-True ($packageSource.Contains('"' + $payloadFile + '"')) `
        "package manifest must hash $payloadFile"
    Assert-True ($source.Contains('"' + $payloadFile + '"')) `
        "registration verifier must require $payloadFile"
}
Assert-True ($source.Contains("Duplicate path in hash manifest")) `
    "manifest verifier must reject duplicate paths"

$dangerousCommandStrings = @($ast.FindAll({
    param($node)
    $isString = $node -is [System.Management.Automation.Language.StringConstantExpressionAst] -or
        $node -is [System.Management.Automation.Language.ExpandableStringExpressionAst]
    return $isString -and $node.Value -match '(?i)(^|\s)-Command($|\s)'
}, $true))
Assert-True ($dangerousCommandStrings.Count -eq 0) `
    "elevation must use -File instead of constructing powershell -Command source"

$unregisterBranchStart = $source.IndexOf(
    'if ($Unregister) {',
    [System.StringComparison]::Ordinal
)
Assert-True ($unregisterBranchStart -ge 0) "top-level unregister branch must exist"
$unregisterBranch = $source.Substring($unregisterBranchStart)
$systemUnregisterPosition = $unregisterBranch.IndexOf(
    "Invoke-DllUnregistration",
    [System.StringComparison]::Ordinal
)
$userUnconfigurePosition = $unregisterBranch.IndexOf(
    "Unconfigure-NeokeyCurrentUser",
    [System.StringComparison]::Ordinal
)
Assert-True ($systemUnregisterPosition -ge 0) "unregister branch must invoke checked DLL unregistration"
Assert-True ($userUnconfigurePosition -gt $systemUnregisterPosition) `
    "user configuration must remain intact until DLL unregistration succeeds"

$regsvrFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Invoke-Regsvr32"
}, $true))
Assert-True ($regsvrFunction.Count -eq 1) "Invoke-Regsvr32 helper must exist exactly once"

# Load only the pure helper definition. Do not execute register.ps1 or touch the
# registry while testing its argument and exit-code handling.
. ([scriptblock]::Create($regsvrFunction[0].Extent.Text))

$script:mockExitCode = 0
$script:capturedStartProcess = $null
function Start-Process {
    [CmdletBinding()]
    param(
        [string]$FilePath,
        [object[]]$ArgumentList,
        [switch]$PassThru,
        [switch]$Wait
    )

    $script:capturedStartProcess = [pscustomobject]@{
        FilePath = $FilePath
        ArgumentList = @($ArgumentList)
        PassThru = [bool]$PassThru
        Wait = [bool]$Wait
    }
    return [pscustomobject]@{ ExitCode = $script:mockExitCode }
}

$testDllPath = "C:\Program Files\Neokey O'Brien & Test\neokey.dll"
Invoke-Regsvr32 `
    -ExecutablePath "regsvr32.exe" `
    -DllFilePath $testDllPath `
    -Operation Unregister `
    -Architecture "test" | Out-Null

Assert-True ($capturedStartProcess.FilePath -eq "regsvr32.exe") `
    "regsvr32 executable must be passed through FilePath"
Assert-True ($capturedStartProcess.PassThru -and $capturedStartProcess.Wait) `
    "regsvr32 must be awaited so its exit code is authoritative"
$expectedArguments = @("/u", "/s", ('"' + $testDllPath + '"'))
Assert-True (
    [string]::Join("|", $capturedStartProcess.ArgumentList) -eq
        [string]::Join("|", $expectedArguments)
) "unregister arguments must preserve a metacharacter-containing DLL path as data"

$script:mockExitCode = 5
$reportedFailure = $false
try {
    Invoke-Regsvr32 `
        -ExecutablePath "regsvr32.exe" `
        -DllFilePath $testDllPath `
        -Operation Register `
        -Architecture "test" | Out-Null
} catch {
    $reportedFailure = $_.Exception.Message -like "*exit code 5*"
}
Assert-True $reportedFailure "non-zero regsvr32 exit codes must fail the operation"

Write-Host "register_script_tests: $passed passed, 0 failed"
