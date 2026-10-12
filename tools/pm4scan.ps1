param([int]$ProcId)
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class PS {
  [StructLayout(LayoutKind.Sequential)] public struct MBI { public IntPtr BaseAddress; public IntPtr AllocationBase; public uint AllocationProtect; public uint __align; public IntPtr RegionSize; public uint State; public uint Protect; public uint Type; }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr hp, IntPtr addr, byte[] buf, IntPtr n, out IntPtr read);
  [DllImport("kernel32.dll")] public static extern IntPtr VirtualQueryEx(IntPtr hp, IntPtr addr, out MBI mbi, IntPtr len);
  public static Dictionary<string, long> counts = new Dictionary<string, long>();
  public static List<string> samples = new List<string>();
  static void Inc(string k) { long c; counts.TryGetValue(k, out c); counts[k] = c + 1; }
  public static long Scanned = 0;
  public static void Scan(IntPtr hp, long lo, long hi) {
    long addr = lo; MBI m; byte[] buf = new byte[0x1000000];
    while (addr < hi) {
      if (VirtualQueryEx(hp, new IntPtr(addr), out m, new IntPtr(Marshal.SizeOf(typeof(MBI)))) == IntPtr.Zero) break;
      long size = (long)m.RegionSize; long bas = (long)m.BaseAddress;
      bool readable = m.State == 0x1000 && (m.Protect & 0x66) != 0 && (m.Protect & 0x100) == 0; // RW/RWX, no guard
      if (readable && m.Type != 0x1000000 /*image*/) {
        long off = 0;
        while (off < size) {
          int chunk = (int)Math.Min(buf.Length, size - off); IntPtr rd;
          if (ReadProcessMemory(hp, new IntPtr(bas + off), buf, new IntPtr(chunk), out rd)) {
            int n = (int)rd; Scanned += n;
            for (int i = 0; i + 4 <= n; i += 4) {
              uint v = BitConverter.ToUInt32(buf, i);
              switch (v) {
                case 0x68750776u: Inc("NOP PatchedFlip"); break;
                case 0x68750777u: Inc("NOP PrepareFlip"); break;
                case 0x68750778u: Inc("NOP PrepareFlipLabel"); break;
                case 0x68750780u: Inc("NOP PrepareFlipInterrupt"); break;
                case 0x68750781u: Inc("NOP PrepareFlipInterruptLabel"); break;
                case 0xC0044700u: { // EVENT_WRITE_EOP count=4
                  if (i + 20 <= n) { uint ec = BitConverter.ToUInt32(buf, i+4); uint dc = BitConverter.ToUInt32(buf, i+12); uint intsel = (dc >> 24) & 3; uint datasel = dc >> 29; uint evt = ec & 0x3f;
                    Inc(string.Format("EVENT_WRITE_EOP evt={0} int_sel={1} data_sel={2}", evt, intsel, datasel));
                    if (intsel != 0 && samples.Count < 6) samples.Add(string.Format("EOP@{0:x}: {1:x8} {2:x8} {3:x8} {4:x8} {5:x8}", bas+off+i, v, ec, BitConverter.ToUInt32(buf,i+8), dc, BitConverter.ToUInt32(buf,i+16))); }
                  break; }
                case 0xC0054900u: case 0xC0064900u: { // RELEASE_MEM
                  if (i + 24 <= n) { uint ec = BitConverter.ToUInt32(buf, i+4); uint dc = BitConverter.ToUInt32(buf, i+8); uint intsel = (dc >> 24) & 7; uint datasel = (dc >> 29) & 7;
                    Inc(string.Format("RELEASE_MEM hdr={0:x8} evt={1} int_sel={2} data_sel={3}", v, ec & 0x3f, intsel, datasel));
                    if (samples.Count < 12) samples.Add(string.Format("RELMEM@{0:x}: {1:x8} {2:x8} {3:x8} {4:x8} {5:x8} {6:x8}", bas+off+i, v, ec, dc, BitConverter.ToUInt32(buf,i+12), BitConverter.ToUInt32(buf,i+16), BitConverter.ToUInt32(buf,i+20))); }
                  break; }
                case 0xC0053C00u: { // WAIT_REG_MEM count=5
                  if (i + 24 <= n) { uint f = BitConverter.ToUInt32(buf, i+4); Inc(string.Format("WAIT_REG_MEM func={0} mem={1} eng={2}", f & 7, (f >> 4) & 1, (f >> 8) & 1)); }
                  break; }
              }
            }
          }
          off += chunk;
        }
      }
      addr = bas + size;
    }
  }
}
"@
$hp = [PS]::OpenProcess(0x0410, $false, $ProcId)
$sw = [Diagnostics.Stopwatch]::StartNew()
[PS]::Scan($hp, 0x10000, 0x7ff000000000)
"scanned {0} MB in {1:n0}s" -f ([PS]::Scanned / 1MB), $sw.Elapsed.TotalSeconds
[PS]::counts.GetEnumerator() | Sort-Object Name | ForEach-Object { "  {0,-60} {1}" -f $_.Key, $_.Value }
"--- samples ---"; [PS]::samples
