# Pull the Odin M2 Blue save over MTP (Explorer Shell COM; adb is useless on the Odin).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/odin_mtp_pull.ps1 -Dest <dir>
# Copies every Gambatte *.srm whose name contains "Blue" (edit the -match below for other games).
param([string]$Dest)
$shell = New-Object -ComObject Shell.Application
$pc = $shell.NameSpace(17)
$dev = $null
foreach ($i in $pc.Items()) { if ($i.Name -match 'Odin|ODIN') { $dev = $i } }
if (-not $dev) { foreach ($i in $pc.Items()) { Write-Output ("item: " + $i.Name) }; throw "Odin not found" }
Write-Output ("device: " + $dev.Name)
$root = $dev.GetFolder
$storage = $null
foreach ($i in $root.Items()) { Write-Output ("storage: " + $i.Name); if ($i.Name -match 'Internal') { $storage = $i } }
$saves = $storage.GetFolder.ParseName('Emulation').GetFolder.ParseName('saves').GetFolder
foreach ($core in $saves.Items()) {
  Write-Output ("core: " + $core.Name)
  if ($core.IsFolder) {
    foreach ($f in $core.GetFolder.Items()) {
      Write-Output ("   " + $f.Name + "  " + $core.GetFolder.GetDetailsOf($f, 3))
      if ($f.Name -match 'Blue' -and $f.Name -match '\.srm$') {
        $destF = $shell.NameSpace($Dest)
        $destF.CopyHere($f, 0)
        Start-Sleep -Seconds 3
      }
    }
  }
}
Get-ChildItem $Dest | ForEach-Object { Write-Output ("pulled: " + $_.Name + " " + $_.Length) }
