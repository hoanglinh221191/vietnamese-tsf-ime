param(
    [string]$ResourceScript,
    [string]$ResourceHeader,
    [string]$MainSource
)

# The settings window shows one page at a time, so its controls deliberately sit
# on top of each other in the template. Nothing about a control says which page
# it belongs to - main.cpp says so, in kConfigPage* - and a control left out of
# every list is simply never shown by ShowConfigPage. Neither mistake stops the
# build, and neither is visible until someone opens the window and finds a
# missing option or two labels printed over each other.
#
# So the two halves are checked against each other here: every control in the
# template belongs to exactly one page or to the chrome that is always on
# screen, and within a page no two controls overlap.

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

$repoRoot = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($ResourceScript)) {
    $ResourceScript = Join-Path $repoRoot "src\config-app\resources.rc"
}
if ([string]::IsNullOrWhiteSpace($ResourceHeader)) {
    $ResourceHeader = Join-Path $repoRoot "src\config-app\resources.h"
}
if ([string]::IsNullOrWhiteSpace($MainSource)) {
    $MainSource = Join-Path $repoRoot "src\config-app\main.cpp"
}

$rc = Get-Content -LiteralPath $ResourceScript -Raw
$main = Get-Content -LiteralPath $MainSource -Raw

$dialogStart = $rc.IndexOf("IDD_CONFIG_DIALOG DIALOGEX")
Assert-True ($dialogStart -ge 0) "resources.rc must define IDD_CONFIG_DIALOG"
$dialogBody = $rc.Substring($dialogStart)
$dialogEnd = $dialogBody.IndexOf("`nEND")
Assert-True ($dialogEnd -gt 0) "IDD_CONFIG_DIALOG must be terminated by END"
$dialogBody = $dialogBody.Substring(0, $dialogEnd)

$dialogSize = [regex]::Match(
    $dialogBody, "IDD_CONFIG_DIALOG DIALOGEX\s+\d+,\s*\d+,\s*(\d+),\s*(\d+)")
Assert-True $dialogSize.Success "the dialog must declare its size"
$dialogWidth = [int]$dialogSize.Groups[1].Value
$dialogHeight = [int]$dialogSize.Groups[2].Value

$keywords = @(
    "CONTROL", "LTEXT", "RTEXT", "CTEXT", "PUSHBUTTON", "DEFPUSHBUTTON",
    "AUTOCHECKBOX", "AUTORADIOBUTTON", "GROUPBOX", "COMBOBOX", "EDITTEXT",
    "LISTBOX")

# name -> rectangle. A combo box declares the height of its dropped-down list,
# not of the closed control, so comparing that height would report an overlap
# with every row beneath it.
$rects = @{}
foreach ($rawLine in $dialogBody -split "`r?`n") {
    $line = $rawLine.Trim()
    if ($line.Length -eq 0) { continue }
    $keyword = ($line -split "\s+")[0]
    if ($keywords -notcontains $keyword) { continue }

    $bare = [regex]::Replace($line, '"[^"]*"', "")
    $identMatch = [regex]::Match($bare, "\b(ID[A-Z]*_\w+|IDOK|IDCANCEL|IDAPPLY)\b")
    if (-not $identMatch.Success) { continue }
    $numbers = @([regex]::Matches($bare, "-?\d+") | ForEach-Object { [int]$_.Value })
    if ($numbers.Count -lt 4) { continue }

    $height = $numbers[3]
    if ($keyword -eq "COMBOBOX") { $height = 12 }
    $rects[$identMatch.Groups[1].Value] = [pscustomobject]@{
        X = $numbers[0]
        Y = $numbers[1]
        W = $numbers[2]
        H = $height
    }
}

Assert-True ($rects.Count -gt 40) `
    "the dialog template should parse to a few dozen controls, got $($rects.Count)"

function Get-PageMembers {
    param([string]$ArrayName)

    $marker = "constexpr int $ArrayName[] = {"
    $start = $main.IndexOf($marker)
    if ($start -lt 0) {
        throw "FAILED: main.cpp must define $ArrayName"
    }
    $body = $main.Substring($start + $marker.Length)
    $body = $body.Substring(0, $body.IndexOf("};"))
    return @([regex]::Matches($body, "\bID[A-Z]*_\w+\b") |
        ForEach-Object { $_.Value })
}

$pages = [ordered]@{
    "Correction"         = Get-PageMembers "kConfigPageCorrection"
    "Vietnamese typing"  = Get-PageMembers "kConfigPageTyping"
    "Utilities"          = Get-PageMembers "kConfigPageUtilities"
    "Applications"       = Get-PageMembers "kConfigPageApps"
    "Hotkey and startup" = Get-PageMembers "kConfigPageHotkey"
}

# The panels are backdrops the pages are drawn on, so they contain the controls
# rather than collide with them. Taken from the template rather than listed, so
# that a new one is covered by the check below without being added here too.
$panels = @($rects.Keys | Where-Object { $_ -like "IDC_PANEL_*" })
Assert-True ($panels.Count -ge 3) "the dialog must have its layout panels"

# A panel is a rectangle, not a control: the dialog paints the surface itself in
# DrawDialogSurfaceMarkers and hides the static that gave it the coordinates. A
# panel left out of kSurfaceMarkerIds stays visible, and because the dialog has
# WS_CLIPCHILDREN a visible static that paints nothing cuts its own rectangle
# out of the background - a black block, with every control behind it gone.
# That shipped once, as the whole left column.
$markerStart = $main.IndexOf("kSurfaceMarkerIds = std::to_array({")
Assert-True ($markerStart -ge 0) "main.cpp must define kSurfaceMarkerIds"
$markerBody = $main.Substring($markerStart)
$markerBody = $markerBody.Substring(0, $markerBody.IndexOf("});"))
foreach ($name in $panels) {
    Assert-True ($markerBody -match "\b$name\b") `
        "$name must be in kSurfaceMarkerIds or it will paint a black block over the dialog"
}

$paged = @{}
foreach ($title in $pages.Keys) {
    foreach ($name in $pages[$title]) {
        if ($paged.ContainsKey($name)) {
            Assert-True $false `
                "$name is on both the $($paged[$name]) page and the $title page"
        }
        $paged[$name] = $title
        Assert-True ($rects.ContainsKey($name)) `
            "$name is on the $title page but has no control in the template"
    }
}

$always = @($rects.Keys | Where-Object { -not $paged.ContainsKey($_) })

# ShowConfigPage only ever shows and hides the controls the page lists name, so
# a control left out of all of them is never hidden - it stays on screen over
# every page. Caught here by where it sits: anything inside the page panel that
# belongs to no page is that mistake, and saying so is more use than the pile of
# overlaps it would otherwise produce.
$optionsPanel = $rects["IDC_PANEL_OPTIONS"]
Assert-True ($null -ne $optionsPanel) "the options panel must exist"
foreach ($name in $always) {
    if ($panels -contains $name) { continue }
    $r = $rects[$name]
    $inside = ($r.X -ge $optionsPanel.X) -and ($r.Y -ge $optionsPanel.Y) -and
              (($r.X + $r.W) -le ($optionsPanel.X + $optionsPanel.W)) -and
              (($r.Y + $r.H) -le ($optionsPanel.Y + $optionsPanel.H))
    Assert-True (-not $inside) `
        "$name sits in the page area but is on no page, so it would show over every page"
}

function Test-Overlap {
    param($A, $B)

    return ($A.X -lt ($B.X + $B.W)) -and ($B.X -lt ($A.X + $A.W)) -and
           ($A.Y -lt ($B.Y + $B.H)) -and ($B.Y -lt ($A.Y + $A.H))
}

foreach ($title in $pages.Keys) {
    $visible = @($pages[$title] + $always |
        Where-Object { $rects.ContainsKey($_) -and $panels -notcontains $_ })
    for ($i = 0; $i -lt $visible.Count; $i++) {
        for ($j = $i + 1; $j -lt $visible.Count; $j++) {
            $a = $visible[$i]
            $b = $visible[$j]
            if (Test-Overlap $rects[$a] $rects[$b]) {
                Assert-True $false `
                    ("on the $title page, $a " +
                     "($($rects[$a].X),$($rects[$a].Y),$($rects[$a].W),$($rects[$a].H)) " +
                     "overlaps $b " +
                     "($($rects[$b].X),$($rects[$b].Y),$($rects[$b].W),$($rects[$b].H))")
            }
        }
    }
    Assert-True $true "no two controls overlap on the $title page"
}

foreach ($name in $rects.Keys) {
    $r = $rects[$name]
    Assert-True (
        $r.X -ge 0 -and $r.Y -ge 0 -and
        ($r.X + $r.W) -le $dialogWidth -and ($r.Y + $r.H) -le $dialogHeight) `
        "$name must fit inside the ${dialogWidth}x${dialogHeight} dialog"
}

# The footer rule and the buttons under it stay clear of every page, which is
# what lets the pages share one rectangle above it.
$separator = $rects["IDC_STATIC_FOOTER_SEPARATOR"]
Assert-True ($null -ne $separator) "the footer separator must exist"
foreach ($title in $pages.Keys) {
    foreach ($name in $pages[$title]) {
        $r = $rects[$name]
        Assert-True (($r.Y + $r.H) -le $separator.Y) `
            "$name on the $title page must sit above the footer rule"
    }
}

# Every nav button needs a page, and every page a nav button.
$navButtons = @(
    "IDC_BUTTON_NAV_CORRECTION", "IDC_BUTTON_NAV_TYPING",
    "IDC_BUTTON_NAV_UTILITIES", "IDC_BUTTON_NAV_APPS",
    "IDC_BUTTON_NAV_HOTKEY")
Assert-True ($navButtons.Count -eq $pages.Count) `
    "there must be one nav button per page"
foreach ($name in $navButtons) {
    Assert-True ($rects.ContainsKey($name)) "$name must exist in the template"
    Assert-True ($main.Contains($name)) "$name must be wired up in main.cpp"
}

Write-Host "config_dialog_layout_tests: $passed passed, 0 failed"
Write-Host ("  $($rects.Count) controls, $($paged.Count) on pages, " +
            "$($always.Count) always visible")
