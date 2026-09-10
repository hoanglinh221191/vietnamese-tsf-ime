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
$setupSource = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) "setup.iss") -Raw

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
Assert-True ($configureFunction[0].Extent.Text.Contains('Set-NeokeyProfilePreference -EnableEnglish $registerEnglish')) `
    "Setup must persist its profile choice for the original desktop user"

# Setup uses regserver rather than Invoke-DllRegistration. Inno applies
# [Registry] before DLL registration, so both registry views must override a
# stored opt-out even on an upgrade or repair (not just a fresh install).
foreach ($view in @('HKCU64', 'HKCU32')) {
    $entry = 'Root: ' + $view + '; Subkey: "Software\Neokey"; ValueType: dword; ValueName: "RegisterEnglishProfile"; ValueData: "1"'
    Assert-True ($setupSource.Contains($entry)) "Setup enables ENG before regserver in $view"
}
$setupConfigRun = @($setupSource -split '\r?\n' | Where-Object {
    $_ -match '^Filename:.*-ConfigureCurrentUserOnly'
})
Assert-True ($setupConfigRun.Count -eq 1) "Setup configures the desktop user exactly once"
Assert-True ($setupConfigRun[0] -match '-SetDefault' -and
             $setupConfigRun[0] -match 'runasoriginaluser' -and
             $setupConfigRun[0] -notmatch '-NoEnglishProfile') `
    "Setup enables both profiles and selects VIE for the original user"

$preferenceHelper = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Set-NeokeyProfilePreference"
}, $true))
Assert-True ($preferenceHelper.Count -eq 1) "profile preference helper exists once"
. ([scriptblock]::Create($preferenceHelper[0].Extent.Text))

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
    $preferenceKey = Join-Path $scratchRoot "ProfilePreference"
    Set-NeokeyProfilePreference -EnableEnglish $true -KeyPath $preferenceKey
    Assert-True ((Get-ItemPropertyValue $preferenceKey RegisterEnglishProfile) -eq 1) `
        "fresh registration enables ENG"
    Set-NeokeyProfilePreference -EnableEnglish $false -KeyPath $preferenceKey
    Assert-True ((Get-ItemPropertyValue $preferenceKey RegisterEnglishProfile) -eq 0) `
        "explicit portable opt-out remains supported"
    Set-NeokeyProfilePreference -EnableEnglish $true -KeyPath $preferenceKey
    Assert-True ((Get-ItemPropertyValue $preferenceKey RegisterEnglishProfile) -eq 1) `
        "an upgrade replaces a previously stored ENG opt-out"

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

# --- Uninstall must put Vietnamese back the way it found it -----------------
#
# A user reported a Vietnamese layout left behind after uninstalling, on a
# machine that had none before installing Neokey. Install adds the vi-VN entry
# when it is missing and prunes every keyboard filed under it; uninstall then
# saw an empty language and filled it with Microsoft's Vietnamese keyboard - the
# one that turns the number row into tone marks. Nothing recorded what was
# there, so nothing could put it back.

foreach ($helperName in @(
        "Get-VietnameseCleanupAction",
        "Test-ShouldRecordPreNeokeyState",
        "Test-ShouldRecordLayoutSubstitute")) {
    $helper = @($ast.FindAll({
        param($node)
        return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $helperName
    }, $true))
    Assert-True ($helper.Count -eq 1) "$helperName is defined exactly once"
    # Pure decision helpers: safe to load and call without running the script or
    # touching the registry.
    . ([scriptblock]::Create($helper[0].Extent.Text))
}

$cleanupCases = @(
    @{
        Name = "legacy install, Neokey was the only thing under Vietnamese"
        Remaining = @(); Recorded = $null; Added = $false
        Only = $false; Display = $false
        Expected = "RemoveLanguage"
    },
    @{
        Name = "this install added the language"
        Remaining = @(); Recorded = $null; Added = $true
        Only = $false; Display = $false
        Expected = "RemoveLanguage"
    },
    @{
        Name = "a keyboard was pruned at install"
        Remaining = @(); Recorded = @("042A:0000042a"); Added = $false
        Only = $false; Display = $false
        Expected = "RestoreRecorded"
    },
    @{
        Name = "another IME was pruned at install and is still registered"
        Remaining = @(); Recorded = @("042A:0000042a", "042A:{aaaa}{bbbb}"); Added = $false
        Only = $false; Display = $false
        Expected = "RestoreRecorded"
    },
    @{
        Name = "a keyboard was added under Vietnamese after Neokey"
        Remaining = @("042A:00000042"); Recorded = $null; Added = $true
        Only = $false; Display = $false
        Expected = "Leave"
    },
    @{
        Name = "restoring wins over leaving, so both come back"
        Remaining = @("042A:00000042"); Recorded = @("042A:0000042a"); Added = $false
        Only = $false; Display = $false
        Expected = "RestoreRecorded"
    },
    @{
        Name = "Vietnamese is the only language this user has"
        Remaining = @(); Recorded = $null; Added = $true
        Only = $true; Display = $false
        Expected = "InstallStockKeyboard"
    },
    @{
        Name = "Windows itself is displayed in Vietnamese"
        Remaining = @(); Recorded = $null; Added = $true
        Only = $false; Display = $true
        Expected = "InstallStockKeyboard"
    },
    @{
        Name = "the language was already there and had nothing else in it"
        Remaining = @(); Recorded = @(); Added = $false
        Only = $false; Display = $false
        Expected = "InstallStockKeyboard"
    },
    @{
        Name = "we added the language and the record says it was empty"
        Remaining = @(); Recorded = @(); Added = $true
        Only = $false; Display = $false
        Expected = "RemoveLanguage"
    },
    @{
        Name = "a blank record reads as no keyboards, not as an unknown machine"
        Remaining = @(); Recorded = @("", "   "); Added = $false
        Only = $false; Display = $false
        Expected = "InstallStockKeyboard"
    }
)

foreach ($case in $cleanupCases) {
    $plan = Get-VietnameseCleanupAction `
        -RemainingInputMethods $case.Remaining `
        -RecordedInputMethods $case.Recorded `
        -AddedLanguage $case.Added `
        -IsOnlyLanguage $case.Only `
        -IsDisplayLanguage $case.Display
    Assert-True ($plan.Action -eq $case.Expected) `
        "cleanup plan: $($case.Name) -> $($case.Expected), got $($plan.Action)"
}

# The restored list is what goes back into the language, so it has to carry
# every recorded keyboard and nothing invented.
$restorePlan = Get-VietnameseCleanupAction `
    -RemainingInputMethods @() `
    -RecordedInputMethods @("042A:0000042a", "042A:{aaaa}{bbbb}") `
    -AddedLanguage $false -IsOnlyLanguage $false -IsDisplayLanguage $false
Assert-True (($restorePlan.InputMethods -join ";") -eq "042A:0000042a;042A:{aaaa}{bbbb}") `
    "a restore plan must name every recorded keyboard"

# Recording is the whole fix, and only the run that changes something can do it.
# A repair or upgrade run sees a machine already in Neokey's shape; recording
# that would tell the uninstaller the machine arrived that way.
$recordCases = @(
    @{ Added = $true;  Replaced = @();                Already = $false; Expected = $true;  Name = "first install that adds the language" },
    @{ Added = $false; Replaced = @("042A:0000042a"); Already = $false; Expected = $true;  Name = "first install that prunes a keyboard" },
    @{ Added = $false; Replaced = @();                Already = $false; Expected = $false; Name = "repair run that changes nothing" },
    @{ Added = $true;  Replaced = @("042A:0000042a"); Already = $true;  Expected = $false; Name = "a record already exists" }
)
foreach ($case in $recordCases) {
    $shouldRecord = Test-ShouldRecordPreNeokeyState `
        -AddedLanguage $case.Added `
        -ReplacedInputMethods $case.Replaced `
        -AlreadyRecorded $case.Already
    Assert-True ($shouldRecord -eq $case.Expected) `
        "record decision: $($case.Name) -> $($case.Expected)"
}

# The layout substitute is the same trap in miniature. An upgrade run finds the
# value Neokey itself wrote on the last install, and claiming that as the user's
# would make uninstall preserve Neokey's own leftover.
$substituteCases = @(
    @{ Recorded = $null; Existing = "";         Expected = $true;  Name = "no value on a machine that never had one" },
    @{ Recorded = $null; Existing = "0000042a"; Expected = $true;  Name = "a value the user set to something else" },
    @{ Recorded = $null; Existing = "00000409"; Expected = $false; Name = "a value indistinguishable from Neokey's own" },
    @{ Recorded = "";    Existing = "";         Expected = $false; Name = "a record already exists" },
    @{ Recorded = "0000042a"; Existing = "0000042a"; Expected = $false; Name = "a filled record already exists" }
)
foreach ($case in $substituteCases) {
    $shouldRecord = Test-ShouldRecordLayoutSubstitute `
        -AlreadyRecorded $case.Recorded `
        -ExistingValue $case.Existing `
        -ValueAboutToBeWritten "00000409"
    Assert-True ($shouldRecord -eq $case.Expected) `
        "substitute record decision: $($case.Name) -> $($case.Expected)"
}

$removeFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Remove-NeokeyFromUserLanguageList"
}, $true))
Assert-True ($removeFunction.Count -eq 1) "Remove-NeokeyFromUserLanguageList is defined once"
$removeText = $removeFunction[0].Extent.Text
Assert-True ($removeText.Contains("Get-VietnameseCleanupAction")) `
    "uninstall must decide through the tested plan rather than inline"
# The stock Vietnamese keyboard is what the reported machines were left with. It
# may still be installed, but only on the branch that has established there is
# nowhere else for the language to go.
$planPos = $removeText.IndexOf("Get-VietnameseCleanupAction")
$stockPos = $removeText.IndexOf("042A:0000042a")
Assert-True ($stockPos -gt $planPos) `
    "the stock Vietnamese keyboard must only be reachable through the plan"
Assert-True ((@([regex]::Matches($removeText, [regex]::Escape("042A:0000042a")))).Count -eq 1) `
    "the stock Vietnamese keyboard must be installed from one place only"

$saveCall = $addText.IndexOf("Save-PreNeokeyVietnameseState")
Assert-True ($saveCall -ge 0) `
    "install must record what Vietnamese looked like before it changed it"
Assert-True ($addText.Contains('-ReplacedInputMethods $legacyTips')) `
    "the record must carry the keyboards install pruned, so uninstall can restore them"
Assert-True ($addText.IndexOf("Set-WinUserLanguageList") -gt $saveCall) `
    "the record must be written before the language list is committed"

$defaultFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Set-NeokeyAsDefaultInputMethod"
}, $true))
Assert-True ($defaultFunction.Count -eq 1) "Set-NeokeyAsDefaultInputMethod is defined once"
$defaultText = $defaultFunction[0].Extent.Text
$readSubstitute = $defaultText.IndexOf("preNeokeySubstituteValue")
$writeSubstitute = $defaultText.IndexOf('Set-ItemProperty -Path $substPath')
Assert-True ($readSubstitute -ge 0 -and $writeSubstitute -gt $readSubstitute) `
    "the old layout substitute must be recorded before it is overwritten"

$unconfigureFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Unconfigure-NeokeyCurrentUser"
}, $true))
Assert-True ($unconfigureFunction.Count -eq 1) "Unconfigure-NeokeyCurrentUser is defined once"
$unconfigureText = $unconfigureFunction[0].Extent.Text
$languagePos = $unconfigureText.IndexOf("Remove-NeokeyFromUserLanguageList")
$substitutePos = $unconfigureText.IndexOf("Restore-VietnameseLayoutSubstitute")
$clearPos = $unconfigureText.IndexOf("Clear-PreNeokeyVietnameseState")
Assert-True ($substitutePos -gt $languagePos) `
    "uninstall must restore the layout substitute as well as the language list"
Assert-True ($clearPos -gt $substitutePos) `
    "the record must be cleared only after everything that reads it has run"

# --- The whole uninstall path, run against a stand-in language list ---------
#
# The plan above is pure, but the bug users hit was in the wiring around it:
# reading the record, applying the chosen action, and handing the list back to
# Windows. That is exercised here by shadowing the four cmdlets it calls, the
# same way Start-Process is shadowed further up. Nothing touches the real
# language list.

$removeFunctionText = $removeFunction[0].Extent.Text
. ([scriptblock]::Create($removeFunctionText))

# The names of the recorded values are taken from register.ps1 rather than
# repeated here, so renaming one there breaks these tests instead of quietly
# leaving them reading $null and passing.
$recordVariableLines = @([regex]::Matches(
    $source,
    '(?m)^\$script:(?:neokeySettingsPath|preNeokey\w*Value)\s*=.*$') |
    ForEach-Object { $_.Value })
Assert-True ($recordVariableLines.Count -eq 4) `
    "register.ps1 must name the recorded values at script scope"
. ([scriptblock]::Create($recordVariableLines -join "`n"))

$tipStr = "042A:{11111111-2222-3333-4444-555555555555}{66666666-7777-8888-9999-000000000000}"
$tipStrEnglish = "0409:{11111111-2222-3333-4444-555555555555}{66666666-7777-8888-9999-000000000000}"

$script:fakeLanguageList = $null
$script:fakeUiCulture = "en-US"
$script:fakeRecord = @{}
$script:committedList = $null

# The leading comma matters: the real cmdlet hands back the List itself, and
# without it PowerShell unrolls the stand-in into a fixed-size array that
# nothing can be removed from.
function Get-WinUserLanguageList { return ,$script:fakeLanguageList }
function Set-WinUserLanguageList {
    param([Parameter(Position = 0)]$LanguageList, [switch]$Force)
    $script:committedList = $LanguageList
}
function Get-DefaultInputMethodTip { return $null }
function Get-UICulture { return [pscustomobject]@{ Name = $script:fakeUiCulture } }
function Get-NeokeySettingValue {
    param([string]$Name)
    if ($script:fakeRecord.ContainsKey($Name)) { return $script:fakeRecord[$Name] }
    return $null
}

function New-FakeLanguage {
    param([string]$Tag, [string[]]$Tips)
    return [pscustomobject]@{
        LanguageTag = $Tag
        InputMethodTips = [System.Collections.Generic.List[string]]@($Tips)
    }
}

function Invoke-FakeUninstall {
    param([object[]]$Languages, [hashtable]$Record, [string]$UiCulture = "en-US")
    $script:fakeLanguageList = [System.Collections.Generic.List[object]]@($Languages)
    $script:fakeRecord = $Record
    $script:fakeUiCulture = $UiCulture
    $script:committedList = $null
    Remove-NeokeyFromUserLanguageList | Out-Null
    return $script:committedList
}

# 1. The machine in the report: Neokey put vi-VN there, nothing was recorded,
#    and Neokey's is the only keyboard under it.
$result = Invoke-FakeUninstall -Languages @(
    (New-FakeLanguage -Tag "en-US" -Tips @("0409:00000409", $tipStrEnglish)),
    (New-FakeLanguage -Tag "vi-VN" -Tips @($tipStr))
) -Record @{}
Assert-True ($null -ne $result) "uninstall must commit a language list"
Assert-True (@($result | Where-Object { $_.LanguageTag -like "vi*" }).Count -eq 0) `
    "a Vietnamese entry that held nothing but Neokey must not be left behind"
$allTips = @($result | ForEach-Object { $_.InputMethodTips })
Assert-True ($allTips -notcontains "042A:0000042a") `
    "uninstall must not leave Microsoft's Vietnamese keyboard on a machine that had none"
Assert-True ($allTips -notcontains $tipStr -and $allTips -notcontains $tipStrEnglish) `
    "both Neokey copies must be gone"
Assert-True ($allTips -contains "0409:00000409") `
    "the US keyboard must survive, or there is no way back in"

# 2. A machine that did have a Vietnamese keyboard, recorded at install.
$result = Invoke-FakeUninstall -Languages @(
    (New-FakeLanguage -Tag "en-US" -Tips @("0409:00000409")),
    (New-FakeLanguage -Tag "vi-VN" -Tips @($tipStr))
) -Record @{ PreviousVietnameseInputMethods = "042A:0000042a" }
$vi = @($result | Where-Object { $_.LanguageTag -like "vi*" })
Assert-True ($vi.Count -eq 1) "a Vietnamese entry the user already had must stay"
Assert-True ($vi[0].InputMethodTips -contains "042A:0000042a") `
    "the keyboard install pruned must come back"
Assert-True ($vi[0].InputMethodTips -notcontains $tipStr) `
    "Neokey must still be removed when a keyboard is restored"

# 3. Another IME's profile, pruned at install, restored on the way out.
$otherIme = "042A:{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}{ffffffff-1111-2222-3333-444444444444}"
$result = Invoke-FakeUninstall -Languages @(
    (New-FakeLanguage -Tag "en-US" -Tips @("0409:00000409")),
    (New-FakeLanguage -Tag "vi-VN" -Tips @($tipStr))
) -Record @{ PreviousVietnameseInputMethods = "042A:0000042a;$otherIme" }
$vi = @($result | Where-Object { $_.LanguageTag -like "vi*" })
Assert-True ($vi[0].InputMethodTips -contains $otherIme) `
    "another IME's profile must come back too, not just the stock keyboard"

# 4. Vietnamese is the only language on the machine. It cannot be removed, so
#    something has to take Neokey's place.
$result = Invoke-FakeUninstall -Languages @(
    (New-FakeLanguage -Tag "vi-VN" -Tips @($tipStr))
) -Record @{ AddedVietnameseLanguage = 1 }
$vi = @($result | Where-Object { $_.LanguageTag -like "vi*" })
Assert-True ($vi.Count -eq 1) "the last language on the machine must never be removed"
Assert-True ($vi[0].InputMethodTips -contains "042A:0000042a") `
    "a language that cannot be removed must not be left with no keyboard"

# 5. Windows itself displayed in Vietnamese: same reasoning, different reason.
$result = Invoke-FakeUninstall -Languages @(
    (New-FakeLanguage -Tag "vi-VN" -Tips @($tipStr)),
    (New-FakeLanguage -Tag "en-US" -Tips @("0409:00000409"))
) -Record @{ AddedVietnameseLanguage = 1 } -UiCulture "vi-VN"
$vi = @($result | Where-Object { $_.LanguageTag -like "vi*" })
Assert-True ($vi.Count -eq 1) "the Windows display language must never be removed"

# 6. A keyboard the user added under Vietnamese after installing Neokey is
#    theirs, and the language stays for it.
$result = Invoke-FakeUninstall -Languages @(
    (New-FakeLanguage -Tag "en-US" -Tips @("0409:00000409")),
    (New-FakeLanguage -Tag "vi-VN" -Tips @($tipStr, "042A:00000042"))
) -Record @{ AddedVietnameseLanguage = 1 }
$vi = @($result | Where-Object { $_.LanguageTag -like "vi*" })
Assert-True ($vi.Count -eq 1 -and $vi[0].InputMethodTips -contains "042A:00000042") `
    "a keyboard added after Neokey must survive Neokey's removal"
Assert-True ($vi[0].InputMethodTips -notcontains "042A:0000042a") `
    "and nothing else must be added alongside it"

# 7. Neokey was never in this account's list - the elevated account during an
#    installer uninstall, for one. Nothing to do, and nothing to undo.
$result = Invoke-FakeUninstall -Languages @(
    (New-FakeLanguage -Tag "en-US" -Tips @("0409:00000409")),
    (New-FakeLanguage -Tag "vi-VN" -Tips @("042A:0000042a"))
) -Record @{}
Assert-True ($null -eq $result) `
    "an account that never had Neokey must not have its language list rewritten"

Remove-Item -Path "function:Get-WinUserLanguageList" -ErrorAction SilentlyContinue
Remove-Item -Path "function:Set-WinUserLanguageList" -ErrorAction SilentlyContinue
Remove-Item -Path "function:Get-UICulture" -ErrorAction SilentlyContinue
Remove-Item -Path "function:Get-NeokeySettingValue" -ErrorAction SilentlyContinue
Remove-Item -Path "function:Get-DefaultInputMethodTip" -ErrorAction SilentlyContinue

# --- The record itself, through a real registry key --------------------------
#
# Everything above shadows the registry. What is left untested is the seam the
# whole fix rests on: that what install writes is what uninstall reads back.
# Three states have to survive the trip, and the empty one - "there was nothing
# here" - has to stay distinguishable from "nothing was ever recorded".

$recordScratch = "HKCU:\Software\NeokeyRecordTests"
Remove-Item -LiteralPath $recordScratch -Recurse -Force -ErrorAction SilentlyContinue
New-Item -Path "HKCU:\Software" -Name "NeokeyRecordTests" -Force | Out-Null
try {
    foreach ($helperName in @(
            "Get-NeokeySettingValue",
            "Set-NeokeySettingValue",
            "Save-PreNeokeyVietnameseState",
            "Clear-PreNeokeyVietnameseState")) {
        $helper = @($ast.FindAll({
            param($node)
            return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -eq $helperName
        }, $true))
        Assert-True ($helper.Count -eq 1) "$helperName is defined exactly once"
        . ([scriptblock]::Create($helper[0].Extent.Text))
    }
    $script:neokeySettingsPath = $recordScratch

    # Reads the record the way Remove-NeokeyFromUserLanguageList does, so the
    # two sides of the trip are exercised together rather than assumed to match.
    # The leading comma is this helper's own business: returning an empty array
    # from a function hands back nothing at all. The code being tested assigns
    # the same expression inline, where an empty array stays one.
    function Read-RecordedTips {
        $raw = Get-NeokeySettingValue $script:preNeokeyTipsValue
        if ($null -eq $raw) { return $null }
        return ,@((([string]$raw) -split ";") |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    }

    Assert-True ($null -eq (Read-RecordedTips)) `
        "a machine with no record must read as unknown, not as empty"

    # A first install that found a Vietnamese keyboard and replaced it.
    Save-PreNeokeyVietnameseState -AddedLanguage $false -ReplacedInputMethods @("042A:0000042a")
    $roundTripped = Read-RecordedTips
    Assert-True ($null -ne $roundTripped -and ($roundTripped -join ";") -eq "042A:0000042a") `
        "a recorded keyboard must survive the registry round trip"
    Assert-True ([int](Get-NeokeySettingValue $script:preNeokeyLanguageAddedValue) -eq 0) `
        "an install that did not add the language must not claim it did"

    # A second run must not overwrite what the first one saw.
    Save-PreNeokeyVietnameseState -AddedLanguage $true -ReplacedInputMethods @()
    Assert-True ((Read-RecordedTips) -join ";" -eq "042A:0000042a") `
        "a repair run must not overwrite the first run's record"

    Clear-PreNeokeyVietnameseState
    Assert-True ($null -eq (Read-RecordedTips)) `
        "uninstall must clear the record so a later install records afresh"

    # A first install on a machine with no Vietnamese at all. The record is
    # empty, and empty has to survive as empty.
    Save-PreNeokeyVietnameseState -AddedLanguage $true -ReplacedInputMethods @()
    $roundTripped = Read-RecordedTips
    Assert-True ($null -ne $roundTripped -and $roundTripped.Count -eq 0) `
        "an empty record must read back as empty, not as unknown"
    Assert-True ([int](Get-NeokeySettingValue $script:preNeokeyLanguageAddedValue) -eq 1) `
        "an install that added the language must record it"

    # And that is the machine in the report: the plan has to take the language
    # away rather than fill it with Microsoft's keyboard.
    $plan = Get-VietnameseCleanupAction `
        -RemainingInputMethods @() `
        -RecordedInputMethods $roundTripped `
        -AddedLanguage ([int](Get-NeokeySettingValue $script:preNeokeyLanguageAddedValue) -eq 1) `
        -IsOnlyLanguage $false -IsDisplayLanguage $false
    Assert-True ($plan.Action -eq "RemoveLanguage") `
        "a record written by install must lead uninstall to remove the language it added"

    # Several keyboards, including another IME's profile with its braces.
    Clear-PreNeokeyVietnameseState
    $otherIme = "042A:{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}{ffffffff-1111-2222-3333-444444444444}"
    Save-PreNeokeyVietnameseState `
        -AddedLanguage $false `
        -ReplacedInputMethods @("042A:0000042a", $otherIme)
    $roundTripped = Read-RecordedTips
    Assert-True (($roundTripped -join ";") -eq "042A:0000042a;$otherIme") `
        "every recorded keyboard must survive, braces and all"
} finally {
    Remove-Item -LiteralPath $recordScratch -Recurse -Force -ErrorAction SilentlyContinue
}

# --- Uninstall must leave nothing behind ------------------------------------
#
# Two uninstallers remove the same machine, so both read one inventory. A
# setting added to the app and not to that inventory is what leaves a machine
# carrying Neokey's rules after Neokey is gone - and a reinstall then looks
# haunted rather than clean.

foreach ($helperName in @(
        "Get-NeokeyResidueTargets",
        "Test-NeokeyResidueTargetPresent",
        "Remove-NeokeyResidue")) {
    $helper = @($ast.FindAll({
        param($node)
        return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $helperName
    }, $true))
    Assert-True ($helper.Count -eq 1) "$helperName is defined exactly once"
    . ([scriptblock]::Create($helper[0].Extent.Text))
}

$residueTargets = @(Get-NeokeyResidueTargets -PackageDirectory "C:\Neokey")
$residuePaths = @($residueTargets | ForEach-Object { $_.Path })
Assert-True ($residuePaths -contains "HKCU:\Software\Neokey") `
    "the inventory must cover the settings key"
Assert-True (@($residueTargets | Where-Object {
        $_.Kind -eq "RegistryValue" -and $_.Name -eq "Neokey"
    }).Count -eq 1) "the inventory must cover the startup entry"
Assert-True (@($residueTargets | Where-Object { $_.Path -like "*\neokey.log" }).Count -ge 1) `
    "the inventory must cover the log"
Assert-True (@($residuePaths | Where-Object { $_ -like "*\register_elevated.log" }).Count -eq 1) `
    "the inventory must cover the log the elevated half writes"
Assert-True (@($residuePaths | Where-Object { $_ -like "*\Neokey" -and $_ -notlike "HKCU:*" }).Count -ge 1) `
    "the inventory must cover the per-user data folder"

# -KeepUserData exists for the one thing on the list the user typed themselves.
# If it ever came to spare a log or a registry key, an uninstall would quietly
# stop being an uninstall.
$sparedTargets = @($residueTargets | Where-Object { $_.UserData })
Assert-True ($sparedTargets.Count -eq 1) `
    "exactly one inventory entry may be user-authored data"
Assert-True ($sparedTargets[0].Path -like "*\Neokey") `
    "the spared entry must be the shorthand folder"

$residueScratch = Join-Path ([System.IO.Path]::GetTempPath()) "NeokeyResidueTests"
$residueKey = "HKCU:\Software\NeokeyResidueTests"
$residueValueKey = "HKCU:\Software\NeokeyResidueTestsValue"
try {
    # The tray app is a real window on a real desktop. Tests must not close it.
    function Stop-NeokeyTrayApp { $script:trayStopCalled = $true }

    function New-ResidueFixture {
        Remove-Item -LiteralPath $residueScratch -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $residueKey -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $residueValueKey -Recurse -Force -ErrorAction SilentlyContinue

        New-Item -ItemType Directory -Path $residueScratch -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $residueScratch "data") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $residueScratch "data\typed.txt") -Value "kept" -Encoding utf8
        Set-Content -LiteralPath (Join-Path $residueScratch "some.log") -Value "log" -Encoding utf8
        New-Item -Path "HKCU:\Software" -Name "NeokeyResidueTests" -Force | Out-Null
        New-Item -Path "HKCU:\Software" -Name "NeokeyResidueTestsValue" -Force | Out-Null
        New-ItemProperty -Path $residueValueKey -Name "Neokey" -Value "run me" -PropertyType String -Force | Out-Null
    }

    function Get-NeokeyResidueTargets {
        return @(
            [pscustomobject]@{ Kind = "RegistryKey"; Path = $residueKey; Label = "settings"; UserData = $false },
            [pscustomobject]@{ Kind = "RegistryValue"; Path = $residueValueKey; Name = "Neokey"; Label = "start with Windows"; UserData = $false },
            [pscustomobject]@{ Kind = "Directory"; Path = (Join-Path $residueScratch "data"); Label = "shorthand data"; UserData = $true },
            [pscustomobject]@{ Kind = "File"; Path = (Join-Path $residueScratch "some.log"); Label = "log"; UserData = $false },
            [pscustomobject]@{ Kind = "File"; Path = (Join-Path $residueScratch "absent.log"); Label = "log"; UserData = $false }
        )
    }

    # Every kind in the inventory has to be present before it can be removed, or
    # the removal below would pass by doing nothing.
    New-ResidueFixture
    foreach ($target in Get-NeokeyResidueTargets) {
        $expected = $target.Path -notlike "*absent.log"
        Assert-True ((Test-NeokeyResidueTargetPresent $target) -eq $expected) `
            "presence check for $($target.Kind) $($target.Path)"
    }

    $script:trayStopCalled = $false
    Remove-NeokeyResidue | Out-Null
    Assert-True $script:trayStopCalled `
        "the tray app must be closed before its settings key is removed"
    foreach ($target in Get-NeokeyResidueTargets) {
        Assert-True (-not (Test-NeokeyResidueTargetPresent $target)) `
            "a full uninstall must remove $($target.Kind) $($target.Path)"
    }

    # -KeepUserData spares the shorthand folder and nothing else.
    New-ResidueFixture
    Remove-NeokeyResidue -KeepUserData | Out-Null
    foreach ($target in Get-NeokeyResidueTargets) {
        $shouldSurvive = $target.UserData
        Assert-True ((Test-NeokeyResidueTargetPresent $target) -eq $shouldSurvive) `
            "-KeepUserData handling of $($target.Kind) $($target.Path)"
    }
    Assert-True (Test-Path -LiteralPath (Join-Path $residueScratch "data\typed.txt")) `
        "-KeepUserData must keep what is inside the folder, not just the folder"

    # A missing target is not a failure. Uninstalling twice, or uninstalling a
    # machine that never enabled logging, must still report success.
    New-ResidueFixture
    Remove-NeokeyResidue | Out-Null
    Remove-NeokeyResidue | Out-Null
    Assert-True $true "removing residue that is already gone must not throw"
} finally {
    Remove-Item -Path "function:Stop-NeokeyTrayApp" -ErrorAction SilentlyContinue
    Remove-Item -Path "function:Get-NeokeyResidueTargets" -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $residueScratch -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $residueKey -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $residueValueKey -Recurse -Force -ErrorAction SilentlyContinue
}

$unconfigureText = $unconfigureFunction[0].Extent.Text
Assert-True ($unconfigureText.Contains("Remove-NeokeyResidue")) `
    "uninstall must clear the machine footprint, not only the language list"
Assert-True ($unconfigureText.IndexOf("Remove-NeokeyResidue") -gt
    $unconfigureText.IndexOf("Clear-PreNeokeyVietnameseState")) `
    "the settings key must not be removed before the things that read it have run"

$dllUnregisterFunction = @($ast.FindAll({
    param($node)
    return $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Invoke-DllUnregistration"
}, $true))
Assert-True ($dllUnregisterFunction.Count -eq 1) "Invoke-DllUnregistration is defined once"
$dllUnregisterText = $dllUnregisterFunction[0].Extent.Text
$sweepPosition = $dllUnregisterText.IndexOf("Remove-NeokeyMachineRegistryResidue")
Assert-True ($sweepPosition -gt $dllUnregisterText.LastIndexOf("Invoke-Regsvr32")) `
    "leftover keys must be swept after unregistration, never before it"

# The installer never runs the script's sweep - Inno unregisters the DLLs
# itself, from its own uninstall step - so it carries its own, and it has to sit
# at the step that runs once unregistration is done.
# A prerelease version - 0.1.15-dev - is a valid name for a build and not a
# valid VERSIONINFO number, which is four integers and nothing else. The numeric
# part goes in the number fields and the whole string in the text ones beside
# them; putting the whole string in a number field fails the compile outright,
# and putting the numeric part in the text ones makes package.ps1's readback
# check disagree with VERSION.
Assert-True ($setupSource.Contains("MyNumericVersion")) `
    "the installer must derive a numeric version for the VERSIONINFO fields"
foreach ($numericOnly in @("VersionInfoVersion", "VersionInfoProductVersion")) {
    Assert-True ($setupSource -match ([regex]::Escape($numericOnly) + '=\{#MyNumericVersion\}')) `
        "$numericOnly must take the numeric version, not the full string"
}
foreach ($textField in @("VersionInfoTextVersion", "VersionInfoProductTextVersion")) {
    Assert-True ($setupSource -match ([regex]::Escape($textField) + '=\{#MyAppVersion\}')) `
        "$textField must carry the full version string, suffix and all"
}
Assert-True ($setupSource -match 'AppVersion=\{#MyAppVersion\}') `
    "the version Windows shows in Apps and Features must keep its suffix"
Assert-True ($packageSource.Contains('VersionInfo.ProductVersion')) `
    "packaging must read the built installer's version back rather than trust the compile"

Assert-True ($setupSource.Contains("[UninstallDelete]")) `
    "the installer must clear its own program folder"
Assert-True ($setupSource.Contains("neokey_shorthand.txt")) `
    "the installer must delete the shorthand file it may not have recorded"
Assert-True ($setupSource.Contains("CurUninstallStepChanged")) `
    "the installer must sweep leftover registration keys"
$postUninstallPosition = $setupSource.IndexOf("usPostUninstall")
$sweepCallPosition = $setupSource.IndexOf("RemoveLeftoverRegistrationKeys();", $postUninstallPosition)
Assert-True ($postUninstallPosition -ge 0 -and $sweepCallPosition -gt $postUninstallPosition) `
    "the installer's sweep must be gated on usPostUninstall"

Write-Host "register_script_tests: $passed passed, 0 failed"
