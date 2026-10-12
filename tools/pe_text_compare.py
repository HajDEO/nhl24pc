"""Compare the .text sections of two PE files byte for byte (to check that a rebuild with debug
info kept the same code layout).  python tools/pe_text_compare.py a.exe b.exe"""
import struct, sys
def sections(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = pe + 24 + opt
    out = {}
    for i in range(nsec):
        s = d[base + i * 40: base + (i + 1) * 40]
        name = s[:8].rstrip(b'\0').decode()
        vsize, va, rsize, rptr = struct.unpack_from('<IIII', s, 8)
        out[name] = (va, vsize, d[rptr:rptr + rsize])
    return out
a, b = sections(sys.argv[1]), sections(sys.argv[2])
for name in a:
    if name not in b:
        print(name, 'missing in second'); continue
    va1, vs1, d1 = a[name]; va2, vs2, d2 = b[name]
    same = d1 == d2
    first = next((i for i in range(min(len(d1), len(d2))) if d1[i] != d2[i]), None) if not same else None
    print('%-8s va %#x/%#x size %#x/%#x %s%s' % (name, va1, va2, len(d1), len(d2), 'IDENTICAL' if same else 'DIFFERS', '' if same else ' first diff at +%#x' % first if first is not None else ' (length)'))
