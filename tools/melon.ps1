<#
Drives melonDS for smoke tests without touching the desktop: the window is
kept behind other windows, input goes in as window messages, and screenshots
come from PrintWindow. The mouse cursor, keyboard focus and other windows are
never used.

  .\tools\melon.ps1 start [rom]          # default rom: sdroot\PDA.NDS
  .\tools\melon.ps1 shot out.png         # both DS screens, at the window's scale
  .\tools\melon.ps1 key A [holdMs]       # A B X Y L R START SELECT UP DOWN LEFT RIGHT LID
  .\tools\melon.ps1 tap 128 96           # bottom-screen pixel (0..255, 0..191)
  .\tools\melon.ps1 type "buy milk`n"    # big-key keyboard: a-z 0-9 space . , - / : and `n
  .\tools\melon.ps1 drag "x,y x,y ..."   # one stylus stroke through bottom-screen points
  .\tools\melon.ps1 mode ds|dsi          # DS + DLDI (fat:/) or DSi + DSi SD (sd:/)
  .\tools\melon.ps1 close                # graceful close (writes the SD folder back)
Key map matches emu\melonDS-1.1\melonDS.toml.
#>
param([string]$Cmd, [string]$A1, [string]$A2)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "emu\melonDS-1.1\melonDS.exe"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MW {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr ChildWindowFromPointEx(IntPtr h, POINT p, uint flags);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[MW]::SetProcessDPIAware() | Out-Null

$SWP_NOSIZE = 0x1; $SWP_NOMOVE = 0x2; $SWP_NOACTIVATE = 0x10
$HWND_BOTTOM = [IntPtr]1

function Get-Win {
    $p = Get-Process melonDS -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { throw "melonDS is not running" }
    $h = $p.MainWindowHandle
    if ([MW]::IsIconic($h)) {
        [MW]::ShowWindow($h, 4) | Out-Null   # SW_SHOWNOACTIVATE
        Start-Sleep -Milliseconds 300
    }
    # Behind every other window; PrintWindow still sees it.
    [MW]::SetWindowPos($h, $HWND_BOTTOM, 0, 0, 0, 0, ($SWP_NOSIZE -bor $SWP_NOMOVE -bor $SWP_NOACTIVATE)) | Out-Null
    return $h
}

# Client area as a bitmap, rendered by the window itself.
function Get-ClientBitmap($h) {
    $wr = New-Object MW+RECT; [MW]::GetWindowRect($h, [ref]$wr) | Out-Null
    $cr = New-Object MW+RECT; [MW]::GetClientRect($h, [ref]$cr) | Out-Null
    $pt = New-Object MW+POINT; [MW]::ClientToScreen($h, [ref]$pt) | Out-Null
    $ww = $wr.R - $wr.L; $wh = $wr.B - $wr.T
    $full = New-Object System.Drawing.Bitmap $ww, $wh
    $g = [System.Drawing.Graphics]::FromImage($full)
    $hdc = $g.GetHdc()
    [MW]::PrintWindow($h, $hdc, 2) | Out-Null   # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    $rect = New-Object System.Drawing.Rectangle ($pt.X - $wr.L), ($pt.Y - $wr.T), $cr.R, $cr.B
    $client = $full.Clone($rect, $full.PixelFormat)
    $full.Dispose()
    return $client
}

# DS screens inside the client area: 4:3 each, stacked, centered below the menu bar.
function Get-Layout($h, $bmp) {
    $w = $bmp.Width; $hgt = $bmp.Height
    $menuColor = $bmp.GetPixel(2, 2)
    $top = 0
    for ($y = 0; $y -lt [Math]::Min(80, $hgt); $y++) {
        $c = $bmp.GetPixel([int]($w / 2), $y)
        if ([Math]::Abs($c.R - $menuColor.R) + [Math]::Abs($c.G - $menuColor.G) + [Math]::Abs($c.B - $menuColor.B) -gt 40) { $top = $y; break }
    }
    $areaH = $hgt - $top
    $scale = [Math]::Min($w / 256.0, $areaH / 384.0)
    $sw = 256 * $scale; $sh = 192 * $scale
    return @{ X = ($w - $sw) / 2; Y = $top + ($areaH - 2 * $sh) / 2; S = $scale; SH = $sh }
}

function MakeLParam([int]$lo, [int]$hi) { return [IntPtr](($hi -shl 16) -bor ($lo -band 0xFFFF)) }

$keys = @{ A = 0x58; B = 0x5A; X = 0x53; Y = 0x41; L = 0x51; R = 0x57; START = 0x0D; SELECT = 0x08;
           UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27; LID = 0x23 }

function Send-Key($h, $name, $hold) {
    $vk = [uint32]$keys[$name.ToUpper()]
    if (-not $vk) { throw "unknown key $name" }
    $scan = [MW]::MapVirtualKey($vk, 0)
    $ext = if ($vk -ge 0x21 -and $vk -le 0x28) { 1 } else { 0 }
    $down = 1 -bor ($scan -shl 16) -bor ($ext -shl 24)
    $up = $down -bor (1 -shl 30) -bor (1 -shl 31)
    [MW]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr]$down) | Out-Null
    Start-Sleep -Milliseconds $hold
    [MW]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr][int64]([uint32]$up)) | Out-Null
    Start-Sleep -Milliseconds 150
}

function Send-Tap($h, $L, $dx, $dy) {
    $cx = [int]($L.X + ($dx + 0.5) * $L.S)
    $cy = [int]($L.Y + $L.SH + ($dy + 0.5) * $L.S)
    # Deliver to whichever (possibly child) window owns that point.
    $pt = New-Object MW+POINT; $pt.X = $cx; $pt.Y = $cy
    $target = [MW]::ChildWindowFromPointEx($h, $pt, 1)
    if ($target -eq [IntPtr]::Zero) { $target = $h }
    if ($target -ne $h) {
        [MW]::ClientToScreen($h, [ref]$pt) | Out-Null
        [MW]::ScreenToClient($target, [ref]$pt) | Out-Null
    }
    $lp = MakeLParam $pt.X $pt.Y
    [MW]::PostMessage($target, 0x200, [IntPtr]0, $lp) | Out-Null
    [MW]::PostMessage($target, 0x201, [IntPtr]1, $lp) | Out-Null
    Start-Sleep -Milliseconds 100
    [MW]::PostMessage($target, 0x202, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 150
}

# Press at the first point, move through the rest (interpolated every ~4 px,
# one move per frame), release at the last.
function Send-Drag($h, $L, $pts) {
    function ToClient($dx, $dy) {
        $p = New-Object MW+POINT
        $p.X = [int]($L.X + ($dx + 0.5) * $L.S); $p.Y = [int]($L.Y + $L.SH + ($dy + 0.5) * $L.S)
        return $p
    }
    $first = ToClient $pts[0][0] $pts[0][1]
    $target = [MW]::ChildWindowFromPointEx($h, $first, 1)
    if ($target -eq [IntPtr]::Zero) { $target = $h }
    function Post($msg, $w, $dx, $dy) {
        $p = ToClient $dx $dy
        if ($target -ne $h) { [MW]::ClientToScreen($h, [ref]$p) | Out-Null; [MW]::ScreenToClient($target, [ref]$p) | Out-Null }
        [MW]::PostMessage($target, $msg, [IntPtr]$w, (MakeLParam $p.X $p.Y)) | Out-Null
    }
    Post 0x200 0 $pts[0][0] $pts[0][1]
    Post 0x201 1 $pts[0][0] $pts[0][1]
    Start-Sleep -Milliseconds 50
    for ($i = 1; $i -lt $pts.Count; $i++) {
        $ax = $pts[$i - 1][0]; $ay = $pts[$i - 1][1]; $bx = $pts[$i][0]; $by = $pts[$i][1]
        $steps = [Math]::Max(1, [int]([Math]::Sqrt(($bx - $ax) * ($bx - $ax) + ($by - $ay) * ($by - $ay)) / 4))
        for ($s = 1; $s -le $steps; $s++) {
            Post 0x200 1 ($ax + ($bx - $ax) * $s / $steps) ($ay + ($by - $ay) * $s / $steps)
            Start-Sleep -Milliseconds 17
        }
    }
    $last = $pts[$pts.Count - 1]
    Post 0x202 0 $last[0] $last[1]
    Start-Sleep -Milliseconds 150
}

switch ($Cmd) {
    "start" {
        $rom = if ($A1) { $A1 } else { Join-Path $root "sdroot\PDA.NDS" }
        # Minimized start: melonDS never takes focus from the user.
        Start-Process -FilePath $exe -ArgumentList "`"$rom`"" -WindowStyle Minimized | Out-Null
        Start-Sleep -Seconds 6
        Get-Win | Out-Null
        "started $rom"
    }
    "shot" {
        $h = Get-Win
        $bmp = Get-ClientBitmap $h
        $L = Get-Layout $h $bmp
        $rect = New-Object System.Drawing.Rectangle ([int]$L.X), ([int]$L.Y), ([int](256 * $L.S)), ([int](384 * $L.S))
        $out = $bmp.Clone($rect, $bmp.PixelFormat)
        $out.Save($A1, [System.Drawing.Imaging.ImageFormat]::Png)
        "saved $A1 (scale $($L.S))"
    }
    "key" {
        $h = Get-Win
        $hold = if ($A2) { [int]$A2 } else { 80 }
        Send-Key $h $A1 $hold
        "key $A1"
    }
    "tap" {
        $h = Get-Win
        $bmp = Get-ClientBitmap $h
        $L = Get-Layout $h $bmp
        Send-Tap $h $L ([double]$A1) ([double]$A2)
        "tap $A1,$A2"
    }
    "type" {
        $h = Get-Win
        $bmp = Get-ClientBitmap $h
        $L = Get-Layout $h $bmp
        # Big-key keyboard (source/input/layout.c): rows of 38 px from y = 40.
        # Letters type in lower case unless the PDA auto-capitalizes.
        function KeyX($x0, $x1, $n, $i) { [int](($x0 + ($x1 - $x0) * $i / $n + $x0 + ($x1 - $x0) * ($i + 1) / $n) / 2) }
        $letters = @(
            @{ Y = 59;  X0 = 0;  X1 = 256; Keys = "qwertyuiop" },
            @{ Y = 97;  X0 = 13; X1 = 243; Keys = "asdfghjkl" },
            @{ Y = 135; X0 = 38; X1 = 218; Keys = "zxcvbnm" }
        )
        $symbols = @(
            @{ Y = 59; X0 = 0; X1 = 256; Keys = "1234567890" },
            @{ Y = 97; X0 = 0; X1 = 256; Keys = "-/:;()$&@`"" }
        )
        foreach ($ch in $A1.ToCharArray()) {
            if ($ch -eq ' ') { Send-Tap $h $L 135 173; continue }
            if ($ch -eq "`n") { Send-Tap $h $L 230 173; continue }
            if ($ch -eq ',') { Send-Tap $h $L 79 173; continue }
            if ($ch -eq '.') { Send-Tap $h $L 191 173; continue }
            $done = $false
            foreach ($r in $letters) {
                $i = $r.Keys.IndexOf([string]$ch)
                if ($i -ge 0) { Send-Tap $h $L (KeyX $r.X0 $r.X1 $r.Keys.Length $i) $r.Y; $done = $true; break }
            }
            if ($done) { continue }
            foreach ($r in $symbols) {
                $i = $r.Keys.IndexOf([string]$ch)
                if ($i -ge 0) {
                    Send-Tap $h $L 18 173   # 123
                    Send-Tap $h $L (KeyX $r.X0 $r.X1 $r.Keys.Length $i) $r.Y
                    Send-Tap $h $L 18 173   # abc
                    $done = $true; break
                }
            }
            if (-not $done) { throw "cannot type '$ch'" }
        }
        "typed '$A1'"
    }
    "drag" {
        $h = Get-Win
        $bmp = Get-ClientBitmap $h
        $L = Get-Layout $h $bmp
        $pts = @($A1.Trim() -split '\s+' | ForEach-Object { $xy = $_ -split ','; ,@([double]$xy[0], [double]$xy[1]) })
        Send-Drag $h $L $pts
        "drag $($pts.Count) points"
    }
    "mode" {
        $toml = Join-Path $root "emu\melonDS-1.1\melonDS.toml"
        $v = if ($A1 -eq "dsi") { 1 } else { 0 }
        $c = Get-Content $toml -Raw
        $c = [regex]::Replace($c, "(?m)^ConsoleType = \d+", "ConsoleType = $v")
        # DLDI (fat:/) only in DS mode; DSi mode uses the DSi SD folder (sd:/).
        $dl = if ($v -eq 1) { "false" } else { "true" }
        $c = [regex]::Replace($c, "(?ms)(^\[DLDI\][^\[]*?^Enable = )\w+", "`${1}$dl")
        [IO.File]::WriteAllText($toml, $c)
        "ConsoleType = $v"
    }
    "layout" {
        $h = Get-Win
        $bmp = Get-ClientBitmap $h
        Get-Layout $h $bmp | Format-Table -AutoSize | Out-String
    }
    "close" {
        $p = Get-Process melonDS -ErrorAction SilentlyContinue
        foreach ($q in $p) { $q.CloseMainWindow() | Out-Null; $q.WaitForExit(10000) | Out-Null }
        "closed"
    }
    default { throw "usage: melon.ps1 start|shot|key|tap|type|mode|close" }
}
