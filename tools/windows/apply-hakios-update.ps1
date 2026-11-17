# apply-hakios-update.ps1 -- put an update package onto the HakiOS stick, from the laptop.
#
#   powershell -ExecutionPolicy Bypass -File apply-hakios-update.ps1                (latest zip here)
#   powershell -ExecutionPolicy Bypass -File apply-hakios-update.ps1 -Zip .\hakios-gaokun3-update-g12345678.zip
#
# It finds the stick by the HAKIOS_ESP volume label (mounting it if Windows hides it), shows what
# it is about to replace, and copies the package's files into place. The full image is not touched;
# system.apfs and the partitions stay as they are.
param(
    [string]$Zip = ""
)
$ErrorActionPreference = "Stop"

if (-not $Zip) {
    $all = Get-ChildItem -Path . -Filter "hakios-gaokun3-update-*.zip" |
           Sort-Object LastWriteTime -Descending
    if (-not $all) { Write-Error "no hakios-gaokun3-update-*.zip found here; pass -Zip <path>"; exit 1 }
    $Zip = $all[0].FullName
    if ($all.Count -gt 1) {
        Write-Host "several packages here, newest first:"
        foreach ($z in $all) { Write-Host ("  {0}  ({1})" -f $z.Name, $z.LastWriteTime) }
    }
}
Write-Host "package: $Zip"

# say the stamp out loud: the export of the next boot must carry it, or the machine did not take this package
$stampMatch = [regex]::Match((Split-Path -Leaf $Zip), 'g[0-9a-f]{7,8}')
if ($stampMatch.Success) {
    Write-Host ("==> THE STAMP IN THIS PACKAGE: {0}" -f $stampMatch.Value) -ForegroundColor Yellow
    Write-Host ("    after booting, the logdump's kernel.ver line must end in -{0}" -f $stampMatch.Value) -ForegroundColor Yellow
}

# the manifest first: the package says whether the loader changed
$tmp = Join-Path $env:TEMP ("hakios-update-" + [guid]::NewGuid().ToString("N"))
Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
$manifest = Get-Content -Raw (Join-Path $tmp "manifest.txt") -ErrorAction SilentlyContinue
if ($manifest) { Write-Host "`n--- manifest ---`n$manifest`n----------------" }
$loaderChanged = $manifest -match "LOADER:\s*yes"

# find the stick: the HAKIOS_ESP volume, on a removable disk
$vol = Get-Volume -ErrorAction SilentlyContinue | Where-Object { $_.FileSystemLabel -eq "HAKIOS_ESP" }
if (-not $vol) { Write-Error "no volume labelled HAKIOS_ESP found -- is the stick plugged in?"; exit 1 }
if (-not $vol.DriveLetter) {
    Write-Host "the ESP has no letter; mounting it..."
    $part = Get-Partition -Volume $vol
    foreach ($c in @('S','T','U','V')) {
        if (-not (Get-PSDrive -Name $c -ErrorAction SilentlyContinue)) {
            try {
                Add-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath "$c`:\" -ErrorAction Stop
                break
            } catch { }
        }
    }
    $part = Get-Partition -Volume $vol
    if (-not $part.DriveLetter) { Write-Error "could not mount HAKIOS_ESP; do it by hand in DiskGenius"; exit 1 }
}
$root = "$([char]$part.DriveLetter):\"
Write-Host "target: stick volume $root"

$payload = Join-Path $tmp "EFI\hakios\payload.bin"
if (-not (Test-Path $payload)) { Write-Error "package has no EFI\hakios\payload.bin"; exit 1 }
$dest = Join-Path $root "EFI\hakios\payload.bin"
Write-Host "`nreplacing $dest"
Write-Host "  old: $((Get-Item $dest -ErrorAction SilentlyContinue).Length) bytes  new: $((Get-Item $payload).Length) bytes"
Copy-Item -Force $payload $dest

if ($loaderChanged) {
    $ldr = Join-Path $tmp "EFI\BOOT\BOOTAA64.EFI"
    $dest2 = Join-Path $root "EFI\BOOT\BOOTAA64.EFI"
    Write-Host "replacing $dest2 (manifest says the loader changed)"
    Copy-Item -Force $ldr $dest2
} else {
    Write-Host "loader unchanged; BOOTAA64.EFI left alone"
}

# verify by hash against the package's own bytes
function HashOf($p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLower() }
$ok = (HashOf $dest) -eq (HashOf $payload)
if ($loaderChanged) { $ok = $ok -and ((HashOf $dest2) -eq (HashOf $ldr)) }
Remove-Item -Recurse -Force $tmp
if ($ok) { Write-Host "`nOK -- the stick now carries this package. Boot it." }
else { Write-Host "`nWARNING: hashes differ after copy -- read the files back or redo by hand." ; exit 1 }
