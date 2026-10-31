#!/bin/bash
# Patches tools/launcher.ps1 of the NHL24PC repack so that it
#  1) compiles under Wine's PowerShell 7 (.NET 8): adds missing assembly references
#  2) on PLAY saves NHLPORT_* settings to linux_env.sh and exits instead of starting the Windows exe
# Run from the game folder. Safe to run twice; the original is kept as tools/launcher.ps1.bak
set -e
cd "$(dirname "$(readlink -f "$0")")"
f=tools/launcher.ps1
[ -f "$f" ] || { echo "$f not found - run this from the game folder"; exit 1; }
[ -f "$f.bak" ] || cp "$f" "$f.bak"

python3 - "$f" <<'EOF'
import sys
p = sys.argv[1]
src = open(p, encoding='utf-8-sig', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
lines = src.split(nl)

extra = ['System.ComponentModel.Primitives', 'System.ComponentModel.TypeConverter',
         'System.Windows.Forms.Primitives', 'System.Drawing.Primitives',
         'System.Collections', 'System.Drawing.Common']
refs = play = False
i = 0
while i < len(lines):
    l = lines[i]
    if not refs and 'Add-Type -ReferencedAssemblies System.Windows.Forms' in l:
        missing = [a for a in extra if a + ',' not in l + ',' and a + ' ' not in l]
        if missing:
            lines[i] = l.replace(' -TypeDefinition', ', ' + ', '.join(missing) + ' -TypeDefinition', 1)
        refs = True
    if not play and 'linux_play.flag' in src:
        play = True
    if not play and '$script:GameProc = Start-Process -FilePath (Join-Path $Root $ExeName)' in l:
        ind = l[:len(l) - len(l.lstrip())]
        lines[i:i+3] = [
            ind + "# linux: save settings for the native build instead of starting the Windows exe",
            ind + "$envLines = Get-ChildItem Env: | Where-Object { $_.Name -like 'NHLPORT_*' } | ForEach-Object { \"export $($_.Name)='$($_.Value)'\" }",
            ind + "[IO.File]::WriteAllText((Join-Path $Root 'linux_env.sh'), (($envLines -join \"`n\") + \"`n\"))",
            ind + "[IO.File]::WriteAllText((Join-Path $Root 'linux_play.flag'), 'play')",
            ind + "$script:GameProc = $null",
        ]
        play = True
    i += 1

open(p, 'w', encoding='utf-8-sig', newline='').write(nl.join(lines))
print('assembly references:', 'ok' if refs else 'Add-Type line NOT found')
print('PLAY handoff:       ', 'ok' if play else 'Start-Process line NOT found')
EOF
