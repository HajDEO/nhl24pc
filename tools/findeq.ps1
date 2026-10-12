param([int]$ProcId)
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic; using System.Text;
public static class MS {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr BaseAddress; public IntPtr AllocationBase; public uint AllocationProtect; public uint __align; public IntPtr RegionSize; public uint State; public uint Protect; public uint Type; }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr hp, IntPtr addr, byte[] buf, IntPtr n, out IntPtr read);
  [DllImport("kernel32.dll")] public static extern IntPtr VirtualQueryEx(IntPtr hp, IntPtr addr, out MBI mbi, IntPtr len);
  public static byte[] Read(IntPtr hp, long addr, int n) { byte[] b = new byte[n]; IntPtr rd; if (!ReadProcessMemory(hp, new IntPtr(addr), b, new IntPtr(n), out rd)) return null; return b; }
  // find needle in committed private (heap) regions below 0x7ff000000000 but above 0x100000000 (host heap range)
  public static List<long> Find(IntPtr hp, byte[] needle, long lo, long hi, int maxHits) {
    var hits = new List<long>(); long addr = lo; MBI m;
    while (addr < hi && hits.Count < maxHits) {
      if (VirtualQueryEx(hp, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) == IntPtr.Zero) break;
      long size = (long)m.RegionSize; long bas = (long)m.BaseAddress;
      if (m.State == 0x1000 && m.Type == 0x20000 && (m.Protect == 0x04 || m.Protect == 0x08) && size <= 0x20000000) {
        long off = 0;
        while (off < size && hits.Count < maxHits) {
          int chunk = (int)Math.Min(0x1000000, size - off);
          byte[] b = Read(hp, bas + off, chunk);
          if (b != null) {
            for (int i = 0; i + needle.Length <= b.Length; i++) {
              if (b[i] != needle[0]) continue; bool ok = true;
              for (int k = 1; k < needle.Length; k++) if (b[i+k] != needle[k]) { ok = false; break; }
              if (ok) hits.Add(bas + off + i);
            }
          }
          off += chunk;
        }
      }
      addr = bas + size;
    }
    return hits;
  }
}
"@
$hp = [MS]::OpenProcess(0x0410, $false, $ProcId)
$needle = [Text.Encoding]::ASCII.GetBytes("GfxEopQueue") + [byte[]]@(0)
$hits = [MS]::Find($hp, $needle, 0x100000000, 0x7ff000000000, 40)
"hits: {0}" -f $hits.Count
foreach ($h in $hits) {
  $b = [MS]::Read($hp, $h - 0x10, 0x90); if (-not $b) { continue }
  $line = "@{0:x}:" -f ($h - 0x10); for ($o = 0; $o -lt 0x90; $o += 8) { $line += " {0:x16}" -f [BitConverter]::ToUInt64($b, $o) }
  $line
  # MSVC std::string: [buf 16][size 8][cap 8]; string starts at $h -> size at h+16, cap at h+24; then mutex (8) then vector begin/end/cap
  $size = [BitConverter]::ToUInt64($b, 0x20); $cap = [BitConverter]::ToUInt64($b, 0x28)
  if ($size -eq 11 -and $cap -eq 15) {
    $vb = [BitConverter]::ToUInt64($b, 0x38); $ve = [BitConverter]::ToUInt64($b, 0x40)
    "  looks like EqueueInternal: handle={0:x} events vector begin={1:x} end={2:x} count~={3}" -f [BitConverter]::ToUInt64($b, 0x8), $vb, $ve, (($ve - $vb) / 0x30)
    if ($vb -gt 0x10000 -and $ve -ge $vb -and ($ve - $vb) -lt 0x400) {
      $ev = [MS]::Read($hp, [long]$vb, [int]($ve - $vb))
      for ($o = 0; $o + 0x20 -le $ev.Length; $o += 8) { "    ev+{0:x2}: {1:x16}" -f $o, [BitConverter]::ToUInt64($ev, $o) }
    }
  }
}
