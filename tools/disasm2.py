import sys, struct, re, capstone
f, base = sys.argv[1], int(sys.argv[2], 16)
ebase = 0x5160000
plt_base = 0xdeef000; plt = open(sys.argv[3], 'rb').read()
names = {}
for line in open(sys.argv[4]):
    m = re.match(r'^0x([0-9a-f]+)\s+JUMP_SLOT\s+(\S+)\s+(\S+)\s+(.*)$', line)
    if m: names[ebase + int(m.group(1), 16)] = (m.group(4).strip() if m.group(4).strip() not in ('?', '') else m.group(2)) + '@' + m.group(3)
def thunk(t):
    o = t - plt_base
    if 0 <= o < len(plt) - 6 and plt[o] == 0xff and plt[o+1] == 0x25:
        got = t + 6 + struct.unpack_from('<i', plt, o + 2)[0]
        return names.get(got, 'GOT eboot+%x' % (got - ebase))
    return None
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.syntax = capstone.CS_OPT_SYNTAX_INTEL; md.skipdata = True
for i in md.disasm(open(f, 'rb').read(), base):
    tag = ''
    if i.mnemonic.startswith(('call', 'j')) and i.op_str.startswith('0x'):
        t = int(i.op_str, 16); n = thunk(t)
        if n: tag = '   ; -> ' + n
        elif ebase <= t < 0x10e30000: tag = '   ; eboot+0x%x' % (t - ebase)
    print('%x  +%-8x %-7s %s%s' % (i.address, i.address - ebase, i.mnemonic, i.op_str, tag))
