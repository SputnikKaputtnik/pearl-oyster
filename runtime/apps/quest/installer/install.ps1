# Oyster - Pearl for Meta Quest: installer (Windows PowerShell 5.1 or later).
#
# Copies the content of YOUR OWN Pearl installation (Steam AppID 476540, build 1340090) to the
# headset, installs the app and grants it access to the data. Nothing of Pearl is downloaded or
# included in this package.
#
#   install.ps1 [-PearlPath <folder>] [-VerifyHashes] [-SkipData] [-Serial <adb serial>]
param(
    [string]$PearlPath,
    [switch]$VerifyHashes,
    [switch]$SkipData,
    [string]$Serial
)
$ErrorActionPreference = 'Stop'
$Here = $PSScriptRoot
$Package = 'org.oyster.pearl'
$Remote = '/sdcard/Oyster/pearl'
$Folders = @('common', 'pearl_vrcam', 'pearlpackage', 'story')

function Step($text) { Write-Host "`n== $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "`nERROR: $text" -ForegroundColor Red; exit 1 }

# ---- adb ----------------------------------------------------------------------------------------
function Find-Adb {
    $cmd = Get-Command adb -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { return [string]$cmd.Source }
    foreach ($p in @("$Here\platform-tools\adb.exe", "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe")) {
        if (Test-Path $p) { return $p }
    }
    Write-Host 'adb (Android platform-tools) was not found.'
    $answer = Read-Host 'Download the official platform-tools from Google (about 7 MB) next to this installer? [Y/n]'
    if ($answer -and $answer -notmatch '^[Yy]') { Fail 'adb is required. Install the Android platform-tools and run again.' }
    $zip = Join-Path $env:TEMP 'platform-tools-latest-windows.zip'
    Invoke-WebRequest -UseBasicParsing 'https://dl.google.com/android/repository/platform-tools-latest-windows.zip' -OutFile $zip
    Expand-Archive -Force $zip $Here
    Remove-Item $zip
    return "$Here\platform-tools\adb.exe"
}

function Adb {
    # adb reports on stderr; Windows PowerShell would turn that into a terminating error
    $ErrorActionPreference = 'Continue'
    $a = @()
    if ($script:Serial) { $a += @('-s', $script:Serial) }
    & $script:AdbExe @($a + $args)
}

# ---- Pearl installation -------------------------------------------------------------------------
function Find-Pearl {
    $roots = @()
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        $p = Get-ItemProperty $key -ErrorAction SilentlyContinue
        if ($p.SteamPath) { $roots += $p.SteamPath }
        if ($p.InstallPath) { $roots += $p.InstallPath }
    }
    $libs = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        $libs += $r
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content -Raw $vdf), '"path"\s+"([^"]+)"')) {
                $libs += $m.Groups[1].Value -replace '\\\\', '\'
            }
        }
    }
    foreach ($lib in ($libs | Select-Object -Unique)) {
        $acf = Join-Path $lib 'steamapps\appmanifest_476540.acf'
        if (Test-Path $acf) {
            $m = [regex]::Match((Get-Content -Raw $acf), '"installdir"\s+"([^"]+)"')
            if ($m.Success) {
                $dir = Join-Path $lib ('steamapps\common\' + $m.Groups[1].Value)
                if (Test-Path $dir) { return $dir }
            }
        }
    }
    return $null
}

function Ask-Folder {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = 'Select your Pearl folder (it contains pearl_vrcam, pearlpackage, common, story)'
        if ($dlg.ShowDialog() -eq 'OK') { return $dlg.SelectedPath }
    } catch { }
    return Read-Host 'Path of your Pearl folder'
}

function Test-Pearl($root) {
    $list = Join-Path $Here 'pearl-content.tsv'
    $entries = Get-Content $list | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object {
        $f = $_ -split "`t"
        [pscustomobject]@{ Path = $f[0]; Size = [int64]$f[1]; Sha = $f[2] }
    }
    $bad = @()
    $i = 0
    foreach ($e in $entries) {
        $i++
        if ($i % 200 -eq 0) { Write-Progress -Activity 'Checking Pearl files' -PercentComplete (100 * $i / $entries.Count) }
        $file = Join-Path $root ($e.Path -replace '/', '\')
        if (-not (Test-Path -LiteralPath $file)) { $bad += "missing: $($e.Path)"; continue }
        if ((Get-Item -LiteralPath $file).Length -ne $e.Size) { $bad += "different size: $($e.Path)"; continue }
        if ($VerifyHashes -and (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower() -ne $e.Sha) {
            $bad += "different content: $($e.Path)"
        }
    }
    Write-Progress -Activity 'Checking Pearl files' -Completed
    return , $bad
}

# ---- main ---------------------------------------------------------------------------------------
Write-Host 'Oyster - Pearl for Meta Quest' -ForegroundColor White
$apk = Get-ChildItem -Path $Here -Filter 'oyster-pearl-*.apk' | Select-Object -First 1
if (-not $apk) { Fail "The app (oyster-pearl-*.apk) is missing next to this script." }

if (-not $SkipData) {
    Step 'Pearl installation'
    if (-not $PearlPath) { $PearlPath = Find-Pearl }
    if ($PearlPath) { Write-Host "Found: $PearlPath" }
    else {
        Write-Host 'Pearl was not found in your Steam libraries.'
        $PearlPath = Ask-Folder
    }
    if (-not $PearlPath -or -not (Test-Path (Join-Path $PearlPath 'pearl_vrcam'))) { Fail "No Pearl installation at '$PearlPath'." }
    $bad = Test-Pearl $PearlPath
    if ($bad.Count -gt 0) {
        $bad | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
        Fail "$($bad.Count) files do not match the Steam build 1340090. Verify the files in Steam (Properties > Installed Files) and run again."
    }
    Write-Host ($(if ($VerifyHashes) { 'All 4474 files verified (SHA-256).' } else { 'All 4474 files present (sizes checked; -VerifyHashes checks contents).' }))
}

Step 'Headset'
$script:AdbExe = Find-Adb
$wanted = $Serial
$script:Serial = ''
Adb start-server 2>&1 | Out-Null
while ($true) {
    $lines = Adb devices 2>&1 | ForEach-Object { "$_" } | Select-Object -Skip 1 | Where-Object { $_ -match '\S' }
    $ready = @($lines | Where-Object { $_ -match "`tdevice$" })
    if ($wanted) { $ready = @($ready | Where-Object { $_.StartsWith($wanted) }) }
    if ($ready.Count -eq 1) { $script:Serial = ($ready[0] -split "`t")[0]; break }
    if ($ready.Count -gt 1) { Fail 'More than one device is connected. Run again with -Serial <serial> (see "adb devices").' }
    if ($lines -match 'unauthorized') { Write-Host 'Put on the headset and allow USB debugging for this computer.' }
    else { Write-Host 'Connect the Quest by USB (developer mode on, see README.txt).' }
    Read-Host 'Press Enter to try again' | Out-Null
}
$model = (Adb shell getprop ro.product.model).Trim()
Write-Host "Connected: $model ($Serial)"
if ($model -notmatch 'Quest') { Write-Host 'Warning: this does not look like a Meta Quest.' -ForegroundColor Yellow }

if (-not $SkipData) {
    Step 'Copying Pearl to the headset (about 2.2 GB; unchanged files are skipped on later runs)'
    Adb shell mkdir -p $Remote | Out-Null
    foreach ($f in $Folders) {
        Write-Host "  $f"
        Adb push --sync (Join-Path $PearlPath $f) "$Remote/"
        if ($LASTEXITCODE -ne 0) { Fail "Copying $f failed." }
    }
}

Step "Installing the app ($($apk.Name))"
$out = Adb install -r $apk.FullName 2>&1 | Out-String
if ($out -match 'INSTALL_FAILED_UPDATE_INCOMPATIBLE') {
    Write-Host 'An older build with a different signature is installed: replacing it (Pearl data stays on the headset).'
    Adb uninstall $Package | Out-Null
    $out = Adb install $apk.FullName 2>&1 | Out-String
}
if ($out -notmatch 'Success') { Fail "Installing the app failed:`n$out" }
Adb shell appops set --uid $Package MANAGE_EXTERNAL_STORAGE allow
if ($LASTEXITCODE -ne 0) { Fail 'Granting the app access to /sdcard/Oyster failed.' }

Step 'Done'
Write-Host 'In the headset: Library > Unknown Sources > "Oyster - Pearl". Sit down, the story starts when you are seated.'
Write-Host 'Settings: /sdcard/Oyster/oyster.cfg on the headset (written on the first start, see README.txt).'
