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

$packageTokens = $null
$packageParseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile(
    $PackageScript,
    [ref]$packageTokens,
    [ref]$packageParseErrors
) | Out-Null
Assert-True ($packageParseErrors.Count -eq 0) "package.ps1 must parse without errors"

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

$arm64PreviewPayloadFiles = @(
    "neokey_arm64.dll",
    "neokey_config_arm64.exe",
    "core_tests_arm64.exe",
    "ARM64_PREVIEW.md"
)
foreach ($payloadFile in $arm64PreviewPayloadFiles) {
    Assert-True ($packageSource.Contains('"' + $payloadFile + '"')) `
        "ARM64 preview manifest must hash $payloadFile"
}
Assert-True ($packageSource.Contains("Get-PeMachine")) `
    "ARM64 preview packaging must inspect the PE machine type"
Assert-True ($packageSource.Contains("0xAA64")) `
    "ARM64 preview packaging must require the AA64 PE machine"
Assert-True ($packageSource.Contains("windows-arm64-preview")) `
    "ARM64 preview manifest must be labeled separately"
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

$activateFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Activate-NeokeyInCurrentSession"
}, $true))
Assert-True ($activateFunction.Count -eq 1) "Activate-NeokeyInCurrentSession must exist exactly once"
Assert-True (-not $activateFunction[0].Extent.Text.Contains("]::SPIF_UPDATEINIFILE")) `
    "session activation must not pass SPIF_UPDATEINIFILE, which rewrites Preload without CTF\SortOrder and desynchronises the two"

$inputOrderFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Set-NeokeyInputOrder"
}, $true))
Assert-True ($inputOrderFunction.Count -eq 1) "Set-NeokeyInputOrder must exist exactly once"
$inputOrderText = $inputOrderFunction[0].Extent.Text
Assert-True ($inputOrderText.Contains('HKCU:\Keyboard Layout\Preload')) `
    "input order must cover the legacy Win32 list"
Assert-True ($inputOrderText.Contains('HKCU:\Software\Microsoft\CTF\SortOrder\Language')) `
    "input order must cover CTF's copy of the same list, or the two drift apart"

# Reordering the input list must never reorder the preferred languages list.
# Those are what Microsoft Store apps - Notepad, Clock, Calculator - resolve
# their interface language against, and an earlier version of this script turned
# them Vietnamese for everyone who installed Neokey. The comments explaining that
# name the very things being banned, so match on code with the comments removed.
$codeOnly = -join (
    $tokens |
        Where-Object { $_.Kind -ne [System.Management.Automation.Language.TokenKind]::Comment } |
        ForEach-Object { $_.Text }
)
Assert-True (-not $codeOnly.Contains("Set-WinUILanguageOverride")) `
    "registration must not override the Windows UI language"
Assert-True (-not $codeOnly.Contains("PreferredUILanguages")) `
    "registration must not write PreferredUILanguages"
Assert-True (-not $codeOnly.Contains('International\User Profile')) `
    "registration must not write the preferred languages list directly"

# The substitute is what keeps the number row typing digits rather than tone
# marks, so a machine that lost it cannot type VNI at all. -Status has to say so
# on its own: the alternative is asking a user at the far end of a support
# thread to press keys and describe what appeared.
Assert-True ($codeOnly.Contains('Keyboard Layout\Substitutes')) `
    "-Status must read the Vietnamese layout substitute"
Assert-True ($codeOnly.Contains("Vietnamese layout substitute")) `
    "-Status must report the substitute in its output"
Assert-True ($codeOnly.Contains("00000409")) `
    "-Status must know which layout the substitute is supposed to name"

# The English copy is an addition, never a replacement. If registration ever
# stops filing the Vietnamese profile, every existing user loses the input they
# have been using, so the switch must not be able to reach that decision.
Assert-True ($codeOnly.Contains('$tipStrEnglish="0409:')) `
    "the English copy must be a second TIP rather than a different one"
Assert-True ($codeOnly.Contains('$tipStr="042A:')) `
    "the Vietnamese TIP must stay a constant, not follow a switch"
Assert-True ($codeOnly.Contains("RegisterEnglishProfile")) `
    "registration must record the English copy choice for the DLL to read"

$addFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Add-NeokeyToUserLanguageList"
}, $true))
Assert-True ($addFunction.Count -eq 1) "Add-NeokeyToUserLanguageList is defined once"
$addText = $addFunction[0].Extent.Text
Assert-True ($addText.Contains('$registerEnglish')) `
    "adding to the language list must honour the English switch"
Assert-True ($addText.Contains('$tipStrEnglish')) `
    "adding to the language list must know the English TIP"
# Pruning other input methods is right under Vietnamese, where the entry being
# removed is a layout nobody asked for, and wrong under English, where it is the
# US keyboard - the only way back if this service ever fails to load.
$prunePos = $addText.IndexOf("Removed redundant built-in Vietnamese keyboard")
$englishPos = $addText.IndexOf('if ($registerEnglish)')
Assert-True ($prunePos -ge 0 -and $englishPos -ge 0 -and $prunePos -lt $englishPos) `
    "the English branch must come after the Vietnamese pruning, never inside it"

# regsvr32 runs in the elevated re-launch, and that is where the DLL reads the
# choice. A switch that stopped at the elevation boundary would be accepted,
# reported as applied, and do nothing at all.
Assert-True ($source.Contains('$args += " -NoEnglishProfile"')) `
    "the English switch must be forwarded across elevation"

# Both copies are registered by default, but Vietnamese stays the input the
# machine comes up in: it leads the input order and holds the default-method
# override. If the English copy ever took either of those, every user would find
# their tray saying ENG after an ordinary install.
Assert-True ($codeOnly.Contains('$registerEnglish=-not$NoEnglishProfile')) `
    "the English copy must be on unless the switch turns it off"
$defaultTipUses = ([regex]::Matches($source, [regex]::Escape('$tipStr'))).Count
$englishTipUses = ([regex]::Matches($source, [regex]::Escape('$tipStrEnglish'))).Count
Assert-True ($defaultTipUses -gt $englishTipUses) `
    "the Vietnamese TIP must remain the one the rest of registration is built on"
$orderText = $inputOrderFunction[0].Extent.Text
Assert-True (-not $orderText.Contains('$tipStrEnglish')) `
    "the English copy must never reach the input order, which decides the default"
# Checked at the one place that sets it, rather than across whole functions:
# the function that removes the override legitimately mentions both copies,
# because uninstalling has to take both off.
$overrideWrites = @([regex]::Matches(
    $source, 'Set-WinDefaultInputMethodOverride\s+-InputTip\s+(\$\w+)'))
Assert-True ($overrideWrites.Count -ge 1) `
    "registration must set the default input method override"
foreach ($write in $overrideWrites) {
    Assert-True ($write.Groups[1].Value -eq '$tipStr') `
        "the default input method override must be the Vietnamese copy"
}


$configureFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Configure-NeokeyCurrentUser"
}, $true))
Assert-True ($configureFunction.Count -eq 1) "Configure-NeokeyCurrentUser must exist exactly once"
Assert-True ($configureFunction[0].Extent.Text.Contains("Set-NeokeyAutoStart")) `
    "-SetDefault must create the startup entry that uninstall removes"

$autoStartFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Set-NeokeyAutoStart"
}, $true))
Assert-True ($autoStartFunction.Count -eq 1) "Set-NeokeyAutoStart must exist exactly once"
$autoStartText = $autoStartFunction[0].Extent.Text
Assert-True ($autoStartText.Contains('HKCU:\Software\Microsoft\Windows\CurrentVersion\Run')) `
    "startup entry must go under the per-user Run key that uninstall clears"
Assert-True ($autoStartText.Contains("-silent")) `
    "startup entry must launch the config app to the tray without opening its window"

# These two helpers only mean anything against a registry key, so exercise them
# against a throwaway one of our own. Nothing Windows owns is touched, and the
# key is removed again even if an assertion throws.
$orderHelpers = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -in @("Get-InputListOrder", "Set-InputListOrder")
}, $true))
Assert-True ($orderHelpers.Count -eq 2) "both input order helpers must exist"
foreach ($helper in $orderHelpers) {
    . ([scriptblock]::Create($helper.Extent.Text))
}

$scratchRoot = "HKCU:\Software\NeokeyRegisterScriptTests\$([guid]::NewGuid().ToString('n'))"
try {
    $orderCases = @(
        @{
            Name = "reorders the Win32 list"
            Style = "Preload"
            Seed = [ordered]@{ "1" = "00000409"; "2" = "0000042a"; "3" = "00000804" }
            ExpectedNames = "1,2,3"
            ExpectedValues = "0000042a,00000409,00000804"
        },
        @{
            Name = "reorders CTF's list with its own 8 hex digit names"
            Style = "Ctf"
            Seed = [ordered]@{ "00000000" = "00000409"; "00000001" = "0000042a"; "00000002" = "00000804" }
            ExpectedNames = "00000000,00000001,00000002"
            ExpectedValues = "0000042a,00000409,00000804"
        },
        @{
            Name = "drops a duplicate and clears the trailing entry"
            Style = "Preload"
            Seed = [ordered]@{ "1" = "0000042a"; "2" = "0000042a"; "3" = "00000409" }
            ExpectedNames = "1,2"
            ExpectedValues = "0000042a,00000409"
        },
        @{
            Name = "treats an upper case layout id as the same layout"
            Style = "Preload"
            Seed = [ordered]@{ "1" = "00000409"; "2" = "0000042A" }
            ExpectedNames = "1,2"
            ExpectedValues = "0000042a,00000409"
        }
    )

    $caseIndex = 0
    foreach ($case in $orderCases) {
        $casePath = Join-Path $scratchRoot "case$caseIndex"
        $caseIndex++
        New-Item -Path $casePath -Force | Out-Null
        foreach ($seedName in $case.Seed.Keys) {
            Set-ItemProperty -LiteralPath $casePath -Name $seedName -Value $case.Seed[$seedName] -Force
        }

        $currentOrder = @(Get-InputListOrder -Path $casePath)
        $desiredOrder = @("0000042a")
        foreach ($entry in $currentOrder) {
            if ($entry.Value -ne "0000042a" -and $entry.Value -notin $desiredOrder) {
                $desiredOrder += $entry.Value
            }
        }
        Set-InputListOrder -Path $casePath -Values $desiredOrder -Style $case.Style

        $result = @(Get-InputListOrder -Path $casePath)
        Assert-True ((($result | ForEach-Object { $_.Value }) -join ",") -eq $case.ExpectedValues) `
            "input order $($case.Name)"
        Assert-True ((($result | ForEach-Object { $_.Name }) -join ",") -eq $case.ExpectedNames) `
            "input order $($case.Name): entry names"
    }

    Assert-True ((@(Get-InputListOrder -Path (Join-Path $scratchRoot "absent"))).Count -eq 0) `
        "a missing input list must read as empty rather than throw"
} finally {
    Remove-Item -LiteralPath "HKCU:\Software\NeokeyRegisterScriptTests" -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "register_script_tests: $passed passed, 0 failed"
