"""Identify functions of an unsymbolized PE (x64) by the strings they reference.

For every exe+0x<off> frame in a frames file, find the containing function via the .pdata
RUNTIME_FUNCTION table, disassemble it with capstone and collect the strings reached through
rip-relative lea/mov operands (shadPS4's LOG_* macros reference __func__ and the format string,
assert messages are strings too), so the function can be named without a PDB.

  python tools/pe_func_strings.py nhl24.exe frames.txt [thread#,thread#,...]
"""
import re, struct, sys
import capstone

exe, frames_file = sys.argv[1], sys.argv[2]
only = set(int(x) for x in sys.argv[3].split(',')) if len(sys.argv) > 3 else None
d = open(exe, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
image_base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
secs = []
for i in range(nsec):
    s = d[pe + 24 + opt + i * 40: pe + 24 + opt + (i + 1) * 40]
    name = s[:8].rstrip(b'\0').decode()
    vsize, va, rsize, rptr = struct.unpack_from('<IIII', s, 8)
    secs.append((name, va, vsize, rptr, rsize))


def rva_to_off(rva):
    for name, va, vsize, rptr, rsize in secs:
        if va <= rva < va + max(vsize, rsize):
            return rptr + (rva - va)
    return None


def sec_of(rva):
    for name, va, vsize, rptr, rsize in secs:
        if va <= rva < va + max(vsize, rsize):
            return name
    return None


# function table
pd = next(s for s in secs if s[0] == '.pdata')
funcs = []
for i in range(pd[4] // 12):
    b, e, u = struct.unpack_from('<III', d, pd[3] + i * 12)
    if b and e > b:
        funcs.append((b, e))
funcs.sort()
begins = [f[0] for f in funcs]
import bisect


def func_of(rva):
    i = bisect.bisect_right(begins, rva) - 1
    while i >= 0:
        b, e = funcs[i]
        if b <= rva < e:
            return b, e
        i -= 1
    return None


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def cstring(rva, maxlen=120):
    off = rva_to_off(rva)
    if off is None:
        return None
    end = d.find(b'\0', off, off + maxlen)
    if end < 0:
        return None
    raw = d[off:end]
    if len(raw) < 5 or any(c < 0x20 or c > 0x7e for c in raw):
        return None
    return raw.decode()


def strings_of(b, e):
    off = rva_to_off(b)
    code = d[off:off + (e - b)]
    found = []
    calls = []
    for ins in md.disasm(code, image_base + b):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                target = ins.address + ins.size + op.mem.disp - image_base
                s = cstring(target)
                if s and s not in found:
                    found.append(s)
        if ins.mnemonic == 'call' and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
            calls.append(ins.operands[0].imm - image_base)
    return found, calls


text = open(frames_file, encoding='utf-8').read()
cache = {}
for line in text.splitlines():
    m = re.match(r'thread #(\d+):', line)
    if not m or (only is not None and int(m.group(1)) not in only):
        continue
    print(line[:160])
    for fm in re.finditer(r'exe\+0x([0-9a-f]+)', line):
        rva = int(fm.group(1), 16)
        f = func_of(rva)
        if not f:
            print('   +%#x: no function (%s)' % (rva, sec_of(rva)))
            continue
        if f not in cache:
            cache[f] = strings_of(*f)
        strs, calls = cache[f]
        # strings referenced by direct callees too (one level), helps with thin wrappers
        extra = []
        for c in calls[:12]:
            cf = func_of(c)
            if cf and cf != f:
                if cf not in cache:
                    cache[cf] = strings_of(*cf)
                extra += [s for s in cache[cf][0] if s not in strs and s not in extra][:3]
        print('   +%#x in [%#x..%#x) size %d: %s' % (rva, f[0], f[1], f[1] - f[0],
              ' | '.join(s[:70] for s in strs[:8]) or '(no strings)'))
        if extra:
            print('        callees: %s' % ' | '.join(s[:60] for s in extra[:6]))
