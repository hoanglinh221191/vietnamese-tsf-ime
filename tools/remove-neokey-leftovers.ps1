<#
.SYNOPSIS
Removes what Neokey 0.1.14 and earlier left behind when it was uninstalled.

.DESCRIPTION
Uninstalling those versions added Microsoft's Vietnamese keyboard to the
language list on the way out, on machines that had never had it, and left the
settings key, the shorthand folder and the log where they were. This takes the
lot away.

It changes nothing outside the current user's own settings, so it does not need
Administrator. It refuses to act while Neokey is still registered, and it
refuses to remove a Vietnamese entry that holds anything other than Microsoft's
own keyboard - another Vietnamese IME's profile lives in the same place.

The same thing can be done by hand:

    Settings > Time and language > Language and region
    Find the Vietnamese row - "Tieng Viet" on a Vietnamese Windows
    Click the three dots at the right of that row, then Remove

That is greyed out when Vietnamese is the language Windows itself is displayed
in, or when it is the only language on the machine. In either case change the
display language or add another language first.

.PARAMETER Force
Remove without asking first.

.PARAMETER WhatIf
Report what would be removed and change nothing.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File remove-neokey-leftovers.ps1
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$clsid = "{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}"
$profileGuid = "{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}"
$vietnameseTip = "042A:$clsid$profileGuid"
$englishTip = "0409:$clsid$profileGuid"
$stockVietnameseKeyboard = "042A:0000042a"

function Write-Heading {
    param([string]$Text)
    Write-Host ""
    Write-Host $Text
    Write-Host ("-" * $Text.Length)
}

# Whether the Vietnamese entry is the one Neokey's old uninstall put there, and
# nothing else. Every refusal below is a case where removing it would take away
# something the user did not get from Neokey.
function Get-LeftoverLanguagePlan {
    param(
        [object]$VietnameseInputMethods,
        [int]$LanguageCount,
        [string]$UiCulture,
        [string]$StockKeyboard = "042A:0000042a"
    )

    if ($null -eq $VietnameseInputMethods) {
        return [pscustomobject]@{ Removable = $false; Reason = "not present" }
    }
    $tips = @($VietnameseInputMethods)
    if ($tips.Count -ne 1 -or $tips[0] -ne $StockKeyboard) {
        return [pscustomobject]@{
            Removable = $false
            Reason = "something other than Microsoft's Vietnamese keyboard is filed under it, which means another Vietnamese IME is using this entry"
        }
    }
    if ($LanguageCount -le 1) {
        return [pscustomobject]@{
            Removable = $false
            Reason = "it is the only language on this machine"
        }
    }
    if ($UiCulture -like "vi*") {
        return [pscustomobject]@{
            Removable = $false
            Reason = "Windows itself is displayed in Vietnamese"
        }
    }
    return [pscustomobject]@{
        Removable = $true
        Reason = "holds nothing but Microsoft's Vietnamese keyboard, so it can go"
    }
}

# Refuses to run against a working installation. Somebody who still has Neokey
# would otherwise take their own input method away with this.
function Test-NeokeyStillInstalled {
    $registrationKeys = @(
        "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$clsid",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP\$clsid",
        "HKLM:\SOFTWARE\Classes\CLSID\$clsid",
        "HKCU:\SOFTWARE\Classes\CLSID\$clsid"
    )
    foreach ($key in $registrationKeys) {
        if (Test-Path -LiteralPath $key) {
            return $key
        }
    }

    $list = Get-WinUserLanguageList
    foreach ($language in $list) {
        if ($language.InputMethodTips -contains $vietnameseTip -or
            $language.InputMethodTips -contains $englishTip) {
            return "the user language list"
        }
    }
    return $null
}

$stillInstalled = Test-NeokeyStillInstalled
if ($null -ne $stillInstalled) {
    Write-Warning "Neokey is still registered on this machine ($stillInstalled)."
    Write-Host ""
    Write-Host "Uninstall Neokey first - uninstall.bat in the portable folder, or"
    Write-Host "Settings > Apps if it was installed with NeokeySetup.exe. Versions"
    Write-Host "from 0.1.15 onwards clean up after themselves and this script has"
    Write-Host "nothing left to do."
    exit 1
}

Write-Heading "What is left behind"

$languageList = Get-WinUserLanguageList
$vietnamese = $languageList | Where-Object { $_.LanguageTag -like "vi*" } | Select-Object -First 1
$uiCulture = (Get-UICulture).Name

$plan = Get-LeftoverLanguagePlan `
    -VietnameseInputMethods $(if ($null -eq $vietnamese) { $null } else { @($vietnamese.InputMethodTips) }) `
    -LanguageCount $languageList.Count `
    -UiCulture $uiCulture `
    -StockKeyboard $stockVietnameseKeyboard
$removableLanguage = $plan.Removable

if ($null -eq $vietnamese) {
    Write-Host "  Vietnamese language entry: not present"
} else {
    Write-Host "  Vietnamese language entry: $($vietnamese.LanguageTag) [$($vietnamese.InputMethodTips -join ', ')]"
    if ($removableLanguage) {
        Write-Host "    $($plan.Reason)"
    } else {
        Write-Host "    Kept: $($plan.Reason)."
    }
}

$substitutePath = "HKCU:\Keyboard Layout\Substitutes"
$substitute = (Get-ItemProperty -Path $substitutePath -Name "0000042a" -ErrorAction SilentlyContinue)."0000042a"
if ($null -ne $substitute) {
    Write-Host "  Vietnamese layout substitute: 0000042a -> $substitute"
}

$leftovers = @()
if (Test-Path -LiteralPath "HKCU:\Software\Neokey") {
    $leftovers += [pscustomobject]@{ Kind = "RegistryKey"; Path = "HKCU:\Software\Neokey"; Label = "settings" }
}
$runValue = (Get-ItemProperty `
    -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" `
    -Name "Neokey" -ErrorAction SilentlyContinue).Neokey
if ($null -ne $runValue) {
    $leftovers += [pscustomobject]@{ Kind = "RegistryValue"; Path = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"; Name = "Neokey"; Label = "start with Windows" }
}
if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    $dataDirectory = Join-Path $env:LOCALAPPDATA "Neokey"
    if (Test-Path -LiteralPath $dataDirectory) {
        $leftovers += [pscustomobject]@{ Kind = "Directory"; Path = $dataDirectory; Label = "shorthand data" }
    }
}
foreach ($directory in @($env:TEMP, "C:\Temp")) {
    if ([string]::IsNullOrWhiteSpace($directory)) { continue }
    $log = Join-Path $directory "neokey.log"
    if (Test-Path -LiteralPath $log) {
        $leftovers += [pscustomobject]@{ Kind = "File"; Path = $log; Label = "log" }
    }
}
foreach ($leftover in $leftovers) {
    $name = if ($leftover.Kind -eq "RegistryValue") { "$($leftover.Path)\$($leftover.Name)" } else { $leftover.Path }
    Write-Host "  $($leftover.Label): $name"
}

if (-not $removableLanguage -and $leftovers.Count -eq 0) {
    Write-Host ""
    Write-Host "Nothing to remove."
    exit 0
}

if (-not $Force -and -not $WhatIfPreference) {
    Write-Host ""
    $answer = Read-Host "Remove all of the above? [y/N]"
    if ($answer -notmatch '^(y|yes)$') {
        Write-Host "Nothing was changed."
        exit 0
    }
}

Write-Heading "Removing"

if ($removableLanguage) {
    if ($PSCmdlet.ShouldProcess("the Vietnamese language entry", "Remove")) {
        # By position rather than by object: the entry came out of a pipeline,
        # and matching it back would rest on how it was wrapped.
        for ($i = $languageList.Count - 1; $i -ge 0; $i--) {
            if ($languageList[$i].LanguageTag -like "vi*") {
                $languageList.RemoveAt($i)
            }
        }
        Set-WinUserLanguageList $languageList -Force
        Write-Host "  Removed the Vietnamese language entry."
    }

    # Only alongside the language, and only when it reads exactly what Neokey
    # wrote. Left on its own it is never consulted, and other Vietnamese IMEs
    # write the same value - taking theirs would hand their users the tone-mark
    # number row this is meant to be undoing.
    if ($substitute -eq "00000409") {
        if ($PSCmdlet.ShouldProcess("the Vietnamese layout substitute", "Remove")) {
            Remove-ItemProperty -Path $substitutePath -Name "0000042a" -ErrorAction SilentlyContinue
            Write-Host "  Removed the Vietnamese layout substitute."
        }
    }
}

foreach ($leftover in $leftovers) {
    $name = if ($leftover.Kind -eq "RegistryValue") { "$($leftover.Path)\$($leftover.Name)" } else { $leftover.Path }
    if (-not $PSCmdlet.ShouldProcess($name, "Remove")) {
        continue
    }
    try {
        switch ($leftover.Kind) {
            "RegistryValue" {
                Remove-ItemProperty -Path $leftover.Path -Name $leftover.Name -Force -ErrorAction Stop
            }
            "RegistryKey" {
                Remove-Item -LiteralPath $leftover.Path -Recurse -Force -ErrorAction Stop
            }
            default {
                Remove-Item -LiteralPath $leftover.Path -Recurse -Force -ErrorAction Stop
            }
        }
        Write-Host "  Removed $($leftover.Label): $name"
    } catch {
        Write-Warning "Could not remove $($leftover.Label) at ${name}: $($_.Exception.Message)"
    }
}

Write-Host ""
Write-Host "Done. Sign out and back in if the language button is still in the taskbar."
