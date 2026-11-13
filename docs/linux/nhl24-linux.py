#!/usr/bin/env python3
# NHL 24 PC - native Linux installer and launcher (no Wine or PowerShell).
# Works with the current https://github.com/HajDEO/nhl24pc source. Python 3.8+,
# standard library only.
#
#   ./nhl24-linux.py install BASE.pkg [UPDATE.pkg]   extract + patch + config + menu entry
#   ./nhl24-linux.py update UPDATE.pkg               install / replace the 1.60 update
#   ./nhl24-linux.py remove-update                   back to 1.00
#   ./nhl24-linux.py settings                        text-mode settings (resolution, Russian text...)
#   ./nhl24-linux.py play                            apply settings and start the game
#   ./nhl24-linux.py status                          what is installed / patched
#   ./nhl24-linux.py mod [install|remove|status]     Russia / Belarus national teams mod
#   ./nhl24-linux.py desktop [--remove-wine]         menu + desktop shortcuts (and remove old Wine ones)
#
# Game patches are found by byte signatures, checked before writing, and work on
# CUSA37934 (EU) and CUSA37933 (US), versions 1.00 and 1.60.

import json, os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.realpath(__file__))
REPO_ROOT = os.path.realpath(os.path.join(ROOT, '..', '..'))
EMU_CANDIDATES = [os.path.join(ROOT, 'nhl24-linux'), os.path.join(ROOT, 'nhl24'),
                  os.path.join(REPO_ROOT, 'build', 'nhl24')]


def find_emulator():
    for path in EMU_CANDIDATES:
        if os.access(path, os.X_OK):
            return path
    return EMU_CANDIDATES[0]


def port_version():
    version_file = os.path.join(ROOT, 'VERSION')
    try:
        with open(version_file, encoding='utf-8') as f:
            version = f.read().strip()
        if version:
            return version
    except OSError:
        pass
    header = os.path.join(REPO_ROOT, 'src', 'common', 'port_version.h')
    try:
        with open(header, encoding='utf-8') as f:
            match = re.search(r'^#define NHLPORT_VERSION "([^"]+)"', f.read(), re.M)
        if match:
            return match.group(1)
    except OSError:
        pass
    return 'development'


PORT_VERSION = port_version()


def refresh_runtime_paths():
    global EMU, DATA_ROOT, GAME, UPDATE, USER, CONFIG, SETTINGS, OFFSET_CACHE, LOC_CACHE
    EMU = find_emulator()
    emulator_dir = os.path.dirname(os.path.realpath(EMU))
    DATA_ROOT = ROOT if emulator_dir == os.path.realpath(ROOT) else emulator_dir
    GAME = os.path.join(DATA_ROOT, 'game')
    UPDATE = os.path.join(DATA_ROOT, 'game-UPDATE')
    USER = os.path.join(DATA_ROOT, 'user')
    CONFIG = os.path.join(USER, 'config.json')
    SETTINGS = os.path.join(USER, 'launcher_settings.json')
    OFFSET_CACHE = os.path.join(USER, 'eboot_offsets.json')
    LOC_CACHE = os.path.join(USER, 'loc_offset.json')


refresh_runtime_paths()


def config_templates():
    return [os.path.join(DATA_ROOT, 'config_default.json'),
            os.path.join(ROOT, 'config_default.json'),
            os.path.join(ROOT, 'release', 'user', 'config.json'),
            os.path.join(ROOT, 'tools', 'config_default.json'),
            os.path.join(REPO_ROOT, 'release', 'user', 'config.json')]


KNOWN_TITLES = ('CUSA37933', 'CUSA37934')

RES_MODES = {'720': (1280, 720), '1080': (1920, 1080), '1200': (1920, 1200),
             '1440': (2560, 1440), '1600': (2560, 1600), '2160': (3840, 2160)}
FPS_VALUES = (60, 90, 120, 144)
# Render resolution: Frostbite settings block "1.0f 1.0f 0 0 | W H | 0 0 | 3840 2160"
RES_SIG = '0000803f0000803f0000000000000000 ???????????????? 0000000000000000000f000070080000'
RES_SKIP = 16
# Dynamic resolution off (GameRenderSettings.DynamicResolutionScaleEnable, field 0x16E)
DRS_PATCHES = [
    ('6601000000000000', 'c6836e01000001', 'c6836e01000000', 'c5f8138b70010000'),
    ('c5fa1187c4000000', '8a866e010000',   '31c090909090',   '88876e0100008b46'),
    ('000088886d010000', '0fb68b6e010000', '31c90f1f440000', '88886e0100000fb6'),
    ('0080b86e01000000', '0f85????????',   '660f1f440000',   '31c048b905000000'),
]
# Freeze before the first puck drop: cmp qword ptr [rdi+8],0 ; je -> jmp
FREEZE_ORIG = '48837f08007448'
FREEZE_NEW = '48837f0800eb48'
# Language table: "eng\0eng_us\0fre\0" / "ger\0ger_de\0swe\0"
LOC_ENG_SIG = '656e6700 ???????????? 0066726500'
LOC_GER_SIG = '67657200 ???????????? 0073776500'

DEFAULT_SETTINGS = {
    'resolution': '1440', 'fullscreen': True, 'hdr': 'off', 'vsync': False, 'fps': 120,
    'fsr': True, 'diag': False, 'lang': '', 'ru_text': False, 'stable': False,
}


def say(msg, color=''):
    codes = {'ok': '32', 'warn': '33', 'err': '31', 'head': '36'}
    if color and sys.stdout.isatty():
        print('\033[%sm%s\033[0m' % (codes[color], msg))
    else:
        print(msg)


def fail(msg):
    say('ОШИБКА: ' + msg, 'err')
    sys.exit(1)


# ---------------- byte signatures ----------------

def sig_regex(hexsig):
    h = re.sub(r'\s', '', hexsig)
    parts = []
    for i in range(0, len(h), 2):
        t = h[i:i + 2]
        parts.append(b'.' if t == '??' else re.escape(bytes([int(t, 16)])))
    return re.compile(b'(?=' + b''.join(parts) + b')', re.DOTALL)


def find_sig(data, hexsig):
    return [m.start() for m in sig_regex(hexsig).finditer(data)]


def match_sig(cur, hexsig):
    h = re.sub(r'\s', '', hexsig)
    if len(cur) < len(h) // 2:
        return False
    for i in range(0, len(h), 2):
        t = h[i:i + 2]
        if t != '??' and cur[i // 2] != int(t, 16):
            return False
    return True


def eboot_path():
    upd = os.path.join(UPDATE, 'eboot.bin')
    return upd if os.path.isfile(upd) else os.path.join(GAME, 'eboot.bin')


def read_at(path, off, n):
    with open(path, 'rb') as f:
        f.seek(off)
        return f.read(n)


def write_at(path, off, b):
    with open(path, 'r+b') as f:
        f.seek(off)
        f.write(b)


def read_json(path):
    try:
        with open(path, encoding='utf-8-sig') as f:
            return json.load(f)
    except Exception:
        return None


def write_json(path, obj, compact=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(obj, f, separators=(',', ':') if compact else None, indent=None if compact else 2)


def known_res(b):
    if len(b) < 8:
        return None
    w, h = struct.unpack('<II', b[:8])
    for k, (rw, rh) in RES_MODES.items():
        if (rw, rh) == (w, h):
            return k
    return None


def offsets_valid(eb, o):
    try:
        if not o or o.get('size') != os.path.getsize(eb):
            return False
        if not known_res(read_at(eb, o['res'], 8)):
            return False
        for (pre, orig, new, post), off in zip(DRS_PATCHES, o['drs']):
            nb = bytes.fromhex(new)
            cur = read_at(eb, off, len(nb))
            if not (match_sig(cur, orig) or cur == nb):
                return False
        return True
    except Exception:
        return False


def patch_offsets(eb):
    o = read_json(OFFSET_CACHE)
    if offsets_valid(eb, o):
        return o
    with open(eb, 'rb') as f:
        data = f.read()
    hits = find_sig(data, RES_SIG)
    if len(hits) != 1:
        raise RuntimeError('разрешение не найдено в eboot.bin (%d совпадений), нужна версия 1.00 или 1.60' % len(hits))
    drs = []
    for pre, orig, new, post in DRS_PATCHES:
        h = find_sig(data, pre + new + post)
        if len(h) != 1:
            h = find_sig(data, pre + orig + post)
        if len(h) != 1:
            raise RuntimeError('код динамического разрешения не распознан (%d совпадений для %s)' % (len(h), orig))
        drs.append(h[0] + 8)
    o = {'size': len(data), 'res': hits[0] + RES_SKIP, 'drs': drs}
    write_json(OFFSET_CACHE, o, compact=True)
    return o


def get_render_res(eb):
    return known_res(read_at(eb, patch_offsets(eb)['res'], 8)) or 'unknown'


def set_render_res(eb, mode):
    w, h = RES_MODES[mode]
    write_at(eb, patch_offsets(eb)['res'], struct.pack('<II', w, h))


def drs_state(eb):
    o = patch_offsets(eb)
    n = sum(read_at(eb, off, len(bytes.fromhex(p[2]))) == bytes.fromhex(p[2])
            for p, off in zip(DRS_PATCHES, o['drs']))
    return 'off' if n == len(DRS_PATCHES) else ('on' if n == 0 else 'partial')


def disable_drs(eb):
    o = patch_offsets(eb)
    for p, off in zip(DRS_PATCHES, o['drs']):
        nb = bytes.fromhex(p[2])
        if read_at(eb, off, len(nb)) != nb:
            write_at(eb, off, nb)


def freeze_fix(eb):
    with open(eb, 'rb') as f:
        data = f.read()
    hits, done = find_sig(data, FREEZE_ORIG), find_sig(data, FREEZE_NEW)
    if not hits and done:
        return 'already'
    if len(hits) != 1:
        raise RuntimeError('код зависания не распознан (%d совпадений)' % len(hits))
    write_at(eb, hits[0] + 5, b'\xeb')
    return 'patched'


def loc_offsets(eb):
    size = os.path.getsize(eb)
    c = read_json(LOC_CACHE)
    if c and c.get('size') == size:
        try:
            e = read_at(eb, c['eng'], 6).decode('ascii')
            g = read_at(eb, c['ger'], 6).decode('ascii')
            if e in ('eng_us', 'rus_ru') and g in ('ger_de', 'rus_ru'):
                return c
        except Exception:
            pass
    with open(eb, 'rb') as f:
        data = f.read()

    def find(sig, orig):
        h = [x for x in find_sig(data, sig) if data[x + 4:x + 10] in (orig.encode(), b'rus_ru')]
        if len(h) != 1:
            raise RuntimeError('таблица языков не найдена (%d совпадений для %s)' % (len(h), orig))
        return h[0] + 4

    c = {'size': size, 'eng': find(LOC_ENG_SIG, 'eng_us'), 'ger': find(LOC_GER_SIG, 'ger_de')}
    write_json(LOC_CACHE, c, compact=True)
    return c


def set_russian_text(eb, on):
    c = loc_offsets(eb)
    for off, want in ((c['eng'], 'rus_ru' if on else 'eng_us'), (c['ger'], 'ger_de')):
        if read_at(eb, off, 6) != want.encode():
            write_at(eb, off, want.encode())
    set_config({'console_language': 1})


def russian_state(eb):
    try:
        return read_at(eb, loc_offsets(eb)['eng'], 6) == b'rus_ru'
    except Exception:
        return False


# ---------------- param.sfo / PKG ----------------

def sfo_from_bytes(d):
    r = {}
    if len(d) < 20 or d[1:4] != b'PSF':
        return r
    key_start, data_start, n = struct.unpack_from('<III', d, 8)
    for i in range(n):
        e = 20 + i * 16
        ko, fmt, ln, _, do = struct.unpack_from('<HHIII', d, e)
        k0 = key_start + ko
        k1 = d.index(b'\0', k0)
        key = d[k0:k1].decode('ascii', 'replace')
        v0 = data_start + do
        if fmt == 0x0404:
            r[key] = struct.unpack_from('<I', d, v0)[0]
        else:
            r[key] = d[v0:v0 + ln].decode('utf-8', 'replace').rstrip('\0')
    return r


def sfo_file(path):
    try:
        with open(path, 'rb') as f:
            return sfo_from_bytes(f.read())
    except Exception:
        return {}


def pkg_info(path):
    # PKG header and entry table are big-endian; param.sfo (entry id 0x1000) is not encrypted
    try:
        with open(path, 'rb') as f:
            h = f.read(0x20)
            if len(h) < 0x20 or struct.unpack_from('>I', h, 0)[0] != 0x7F434E54:
                return {}
            count, table = struct.unpack_from('>I', h, 0x10)[0], struct.unpack_from('>I', h, 0x18)[0]
            if not 0 < count <= 100000:
                return {}
            f.seek(table)
            t = f.read(count * 32)
            for i in range(count):
                if struct.unpack_from('>I', t, i * 32)[0] != 0x1000:
                    continue
                off, size = struct.unpack_from('>II', t, i * 32 + 16)
                if not 0 < size <= 1 << 20:
                    return {}
                f.seek(off)
                return sfo_from_bytes(f.read(size))
    except Exception:
        pass
    return {}


# ---------------- emulator config / settings ----------------

def set_config(values):
    if not os.path.isfile(CONFIG):
        tpl = next((p for p in config_templates() if os.path.isfile(p)), None)
        if not tpl:
            raise RuntimeError('no emulator config template was found')
        os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
        shutil.copy(tpl, CONFIG)

    config = read_json(CONFIG)
    if not isinstance(config, dict):
        raise RuntimeError('invalid JSON in %s' % CONFIG)
    sections = {
        'console_language': 'General', 'neo_mode': 'General',
        'extra_dmem_in_mbytes': 'General', 'show_fps_counter': 'General',
        'show_splash': 'General', 'redzone_patches': 'General',
        'readbacks_mode': 'GPU', 'present_mode': 'GPU', 'vblank_frequency': 'GPU',
        'full_screen': 'GPU', 'hdr_allowed': 'GPU', 'window_width': 'GPU',
        'window_height': 'GPU', 'fsr_enabled': 'GPU', 'rcas_enabled': 'GPU',
    }
    missing = []
    for k, v in values.items():
        section_name = sections.get(k)
        section = config.get(section_name) if section_name else None
        if not isinstance(section, dict) or k not in section:
            missing.append('%s.%s' % (section_name or '?', k))
            continue
        section[k] = v
    if missing:
        raise RuntimeError('config template is missing: %s' % ', '.join(missing))
    write_json(CONFIG, config)


def load_settings():
    s = dict(DEFAULT_SETTINGS)
    j = read_json(SETTINGS)
    if isinstance(j, dict):
        s.update({k: v for k, v in j.items() if k in s})
    return s


def save_settings(s):
    write_json(SETTINGS, s)


# ---------------- hardware ----------------

def vram_gb():
    best = 0
    drm = '/sys/class/drm'
    try:
        for card in os.listdir(drm):
            p = os.path.join(drm, card, 'device', 'mem_info_vram_total')   # amdgpu
            if os.path.isfile(p):
                with open(p) as f:
                    best = max(best, int(f.read()) // (1 << 30))
    except Exception:
        pass
    if not best and shutil.which('nvidia-smi'):
        try:
            out = subprocess.run(['nvidia-smi', '--query-gpu=memory.total', '--format=csv,noheader,nounits'],
                                 capture_output=True, text=True, timeout=5).stdout
            best = max(int(x) for x in out.split()) // 1024
        except Exception:
            pass
    return best


def screen_size():
    if shutil.which('xrandr'):
        try:
            out = subprocess.run(['xrandr', '--current'], capture_output=True, text=True, timeout=5).stdout
            m = re.search(r'(\d+)x(\d+)\+0\+0', out) or re.search(r'current (\d+) x (\d+)', out)
            if m:
                return int(m.group(1)), int(m.group(2))
        except Exception:
            pass
    return 1920, 1080


def physical_cores():
    try:
        with open('/proc/cpuinfo') as f:
            pairs = set(re.findall(r'physical id\s*:\s*(\d+).*?core id\s*:\s*(\d+)', f.read(), re.S))
        if pairs:
            return len(pairs)
    except Exception:
        pass
    return (os.cpu_count() or 0) // 2


def best_resolution():
    w, h = screen_size()
    wide10 = h > 0 and abs(w / h - 1.6) < 0.02
    v = vram_gb()
    if 0 < v <= 4:
        return '720'
    if 0 < v <= 6:
        h = min(h, 1200 if wide10 else 1080)
    elif 0 < v <= 8:
        h = min(h, 1600 if wide10 else 1440)
    if h >= 2160:
        return '2160'
    if wide10 and h >= 1600:
        return '1600'
    if h >= 1440:
        return '1440'
    if wide10 and h >= 1200:
        return '1200'
    return '1080'


# ---------------- extraction ----------------

def need_emu():
    refresh_runtime_paths()
    if not os.access(EMU, os.X_OK):
        fail('emulator not found; checked: %s' % ', '.join(EMU_CANDIDATES))


def extract(pkg, target_dir):
    need_emu()
    size = os.path.getsize(pkg)
    free = shutil.disk_usage(DATA_ROOT).free
    if free < size + (5 << 30):
        fail('мало места: свободно %.0f ГБ, нужно около %.0f ГБ' % (free / 2**30, (size + (5 << 30)) / 2**30))
    if os.path.exists(target_dir):
        say('Удаляю прежнюю папку %s...' % os.path.basename(target_dir))
        shutil.rmtree(target_dir)
    say('Распаковка %s (%.1f ГБ)...' % (os.path.basename(pkg), size / 2**30))
    os.makedirs(os.path.join(USER, 'log'), exist_ok=True)
    log = open(os.path.join(USER, 'log', 'extract.log'), 'w')
    p = subprocess.Popen([EMU, '--extract-pkg', pkg, '--extract-to', GAME], cwd=DATA_ROOT,
                         stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1)
    last_err = ''
    try:
        for line in p.stdout:
            log.write(line)
            parts = line.split()
            if parts[:1] == ['progress'] and len(parts) >= 5:
                done, total = int(parts[3]), max(1, int(parts[4]))
                print('\r  %5.1f / %.1f ГБ  (%d%%)' % (done / 2**30, total / 2**30, 100 * done // total),
                      end='', flush=True)
            elif line.startswith('error'):
                last_err = line.strip()
        p.wait()
    except KeyboardInterrupt:
        p.kill()
        p.wait()
        print()
        say('Отменено, удаляю недораспакованное...', 'warn')
        shutil.rmtree(target_dir, ignore_errors=True)
        sys.exit(2)
    finally:
        log.close()
    print()
    if not os.path.isfile(os.path.join(target_dir, 'eboot.bin')):
        shutil.rmtree(target_dir, ignore_errors=True)
        fail('не распаковалось (код %s): %s\nПодробности: user/log/extract.log' % (p.returncode, last_err))
    say('Распаковано.', 'ok')


def install_base(pkg):
    info = pkg_info(pkg)
    if info.get('CATEGORY') == 'gp':
        say('Это PKG обновления, ставлю как обновление.', 'warn')
        return install_update(pkg)
    if info:
        say('PKG: %s [%s] версия %s' % (info.get('TITLE'), info.get('TITLE_ID'), info.get('APP_VER')))
        if info.get('TITLE_ID') not in KNOWN_TITLES:
            say('ВНИМАНИЕ: проверено на CUSA37934 и CUSA37933, другие версии могут не работать.', 'warn')
    extract(pkg, GAME)


def install_update(pkg):
    if not os.path.isfile(os.path.join(GAME, 'eboot.bin')):
        fail('сначала установите базовую игру 1.00')
    info = pkg_info(pkg)
    if info and info.get('CATEGORY') != 'gp':
        fail('это не PKG обновления, а базовая игра')
    base = sfo_file(os.path.join(GAME, 'sce_sys', 'param.sfo'))
    if info.get('TITLE_ID') and base.get('TITLE_ID') and info['TITLE_ID'] != base['TITLE_ID']:
        fail('обновление для %s, а игра %s - регион должен совпадать' % (info['TITLE_ID'], base['TITLE_ID']))
    if info:
        say('Обновление: %s [%s] версия %s' % (info.get('TITLE'), info.get('TITLE_ID'), info.get('APP_VER')))
        if info.get('APP_VER') != '01.60':
            say('ВНИМАНИЕ: проверено только с версией 01.60.', 'warn')
    extract(pkg, UPDATE)


# ---------------- install steps ----------------

def patch_game(res=None):
    eb = eboot_path()
    if not os.path.isfile(eb):
        fail('нет game/eboot.bin')
    say('\n== Патчи игры (%s) ==' % os.path.relpath(eb, DATA_ROOT), 'head')
    if not os.path.isfile(eb + '.orig'):
        shutil.copy2(eb, eb + '.orig')
        say('Резервная копия: %s.orig' % os.path.relpath(eb, DATA_ROOT))
    for name, fn in (('Зависание перед первым вбрасыванием', lambda: freeze_fix(eb)),
                     ('Динамическое разрешение выключено', lambda: disable_drs(eb))):
        try:
            r = fn()
            say('%s: %s' % (name, 'уже исправлено' if r == 'already' else 'ok'), 'ok')
        except Exception as e:
            say('%s: НЕ применено - %s' % (name, e), 'warn')
    if res:
        try:
            set_render_res(eb, res)
            say('Разрешение рендера: %sp' % res, 'ok')
        except Exception as e:
            say('Разрешение НЕ изменено - %s' % e, 'warn')
    mark = os.path.join(USER, 'freezefix_%d.ok' % os.path.getsize(eb))
    with open(mark, 'w') as f:
        f.write('ok')


def desktop_dir():
    try:
        d = subprocess.run(['xdg-user-dir', 'DESKTOP'], capture_output=True, text=True, timeout=3).stdout.strip()
        if d and os.path.isdir(d):
            return d
    except Exception:
        pass
    for d in ('~/Desktop', '~/Рабочий стол'):
        d = os.path.expanduser(d)
        if os.path.isdir(d):
            return d
    return None


def wine_leftovers():
    """Menu/desktop entries Wine made for the repack (they start the old launcher through Wine)."""
    found = []
    roots = [os.path.expanduser('~/.local/share/applications/wine')]
    d = desktop_dir()
    if d:
        roots.append(d)
    for r in roots:
        for dp, _, files in os.walk(r):
            for f in files:
                p = os.path.join(dp, f)
                if not (f.endswith('.desktop') or f.endswith('.lnk')):
                    continue
                try:
                    txt = open(p, encoding='utf-8', errors='replace').read()
                except OSError:
                    continue
                if f.endswith('.lnk') or (re.search(r'wine', txt, re.I) and re.search(r'nhl', txt + f, re.I)):
                    if re.search(r'nhl', f + txt, re.I):
                        found.append(p)
    return found


def write_desktop_entries(on_desktop=True, remove_wine=False):
    apps = os.path.expanduser('~/.local/share/applications')
    os.makedirs(apps, exist_ok=True)
    icon = next((p for p in (os.path.join(UPDATE, 'sce_sys', 'icon0.png'),
                             os.path.join(GAME, 'sce_sys', 'icon0.png')) if os.path.isfile(p)), 'applications-games')
    me = os.path.realpath(__file__)
    gui = os.path.join(ROOT, 'nhl24-launcher.py')
    main = ('python3 "%s"' % gui) if os.path.isfile(gui) else ('python3 "%s" play' % me)
    language = (load_settings().get('lang') or
                ('ru' if os.environ.get('LANG', '').startswith('ru') else 'en'))
    settings_name = 'NHL 24 - настройки' if language == 'ru' else 'NHL 24 - Settings'
    entries = (('nhl24.desktop', 'NHL 24', main, 'false', 'StartupWMClass=nhl24-launcher\n'),
               ('nhl24-settings.desktop', settings_name, 'python3 "%s" settings' % me, 'true', ''))
    for fname, name, cmd, term, extra in entries:
        path = os.path.join(apps, fname)
        with open(path, 'w') as f:
            f.write('[Desktop Entry]\nType=Application\nName=%s\nComment=NHL 24 (PS4) - native Linux build\n'
                    'Exec=%s\nPath=%s\nIcon=%s\nTerminal=%s\nCategories=Game;SportsGame;\nKeywords=nhl;hockey;\n%s'
                    % (name, cmd, DATA_ROOT, icon, term, extra))
        os.chmod(path, 0o755)
    say('Ярлыки «NHL 24» и «NHL 24 - настройки» добавлены в меню приложений.', 'ok')

    d = desktop_dir()
    dst = os.path.join(d, 'NHL 24.desktop') if (on_desktop and d) else None
    old = [p for p in wine_leftovers() if p != dst]     # the desktop one is replaced below anyway
    if old and remove_wine:
        for p in old:
            os.remove(p)
        say('Удалены старые ярлыки Wine: %d.' % len(old), 'ok')
    elif old:
        say('Найдены старые ярлыки Wine (запускают нерабочий Wine-лаунчер):', 'warn')
        for p in old:
            print('   ' + p)
        say('Удалить их: ./nhl24-linux.py desktop --remove-wine', 'warn')

    if dst:
        shutil.copy(os.path.join(apps, 'nhl24.desktop'), dst)
        os.chmod(dst, 0o755)            # KDE Plasma runs only executable .desktop files from the desktop
        if shutil.which('gio'):         # GNOME and others: mark as trusted
            subprocess.run(['gio', 'set', dst, 'metadata::trusted', 'true'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        say('Ярлык на рабочем столе: %s (при первом запуске Plasma спросит - ответьте «Выполнить»).' % dst, 'ok')


def cmd_desktop(args):
    write_desktop_entries(on_desktop='--no-desktop' not in args, remove_wine='--remove-wine' in args)


def cmd_install(args):
    if not args:
        fail('укажите PKG: install BASE.pkg [UPDATE.pkg]')
    need_emu()
    say('== NHL 24 PC - установка для Linux ==', 'head')
    install_base(args[0])
    if len(args) > 1:
        install_update(args[1])
    res = best_resolution()
    patch_game(res)

    say('\n== Настройки эмулятора ==', 'head')
    cores = physical_cores()
    fps = 60 if not cores else 120 if cores >= 8 else 90 if cores >= 6 else 60
    v = vram_gb()
    say('Видеопамять: %s ГБ, ядер: %s -> лимит FPS %d. PS4 Pro на Linux выключен.' % (v or '?', cores or '?', fps))
    set_config({'readbacks_mode': 1, 'present_mode': 'Immediate', 'vblank_frequency': fps,
                'neo_mode': False, 'extra_dmem_in_mbytes': 3500, 'full_screen': True,
                'hdr_allowed': False, 'show_splash': True})
    s = load_settings()
    s.update({'resolution': res, 'fps': fps})
    save_settings(s)
    write_desktop_entries()
    say('\nГОТОВО. Запуск: «NHL 24» в меню или ./nhl24-linux.py play', 'ok')
    say('Первый запуск: 5-15 минут чёрного экрана, пока компилируются шейдеры.', 'warn')


def cmd_update(args):
    if not args:
        fail('укажите PKG обновления')
    install_update(args[0])
    patch_game(load_settings()['resolution'])


def cmd_remove_update(_):
    if os.path.isdir(UPDATE):
        shutil.rmtree(UPDATE)
        say('Обновление удалено, игра снова версии 1.00.', 'ok')
    else:
        say('Обновление не установлено.')


def cmd_status(_):
    eb = eboot_path()
    if not os.path.isfile(eb):
        fail('игра не установлена')
    sfo = sfo_file(os.path.join(os.path.dirname(eb), 'sce_sys', 'param.sfo'))
    say('Игра: %s [%s] версия %s' % (sfo.get('TITLE'), sfo.get('TITLE_ID'), sfo.get('APP_VER')))
    for name, fn in (('Разрешение рендера', lambda: get_render_res(eb) + 'p'),
                     ('Динамическое разрешение', lambda: drs_state(eb)),
                     ('Русский текст', lambda: 'да' if russian_state(eb) else 'нет')):
        try:
            say('%s: %s' % (name, fn()))
        except Exception as e:
            say('%s: ? (%s)' % (name, e), 'warn')
    say('Эмулятор: %s' % (EMU if os.access(EMU, os.X_OK) else 'НЕТ'))


# ---------------- settings menu ----------------

def cmd_settings(_):
    s = load_settings()
    has_update = os.path.isfile(os.path.join(UPDATE, 'eboot.bin'))

    def cycle(seq, cur):
        seq = list(seq)
        return seq[(seq.index(cur) + 1) % len(seq)] if cur in seq else seq[0]

    yn = lambda b: 'вкл' if b else 'выкл'
    items = [
        ('Разрешение', lambda: s['resolution'] + 'p', lambda: s.update(resolution=cycle(RES_MODES, str(s['resolution'])))),
        ('Полный экран', lambda: yn(s['fullscreen']), lambda: s.update(fullscreen=not s['fullscreen'])),
        ('Лимит FPS', lambda: str(s['fps']), lambda: s.update(fps=cycle(FPS_VALUES, int(s['fps'])))),
        ('Вертикальная синхронизация', lambda: yn(s['vsync']), lambda: s.update(vsync=not s['vsync'])),
        ('Апскейл FSR', lambda: yn(s['fsr']), lambda: s.update(fsr=not s['fsr'])),
        ('Русский текст в игре' + ('' if has_update else ' (нужно обновление 1.60)'),
         lambda: yn(s['ru_text']), lambda: s.update(ru_text=not s['ru_text'])),
        ('Стабильный режим (если вылетает)', lambda: yn(s['stable']), lambda: s.update(stable=not s['stable'])),
        ('Диагностика', lambda: yn(s['diag']), lambda: s.update(diag=not s['diag'])),
    ]
    while True:
        print('\n== NHL 24 - настройки ==')
        for i, (name, val, _) in enumerate(items, 1):
            print('  %2d. %-40s %s' % (i, name, val()))
        print('   P. Играть      S. Сохранить и выйти      Q. Выйти без сохранения')
        print('  (режим PS4 Pro на Linux всегда выключен)')
        try:
            a = input('Выбор: ').strip().lower()
        except EOFError:
            return
        if a in ('q', 'й'):
            return
        if a in ('s', 'ы', 'p', 'з'):
            save_settings(s)
            say('Сохранено.', 'ok')
            if a in ('p', 'з'):
                cmd_play([])
            return
        if a.isdigit() and 1 <= int(a) <= len(items):
            items[int(a) - 1][2]()


# ---------------- play ----------------

def gpu_names():
    names = []
    try:
        base = '/proc/driver/nvidia/gpus'
        for g in os.listdir(base):
            with open(os.path.join(base, g, 'information')) as f:
                m = re.search(r'Model:\s*(.+)', f.read())
                if m:
                    names.append('NVIDIA ' + m.group(1).strip())
    except OSError:
        pass
    if shutil.which('lspci'):
        try:
            out = subprocess.run(['lspci'], capture_output=True, text=True, timeout=5).stdout
            names += [l.split(': ', 1)[1] for l in out.splitlines()
                      if re.search(r'VGA|3D controller|Display controller', l) and ': ' in l]
        except Exception:
            pass
    return names


def prepare_launch(s):
    """Patch eboot + write config.json from settings s; returns the environment for the emulator."""
    eb = eboot_path()
    has_update = os.path.isfile(os.path.join(UPDATE, 'eboot.bin'))
    res = str(s['resolution']) if str(s['resolution']) in RES_MODES else '1080'
    try:
        if get_render_res(eb) != res:
            set_render_res(eb, res)
        if drs_state(eb) != 'off':
            disable_drs(eb)
    except Exception as e:
        say('Разрешение/DRS не применены: %s' % e, 'warn')
    try:
        set_russian_text(eb, has_update and bool(s['ru_text']))
    except Exception as e:
        say('Русский текст не применён: %s' % e, 'warn')
    mark = os.path.join(USER, 'freezefix_%d.ok' % os.path.getsize(eb))
    if not os.path.isfile(mark):
        try:
            freeze_fix(eb)
            with open(mark, 'w') as f:
                f.write('ok')
        except Exception as e:
            say('Фикс зависания не применён: %s' % e, 'warn')

    sw, sh = screen_size()
    rw, rh = RES_MODES[res]
    ww = min(rw, int(sw * 0.85))
    wh = int(ww * rh / rw)
    if wh > int(sh * 0.85):
        wh = int(sh * 0.85)
        ww = int(wh * rw / rh)
    set_config({
        'hdr_allowed': s.get('hdr') == 'on', 'vblank_frequency': int(s['fps']), 'neo_mode': False,
        'full_screen': bool(s['fullscreen']), 'window_width': ww, 'window_height': wh,
        'extra_dmem_in_mbytes': 0 if res in ('720', '1080', '1200') else 3500,
        'readbacks_mode': 1, 'present_mode': 'Fifo' if s['vsync'] else 'Immediate',
        'show_fps_counter': False, 'show_splash': True, 'redzone_patches': False,
        'fsr_enabled': bool(s['fsr']), 'rcas_enabled': bool(s['fsr']),
    })

    env = dict(os.environ)
    for key, on, val in (('NHLPORT_DIAG', s['diag'], '1'),
                         ('NHLPORT_READBACK_DRAIN', s['stable'], '1')):
        if on:
            env[key] = val
        else:
            env.pop(key, None)
    return env


def cmd_play(_):
    need_emu()
    s = load_settings()
    if not os.path.isfile(eboot_path()):
        fail('игра не установлена: ./nhl24-linux.py install BASE.pkg [UPDATE.pkg]')
    env = prepare_launch(s)
    os.makedirs(os.path.join(USER, 'log'), exist_ok=True)
    log = open(os.path.join(USER, 'log', 'linux_run.log'), 'w')
    say('Запуск NHL 24... (лог: user/log/linux_run.log)', 'ok')
    os.chdir(DATA_ROOT)
    os.dup2(log.fileno(), 1)
    os.dup2(log.fileno(), 2)
    os.execve(EMU, [EMU], env)


# ---------------- national teams mod (port of mods.ps1) ----------------
# A mod is tools/mods/<n>.json + <n>.dat. It never ships whole game files: a new cas_02.cas is
# assembled from the player's own cas_01.cas (unchanged compressed blocks are copied) plus the few
# changed blocks from the .dat, then chunk entries (12 bytes each) in Patch/Gen4b/globals.toc are
# pointed at it. Removing writes the original entries back and deletes cas_02.cas. Everything is
# checked by SHA-1 before writing.

MOD_DIR = os.path.join(DATA_ROOT, 'tools', 'mods')
MOD_BACKUP = os.path.join(USER, 'mods_backup')


def sha1(b):
    import hashlib
    return hashlib.sha1(b).hexdigest()


def game_running():
    try:
        for pid in os.listdir('/proc'):
            if pid.isdigit():
                try:
                    if os.path.realpath('/proc/%s/exe' % pid) == os.path.realpath(EMU):
                        return True
                except OSError:
                    pass
    except OSError:
        pass
    return False


def mod_source_ok(m, patch_dir):
    src = os.path.join(patch_dir, m['cas_src'])
    if not os.path.isfile(src):
        return False
    for c in m['chunks']:
        try:
            b = read_at(src, int(c['src_offset']), int(c['src_size']))
        except OSError:
            return False
        if len(b) != int(c['src_size']) or sha1(b) != c['src_sha1']:
            return False
    return True


def find_mod(toc, legacy, patch_dir):
    if not os.path.isdir(MOD_DIR):
        return None
    for fn in sorted(os.listdir(MOD_DIR)):
        if not fn.endswith('.json'):
            continue
        m = read_json(os.path.join(MOD_DIR, fn))
        if not isinstance(m, dict) or m.get('format') != 1 or len(toc) != int(m.get('toc_size', -1)):
            continue
        if bool(m.get('legacy')) != legacy:
            continue
        n_orig = n_new = 0
        for c in m['chunks']:
            o = int(c['toc_entry_offset'])
            if toc[o:o + 12] == bytes.fromhex(c['toc_orig']):
                n_orig += 1
            elif toc[o:o + 12] == bytes.fromhex(c['toc_new']):
                n_new += 1
        st = 'off' if n_orig == len(m['chunks']) else 'on' if n_new == len(m['chunks']) else ''
        if not st or (legacy and st != 'on'):
            continue
        if st == 'on' and not mod_source_ok(m, patch_dir):
            continue
        m['file'] = os.path.join(MOD_DIR, fn)
        m['state'] = st
        return m
    return None


def cmd_mod(args):
    action = args[0] if args else ''
    patch_dir = os.path.join(UPDATE, 'Patch', 'Gen4b')
    toc_path = os.path.join(patch_dir, 'globals.toc')
    say('== Мод сборных России и Беларуси ==', 'head')
    if not os.path.isfile(toc_path):
        fail('не установлено обновление 1.60 (нет game-UPDATE/Patch/Gen4b/globals.toc)')
    if not os.path.isdir(MOD_DIR):
        fail('нет папки tools/mods с файлами мода (.json + .dat) из сборки')
    with open(toc_path, 'rb') as f:
        toc = bytearray(f.read())

    old = None
    mod = find_mod(toc, False, patch_dir)
    if not mod:
        # an older version of the mod is installed: take its entries back in memory, look again
        old = find_mod(toc, True, patch_dir)
        if old:
            for c in old['chunks']:
                o = int(c['toc_entry_offset'])
                toc[o:o + 12] = bytes.fromhex(c['toc_orig'])
            mod = find_mod(toc, False, patch_dir)
    if not mod:
        bk = os.path.join(MOD_BACKUP, 'globals.toc.orig')
        fail('эта версия игры не поддерживается (мод для 1.60, CUSA37934/CUSA37933). Если поверх '
             'globals.toc ставили другой мод - сначала верните оригинал' + (': ' + bk if os.path.isfile(bk) else '.'))
    state = mod['state']
    cas_src = os.path.join(patch_dir, mod['cas_src'])
    cas_dst = os.path.join(patch_dir, mod['cas_dst'])
    dat_path = os.path.join(MOD_DIR, mod['dat']) if mod.get('dat') else os.path.splitext(mod['file'])[0] + '.dat'

    if action == 'status':
        print('old' if old else state)
        return
    if old:
        say('Установлена СТАРАЯ версия мода: install обновит её, remove удалит.', 'warn')
    else:
        say('Мод %s.' % ('УСТАНОВЛЕН' if state == 'on' else 'не установлен'), 'ok' if state == 'on' else '')
    if not action:
        print('  1 = установить (Польша -> Россия, Великобритания -> Беларусь)\n  2 = удалить (вернуть оригинал)')
        a = input('Выбор (1 / 2, Enter = выход): ').strip()
        action = {'1': 'install', '2': 'remove'}.get(a)
        if not action:
            return
    if game_running():
        fail('сначала закройте игру')

    def write_toc():
        with open(toc_path, 'wb') as f:
            f.write(toc)

    if action == 'remove':
        if state == 'off':
            if old:
                write_toc()       # entries of the old version already taken back
            if os.path.isfile(cas_dst):
                os.remove(cas_dst)
            say('Готово: мод удалён.' if old else 'Удалять нечего, файлы игры оригинальные.', 'ok')
            return
        for c in mod['chunks']:
            o = int(c['toc_entry_offset'])
            toc[o:o + 12] = bytes.fromhex(c['toc_orig'])
        write_toc()
        if os.path.isfile(cas_dst):
            os.remove(cas_dst)
        say('Готово: мод удалён, Польша и Великобритания вернулись.', 'ok')
        return
    if action != 'install':
        fail('неизвестное действие: %s (install | remove | status)' % action)

    if not os.path.isfile(cas_src):
        fail('не найден файл: ' + cas_src)
    if not os.path.isfile(dat_path):
        fail('не найден файл: ' + dat_path)
    if state == 'off' and os.path.isfile(cas_dst):
        # cas_02 is not part of the game; refuse only if another chunk in globals.toc points at it
        b = 0x22C
        cnt, eo = struct.unpack_from('>II', toc, b + 20)
        for i in range(cnt):
            p = b + eo + 12 * i
            if p + 12 <= len(toc) and toc[p + 2] == 0 and toc[p + 3] == 2:
                fail('cas_02.cas уже использует что-то другое в globals.toc - другой мод? Сначала удалите его.')

    say('Проверяю файлы игры...')
    with open(dat_path, 'rb') as f:
        dat = f.read()
    if sha1(dat) != mod['dat_sha1']:
        fail('файл мода повреждён: %s - скачайте сборку заново' % dat_path)
    for c in mod['chunks']:
        if sha1(read_at(cas_src, int(c['src_offset']), int(c['src_size']))) != c['src_sha1']:
            fail('cas_01.cas не совпадает с обновлением 1.60 - переустановите обновление: ./nhl24-linux.py update UPDATE.pkg')

    say('Собираю cas_02.cas...')
    tmp = cas_dst + '.tmp'
    import hashlib
    h = hashlib.sha1()
    try:
        with open(cas_src, 'rb') as fin, open(tmp, 'wb') as fout:
            for c in mod['chunks']:
                if fout.tell() != int(c['toc_new'][8:16], 16):
                    raise RuntimeError('layout mismatch')
                for op in c['ops']:
                    off, ln = int(op[1]), int(op[2])
                    if op[0] == 'd':
                        piece = dat[off:off + ln]
                        fout.write(piece)
                        h.update(piece)
                        continue
                    fin.seek(off)
                    while ln > 0:
                        piece = fin.read(min(ln, 1 << 20))
                        if not piece:
                            raise RuntimeError('unexpected end of cas_01.cas')
                        fout.write(piece)
                        h.update(piece)
                        ln -= len(piece)
            size = fout.tell()
    except Exception as e:
        if os.path.exists(tmp):
            os.remove(tmp)
        fail('не удалось собрать cas_02.cas (%s). Ничего не изменено.' % e)
    if size != int(mod['dst_size']) or h.hexdigest() != mod['dst_sha1']:
        os.remove(tmp)
        fail('не удалось собрать cas_02.cas (проверка не прошла). Ничего не изменено.')
    os.replace(tmp, cas_dst)

    if state == 'off':
        os.makedirs(MOD_BACKUP, exist_ok=True)
        bk = os.path.join(MOD_BACKUP, 'globals.toc.orig')
        if not os.path.isfile(bk):
            with open(bk, 'wb') as f:
                f.write(toc)
        for c in mod['chunks']:
            o = int(c['toc_entry_offset'])
            toc[o:o + 12] = bytes.fromhex(c['toc_new'])
        write_toc()
    elif old:
        write_toc()
    say('Готово: Россия и Беларусь есть в «Сборных» и на чемпионате мира IIHF.', 'ok')

    # saved rosters replace the game's own team database in Play Now
    import glob, time
    rosters = [r for r in glob.glob(os.path.join(USER, 'home', '*', 'savedata', 'CUSA3793*', 'RosterCustom*'))
               if os.path.isdir(r)]
    if rosters and sys.stdin.isatty():
        say('\nУ вас есть сохранённые ростеры (%d). Игра берёт их вместо стандартных, и новых сборных '
            'видно не будет. Убрать их в user/mods_backup/saved_rosters (можно вернуть)?' % len(rosters), 'warn')
        if input('y = да, Enter = нет: ').strip().lower()[:1] in ('y', 'д', 'н'):
            stamp = time.strftime('%Y%m%d_%H%M%S')
            for r in rosters:
                parts = r.rstrip('/').split(os.sep)
                dst = os.path.join(MOD_BACKUP, 'saved_rosters', '_'.join([parts[-4], parts[-2], parts[-1], stamp]))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.move(r, dst)
            say('Сохранённые ростеры убраны.', 'ok')


COMMANDS = {'desktop': cmd_desktop, 'install': cmd_install, 'update': cmd_update, 'remove-update': cmd_remove_update,
            'settings': cmd_settings, 'play': cmd_play, 'status': cmd_status, 'mod': cmd_mod}

if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        print(__doc__ or '', end='')
        print('Команды: install BASE.pkg [UPDATE.pkg] | update UPDATE.pkg | remove-update | settings | play | status |\n'
              '         mod [install|remove|status] | desktop [--remove-wine] [--no-desktop]')
        sys.exit(0 if len(sys.argv) < 2 else 1)
    try:
        COMMANDS[sys.argv[1]](sys.argv[2:])
    except RuntimeError as e:
        fail(str(e))
