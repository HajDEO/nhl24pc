"""Symbolize the per-thread frame lists written by the lldb parse (logs/freeze39_frames.txt style)
against an nhl24.exe that has a PDB: exe+0x<off> entries become function names.

  python tools/symbolize_frames.py logs/freeze39_frames.txt build/nhl24.exe [image_base]
"""
import re, subprocess, sys
frames_file, exe = sys.argv[1], sys.argv[2]
base = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x700000000000
text = open(frames_file, encoding='utf-8').read()
offs = sorted(set(int(m, 16) for m in re.findall(r'exe\+0x([0-9a-f]+)', text)))
addrs = ['0x%x' % (base + o) for o in offs]
cmd = ['C:/Program Files/LLVM/bin/llvm-symbolizer.exe', '--obj=' + exe, '--inlining=0', '--demangle']
out = subprocess.run(cmd, input='\n'.join(addrs), capture_output=True, text=True).stdout
blocks = [b for b in out.strip().split('\n\n')]
names = {}
for o, b in zip(offs, blocks):
    lines = b.split('\n')
    fn = lines[0].strip() if lines else '??'
    loc = lines[1].strip() if len(lines) > 1 else ''
    loc = loc.replace(chr(92), '/')
    loc = loc.split('/nhlport/')[-1]
    fn = re.sub(r'\(.*\)$', '', fn)
    names[o] = '%s [%s]' % (fn[:60], loc.split('/')[-1] if loc else '?')


def sub(m):
    return names.get(int(m.group(1), 16), m.group(0))


print(re.sub(r'exe\+0x([0-9a-f]+)', sub, text))
