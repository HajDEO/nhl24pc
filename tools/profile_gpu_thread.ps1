# Statistical profiler for one thread of a running shadPS4/nhlport process (default: the GPU command
# processor thread). Samples RIP and the first return addresses on the stack, then symbolizes the
# emulator-side addresses with llvm-symbolizer against the exe + PDB (RelWithDebInfo build).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\profile_gpu_thread.ps1 `
#       -Exe ..\build\nhl24.exe -Samples 3000 -IntervalMs 2 -Out logs\prof_gpu.txt
#
# Output: self-time histogram (RIP symbol), inclusive histogram over the top stack frames, raw data.
param(
    [string]$ThreadName = 'shadPS4:GpuCommandProcessor',
    [string]$Exe = (Join-Path $PSScriptRoot '..\build\nhl24.exe'),
    [int]$Samples = 3000,
    [int]$IntervalMs = 2,
    [int]$StackBytes = 2048,
    [string]$Out = (Join-Path $PSScriptRoot '..\logs\prof_gpu.txt'),
    [string]$Symbolizer = 'C:\Program Files\LLVM\bin\llvm-symbolizer.exe'
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class PR {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll")] public static extern uint SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] public static extern uint ResumeThread(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr hp, IntPtr addr, byte[] buf, IntPtr n, out IntPtr read);
  [DllImport("kernel32.dll")] public static extern int GetThreadDescription(IntPtr h, out IntPtr desc);
  [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr h);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static string Name(IntPtr h) { IntPtr p; string s = ""; if (GetThreadDescription(h, out p) >= 0 && p != IntPtr.Zero) { s = Marshal.PtrToStringUni(p); LocalFree(p); } return s; }
  // returns rip, rsp
  public static ulong[] Sample(IntPtr ht) {
    IntPtr raw = Marshal.AllocHGlobal(1232 + 64); IntPtr ctx = new IntPtr(((long)raw + 63) & ~63L);
    for (int i = 0; i < 1232; i++) Marshal.WriteByte(ctx, i, 0);
    Marshal.WriteInt32(ctx, 48, 0x10001);
    ulong[] r = new ulong[2];
    if (SuspendThread(ht) != 0xFFFFFFFF) { if (GetThreadContext(ht, ctx)) { r[0] = (ulong)Marshal.ReadInt64(ctx, 248); r[1] = (ulong)Marshal.ReadInt64(ctx, 152); } ResumeThread(ht); }
    Marshal.FreeHGlobal(raw); return r;
  }
  public static byte[] Read(IntPtr hp, long addr, int n) { byte[] b = new byte[n]; IntPtr rd; if (!ReadProcessMemory(hp, new IntPtr(addr), b, new IntPtr(n), out rd)) return null; return b; }
}
"@
$proc = Get-Process ([IO.Path]::GetFileNameWithoutExtension($Exe)) | Select-Object -First 1
if (-not $proc) { throw "$Exe is not running" }
$exeMod = $proc.Modules | Where-Object { $_.ModuleName -ieq ([IO.Path]::GetFileName($Exe)) } | Select-Object -First 1
$base = [int64]$exeMod.BaseAddress; $end = $base + $exeMod.ModuleMemorySize
$hp = [PR]::OpenProcess(0x0410, $false, $proc.Id)
$tid = 0
foreach ($t in $proc.Threads) {
    $h = [PR]::OpenThread(0x0040, $false, [uint32]$t.Id)
    if ($h -ne [IntPtr]::Zero) { $n = [PR]::Name($h); [PR]::CloseHandle($h) | Out-Null; if ($n -eq $ThreadName) { $tid = $t.Id; break } }
}
if ($tid -eq 0) { throw "thread '$ThreadName' not found" }
$ht = [PR]::OpenThread(0x005A, $false, [uint32]$tid)
Write-Host ("sampling thread {0} ({1}) of pid {2}, exe base {3:x}, {4} samples" -f $tid, $ThreadName, $proc.Id, $base, $Samples)

$mods = @($proc.Modules | ForEach-Object { [pscustomobject]@{ N = $_.ModuleName; S = [int64]$_.BaseAddress; E = [int64]$_.BaseAddress + $_.ModuleMemorySize } })
function ModOf([int64]$a) { foreach ($m in $mods) { if ($a -ge $m.S -and $a -lt $m.E) { return $m.N } }; if ($a -ge 0x5000000 -and $a -lt 0x100000000) { return 'guest code' }; return 'other' }
$self = @{}; $incl = @{}; $other = 0; $modHist = @{}; $via = @{}; $viaMod = @{}
for ($i = 0; $i -lt $Samples; $i++) {
    $s = [PR]::Sample($ht); $rip = [int64]$s[0]
    $inExe = ($rip -ge $base -and $rip -lt $end)
    if ($inExe) { $k = $rip - $base; if ($self.ContainsKey($k)) { $self[$k]++ } else { $self[$k] = 1 } } else { $other++; $mn = ModOf $rip; if ($modHist.ContainsKey($mn)) { $modHist[$mn]++ } else { $modHist[$mn] = 1 } }
    $buf = [PR]::Read($hp, [int64]$s[1], $StackBytes)
    if ($buf) {
        $seen = @{}; $first = -1
        for ($o = 0; $o -lt $StackBytes; $o += 8) {
            $v = [BitConverter]::ToInt64($buf, $o)
            if ($v -ge $base -and $v -lt $end) { $k = $v - $base; if ($first -lt 0) { $first = $k }; if (-not $seen.ContainsKey($k)) { $seen[$k] = 1; if ($incl.ContainsKey($k)) { $incl[$k]++ } else { $incl[$k] = 1 } } }
        }
        if (-not $inExe -and $first -ge 0) { if ($via.ContainsKey($first)) { $via[$first]++ } else { $via[$first] = 1 }; $mk = (ModOf $rip) + '|' + $first; if ($viaMod.ContainsKey($mk)) { $viaMod[$mk]++ } else { $viaMod[$mk] = 1 } }
    }
    Start-Sleep -Milliseconds $IntervalMs
}
[PR]::CloseHandle($ht) | Out-Null
Write-Host ("done: {0} samples in exe, {1} outside (driver/ntdll/game code)" -f ($Samples - $other), $other)

# Symbolize: llvm-symbolizer wants virtual addresses relative to the PE's preferred image base.
$imageBase = 0x140000000
$pe = [IO.File]::ReadAllBytes($Exe)
$peOff = [BitConverter]::ToInt32($pe, 0x3c); $optOff = $peOff + 24
if ([BitConverter]::ToUInt16($pe, $optOff) -eq 0x20b) { $imageBase = [BitConverter]::ToInt64($pe, $optOff + 24) }
$addrs = @($self.Keys) + @($incl.Keys) + @($via.Keys) | Sort-Object -Unique
$tmp = [IO.Path]::GetTempFileName()
($addrs | ForEach-Object { '0x{0:x}' -f ($imageBase + $_) }) | Set-Content $tmp
$symLines = & $Symbolizer --obj="$Exe" --functions=short --inlining=false --demangle @("@$tmp") 2>$null
Remove-Item $tmp -Force
# llvm-symbolizer prints "function\nfile:line\n\n" per address
$symOf = @{}; $idx = 0; $cur = @()
foreach ($line in $symLines) {
    if ($line -eq '') { if ($idx -lt $addrs.Count) { $symOf[$addrs[$idx]] = ($cur -join ' @ '); $idx++ }; $cur = @() } else { $cur += $line }
}
$fn = @{}; foreach ($kv in $self.GetEnumerator()) { $name = $symOf[$kv.Key]; if (-not $name) { $name = ('0x{0:x}' -f $kv.Key) }; $name = ($name -split ' @ ')[0]; if ($fn.ContainsKey($name)) { $fn[$name] += $kv.Value } else { $fn[$name] = $kv.Value } }
$fi = @{}; foreach ($kv in $incl.GetEnumerator()) { $name = $symOf[$kv.Key]; if (-not $name) { $name = ('0x{0:x}' -f $kv.Key) }; $name = ($name -split ' @ ')[0]; if ($fi.ContainsKey($name)) { $fi[$name] += $kv.Value } else { $fi[$name] = $kv.Value } }

$fv = @{}; foreach ($kv in $via.GetEnumerator()) { $name = $symOf[$kv.Key]; if (-not $name) { $name = ('0x{0:x}' -f $kv.Key) }; $name = ($name -split ' @ ')[0]; if ($fv.ContainsKey($name)) { $fv[$name] += $kv.Value } else { $fv[$name] = $kv.Value } }
$report = @()
$report += ("# profile of {0}: {1} samples, {2} in shadPS4.exe, {3} elsewhere (driver / ntdll / guest code), interval {4} ms" -f $ThreadName, $Samples, ($Samples - $other), $other, $IntervalMs)
$report += ''; $report += '## samples outside shadPS4.exe by module'
$modHist.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { $report += ('{0,6} {1,5:P1}  {2}' -f $_.Value, ($_.Value / $Samples), $_.Key) }
$fvm = @{}; foreach ($kv in $viaMod.GetEnumerator()) { $parts = $kv.Key -split '\|'; $name = $symOf[[int64]$parts[1]]; if (-not $name) { $name = ('0x{0:x}' -f [int64]$parts[1]) }; $name = $parts[0] + ' <- ' + (($name -split ' @ ')[0]); if ($fvm.ContainsKey($name)) { $fvm[$name] += $kv.Value } else { $fvm[$name] = $kv.Value } }
$report += ''; $report += '## outside samples by module <- nearest shadPS4.exe caller, top 40'
$fvm.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 40 | ForEach-Object { $report += ('{0,6} {1,5:P1}  {2}' -f $_.Value, ($_.Value / $Samples), $_.Key) }
$report += ''; $report += '## outside samples attributed to the nearest shadPS4.exe caller on the stack, top 40'
$fv.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 40 | ForEach-Object { $report += ('{0,6} {1,5:P1}  {2}' -f $_.Value, ($_.Value / $Samples), $_.Key) }
$report += ''; $report += '## self time (RIP inside shadPS4.exe), top 40'
$fn.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 40 | ForEach-Object { $report += ('{0,6} {1,5:P1}  {2}' -f $_.Value, ($_.Value / $Samples), $_.Key) }
$report += ''; $report += '## inclusive (function appears on the sampled stack), top 60'
$fi.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 60 | ForEach-Object { $report += ('{0,6} {1,5:P1}  {2}' -f $_.Value, ($_.Value / $Samples), $_.Key) }
$report += ''; $report += '## self time by address with file:line, top 40'
$self.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 40 | ForEach-Object { $report += ('{0,6}  rva 0x{1:x}  {2}' -f $_.Value, $_.Key, $symOf[$_.Key]) }
$report | Set-Content $Out -Encoding UTF8
Write-Host "report written to $Out"
$report | Select-Object -First 50
