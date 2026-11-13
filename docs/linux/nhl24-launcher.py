#!/usr/bin/env python3
# NHL 24 PC - native Linux launcher. Uses nhl24-linux.py for package installation,
# game patches, emulator configuration and launching. Python 3.8+ with Tk 8.6.
import importlib.util, math, os, re, shutil, subprocess, sys, threading, time, zipfile

HERE = os.path.dirname(os.path.realpath(__file__))
_spec = importlib.util.spec_from_file_location('nhlcore', os.path.join(HERE, 'nhl24-linux.py'))
core = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(core)

try:
    import tkinter as tk
    from tkinter import font as tkfont
    from tkinter import filedialog
except ImportError:
    print('Нет Tkinter. Gentoo: включите USE="tk" для dev-lang/python и пересоберите:\n'
          '  echo "dev-lang/python tk" | sudo tee -a /etc/portage/package.use/python\n'
          '  sudo emerge -av1 dev-lang/python\n'
          'Debian/Ubuntu: sudo apt install python3-tk   Arch: sudo pacman -S tk')
    sys.exit(1)

SCRIPT_ROOT = core.ROOT
AUTHOR = 'HajDEO'
CHANNEL_URL = 'https://github.com/HajDEO/nhl24pc'
DONATE = [('Ko-fi', 'https://ko-fi.com/hajdeo')]
LAYOUT_W, LAYOUT_H = 1080, 620

# palette: deep ink, graphite cards, ice-blue accent, goal-light red for PLAY, gold for support
BG, PANEL, FIELD, LINE, FOOT = (10, 13, 19), (20, 25, 36), (27, 33, 48), (36, 44, 61), (14, 18, 26)
TEXT, MUTED, ACCENT = (236, 239, 244), (138, 147, 166), (124, 196, 255)
RED, RED_DK, GOLD, WARN = (214, 32, 56), (150, 14, 36), (226, 182, 92), (255, 186, 84)
WHITE, BLACK = (255, 255, 255), (0, 0, 0)
LIST_BG, LIST_HI, TIP_EDGE, DISABLED = (22, 27, 39), (40, 62, 96), (62, 76, 102), (88, 96, 112)


def hx(c):
    return '#%02x%02x%02x' % tuple(max(0, min(255, int(v))) for v in c)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


# ---------------- texts (launcher.ps1, adapted where Linux differs) ----------------
T = {
  'en': dict(
    display='DISPLAY', resolution='Resolution', mode='Display mode', hdr='HDR',
    rutext='Russian game text (pick English in the game)', rutextNeed='Russian game text (needs update 1.60)',
    rutextTip='Puts the Russian translation from the game files in place of English.\nAt the language screen of the game pick English.',
    res=['720p  ·  4 GB video cards', '1080p  ·  Full HD', '1200p  ·  16:10', '1440p  ·  QHD', '1600p  ·  16:10', '2160p  ·  4K'],
    modes=['Fullscreen', 'Windowed'], hdrs=['Off', 'On (experimental)'],
    frames='FRAME RATE', fpscap='FPS cap',
    fps=['60  ·  console, steadiest', '90  ·  6-core CPU', '120  ·  8+ core CPU', '144  ·  experimental'],
    vsync='V-Sync (no tearing)', quality='GRAPHICS', pro='PS4 Pro mode',
    proTip='On Linux the PS4 Pro mode crashes the game on the first frames (texture_cache.cpp:585), so it is always off.',
    fsr='FSR upscaling', fsrTip='Sharper picture when the game renders below the screen resolution.',
    stability='STABILITY', playtime='Played {0} h {1} min',
    stable='Stable mode (for frequent crashes)',
    stableTip='Turn on if the game often crashes or freezes ("device lost", crash on the second match, on goals).\n'
              'The emulator then reads data back from the video card in the safest way.\nCosts a few FPS; leave it off if the game runs fine.',
    stableHint='\n\nAlso turn on "Stable mode" in the launcher.',
    diag='Diagnostics (detailed log)', diagTip='Only when something is wrong: writes a detailed log for the crash report. Costs some FPS.',
    play='PLAY', install='Install PKG', installing='Installing…', installTitle='Install NHL 24 package',
    installDone='Package installed successfully.', installFail='Package installation failed:\n{0}',
    guide='Guide', logs='Crash log', weak='Weak PC preset',
    kbd='Keyboard', kbdTitle='Keyboard controls',
    kbdNote="The game plays best with a gamepad (Xbox / DualShock / DualSense).\nOn a keyboard pick the HYBRID control scheme in the "
            "game's settings (game update 1.60): shot, pass and hit are on buttons.",
    kbdHint='Click a key, then press the new key. Esc = cancel. F1-F12 and Delete are used by the emulator.',
    kbdPress='press a key...', kbdSave='Save', kbdDefault='Defaults', kbdCancel='Cancel',
    kbdSaved='Keyboard controls saved. In a running game press F8 to reload them.',
    kbdNames=['Cross (X)', 'Circle (O)', 'Square', 'Triangle', 'L1', 'R1', 'L2', 'R2', 'L3 (left stick click)', 'R3 (right stick click)',
              'Options', 'Touchpad', 'D-pad up', 'D-pad down', 'D-pad left', 'D-pad right', 'Left stick up', 'Left stick down',
              'Left stick left', 'Left stick right', 'Right stick up', 'Right stick down', 'Right stick left', 'Right stick right'],
    weakDone='Weak PC preset is set:\n1080p (720p on 4 GB cards), FPS cap 60, FSR on and stable mode on.\n\nPress PLAY.',
    reportDone='Crash report saved:\n{0}\n\nAttach it to a GitHub issue and describe what you were doing when the game crashed.',
    reportFail='Could not create the crash report: ',
    crashAsk='\n\nCreate a crash report (zip on the Desktop) to send to the author?',
    author='by {0}', video='Project repository', support='Support the author', supportTitle='Support the author',
    supportText='If you enjoy the project, you can support its author. Thank you!', copy='Copy', copied='Copied', close='Close',
    notInstalled='Game not installed - click Install PKG.', gameLine='{0}  ·  version {1}',
    edition='NHL24 PC  ·  v{0}  ·  LINUX',
    msgNotInstalled='The game is not installed. Click Install PKG and select your base-game package.',
    msgNoEmu='The native Linux emulator was not found. Build it first or use the Linux package.',
    msgRunning='The game is already running.', msgFail='Could not start the game: ',
    tStart='Starting NHL 24...', tCompile='Compiling new shaders: {0}  (cache {1})    {2}:{3:02d}',
    tLoad='Loading...  shader cache: {0} ready    {1}:{2:02d}',
    tFirst='First start: 5-15 min of black screen is normal. Do not close the game.',
    crashTitle='NHL 24 PC - the game crashed', fpsTitle='NHL 24 PC - smoother play',
    fpsAdvice='In the last game the FPS was often below the cap of {0} (typical {1} FPS). Uneven frames feel like micro-stutter '
              'and input lag.\n\nSet the cap to {2} FPS for smoother play?',
    crashVram='Not enough video memory.\n\nIn the launcher: Resolution 1440p or 1080p, or press "Weak PC preset". '
              'A 4 GB video card is below what the game needs: crashes on it cannot be removed completely.',
    crashLost='The video driver reset the GPU (device lost).\n\nLower the FPS cap or resolution, close recording and overlays, '
              'or press "Weak PC preset" in the launcher.',
    crashOther='The game crashed (game {0}).\n\nIf it keeps crashing, try "Weak PC preset" in the launcher.',
    readme='README_ENG.txt', noGuide='Guide not found: {0}',
    ok='OK', yes='Yes', no='No', cancel='Cancel'),
  'ru': dict(
    display='ЭКРАН', resolution='Разрешение', mode='Режим экрана', hdr='HDR',
    rutext='Русский текст в игре (в игре выбери English)', rutextNeed='Русский текст в игре (нужен патч 1.60)',
    rutextTip='Ставит русский перевод из файлов игры на место английского.\nНа экране выбора языка в игре выбери English.',
    res=['720p  ·  карты на 4 ГБ', '1080p  ·  Full HD', '1200p  ·  16:10', '1440p  ·  2K', '1600p  ·  16:10', '2160p  ·  4K'],
    modes=['Полный экран', 'В окне'], hdrs=['Выкл', 'Вкл (эксперимент)'],
    frames='КАДРЫ', fpscap='Лимит FPS',
    fps=['60  ·  как на консоли, ровнее всего', '90  ·  процессор от 6 ядер', '120  ·  процессор от 8 ядер', '144  ·  эксперимент'],
    vsync='V-Sync (без разрывов кадра)', quality='ГРАФИКА', pro='Режим PS4 Pro',
    proTip='На Linux режим PS4 Pro роняет игру на первых кадрах (texture_cache.cpp:585), поэтому он всегда выключен.',
    fsr='Апскейл FSR', fsrTip='Чётче картинка, когда игра рендерит ниже разрешения экрана.',
    stability='СТАБИЛЬНОСТЬ', playtime='Наиграно {0} ч {1} мин',
    stable='Стабильный режим (если часто вылетает)',
    stableTip='Включай, если игра часто вылетает или зависает ("device lost", вылет на втором матче, на голах).\n'
              'Эмулятор тогда читает данные с видеокарты самым надёжным способом.\nСтоит немного FPS; если игра работает нормально - не включай.',
    stableHint='\n\nТакже включи «Стабильный режим» в лаунчере.',
    diag='Диагностика (подробный лог)', diagTip='Только если что-то не так: пишет подробный лог для отчёта о вылете. Немного ест FPS.',
    play='ИГРАТЬ', install='Установить PKG', installing='Установка…', installTitle='Установка PKG NHL 24',
    installDone='PKG успешно установлен.', installFail='Ошибка установки PKG:\n{0}',
    guide='Гайд', logs='Отчёт', weak='Пресет «Слабый ПК»',
    kbd='Клавиатура', kbdTitle='Управление с клавиатуры',
    kbdNote='В игру лучше играть на геймпаде (Xbox / DualShock / DualSense).\nНа клавиатуре выбери в настройках управления игры '
            'схему ГИБРИД (нужен патч 1.60): бросок, пас и силовой на кнопках.',
    kbdHint='Нажми на клавишу в списке, затем новую клавишу. Esc - отмена. F1-F12 и Delete заняты эмулятором.',
    kbdPress='нажми клавишу...', kbdSave='Сохранить', kbdDefault='По умолчанию', kbdCancel='Отмена',
    kbdSaved='Управление сохранено. Если игра запущена, нажми в ней F8, чтобы применить.',
    kbdNames=['Крестик (X)', 'Кружок (O)', 'Квадрат', 'Треугольник', 'L1', 'R1', 'L2', 'R2', 'L3 (нажатие левого стика)',
              'R3 (нажатие правого стика)', 'Options', 'Тачпад', 'Крестовина вверх', 'Крестовина вниз', 'Крестовина влево',
              'Крестовина вправо', 'Левый стик вверх', 'Левый стик вниз', 'Левый стик влево', 'Левый стик вправо',
              'Правый стик вверх', 'Правый стик вниз', 'Правый стик влево', 'Правый стик вправо'],
    weakDone='Пресет «Слабый ПК» выставлен:\n1080p (720p на картах 4 ГБ), лимит 60 FPS, FSR и стабильный режим вкл.\n\nЖми ИГРАТЬ.',
    reportDone='Отчёт о вылете сохранён:\n{0}\n\nПриложи его к issue на GitHub и опиши, что делал, когда игра вылетела.',
    reportFail='Не удалось создать отчёт: ',
    crashAsk='\n\nСоздать отчёт о вылете (zip на рабочем столе), чтобы отправить автору?',
    author='by {0}', video='Репозиторий проекта', support='Поддержать автора', supportTitle='Поддержать автора',
    supportText='Если нравится проект, можно поддержать его автора. Спасибо!', copy='Копировать', copied='Скопировано', close='Закрыть',
    notInstalled='Игра не установлена - нажми «Установить PKG»', gameLine='{0}  ·  версия {1}',
    edition='NHL24 PC  ·  v{0}  ·  LINUX',
    msgNotInstalled='Игра не установлена. Нажми «Установить PKG» и выбери PKG базовой игры.',
    msgNoEmu='Нативный Linux-эмулятор не найден. Сначала собери его или используй Linux-пакет.',
    msgRunning='Игра уже запущена.', msgFail='Не удалось запустить игру: ',
    tStart='Запуск NHL 24...', tCompile='Сборка новых шейдеров: {0}  (в кэше {1})    {2}:{3:02d}',
    tLoad='Загрузка...  шейдеров в кэше: {0}    {1}:{2:02d}',
    tFirst='Первый запуск: 5-15 минут чёрного экрана - это нормально. Не закрывай игру.',
    crashTitle='NHL 24 PC - игра вылетела', fpsTitle='NHL 24 PC - плавность',
    fpsAdvice='В прошлой игре FPS часто не дотягивал до лимита {0} (обычно {1} FPS). Неровные кадры ощущаются как микрофризы '
              'и задержка управления.\n\nПоставить лимит {2} FPS, чтобы игра шла ровнее?',
    crashVram='Не хватило видеопамяти.\n\nВ лаунчере поставь разрешение 1440p или 1080p или нажми «Слабый ПК». '
              'Видеокарты на 4 ГБ игре мало: вылеты на них полностью не убрать.',
    crashLost='Драйвер видеокарты сбросил её (device lost).\n\nПонизь лимит FPS или разрешение, закрой запись и оверлеи '
              'или нажми в лаунчере «Слабый ПК».',
    crashOther='Игра вылетела (игра {0}).\n\nЕсли вылеты повторяются, попробуй «Слабый ПК» в лаунчере.',
    readme='README_RUS.txt', noGuide='Гайд не найден: {0}',
    ok='OK', yes='Да', no='Нет', cancel='Отмена'),
}

RES_KEYS = ['720', '1080', '1200', '1440', '1600', '2160']
FPS_VALS = [60, 90, 120, 144]
KBD_ACTIONS = ['cross', 'circle', 'square', 'triangle', 'l1', 'r1', 'l2', 'r2', 'l3', 'r3', 'options', 'touchpad_center',
               'pad_up', 'pad_down', 'pad_left', 'pad_right',
               'axis_left_y_minus', 'axis_left_y_plus', 'axis_left_x_minus', 'axis_left_x_plus',
               'axis_right_y_minus', 'axis_right_y_plus', 'axis_right_x_minus', 'axis_right_x_plus']
KBD_DEFAULTS = dict(cross='n', circle='b', square='v', triangle='c', l1='q', r1='u', l2='e', r2='o', l3='x', r3='m',
                    options='enter', touchpad_center='space', pad_up='up', pad_down='down', pad_left='left', pad_right='right',
                    axis_left_y_minus='w', axis_left_y_plus='s', axis_left_x_minus='a', axis_left_x_plus='d',
                    axis_right_y_minus='i', axis_right_y_plus='k', axis_right_x_minus='j', axis_right_x_plus='l')
KBD_INI = os.path.join(core.USER, 'input_config', 'default.ini')

# Tk keysym -> shadPS4 key name (input_handler.h). Cyrillic keysyms map to the same physical key (ЙЦУКЕН).
_KEYSYMS = {'space': 'space', 'return': 'enter', 'backspace': 'backspace', 'tab': 'tab', 'up': 'up', 'down': 'down',
            'left': 'left', 'right': 'right', 'insert': 'insert', 'home': 'home', 'end': 'end', 'prior': 'pgup',
            'next': 'pgdown', 'caps_lock': 'capslock', 'kp_add': 'kpplus', 'kp_subtract': 'kpminus',
            'kp_multiply': 'kpasterisk', 'kp_divide': 'kpslash', 'kp_decimal': 'kpperiod', 'comma': 'comma',
            'period': 'period', 'slash': 'slash', 'semicolon': 'semicolon', 'apostrophe': 'apostrophe',
            'bracketleft': 'lbracket', 'bracketright': 'rbracket', 'backslash': 'backslash', 'minus': 'minus',
            'equal': 'equals', 'grave': 'grave', 'shift_l': 'lshift', 'shift_r': 'rshift', 'control_l': 'lctrl',
            'control_r': 'rctrl', 'alt_l': 'lalt', 'alt_r': 'ralt', 'iso_level3_shift': 'ralt'}
_CYR = dict(shorti='q', tse='w', u='e', ka='r', ie='t', en='y', ghe='u', sha='i', shcha='o', ze='p', ha='lbracket',
            hardsign='rbracket', ef='a', yeru='s', ve='d', a='f', pe='g', er='h', o='j', el='k', de='l', zhe='semicolon',
            e='apostrophe', ya='z', che='x', es='c', em='v', i='b', te='n', softsign='m', be='comma', yu='period', io='grave')


def key_name(keysym):
    k = keysym.lower()
    if len(k) == 1 and ('a' <= k <= 'z' or '0' <= k <= '9'):
        return k
    m = re.fullmatch(r'kp_([0-9])', k) or re.fullmatch(r'kp_(insert|end|down|next|left|begin|right|home|up|prior)', k)
    if m and m.group(1).isdigit():
        return 'kp' + m.group(1)
    if k.startswith('cyrillic_'):
        return _CYR.get(k[9:])
    return _KEYSYMS.get(k)


def read_kbd():
    b = dict(KBD_DEFAULTS)
    try:
        in_kbd = False
        for line in open(KBD_INI, encoding='utf-8-sig'):
            if re.match(r'^\s*#.*Keyboard', line):
                in_kbd = True
                continue
            if re.match(r'^\s*#.*Controller', line):
                in_kbd = False
                continue
            m = re.match(r'^\s*([a-z0-9_]+)\s*=\s*([^#]+?)\s*$', line)
            if in_kbd and m and m.group(1) in b:
                b[m.group(1)] = m.group(2)
    except OSError:
        pass
    return b


def save_kbd(b):
    try:
        lines = open(KBD_INI, encoding='utf-8-sig').read().splitlines()
    except OSError:
        lines = ['# Keyboard bindings', '', '# Controller bindings']
    done, in_kbd, kbd_end = set(), False, -1
    for i, line in enumerate(lines):
        if re.match(r'^\s*#.*Keyboard', line):
            in_kbd, kbd_end = True, i + 1
            continue
        if re.match(r'^\s*#.*Controller', line):
            in_kbd = False
            continue
        m = re.match(r'^\s*([a-z0-9_]+)\s*=', line)
        if in_kbd and m and m.group(1) in b:
            lines[i] = '%s = %s' % (m.group(1), b[m.group(1)])
            done.add(m.group(1))
            kbd_end = i + 1
    if kbd_end < 0:
        lines.insert(0, '# Keyboard bindings')
        kbd_end = 1
    for a in KBD_ACTIONS:
        if a not in done:
            lines.insert(kbd_end, '%s = %s' % (a, b[a]))
            kbd_end += 1
    os.makedirs(os.path.dirname(KBD_INI), exist_ok=True)
    with open(KBD_INI, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def open_path(p):
    try:
        subprocess.Popen(['xdg-open', p], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError:
        pass


def desktop_dir():
    return core.desktop_dir() or os.path.expanduser('~')


# ---------------- drawing ----------------

class Fonts:
    def __init__(self, root, S):
        fams = set(tkfont.families(root))

        def pick(names):
            return next((n for n in names if n in fams), 'TkDefaultFont')
        text = pick(['Segoe UI', 'Noto Sans', 'Inter', 'Cantarell', 'Open Sans', 'DejaVu Sans', 'Liberation Sans', 'FreeSans'])
        head = pick(['Bahnschrift', 'Barlow Semi Condensed', 'Barlow', 'Oswald', 'Roboto Condensed', 'Noto Sans',
                     'DejaVu Sans Condensed', text])
        mono = pick(['Consolas', 'JetBrains Mono', 'Noto Sans Mono', 'DejaVu Sans Mono', 'Liberation Mono', 'Courier'])
        k = 0.93 if text.startswith('DejaVu') else 1.0      # DejaVu runs wider than Segoe UI

        def f(fam, px, bold=False):
            return tkfont.Font(root=root, family=fam, size=-max(6, int(round(px * S * k))), weight='bold' if bold else 'normal')
        self.base, self.small, self.label = f(text, 14), f(text, 12.5), f(text, 12.5, True)
        self.head, self.sub = f(head, 14, True), f(head, 15, True)
        self.play, self.btn, self.mono = f(head, 26, True), f(text, 13, True), f(mono, 14)


def rounded(cv, x, y, w, h, r, fill, back, tags, fill2=None, border=None, stripe=None, stripe_w=0):
    """Rounded rectangle from 1-px rows (anti-aliased edges), optional vertical gradient, border and left stripe."""
    x, y, w, h = int(round(x)), int(round(y)), int(round(w)), int(round(h))
    if w <= 0 or h <= 0:
        return

    def rows(x, y, w, h, r, c1, c2, back, stripe=None):
        r = max(0.0, min(r, w / 2, h / 2))
        for i in range(h):
            yy = i + 0.5
            dy = r - yy if yy < r else (yy - (h - r) if yy > h - r else 0)
            f = r - math.sqrt(max(0.0, r * r - dy * dy)) if dy > 0 else 0.0
            fi = int(f)
            frac = f - fi
            col = c1 if c2 is None else mix(c1, c2, i / max(1, h - 1))
            x0, x1 = x + fi, x + w - fi
            if frac > 0.02:
                edge = hx(mix(back, col, 1 - frac))
                cv.create_line(x0, y + i, x0 + 1, y + i, fill=edge, tags=tags)
                cv.create_line(x1 - 1, y + i, x1, y + i, fill=edge, tags=tags)
                x0, x1 = x0 + 1, x1 - 1
            if x1 > x0:
                cv.create_line(x0, y + i, x1, y + i, fill=hx(col), tags=tags)
                if stripe is not None and x + stripe_w > x0:
                    cv.create_line(x0, y + i, min(x1, x + stripe_w), y + i, fill=hx(stripe), tags=tags)
    if border is not None:
        rows(x, y, w, h, r, border, None, back)
        rows(x + 1, y + 1, w - 2, h - 2, max(0, r - 1), fill, fill2, border, stripe)
    else:
        rows(x, y, w, h, r, fill, fill2, back, stripe)


class Widget:
    interactive = False

    def __init__(self, x, y, w, h, back=BG):
        self.x, self.y, self.w, self.h, self.back = x, y, w, h, back
        self.enabled, self.hover, self.down, self.tip, self.surface = True, False, False, None, None
        self.tag = 'w%d' % id(self)
        self.visible = True

    def redraw(self):
        if self.surface:
            self.surface.cv.delete(self.tag)
            if self.visible:
                self.draw(self.surface.cv)

    def draw(self, cv):
        pass

    def click(self, ev):
        pass


class Label(Widget):
    def __init__(self, x, y, w, h, text, font, color, back=BG, wrap=True, anchor='nw'):
        super().__init__(x, y, w, h, back)
        self.text, self.font, self.color, self.wrap, self.anchor = text, font, color, wrap, anchor

    def draw(self, cv):
        if not self.text:
            return
        if self.anchor == 'w':
            cv.create_text(self.x, self.y + self.h / 2, text=self.text, font=self.font, fill=hx(self.color), anchor='w',
                           width=self.w if self.wrap else 0, tags=self.tag)
        else:
            cv.create_text(self.x, self.y, text=self.text, font=self.font, fill=hx(self.color), anchor='nw',
                           width=self.w if self.wrap else 0, tags=self.tag)


class Link(Label):
    interactive = True

    def __init__(self, *a, on_click=None, **k):
        super().__init__(*a, **k)
        self.on_click = on_click

    def draw(self, cv):
        f = self.font
        if self.hover:
            f = tkfont.Font(font=self.font)
            f.configure(underline=1)
            self._f = f
        cv.create_text(self.x, self.y, text=self.text, font=f, fill=hx(TEXT if self.down else ACCENT), anchor='nw', tags=self.tag)

    def click(self, ev):
        if self.on_click:
            self.on_click()


class Card(Widget):
    def __init__(self, x, y, w, h, r, stripe=None, S=1.0):
        super().__init__(x, y, w, h)
        self.r, self.stripe, self.S = r, stripe, S

    def draw(self, cv):
        rounded(cv, self.x, self.y, self.w, self.h, self.r, PANEL, BG, self.tag, border=LINE,
                stripe=self.stripe, stripe_w=max(2, self.r * 0.3))


class Button(Widget):
    interactive = True

    def __init__(self, x, y, w, h, text, font, back, r, on_click=None, fill=FIELD, fill2=None, hover=None, hover2=None,
                 border=LINE, color=TEXT, shine=False):
        super().__init__(x, y, w, h, back)
        self.text, self.font, self.r, self.on_click = text, font, r, on_click
        self.fill, self.fill2 = fill, fill2 if fill2 is not None else fill
        self.hover1 = hover if hover is not None else mix(fill, WHITE, 0.07)
        self.hover2 = hover2 if hover2 is not None else (self.hover1 if fill2 is None else mix(self.fill2, WHITE, 0.07))
        self.border, self.color, self.shine = border, color, shine

    def draw(self, cv):
        a, b = (self.hover1, self.hover2) if self.hover else (self.fill, self.fill2)
        if self.down:
            a, b = mix(a, BLACK, 0.18), mix(b, BLACK, 0.18)
        brd = None if self.border is None else (mix(self.border, WHITE, 0.15) if self.hover else self.border)
        rounded(cv, self.x, self.y, self.w, self.h, self.r, a, self.back, self.tag, fill2=b if b != a else None, border=brd)
        if self.shine:
            hl = mix(a, WHITE, 70 / 255)
            cv.create_line(self.x + self.r, self.y + 1.5, self.x + self.w - self.r, self.y + 1.5, fill=hx(hl), tags=self.tag)
        tc = self.color if self.enabled else (110, 118, 134)
        cv.create_text(self.x + self.w / 2, self.y + self.h / 2, text=self.text, font=self.font, fill=hx(tc), tags=self.tag)

    def click(self, ev):
        if self.on_click:
            self.on_click()


class Toggle(Widget):
    interactive = True

    def __init__(self, x, y, w, h, text, font, back=PANEL, on_change=None):
        super().__init__(x, y, w, h, back)
        self.text, self.font, self.checked, self.on_change = text, font, False, on_change

    def draw(self, cv):
        fh = self.font.metrics('linespace')
        th = round(fh * 0.95)
        tw = round(th * 1.8)
        ty = self.y + round((self.h - th) / 2)
        off = (44, 52, 70)
        track = ACCENT if self.checked else (mix(off, WHITE, 0.1) if self.hover else off)
        if not self.enabled:
            track = mix(track, self.back, 0.6)
        rounded(cv, self.x + 1, ty, tw, th, th / 2, track, self.back, self.tag)
        k = th - 6
        kx = self.x + 1 + tw - k - 3 if self.checked else self.x + 1 + 3
        knob = BG if self.checked else (150, 160, 178)
        if not self.enabled:
            knob = mix(knob, self.back, 0.5)
        cv.create_oval(kx, ty + 3, kx + k, ty + 3 + k, fill=hx(knob), outline=hx(knob), tags=self.tag)
        tx = self.x + int(tw + th * 0.65) + 1
        fc = (WHITE if self.hover else TEXT) if self.enabled else DISABLED
        cv.create_text(tx, self.y + self.h / 2, text=self.text, font=self.font, fill=hx(fc), anchor='w',
                       width=max(1, self.x + self.w - tx), tags=self.tag)

    def click(self, ev):
        self.set(not self.checked, user=True)

    def set(self, v, user=False):
        old = self.checked
        self.checked = bool(v)
        self.redraw()
        if user and self.on_change and old != self.checked:
            self.on_change(self)


class Combo(Widget):
    interactive = True

    def __init__(self, x, y, w, h, font, S, back=PANEL, on_change=None):
        super().__init__(x, y, w, h, back)
        self.font, self.S, self.items, self.index, self.on_change = font, S, [], -1, on_change
        self.open = False

    def draw(self, cv):
        hot = self.hover or self.open
        rounded(cv, self.x, self.y, self.w, self.h, 7 * self.S, mix(FIELD, WHITE, 0.04) if hot else FIELD, self.back, self.tag,
                border=mix(LINE, ACCENT, 0.55) if hot else LINE)
        fh = self.font.metrics('linespace')
        pad = int(fh * 0.7)
        t = self.items[self.index] if 0 <= self.index < len(self.items) else ''
        cv.create_text(self.x + pad, self.y + self.h / 2, text=t, font=self.font, fill=hx(TEXT if self.enabled else MUTED),
                       anchor='w', tags=self.tag)
        s = fh * 0.2
        cx, cy = self.x + self.w - pad - s, self.y + self.h / 2
        cv.create_line(cx - s, cy - s / 2, cx, cy + s / 2, cx + s, cy - s / 2, fill=hx(ACCENT if hot else MUTED),
                       width=max(1.5, fh / 9), capstyle='round', joinstyle='round', tags=self.tag)

    def set_items(self, items):
        self.items = list(items)
        if self.index >= len(self.items):
            self.index = -1
        self.redraw()

    def select(self, i, user=False):
        self.index = i
        self.redraw()
        if user and self.on_change:
            self.on_change(self)

    def click(self, ev):
        sf = self.surface
        cv = sf.cv
        fh = self.font.metrics('linespace')
        ih = fh + int(fh * 0.75)
        pop = tk.Toplevel(cv)
        pop.overrideredirect(True)
        pop.configure(bg=hx(LIST_BG))
        pc = tk.Canvas(pop, width=self.w, height=ih * len(self.items), bg=hx(LIST_BG), highlightthickness=1,
                       highlightbackground=hx(LINE), bd=0)
        pc.pack()
        hover = [self.index]
        pad = int(fh * 0.7)

        def paint():
            pc.delete('all')
            for i, it in enumerate(self.items):
                sel = i == hover[0]
                pc.create_rectangle(0, i * ih, self.w, (i + 1) * ih, fill=hx(LIST_HI if sel else LIST_BG), width=0)
                pc.create_text(pad, i * ih + ih / 2, text=it, font=self.font, fill=hx(WHITE if sel else TEXT), anchor='w')

        def close(_=None):
            self.open = False
            try:
                pop.grab_release()
                pop.destroy()
            except tk.TclError:
                pass
            self.redraw()

        def motion(e):
            i = e.y // ih
            if 0 <= i < len(self.items) and i != hover[0]:
                hover[0] = i
                paint()

        def press(e):
            if 0 <= e.x < self.w and 0 <= e.y < ih * len(self.items):
                i = e.y // ih
                close()
                if i != self.index:
                    self.select(i, user=True)
            else:
                close()
        paint()
        pop.geometry('+%d+%d' % (cv.winfo_rootx() + self.x, cv.winfo_rooty() + self.y + self.h))
        pc.bind('<Motion>', motion)
        pc.bind('<ButtonRelease-1>', press)
        pop.bind('<Button-1>', lambda e: None if e.widget is pc else close())
        pop.bind('<Escape>', close)
        pop.bind('<FocusOut>', close)
        self.open = True
        self.redraw()
        pop.update_idletasks()
        try:
            pop.grab_set()
        except tk.TclError:
            pass
        pc.bind('<Button-1>', lambda e: None if (0 <= e.x < self.w and 0 <= e.y < ih * len(self.items)) else close())
        pop.focus_set()


class Surface:
    """A canvas holding custom widgets: hover, press, click, cursor and dark tooltips."""

    def __init__(self, master, w, h, bg, fonts, S):
        self.cv = tk.Canvas(master, width=w, height=h, bg=hx(bg), highlightthickness=0, bd=0)
        self.cv.pack()
        self.widgets, self.hover, self.pressed = [], None, None
        self.fonts, self.S = fonts, S
        self.tip_job = self.tip_win = None
        self.cv.bind('<Motion>', self.on_motion)
        self.cv.bind('<Leave>', self.on_leave)
        self.cv.bind('<ButtonPress-1>', self.on_press)
        self.cv.bind('<ButtonRelease-1>', self.on_release)

    def add(self, w):
        w.surface = self
        self.widgets.append(w)
        w.redraw()
        return w

    def hit(self, x, y):
        for w in reversed(self.widgets):
            if w.visible and w.interactive and w.x <= x < w.x + w.w and w.y <= y < w.y + w.h:
                return w
        return None

    def set_hover(self, w):
        if w is self.hover:
            return
        old, self.hover = self.hover, w
        if old:
            old.hover = old.down = False
            old.redraw()
        if w:
            w.hover = w.enabled
            w.redraw()
        self.cv.configure(cursor='hand2' if (w and w.enabled) else '')
        self.hide_tip()
        if w and w.tip:
            self.tip_job = self.cv.after(350, lambda: self.show_tip(w))

    def on_motion(self, e):
        self.set_hover(self.hit(e.x, e.y))

    def on_leave(self, e):
        self.set_hover(None)

    def on_press(self, e):
        self.hide_tip()
        w = self.hit(e.x, e.y)
        if w and w.enabled:
            self.pressed = w
            w.down = True
            w.redraw()

    def on_release(self, e):
        w, self.pressed = self.pressed, None
        if not w:
            return
        w.down = False
        w.redraw()
        if self.hit(e.x, e.y) is w and w.enabled:
            w.click(e)

    def hide_tip(self):
        if self.tip_job:
            self.cv.after_cancel(self.tip_job)
            self.tip_job = None
        if self.tip_win:
            self.tip_win.destroy()
            self.tip_win = None

    def show_tip(self, w):
        self.tip_job = None
        if self.hover is not w or not w.tip:
            return
        S, f = self.S, self.fonts.small
        pad, maxw = int(10 * S), int(420 * S)
        tw = tk.Toplevel(self.cv)
        tw.overrideredirect(True)
        try:
            tw.attributes('-topmost', True)
        except tk.TclError:
            pass
        c = tk.Canvas(tw, bg=hx(LIST_BG), highlightthickness=1, highlightbackground=hx(TIP_EDGE), bd=0)
        c.pack()
        t = c.create_text(pad, pad, text=w.tip, font=f, fill=hx((226, 231, 239)), anchor='nw', width=maxw)
        x0, y0, x1, y1 = c.bbox(t)
        c.configure(width=x1 + pad, height=y1 + pad)
        px, py = self.cv.winfo_pointerx() + 14, self.cv.winfo_pointery() + 22
        sw, sh = self.cv.winfo_screenwidth(), self.cv.winfo_screenheight()
        px, py = min(px, sw - x1 - pad - 4), min(py, sh - y1 - pad - 4)
        tw.geometry('+%d+%d' % (px, py))
        self.tip_win = tw


# ---------------- dialogs ----------------

def center_on(win, parent, w, h):
    try:
        if parent is not None and parent.winfo_viewable():
            x = parent.winfo_rootx() + (parent.winfo_width() - w) // 2
            y = parent.winfo_rooty() + (parent.winfo_height() - h) // 2
        else:
            x, y = (win.winfo_screenwidth() - w) // 2, (win.winfo_screenheight() - h) // 2
    except tk.TclError:
        x, y = 100, 100
    win.geometry('%dx%d+%d+%d' % (w, h, max(0, x), max(0, y)))


def red_button(*a, **k):
    return Button(*a, fill=RED, fill2=RED_DK, hover=mix(RED, WHITE, 0.15), hover2=RED, border=None, color=WHITE, **k)


class App:
    def __init__(self):
        self.root = tk.Tk(className='nhl24-launcher')
        self.root.withdraw()
        self.root.title('NHL 24 PC v' + core.PORT_VERSION)
        self.root.configure(bg=hx(BG))
        self.root.resizable(False, False)
        self.root.update_idletasks()
        dpi = self.root.winfo_fpixels('1i') / 96.0
        sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        fit = min(sw * 0.97 / LAYOUT_W, (sh - 40 * dpi) * 0.97 / LAYOUT_H)
        self.S = max(0.6, min(max(1.0, dpi), fit))
        self.F = Fonts(self.root, self.S)
        self.set = core.load_settings()
        if self.set.get('lang') not in ('en', 'ru'):
            self.set['lang'] = 'ru' if os.environ.get('LANG', '').startswith('ru') else 'en'
        self.eboot = core.eboot_path()
        self.sfo = core.sfo_file(os.path.join(os.path.dirname(self.eboot), 'sce_sys', 'param.sfo'))
        self.has_update = os.path.isfile(os.path.join(core.UPDATE, 'eboot.bin')) or self.sfo.get('APP_VER') == '01.60'
        self.proc = None
        icon = next((p for p in (os.path.join(core.UPDATE, 'sce_sys', 'icon0.png'),
                                 os.path.join(core.GAME, 'sce_sys', 'icon0.png')) if os.path.isfile(p)), None)
        if icon:
            try:
                self._icon = tk.PhotoImage(file=icon)
                self.root.iconphoto(True, self._icon)
            except tk.TclError:
                pass
        self.build()

    def L(self, k):
        return T[self.set['lang']][k]

    def P(self, v):
        return int(round(v * self.S))

    # ----- the key art on the left: game/sce_sys/pic1.png fading into the background -----
    def hero_image(self, hw, hh):
        art_h = int(hh * 0.72)
        art = next((p for p in (os.path.join(core.UPDATE, 'sce_sys', 'pic1.png'),
                                os.path.join(core.GAME, 'sce_sys', 'pic1.png')) if os.path.isfile(p)), None)
        if not art:
            return None
        st = os.stat(art)
        cache = os.path.join(core.USER, 'cache', 'launcher_hero_%dx%d_%d.ppm' % (hw, hh, int(st.st_mtime)))
        if os.path.isfile(cache):
            try:
                return tk.PhotoImage(file=cache)
            except tk.TclError:
                pass
        edge = self.P(70)
        bot_y = int(art_h * 0.40)

        def fade(c, x, y):
            if y >= art_h:
                return BG
            if y > bot_y:
                c = mix(c, BG, (y - bot_y) / max(1, art_h - bot_y))
            if x > hw - edge:
                c = mix(c, BG, (x - (hw - edge)) / edge)
            return c
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        try:
            from PIL import Image
            src = Image.open(art).convert('RGB')
            k = max(hw / src.width, art_h / src.height)
            cw, ch = hw / k, art_h / k
            sx = (src.width - cw) * 0.42
            img = src.crop((int(sx), 0, int(sx + cw), int(ch))).resize((hw, art_h), Image.LANCZOS)
            out = Image.new('RGB', (hw, hh), BG)
            out.paste(img, (0, 0))
            px = out.load()
            for y in range(bot_y, hh):
                for x in range(hw):
                    px[x, y] = fade(px[x, y], x, y)
            for y in range(0, bot_y):
                for x in range(hw - edge, hw):
                    px[x, y] = fade(px[x, y], x, y)
            out.save(cache, 'PPM')
            return tk.PhotoImage(file=cache)
        except ImportError:
            pass
        except Exception:
            return None
        # without Pillow: Tk reads the PNG, pixels are sampled one by one (slow once, then cached)
        try:
            src = tk.PhotoImage(file=art)
            sw, sh = src.width(), src.height()
            k = max(hw / sw, art_h / sh)
            sx = (sw - hw / k) * 0.42
            out = tk.PhotoImage(width=hw, height=hh)
            for y in range(hh):
                row = []
                for x in range(hw):
                    if y < art_h:
                        p = src.get(min(sw - 1, int(sx + (x + 0.5) / k)), min(sh - 1, int((y + 0.5) / k)))
                        if isinstance(p, str):
                            p = tuple(int(v) for v in p.split())
                        c = fade(p[:3], x, y)
                    else:
                        c = BG
                    row.append(hx(c))
                out.put('{' + ' '.join(row) + '}', to=(0, y))
            out.write(cache, format='ppm')
            return out
        except Exception:
            return None

    # ----- window -----
    def build(self):
        P, F = self.P, self.F
        W, H = P(LAYOUT_W), P(LAYOUT_H)
        sf = self.sf = Surface(self.root, W, H, BG, F, self.S)
        cv = sf.cv
        hw, hh = P(384), H
        self.hero = self.hero_image(hw, hh)
        if self.hero:
            cv.create_image(0, 0, image=self.hero, anchor='nw')
        else:
            art_h = int(hh * 0.72)
            for i in range(art_h):
                cv.create_line(0, i, hw, i, fill=hx(mix(PANEL, BG, i / art_h)))

        def lab(x, y, w, h, font, color, text=''):
            return sf.add(Label(P(x), P(y), P(w), P(h), text, font, color))
        self.lbl_edition = lab(28, 368, 328, 24, F.sub, ACCENT)
        self.lbl_game = lab(28, 396, 328, 20, F.small, TEXT)
        self.lbl_time = lab(28, 420, 328, 20, F.small, MUTED)
        self.btn_play = sf.add(Button(P(28), P(448), P(328), P(66), '', F.play, BG, P(10), self.on_play,
                                      fill=mix(RED, WHITE, 0.08), fill2=RED_DK, hover=mix(RED, WHITE, 0.22),
                                      hover2=mix(RED_DK, WHITE, 0.12), border=None, color=WHITE, shine=True))
        self.btn_install = sf.add(Button(P(28), P(524), P(104), P(36), '', F.small, BG, P(8), self.on_install))
        self.btn_guide = sf.add(Button(P(140), P(524), P(104), P(36), '', F.small, BG, P(8), self.on_guide))
        self.btn_logs = sf.add(Button(P(252), P(524), P(104), P(36), '', F.small, BG, P(8), self.on_report))

        cA, cB, cW, y0 = 404, 744, 316, 56
        iw = cW - 32
        self.lnk_en = sf.add(Button(P(LAYOUT_W - 116), P(14), P(48), P(28), 'EN', F.label, BG, P(8), lambda: self.set_lang('en')))
        self.lnk_ru = sf.add(Button(P(LAYOUT_W - 64), P(14), P(48), P(28), 'RU', F.label, BG, P(8), lambda: self.set_lang('ru')))

        def card(x, y, w, h):
            sf.add(Card(P(x), P(y), P(w), P(h), P(12), stripe=ACCENT, S=self.S))
            return x, y

        def clab(c, x, y, w, h, font, color):
            return sf.add(Label(P(c[0] + x), P(c[1] + y), P(w), P(h), '', font, color, back=PANEL))

        def combo(c, x, y, w, on_change=None):
            return sf.add(Combo(P(c[0] + x), P(c[1] + y), P(w), P(36), F.base, self.S, on_change=on_change))

        def toggle(c, x, y, w, h, on_change=None):
            return sf.add(Toggle(P(c[0] + x), P(c[1] + y), P(w), P(h), '', F.base, on_change=on_change))

        def cbtn(c, x, y, w, h, on_click, color=TEXT):
            return sf.add(Button(P(c[0] + x), P(c[1] + y), P(w), P(h), '', F.small, PANEL, P(8), on_click, color=color))

        # DISPLAY
        c = card(cA, y0, cW, 300)
        self.h_screen = clab(c, 18, 14, 200, 22, F.head, ACCENT)
        self.l_res = clab(c, 16, 46, iw, 18, F.label, MUTED)
        self.cb_res = combo(c, 16, 66, iw)
        self.l_mode = clab(c, 16, 112, iw, 18, F.label, MUTED)
        self.cb_win = combo(c, 16, 132, iw)
        self.l_hdr = clab(c, 16, 178, iw, 18, F.label, MUTED)
        self.cb_hdr = combo(c, 16, 198, iw)
        self.chk_ru = toggle(c, 16, 244, iw, 44)
        # FRAME RATE
        c = card(cA, y0 + 312, cW, 180)
        self.h_fps = clab(c, 18, 14, 160, 22, F.head, ACCENT)
        self.btn_kbd = cbtn(c, cW - 16 - 120, 10, 120, 28, self.on_keyboard)
        self.l_fps = clab(c, 16, 46, iw, 18, F.label, MUTED)
        self.cb_fps = combo(c, 16, 66, iw)
        self.chk_vsync = toggle(c, 16, 124, iw, 30)
        # GRAPHICS
        c = card(cB, y0, cW, 236)
        self.h_q = clab(c, 18, 14, 120, 22, F.head, ACCENT)
        self.btn_weak = cbtn(c, cW - 16 - 160, 10, 160, 28, self.on_weak, color=WARN)
        self.chk_pro = toggle(c, 16, 64, iw, 30)
        self.chk_fsr = toggle(c, 16, 116, iw, 30)
        # STABILITY
        c = card(cB, y0 + 248, cW, 244)
        self.h_s = clab(c, 18, 14, 200, 22, F.head, ACCENT)
        self.chk_stable = toggle(c, 16, 58, iw, 44)
        self.chk_diag = toggle(c, 16, 122, iw, 30)

        # footer
        yF = P(564)
        cv.create_rectangle(0, yF, W, H, fill=hx(FOOT), width=0)
        cv.create_line(0, yF, W, yF, fill=hx(LINE), width=max(1, P(1)))
        self.lbl_author = sf.add(Label(P(28), P(564 + 18), P(150), P(22), '', F.label, MUTED, back=FOOT))
        self.lnk_video = sf.add(Link(P(180), P(564 + 18), P(220), P(22), '', F.label, ACCENT, back=FOOT,
                                     on_click=lambda: open_path(CHANNEL_URL)))
        self.lnk_support = sf.add(Button(P(LAYOUT_W - 24 - 230), P(564 + 11), P(230), P(34), '', F.btn, FOOT, P(8),
                                         self.on_support, fill=(40, 32, 18), fill2=(30, 24, 14), hover=(62, 48, 24),
                                         hover2=(48, 37, 18), border=mix(GOLD, FOOT, 0.45), color=GOLD))

        # values
        s = self.set
        res = str(s['resolution'])
        self.cb_res.index = RES_KEYS.index(res) if res in RES_KEYS else 1
        self.cb_win.index = 0 if s['fullscreen'] else 1
        self.cb_hdr.index = 1 if s.get('hdr') == 'on' else 0
        self.cb_fps.index = FPS_VALS.index(int(s['fps'])) if int(s['fps']) in FPS_VALS else 2
        self.chk_ru.checked = self.has_update and bool(s['ru_text'])
        self.chk_ru.enabled = self.has_update
        self.chk_vsync.checked = bool(s['vsync'])
        self.chk_pro.checked = False
        self.chk_pro.enabled = False          # PS4 Pro (neo) mode crashes the Linux build
        self.chk_fsr.checked = bool(s['fsr'])
        self.chk_diag.checked = bool(s['diag'])
        self.chk_stable.checked = bool(s['stable'])
        self.apply_language()

        self.root.bind('<Return>', lambda e: self.on_play())
        self.root.protocol('WM_DELETE_WINDOW', self.root.destroy)
        center_on(self.root, None, W, H)
        self.root.deiconify()

    def play_time(self):
        sec = 0
        try:
            for line in open(os.path.join(core.USER, 'play_time.txt'), encoding='utf-8', errors='replace'):
                m = re.match(r'^\S+\s+(\d+):(\d+):(\d+)', line)
                if m:
                    sec += int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
        except OSError:
            pass
        return self.L('playtime').format(sec // 3600, (sec % 3600) // 60)

    def apply_language(self):
        L = self.L
        sets = [(self.h_screen, 'display'), (self.l_res, 'resolution'), (self.l_mode, 'mode'), (self.l_hdr, 'hdr'),
                (self.h_fps, 'frames'), (self.l_fps, 'fpscap'), (self.h_q, 'quality'),
                (self.h_s, 'stability'), (self.chk_vsync, 'vsync'), (self.chk_pro, 'pro'),
                (self.chk_fsr, 'fsr'), (self.chk_diag, 'diag'), (self.chk_stable, 'stable'),
                (self.btn_play, 'play'), (self.btn_install, 'install'), (self.btn_guide, 'guide'),
                (self.btn_logs, 'logs'), (self.btn_weak, 'weak')]
        for w, k in sets:
            w.text = L(k)
        self.lbl_edition.text = L('edition').format(core.PORT_VERSION)
        self.cb_res.items, self.cb_win.items, self.cb_hdr.items = L('res'), L('modes'), L('hdrs')
        self.cb_fps.items = L('fps')
        self.chk_ru.text = L('rutext') if self.has_update else L('rutextNeed')
        for w, k in ((self.chk_ru, 'rutextTip'), (self.chk_pro, 'proTip'),
                     (self.chk_fsr, 'fsrTip'), (self.chk_diag, 'diagTip'),
                     (self.chk_stable, 'stableTip')):
            w.tip = L(k)
        self.btn_kbd.text = '\u2328  ' + L('kbd')
        self.lbl_time.text = self.play_time()
        self.lbl_author.text = L('author').format(AUTHOR)
        self.lnk_video.text = '\u25b6  ' + L('video')
        self.lnk_support.text = '\u2665  ' + L('support')
        if not os.path.isfile(self.eboot):
            self.lbl_game.text, self.lbl_game.color = L('notInstalled'), WARN
        elif self.sfo:
            self.lbl_game.text = L('gameLine').format(self.sfo.get('TITLE_ID'), self.sfo.get('APP_VER'))
        for b, active in ((self.lnk_en, self.set['lang'] == 'en'), (self.lnk_ru, self.set['lang'] == 'ru')):
            if active:
                b.fill = b.fill2 = FIELD
                b.border, b.color = mix(LINE, ACCENT, 0.5), TEXT
            else:
                b.fill = b.fill2 = BG
                b.border, b.color = None, MUTED
            b.hover1 = b.hover2 = mix(b.fill, WHITE, 0.07)
        for w in self.sf.widgets:
            w.redraw()

    def set_lang(self, lang):
        self.set['lang'] = lang
        self.apply_language()
        try:
            core.save_settings(self.set)
        except Exception:
            pass

    # ----- message box in the launcher's style -----
    def msg(self, text, title='NHL 24 PC', buttons=('ok',), parent=None):
        parent = parent or self.root
        P, F = self.P, self.F
        d = tk.Toplevel(self.root)
        d.withdraw()
        d.title(title)
        d.configure(bg=hx(BG))
        d.resizable(False, False)
        if parent.winfo_viewable():
            d.transient(parent)
        w = P(480)
        probe = tk.Canvas(d)
        t = probe.create_text(0, 0, text=text, font=F.base, anchor='nw', width=w - P(48))
        th = probe.bbox(t)[3]
        probe.destroy()
        h = th + P(24 + 24 + 32 + 16)
        sf = Surface(d, w, h, BG, F, self.S)
        sf.add(Label(P(24), P(24), w - P(48), th, text, F.base, TEXT))
        result = [buttons[-1] if 'cancel' in buttons or 'no' in buttons else buttons[0]]

        def done(r):
            result[0] = r
            d.destroy()
        bw, gap = P(120), P(10)
        x = w - P(24) - len(buttons) * bw - (len(buttons) - 1) * gap
        for i, b in enumerate(buttons):
            mk = red_button if i == 0 else Button
            sf.add(mk(x, h - P(16) - P(32), bw, P(32), self.L(b), F.btn, BG, P(8), lambda b=b: done(b)))
            x += bw + gap
        d.bind('<Return>', lambda e: done(buttons[0]))
        d.bind('<Escape>', lambda e: done(result[0]))
        center_on(d, parent, w, h)
        d.deiconify()
        d.wait_visibility()
        try:
            d.grab_set()
        except tk.TclError:
            pass
        d.focus_set()
        self.root.wait_window(d)
        return result[0]

    # ----- handlers -----
    def on_weak(self):
        v = core.vram_gb()
        self.cb_res.select(0 if 0 < v <= 4 else 1)
        self.cb_fps.select(0)
        self.chk_fsr.set(True)
        self.chk_diag.set(False)
        self.chk_stable.set(True)
        self.msg(self.L('weakDone'))

    def on_install(self):
        if not self.btn_install.enabled:
            return
        core.refresh_runtime_paths()
        if not os.access(core.EMU, os.X_OK):
            self.msg(self.L('msgNoEmu'))
            return
        pkg = filedialog.askopenfilename(parent=self.root, title=self.L('installTitle'),
                                         filetypes=[('PS4 package', '*.pkg'), ('All files', '*')])
        if not pkg:
            return
        self.btn_install.enabled = False
        self.btn_install.text = self.L('installing')
        self.btn_install.redraw()
        os.makedirs(os.path.join(core.USER, 'log'), exist_ok=True)
        log_path = os.path.join(core.USER, 'log', 'install-gui.log')

        def install():
            with open(log_path, 'w', encoding='utf-8') as log:
                result = subprocess.run([sys.executable, os.path.join(SCRIPT_ROOT, 'nhl24-linux.py'),
                                         'install', pkg], cwd=core.DATA_ROOT, stdout=log,
                                        stderr=subprocess.STDOUT)
            self.root.after(0, lambda: finished(result.returncode))

        def finished(code):
            self.btn_install.enabled = True
            self.eboot = core.eboot_path()
            self.sfo = core.sfo_file(os.path.join(os.path.dirname(self.eboot), 'sce_sys', 'param.sfo'))
            self.has_update = (os.path.isfile(os.path.join(core.UPDATE, 'eboot.bin')) or
                               self.sfo.get('APP_VER') == '01.60')
            self.chk_ru.enabled = self.has_update
            self.apply_language()
            if code == 0:
                self.msg(self.L('installDone'))
                return
            try:
                with open(log_path, encoding='utf-8', errors='replace') as log:
                    detail = log.read()[-6000:].strip()
            except OSError as e:
                detail = str(e)
            self.msg(self.L('installFail').format(detail or 'exit code %d' % code))

        threading.Thread(target=install, daemon=True).start()

    def on_guide(self):
        ru = self.set['lang'] == 'ru'
        names = [self.L('readme'), 'LINUX_RU.md' if ru else 'LINUX.md', 'LINUX.md' if ru else 'LINUX_RU.md']
        for n in names:
            for d in (SCRIPT_ROOT, os.path.join(core.REPO_ROOT, 'docs', 'linux')):
                g = os.path.join(d, n)
                if os.path.isfile(g):
                    open_path(g)
                    return
        self.msg(self.L('noGuide').format(os.path.join(SCRIPT_ROOT, names[0])))

    def on_support(self):
        P, F = self.P, self.F
        d = tk.Toplevel(self.root)
        d.withdraw()
        d.title(self.L('supportTitle'))
        d.configure(bg=hx(BG))
        d.resizable(False, False)
        d.transient(self.root)
        w, h = P(520), P(224)
        sf = Surface(d, w, h, BG, F, self.S)
        sf.add(Label(P(24), P(18), P(472), P(24), self.L('supportText'), F.base, TEXT))
        y = 58
        for name, val in DONATE:
            sf.add(Label(P(24), P(y), P(472), P(20), name, F.label, GOLD))
            rounded(sf.cv, P(24), P(y + 22), P(340), P(28), 0, FIELD, BG, 'box', border=LINE)
            sf.cv.create_text(P(32), P(y + 22) + P(14), text=val, font=F.mono, fill=hx(TEXT), anchor='w')
            b = sf.add(Button(P(376), P(y + 20), P(120), P(30), self.L('copy'), F.small, BG, P(8), None))

            def copy(b=b, val=val):
                d.clipboard_clear()
                d.clipboard_append(val)
                b.text = self.L('copied')
                b.redraw()
            b.on_click = copy
            y += 60
        sf.add(red_button(P(376), P(180), P(120), P(32), self.L('close'), F.btn, BG, P(8), d.destroy))
        d.bind('<Escape>', lambda e: d.destroy())
        center_on(d, self.root, w, h)
        d.deiconify()
        d.wait_visibility()
        d.grab_set()

    def on_keyboard(self):
        P, F, L = self.P, self.F, self.L
        b = read_kbd()
        names = L('kbdNames')
        rows = len(KBD_ACTIONS)
        half = (rows + 1) // 2
        d = tk.Toplevel(self.root)
        d.withdraw()
        d.title(L('kbdTitle'))
        d.configure(bg=hx(BG))
        d.resizable(False, False)
        d.transient(self.root)
        w, h = P(700), P(150 + half * 30 + 50)
        sf = Surface(d, w, h, BG, F, self.S)
        sf.add(Label(P(16), P(12), P(668), P(60), L('kbdNote'), F.base, WARN))
        sf.add(Label(P(16), P(78), P(668), P(34), L('kbdHint'), F.small, MUTED))
        state = {'wait': None}
        btns = {}

        def pick(btn):
            if state['wait']:
                w0 = state['wait']
                w0.text = b[w0.action].upper()
                w0.redraw()
            state['wait'] = btn
            btn.text = L('kbdPress')
            btn.redraw()
        for i, a in enumerate(KBD_ACTIONS):
            col, row = i // half, i % half
            x, y = 16 + col * 340, 120 + row * 30
            sf.add(Label(P(x), P(y + 3), P(190), P(22), names[i], F.small, TEXT, wrap=False))
            kb = sf.add(Button(P(x + 194), P(y), P(130), P(26), b[a].upper(), F.small, BG, P(6), None, color=ACCENT))
            kb.action = a
            kb.on_click = lambda kb=kb: pick(kb)
            btns[a] = kb

        def key(e):
            wbtn = state['wait']
            if not wbtn:
                return
            act = wbtn.action
            if e.keysym == 'Escape':
                wbtn.text = b[act].upper()
                wbtn.redraw()
                state['wait'] = None
                return 'break'
            if re.fullmatch(r'F([1-9]|1[0-2])', e.keysym) or e.keysym == 'Delete':
                d.bell()
                return 'break'
            name = key_name(e.keysym)
            if not name:
                d.bell()
                return 'break'
            for other in list(b):     # a key drives one button: the action that had it takes the old key
                if other != act and b[other] == name:
                    b[other] = b[act]
                    btns[other].text = b[other].upper()
                    btns[other].redraw()
            b[act] = name
            wbtn.text = name.upper()
            wbtn.redraw()
            state['wait'] = None
            return 'break'
        d.bind('<KeyPress>', key)
        yb = 150 + half * 30 + 50 - 44

        def save():
            try:
                save_kbd(b)
                d.destroy()
                self.msg(L('kbdSaved'))
            except Exception as ex:
                self.msg(str(ex))

        def defaults():
            for a in KBD_ACTIONS:
                b[a] = KBD_DEFAULTS[a]
                btns[a].text = b[a].upper()
                btns[a].redraw()
            state['wait'] = None
        sf.add(red_button(P(16), P(yb), P(150), P(32), L('kbdSave'), F.btn, BG, P(8), save))
        sf.add(Button(P(176), P(yb), P(150), P(32), L('kbdDefault'), F.btn, BG, P(8), defaults))
        sf.add(Button(P(534), P(yb), P(150), P(32), L('kbdCancel'), F.btn, BG, P(8), d.destroy))
        center_on(d, self.root, w, h)
        d.deiconify()
        d.wait_visibility()
        d.grab_set()
        d.focus_set()

    # ----- crash report: zip on the Desktop -----
    def make_report(self, code=0):
        stamp = time.strftime('%Y%m%d_%H%M')
        z = os.path.join(desktop_dir(), 'NHL24_crash_report_%s.zip' % stamp)
        files = ['user/log/shad_log.txt', 'user/log/nhl24_diag.log', 'user/log/nhl24_threads.csv', 'user/log/linux_run.log',
                 'user/config.json', 'user/launcher_settings.json', 'user/eboot_offsets.json']

        def run(*cmd):
            try:
                return subprocess.run(cmd, capture_output=True, text=True, timeout=10).stdout.strip()
            except Exception:
                return ''
        info = ['NHL 24 PC crash report (Linux)  ' + time.strftime('%Y-%m-%d %H:%M'), 'Exit code: %s' % code,
                'Game: %s v%s' % (self.sfo.get('TITLE_ID'), self.sfo.get('APP_VER'))]
        try:
            st = os.stat(core.EMU)
            info.append('Emulator: %s %s %d' % (os.path.basename(core.EMU),
                        time.strftime('%Y-%m-%d %H:%M', time.localtime(st.st_mtime)), st.st_size))
        except OSError:
            pass
        try:
            osr = dict(re.findall(r'^(\w+)="?(.*?)"?$', open('/etc/os-release').read(), re.M))
            info.append('OS: %s  kernel %s' % (osr.get('PRETTY_NAME', '?'), os.uname().release))
        except OSError:
            info.append('Kernel: ' + os.uname().release)
        try:
            cpu = re.search(r'model name\s*:\s*(.+)', open('/proc/cpuinfo').read())
            mem = re.search(r'MemTotal:\s*(\d+)', open('/proc/meminfo').read())
            info.append('CPU: %s  cores %d / threads %d' % (cpu.group(1) if cpu else '?', core.physical_cores(), os.cpu_count() or 0))
            if mem:
                info.append('RAM: %.1f GB' % (int(mem.group(1)) / 2**20))
        except OSError:
            pass
        for g in core.gpu_names():
            info.append('GPU: ' + g)
        info.append('VRAM: %d GB' % core.vram_gb())
        info.append('Screen: %dx%d' % core.screen_size())
        vk = run('vulkaninfo', '--summary')
        if vk:
            info += ['', '--- vulkaninfo --summary ---', vk]
        info.append('Install path: ' + core.DATA_ROOT)
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
            for f in files:
                p = os.path.join(core.DATA_ROOT, f)
                if os.path.isfile(p):
                    try:
                        zf.write(p, os.path.basename(p))
                    except OSError:
                        pass
            zf.writestr('sysinfo.txt', '\n'.join(info) + '\n')
        return z

    def on_report(self, code=0):
        try:
            z = self.make_report(code)
            open_path(os.path.dirname(z))
            self.msg(self.L('reportDone').format(z))
        except Exception as e:
            self.msg(self.L('reportFail') + str(e))

    # ----- PLAY -----
    def collect(self):
        s = self.set
        s['resolution'] = RES_KEYS[max(0, self.cb_res.index)]
        s['fullscreen'] = self.cb_win.index == 0
        s['hdr'] = ['off', 'on'][max(0, self.cb_hdr.index)]
        s['vsync'] = self.chk_vsync.checked
        s['fps'] = FPS_VALS[max(0, self.cb_fps.index)]
        s['fsr'] = self.chk_fsr.checked
        s['diag'] = self.chk_diag.checked
        s['stable'] = self.chk_stable.checked
        s['ru_text'] = self.chk_ru.checked
        core.save_settings(s)

    def on_play(self):
        if self.proc:
            return
        if not os.path.isfile(self.eboot):
            self.msg(self.L('msgNotInstalled'))
            return
        if not os.access(core.EMU, os.X_OK):
            self.msg(self.L('msgNoEmu'))
            return
        if core.game_running():
            self.msg(self.L('msgRunning'))
            return
        try:
            self.collect()
            env = core.prepare_launch(self.set)
            os.makedirs(os.path.join(core.USER, 'log'), exist_ok=True)
            log = open(os.path.join(core.USER, 'log', 'linux_run.log'), 'w')
            self.proc = subprocess.Popen([core.EMU], cwd=core.DATA_ROOT, env=env, stdout=log,
                                         stderr=subprocess.STDOUT,
                                         stdin=subprocess.DEVNULL)
        except Exception as e:
            self.msg(self.L('msgFail') + str(e))
            return
        self.root.withdraw()
        self.toast()

    def shader_count(self):
        d = os.path.join(core.USER, 'cache', self.sfo.get('TITLE_ID') or 'CUSA37934')
        try:
            return sum(1 for f in os.listdir(d) if f.endswith('.spv'))
        except OSError:
            return 0

    def toast(self):
        # small always-on-top note while the game starts / compiles shaders; never takes focus
        P, F, L = self.P, self.F, self.L
        t = tk.Toplevel(self.root)
        t.overrideredirect(True)
        try:
            t.attributes('-topmost', True)
            t.attributes('-alpha', 0.95)
        except tk.TclError:
            pass
        w, h = P(540), P(96)
        c = tk.Canvas(t, width=w, height=h, bg=hx(PANEL), highlightthickness=0)
        c.pack()
        c.create_rectangle(0, 0, P(4), h, fill=hx(RED), width=0)
        c.create_text(P(16), P(10), text=L('tStart'), font=F.head, fill=hx(ACCENT), anchor='nw')
        l2 = c.create_text(P(16), P(38), text='', font=F.base, fill=hx(TEXT), anchor='nw')
        l3 = c.create_text(P(16), P(64), text='', font=F.small, fill=hx(MUTED), anchor='nw')
        sw, sh = t.winfo_screenwidth(), t.winfo_screenheight()
        t.geometry('%dx%d+%d+%d' % (w, h, (sw - w) // 2, sh - P(140) - h // 2))
        st = {'start': time.time(), 'first': self.shader_count(), 'last': None, 'change': time.time(), 'open': True}
        st['last'] = st['first']

        def close(_=None):
            if st['open']:
                st['open'] = False
                t.destroy()
        c.bind('<Button-1>', close)

        def tick():
            now = time.time()
            if self.proc.poll() is not None:
                close()
                self.after_game()
                return
            if st['open']:
                el = now - st['start']
                n = self.shader_count()
                if n != st['last']:
                    st['last'], st['change'] = n, now
                new = n - st['first']
                mm, ss = int(el // 60), int(el % 60)
                if new > 0:
                    c.itemconfigure(l2, text=L('tCompile').format(new, n, mm, ss))
                    c.itemconfigure(l3, text=L('tFirst'))
                else:
                    c.itemconfigure(l2, text=L('tLoad').format(n, mm, ss))
                if (el > 20 and now - st['change'] > 45) or el > 1800:
                    close()
            self.root.after(1000, tick)
        tick()

    def after_game(self):
        code = self.proc.returncode
        log = os.path.join(core.USER, 'log', 'shad_log.txt')

        def tail(n):
            try:
                with open(log, 'rb') as f:
                    f.seek(0, 2)
                    size = f.tell()
                    f.seek(max(0, size - n))
                    return f.read().decode('utf-8', 'replace')
            except OSError:
                return ''
        end = tail(65536)[-4000:]
        L = self.L
        if code != 0 or '<Critical>' in end:
            if re.search(r'OutOfDeviceMemory|OutOfHostMemory', end):
                m = L('crashVram')
            elif re.search(r'Device lost|DeviceLost|ErrorDeviceLost', end):
                m = L('crashLost')
            else:
                m = L('crashOther').format(self.sfo.get('TITLE_ID') or 'CUSA37934')
            if not self.set['stable'] and not re.search(r'OutOfDeviceMemory|OutOfHostMemory', end):
                m += L('stableHint')
            if self.msg(m + L('crashAsk'), L('crashTitle'), ('yes', 'no')) == 'yes':
                self.on_report(code)
        # frame pacing: if the PC could not hold the cap, offer a lower one
        cap = int(self.set['fps'])
        if cap > 60:
            vals = [float(v) for v in re.findall(r'frame stats: ([0-9.]+) fps', tail(4 << 20)) if float(v) > 20]
            if len(vals) >= 24:
                srt = sorted(vals)
                low = [v for v in srt if v < cap * 0.85]
                typical = int(low[len(low) // 2]) if low else int(srt[len(srt) // 2])
                if len(low) / len(vals) >= 0.35:
                    new = 90 if cap > 90 and typical >= 85 else 60
                    if self.msg(L('fpsAdvice').format(cap, typical, new), L('fpsTitle'), ('yes', 'no')) == 'yes':
                        self.set['fps'] = new
                        core.save_settings(self.set)
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__ == '__main__':
    App().run()
