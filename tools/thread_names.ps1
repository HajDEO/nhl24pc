# Lists the threads of a process with their Win32 thread descriptions: tid (hex) -> name.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\thread_names.ps1 -ProcessId 16844
param([int]$ProcessId)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class TN {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
  [DllImport("kernel32.dll")] public static extern int GetThreadDescription(IntPtr h, out IntPtr desc);
  [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr h);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@
$p = Get-Process -Id $ProcessId
foreach ($t in $p.Threads) {
  $h = [TN]::OpenThread(0x0040, $false, [uint32]$t.Id)  # THREAD_QUERY_LIMITED_INFORMATION
  $name = ''
  if ($h -ne [IntPtr]::Zero) {
    $d = [IntPtr]::Zero
    if ([TN]::GetThreadDescription($h, [ref]$d) -ge 0 -and $d -ne [IntPtr]::Zero) { $name = [Runtime.InteropServices.Marshal]::PtrToStringUni($d); [TN]::LocalFree($d) | Out-Null }
    [TN]::CloseHandle($h) | Out-Null
  }
  '{0:x4} {1,-10} {2,8:n2}s {3}' -f $t.Id, $t.ThreadState, $t.TotalProcessorTime.TotalSeconds, $name
}
