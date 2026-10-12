param([int]$ProcId, [string]$Jmprel, [string]$MapOut)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class TH {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll")] public static extern uint SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] public static extern uint ResumeThread(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr hp, IntPtr addr, byte[] buf, IntPtr n, out IntPtr read);
  [DllImport("kernel32.dll")] public static extern int GetThreadDescription(IntPtr h, out IntPtr desc);
  [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr h);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static ulong[] Sample(IntPtr ht) {
    IntPtr raw = Marshal.AllocHGlobal(1232 + 64); IntPtr ctx = new IntPtr(((long)raw + 63) & ~63L);
    for (int i = 0; i < 1232; i++) Marshal.WriteByte(ctx, i, 0);
    Marshal.WriteInt32(ctx, 48, 0x10001 | 0x2);
    ulong[] r = new ulong[2];
    if (SuspendThread(ht) != 0xFFFFFFFF) { if (GetThreadContext(ht, ctx)) { r[0] = (ulong)Marshal.ReadInt64(ctx, 248); r[1] = (ulong)Marshal.ReadInt64(ctx, 152); } ResumeThread(ht); }
    Marshal.FreeHGlobal(raw); return r;
  }
  public static byte[] Read(IntPtr hp, long addr, int n) { byte[] b = new byte[n]; IntPtr rd; if (!ReadProcessMemory(hp, new IntPtr(addr), b, new IntPtr(n), out rd)) return null; return b; }
  public static string Name(IntPtr h) { IntPtr p; string s = ""; if (GetThreadDescription(h, out p) >= 0 && p != IntPtr.Zero) { s = Marshal.PtrToStringUni(p); LocalFree(p); } return s; }
}
"@
$hp = [TH]::OpenProcess(0x0410, $false, $ProcId)
# --- HLE map from GOT ---
$map = @()
foreach ($line in Get-Content $Jmprel) {
  if ($line -match '^0x([0-9a-f]+)\s+JUMP_SLOT\s+(\S+)\s+(\S+)\s+(.*)$') {
    $off = [Convert]::ToInt64($matches[1], 16); $b = [TH]::Read($hp, 0x5160000 + $off, 8)
    if ($b) { $t = [BitConverter]::ToUInt64($b, 0); $name = $matches[4].Trim(); if ($name -eq '?' -or $name -eq '') { $name = $matches[2] }; $map += [pscustomobject]@{ A=$t; N=$name; L=$matches[3] } }
  }
}
$map = $map | Sort-Object A
$map | ForEach-Object { "{0:x} {1} {2}" -f $_.A, $_.N, $_.L } | Set-Content $MapOut
"HLE map: {0} entries, {1} inside shadPS4.exe" -f $map.Count, ($map | Where-Object { $_.A -ge 0x700000000000 -and $_.A -lt 0x700005177000 }).Count
$hle = $map | Where-Object { $_.A -ge 0x700000000000 -and $_.A -lt 0x700005177000 }
$hleA = @($hle | ForEach-Object { [uint64]$_.A })
function Lab([uint64]$v) {
  if ($v -ge 0x5160000 -and $v -lt 0x10e30000) { return ('eboot+{0:x}' -f ($v - 0x5160000)) }
  if ($v -ge 0x80990000 -and $v -lt 0x80abc000) { return ('libc+{0:x}' -f ($v - 0x80990000)) }
  if ($v -ge 0x8277c000 -and $v -lt 0x82b84000) { return ('rendercore+{0:x}' -f ($v - 0x8277c000)) }
  if ($v -ge 0x80000000 -and $v -lt 0x80190000) { return ('fios2+{0:x}' -f ($v - 0x80000000)) }
  if ($v -ge 0x81ec0000 -and $v -lt 0x81f7c000) { return ('nptk2+{0:x}' -f ($v - 0x81ec0000)) }
  if ($v -ge 0x700000000000 -and $v -lt 0x700005177000) {
    $best = $null; foreach ($e in $hle) { if ($e.A -le $v) { $best = $e } else { break } }
    if ($best -and ($v - $best.A) -lt 0x4000) { return ('shad:{0}+{1:x}' -f $best.N, ($v - $best.A)) }
    return ('shadPS4+{0:x}' -f ($v - 0x700000000000))
  }
  if ($v -ge 0x7fff20f80000 -and $v -lt 0x7fff211e7000) { return ('ntdll+{0:x}' -f ($v - 0x7fff20f80000)) }
  if ($v -ge 0x7fff1e800000 -and $v -lt 0x7fff1ec02000) { return ('kbase+{0:x}' -f ($v - 0x7fff1e800000)) }
  return $null
}
"--- known addresses ---"
foreach ($a in @(0x7000007ab9a0,0x700000780ff0,0x700000791280,0x700000780fa0,0x70000078629a,0x700000779600,0x70000077abe2,0x7000007ad1d1,0x700000780ff9,0x700003871e28,0x700000c9818a,0x700000c9ae9c)) { "  {0:x} = {1}" -f $a, (Lab $a) }
"--- threads ---"
$p = Get-Process -Id $ProcId
foreach ($t in ($p.Threads | Sort-Object Id)) {
  $ht = [TH]::OpenThread(0x005A, $false, [uint32]$t.Id)
  if ($ht -eq [IntPtr]::Zero) { continue }
  $name = [TH]::Name($ht); $s = [TH]::Sample($ht)
  $frames = @(); $buf = [TH]::Read($hp, [long]$s[1], 6144)
  if ($buf) { for ($o = 0; $o -lt 6144 -and $frames.Count -lt 9; $o += 8) { $v = [BitConverter]::ToUInt64($buf, $o); $l = Lab $v; if ($l -and $l -notmatch '^(ntdll|kbase)') { if ($frames.Count -eq 0 -or $frames[-1] -ne $l) { $frames += $l } } } }
  "{0,6} {1,-26} {2,-7} {3,-12} {4,5}s rip={5}" -f $t.Id, $name, $t.ThreadState, $t.WaitReason, [int]$t.TotalProcessorTime.TotalSeconds, (Lab $s[0])
  "       " + ($frames -join ' < ')
  [TH]::CloseHandle($ht) | Out-Null
}
