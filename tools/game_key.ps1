# Sends keyboard keys to the running game window (foreground + keybd_event) and optionally saves a
# scaled screenshot of the window. Usage: game_key.ps1 -Keys "n" [-Shot logs\x.png] [-WaitMs 4000]
param(
    [string]$Keys = '',
    [string]$Shot = '',
    [int]$WaitMs = 4000,
    [string]$ProcessName = 'nhl24'
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class GK {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int n);
  [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out GKRECT r);
}
[StructLayout(LayoutKind.Sequential)] public struct GKRECT { public int L, T, R, B; }
"@
$game = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { throw "$ProcessName is not running" }
[void][GK]::ShowWindow($game.MainWindowHandle, 9)
[void][GK]::SetForegroundWindow($game.MainWindowHandle)
Start-Sleep -Milliseconds 500
foreach ($k in $Keys.ToCharArray()) {
    if ($k -eq ' ') { Start-Sleep -Milliseconds 700; continue }
    $vk = [byte][char]::ToUpper($k)
    [GK]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    [GK]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    "pressed $k"
}
if ($Shot) {
    Start-Sleep -Milliseconds $WaitMs
    Add-Type -AssemblyName System.Drawing
    $r = New-Object GKRECT
    [void][GK]::GetWindowRect($game.MainWindowHandle, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $g.Dispose()
    $sh = [int](640 * $bmp.Height / $bmp.Width)
    $small = New-Object System.Drawing.Bitmap -ArgumentList 640, $sh
    $g2 = [System.Drawing.Graphics]::FromImage($small)
    $g2.DrawImage($bmp, 0, 0, $small.Width, $small.Height)
    $g2.Dispose()
    $small.Save($Shot)
    $bmp.Dispose(); $small.Dispose()
    "saved $Shot"
}
