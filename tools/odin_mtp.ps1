# Pull or push an emulator save on an Android handheld over MTP (Windows Shell COM).
#
# Why MTP and why this dance: on the Ayn Odin adb can't reach storage, and MTP
# silently refuses to OVERWRITE a file. So a push renames the device's current
# file in place (it stays on the device as a backup), copies the new file in
# under the original name, then pulls it back and compares hashes.
#
# Close the game in RetroArch first: it writes SRAM to the .srm on close and
# would undo a push. After a push, load the game from the title screen
# (Continue), never from a save state.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/odin_mtp.ps1 -Action pull -Name "Pokemon - Crystal Version (USA, Europe) (Rev 1)" -Dest C:\path\dir
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/odin_mtp.ps1 -Action push -Name "..." -Source C:\path\file.sav -Tag gsball -ExpectDeviceMd5 <md5>
#
#   -Device    name match for the device under This PC (default 'Odin')
#   -Core      emulator core folder under Emulation/saves (default 'Gambatte')
#   -Ext       save extension on the device (default 'srm'; the PC copy can be .sav)
#   -ExpectDeviceMd5  push refuses unless the device file still has this hash,
#              so a save made on the device since your last pull is never lost
param(
  [ValidateSet('pull', 'push')][string]$Action,
  [string]$Name,
  [string]$Dest,
  [string]$Source,
  [string]$Tag = 'prepush',
  [string]$ExpectDeviceMd5,
  [string]$Device = 'Odin',
  [string]$Core = 'Gambatte',
  [string]$Ext = 'srm',
  [int]$TimeoutSec = 120
)
$ErrorActionPreference = 'Stop'
if (-not $Name) { throw '-Name is required (file name without extension)' }

$job = Start-Job -ArgumentList $Action, $Name, $Dest, $Source, $Tag, $ExpectDeviceMd5, $Device, $Core, $Ext -ScriptBlock {
  param($Action, $Name, $Dest, $Source, $Tag, $ExpectDeviceMd5, $Device, $Core, $Ext)
  $ErrorActionPreference = 'Stop'
  $shell = New-Object -ComObject Shell.Application

  function Get-CoreFolder {
    $dev = $null
    foreach ($i in $shell.NameSpace(17).Items()) { if ($i.Name -match $Device) { $dev = $i } }
    if (-not $dev) { throw "device matching '$Device' not found under This PC (USB mode must be File transfer)" }
    $storage = $null
    foreach ($i in $dev.GetFolder.Items()) { if ($i.Name -match 'Internal') { $storage = $i } }
    if (-not $storage) { throw 'internal storage not visible (unlock the device?)' }
    # ParseName on exact names: enumerating MTP folders can return partial lists.
    $f = $storage.GetFolder.ParseName('Emulation').GetFolder.ParseName('saves').GetFolder.ParseName($Core)
    if (-not $f) { throw "core folder '$Core' not found" }
    return $f.GetFolder
  }

  function Copy-FromDevice($folder, $fileName, $destDir) {
    $item = $folder.ParseName($fileName)
    if (-not $item) { throw "no '$fileName' on the device" }
    New-Item -ItemType Directory -Force $destDir | Out-Null
    $target = Join-Path $destDir $fileName
    if (Test-Path $target) { Remove-Item $target -Force }
    $shell.NameSpace($destDir).CopyHere($item, 16)
    for ($i = 0; $i -lt 60 -and -not (Test-Path $target); $i++) { Start-Sleep -Milliseconds 500 }
    Start-Sleep -Seconds 1
    if (-not (Test-Path $target)) { throw "copy of '$fileName' never arrived" }
    return $target
  }

  $file = "$Name.$Ext"
  $folder = Get-CoreFolder
  if ($Action -eq 'pull') {
    $p = Copy-FromDevice $folder $file $Dest
    "pulled $p md5=" + (Get-FileHash $p -Algorithm MD5).Hash.ToLower()
    $rtc = $folder.ParseName("$Name.rtc")   # Gen 2 clock file, if the core keeps one
    if ($rtc) { $r = Copy-FromDevice $folder "$Name.rtc" $Dest; "pulled $r md5=" + (Get-FileHash $r -Algorithm MD5).Hash.ToLower() }
    return
  }

  # push
  if (-not (Test-Path $Source)) { throw "source '$Source' not found" }
  $tmp = Join-Path ([IO.Path]::GetTempPath()) ("mtp-" + [guid]::NewGuid())
  $cur = Copy-FromDevice $folder $file (Join-Path $tmp 'before')
  $curMd5 = (Get-FileHash $cur -Algorithm MD5).Hash.ToLower()
  "device file before push md5=$curMd5"
  if ($ExpectDeviceMd5 -and $curMd5 -ne $ExpectDeviceMd5.ToLower()) {
    throw "device save changed since your pull (expected $ExpectDeviceMd5). Pull again and redo the edit on that file."
  }
  $stamp = Get-Date -Format 'yyyyMMdd'
  $backupName = "$Name.pre-$Tag-$stamp.$Ext"
  if ($folder.ParseName($backupName)) { $backupName = "$Name.pre-$Tag-" + (Get-Date -Format 'yyyyMMdd-HHmm') + ".$Ext" }
  # Keep the extension so Explorer doesn't pop an extension-change dialog.
  $folder.ParseName($file).Name = $backupName
  Start-Sleep -Seconds 2
  if ($folder.ParseName($file)) { throw "rename didn't take; '$file' is still on the device (a dialog may be waiting on screen)" }
  "device backup: $backupName"
  $stage = Join-Path $tmp 'stage'
  New-Item -ItemType Directory -Force $stage | Out-Null
  Copy-Item $Source (Join-Path $stage $file)
  $folder.CopyHere((Join-Path $stage $file), 16)
  for ($i = 0; $i -lt 60 -and -not $folder.ParseName($file); $i++) { Start-Sleep -Milliseconds 500 }
  Start-Sleep -Seconds 2
  $back = Copy-FromDevice $folder $file (Join-Path $tmp 'after')
  $want = (Get-FileHash $Source -Algorithm MD5).Hash.ToLower()
  $got = (Get-FileHash $back -Algorithm MD5).Hash.ToLower()
  if ($want -ne $got) { throw "VERIFY FAILED: device has $got, expected $want. The old save is on the device as $backupName." }
  "pushed + verified md5=$got"
  Remove-Item $tmp -Recurse -Force
}
if (Wait-Job $job -Timeout $TimeoutSec) {
  Receive-Job $job
} else {
  Stop-Job $job
  throw "timed out after $TimeoutSec s (an Explorer dialog may be waiting on screen, or MTP is wedged: replug the device)"
}
