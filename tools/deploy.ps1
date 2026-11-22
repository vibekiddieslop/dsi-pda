<#
Copies the PDA to an SD card root (a drive letter or the melonDS sdroot folder).

  .\tools\deploy.ps1                       # -> the repo's sdroot folder (melonDS)
  .\tools\deploy.ps1 -Target E:\           # -> real SD card
  .\tools\deploy.ps1 -TestBoot             # also installs testboot.nds as BOOT.NDS
                                           # and pda\apps\TESTAPP.NDS (emulator only!)
Never use -TestBoot on a real SD card: it replaces TWiLight Menu++'s BOOT.NDS.
#>
param(
    [string]$Target = (Join-Path $PSScriptRoot "..\sdroot"),
    [switch]$TestBoot
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$rom = Join-Path $root "pda.nds"
if (-not (Test-Path $rom)) { throw "pda.nds not built. Run tools/build.sh in WSL first." }

if (-not (Test-Path $Target)) { New-Item -ItemType Directory $Target | Out-Null }
Copy-Item $rom (Join-Path $Target "PDA.NDS") -Force
Write-Output "PDA.NDS -> $Target"

if ($TestBoot) {
    $tb = Join-Path $root "tools\testboot\testboot.nds"
    if (-not (Test-Path $tb)) { throw "testboot.nds not built." }
    $boot = Join-Path $Target "BOOT.NDS"
    # A size check is not enough: TWiLight Menu++'s BOOT.NDS is a ~300 KB
    # stub. Only the emulator folders inside this repo, a card without a
    # BOOT.NDS, or one whose BOOT.NDS is already this test ROM may be written.
    if (Test-Path $boot) {
        $inRepo = (Resolve-Path $Target).Path.TrimEnd('\').StartsWith(
            $root.Path.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
        $same = (Get-FileHash $boot).Hash -eq (Get-FileHash $tb).Hash
        if (-not $inRepo -and -not $same) {
            throw "$boot is not the test ROM (it may be TWiLight Menu++). Refusing to overwrite."
        }
    }
    Copy-Item $tb $boot -Force
    $apps = Join-Path $Target "pda\apps"
    New-Item -ItemType Directory -Force $apps | Out-Null
    Copy-Item $tb (Join-Path $apps "TESTAPP.NDS") -Force
    Write-Output "testboot.nds -> BOOT.NDS and pda\apps\TESTAPP.NDS"
}
