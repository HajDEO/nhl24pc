# Reads memory of a running process (ReadProcessMemory) without stopping it:
#   -Read  <hexAddr> <bytes>        print bytes (hex) and floats at the address
#   -Find  <hexPattern>             scan committed regions for the little-endian byte pattern
#                                   (hex string, e.g. "e8e5a6c92101" or a pointer given as -Ptr <hexValue>)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\memscan.ps1 -ProcessName nhl24 -Read 0x10ba61528 -Bytes 16
#   powershell ... -File tools\memscan.ps1 -ProcessName nhl24 -Ptr 0x12021c9a6e8 -Lo 0x10000000000 -Hi 0x12400000000
param(
    [string]$ProcessName = 'nhl24',
    [string]$Read = '',
    [int]$Bytes = 32,
    [string]$Ptr = '',
    [string]$Lo = '0x10000000000',
    [string]$Hi = '0x12400000000',
    [int]$MaxHits = 20
)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class MS {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr BaseAddress; public IntPtr AllocationBase; public uint AllocationProtect; public uint __a; public IntPtr RegionSize; public uint State; public uint Protect; public uint Type; public uint __b; }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr hp, IntPtr addr, byte[] buf, IntPtr n, out IntPtr read);
  [DllImport("kernel32.dll")] public static extern int VirtualQueryEx(IntPtr hp, IntPtr addr, out MBI mbi, int len);
  public static byte[] Read(IntPtr hp, long addr, int n) { byte[] b = new byte[n]; IntPtr rd; if (!ReadProcessMemory(hp, new IntPtr(addr), b, new IntPtr(n), out rd)) return null; return b; }
  public static long[] Find(IntPtr hp, byte[] pat, long lo, long hi, int max) {
    var hits = new System.Collections.Generic.List<long>();
    long a = lo; MBI m;
    while (a < hi && hits.Count < max) {
      if (VirtualQueryEx(hp, new IntPtr(a), out m, Marshal.SizeOf(typeof(MBI))) == 0) break;
      long b = (long)m.BaseAddress, sz = (long)m.RegionSize;
      if (m.State == 0x1000 && (m.Protect & 0x100) == 0 && m.Protect != 0x01) { // committed, not guard, not noaccess
        long end = Math.Min(b + sz, hi);
        const int chunk = 4 << 20;
        for (long p = Math.Max(b, lo); p < end; p += chunk - pat.Length) {
          int n = (int)Math.Min(chunk, end - p);
          byte[] buf = Read(hp, p, n); if (buf == null) continue;
          for (int i = 0; i + pat.Length <= n; i++) {
            if (buf[i] != pat[0]) continue;
            int k = 1; while (k < pat.Length && buf[i + k] == pat[k]) k++;
            if (k == pat.Length) { hits.Add(p + i); if (hits.Count >= max) break; }
          }
          if (hits.Count >= max) break;
        }
      }
      a = b + sz;
    }
    return hits.ToArray();
  }
}
"@
$p = Get-Process $ProcessName -ErrorAction Stop | Select-Object -First 1
$hp = [MS]::OpenProcess(0x0410, $false, $p.Id)
if ($Read) {
  $addr = [Convert]::ToInt64($Read, 16)
  $b = [MS]::Read($hp, $addr, $Bytes)
  if ($b -eq $null) { "read failed at $Read"; exit 1 }
  "bytes @ ${Read}: " + (($b | ForEach-Object { '{0:x2}' -f $_ }) -join ' ')
  for ($i = 0; $i + 4 -le $Bytes; $i += 4) { $f = [BitConverter]::ToSingle($b, $i); $u = [BitConverter]::ToUInt32($b, $i); '  +0x{0:x2}: 0x{1:x8}  f={2}' -f $i, $u, $f }
}
if ($Ptr) {
  $v = [Convert]::ToUInt64($Ptr, 16)
  $pat = [BitConverter]::GetBytes($v)
  $hits = [MS]::Find($hp, $pat, [Convert]::ToInt64($Lo, 16), [Convert]::ToInt64($Hi, 16), $MaxHits)
  "hits for pointer ${Ptr}: " + $hits.Count
  foreach ($h in $hits) { '  0x{0:x}' -f $h }
}
