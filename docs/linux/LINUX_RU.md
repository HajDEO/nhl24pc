# NHL 24 PC на Linux (нативная сборка)

Состояние: **работает** — нативная Linux-сборка этого форка запускает NHL 24 (меню, матчи,
русский текст). Установка, патчи, настройки и запуск — двумя скриптами на Python, без Wine,
PowerShell и .NET. Windows-версия эмулятора под Wine **не работает** (см. раздел «Wine»).

Проверено: Gentoo Linux, Ryzen 7 9700X + Radeon RX 9060 XT, Mesa RADV 26.1.8, clang 23,
Python 3.14 + Tk 8.6, игра CUSA37934 версии 1.00 + обновление 1.60.

## 1. Сборка эмулятора

Зависимости (имена пакетов Gentoo, в других дистрибутивах — как у shadPS4):
`llvm-core/clang dev-build/cmake dev-build/ninja dev-vcs/git dev-util/vulkan-headers
media-libs/vulkan-loader media-libs/alsa-lib media-video/pipewire dev-libs/openssl
dev-libs/libevdev virtual/libudev x11-libs/libXext x11-libs/libXrandr media-libs/libpng
sys-apps/util-linux`

```bash
git clone --recursive https://github.com/HajDEO/nhl24pc.git
cd nhl24pc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j$(nproc)
```

Собирается без правок, результат — `build/nhl24`.

**Рекомендуется: применить правки эмулятора из репака.** Репак NHL24PC V3.2 содержит свои
изменения эмулятора в исходниках (`source_changes/`, 41 файл поверх коммита `bef79997`).
`nhl24pc-v3.2-repack.patch` из этой папки — те же изменения одним патчем. Без него переключатели
лаунчера `NHLPORT_*` (анизотропка, резкость, FPS в меню, пул изображений, фикс GTX 10xx,
диагностика GPU) ничего не делают, а наложение текстур, из-за которого падает режим PS4 Pro,
не обрабатывается.

```bash
git checkout bef7999707db27884cabe5780f301c4a5d2abdc8
git apply /путь/к/nhl24pc-v3.2-repack.patch
cmake --build build -j$(nproc)
```

Под Linux с этим патчем ещё не собиралось: части только для Windows в нём под `#ifdef _WIN32`,
но ошибки компиляции возможны.

## 2. Установка игры

Положите в папку игры эмулятор и скрипты из этой папки:

```
NHL24/
  nhl24-linux          <- build/nhl24, скопированный и переименованный
  nhl24-linux.py       <- установщик, патчи, настройки в терминале
  nhl24-launcher.py    <- окно лаунчера
```

```bash
cd NHL24
chmod +x nhl24-linux nhl24-linux.py nhl24-launcher.py
./nhl24-linux.py install /путь/к/base.pkg /путь/к/update.pkg
```

Установщик делает то же, что Install.bat в Windows:

* распаковывает оба PKG встроенным распаковщиком эмулятора в `game/` и `game-UPDATE/`,
  проверяет тип пакета и совпадение региона;
* патчит `eboot.bin` по сигнатурам (копия в `eboot.bin.orig`): зависание перед первым
  вбрасыванием, отключение динамического разрешения, разрешение под экран и видеопамять;
* создаёт `user/config.json` (из `tools/config_default.json`, если есть) с выключенным PS4 Pro;
* добавляет «NHL 24» в меню приложений и на рабочий стол.

Остальные команды: `status`, `settings` (меню в терминале), `play`, `update UPDATE.pkg`,
`remove-update`, `mod install|remove|status` (сборные России и Беларуси, нужна папка
`tools/mods` из репака), `desktop [--remove-wine]` (пересоздать ярлыки).

Установка с нуля из PKG этим скриптом реализована, но целиком ещё не проверялась
(у автора игра изначально была распакована Windows-установщиком под Wine).

## 3. Окно лаунчера

`nhl24-launcher.py` — перенос лаунчера из репака: то же окно и те же настройки (разрешение,
режим экрана, лимит FPS, FSR, анизотропка, резкость, русский текст, стабильный режим, фикс
GTX 10xx, диагностика, клавиатура, пресет «Слабый ПК», отчёт о вылете). ИГРАТЬ применяет
настройки к `eboot.bin` и `config.json` и запускает `nhl24-linux`. «Режим PS4 Pro» виден,
но заблокирован (см. раздел 4).

Нужен Tk для Python. Gentoo:

```bash
echo "dev-lang/python tk" | sudo tee -a /etc/portage/package.use/python
sudo emerge -av1 --changed-use dev-lang/python
```

Debian/Ubuntu: `sudo apt install python3-tk`, Arch: `sudo pacman -S tk`.
Pillow не обязателен (с ним чуть чётче обложка в окне).

**Русский текст:** включите галочку в лаунчере, а на экране выбора языка в игре выберите **English**.

## 4. PS4 Pro обязательно выключен

С `neo_mode: true` игра падает на первых кадрах:

```
[Debug] <Critical> (shadPS4:GpuComm) texture_cache.cpp:585 ResolveOverlap: Unreachable code!
```

Оба скрипта перед каждым запуском принудительно ставят `neo_mode: false`.

## 5. Запуск

Ярлык «NHL 24» в меню приложений или на рабочем столе, либо:

```bash
./nhl24-launcher.py      # окно лаунчера
./nhl24-linux.py play    # сразу в игру с сохранёнными настройками
```

Первый запуск: несколько минут чёрного экрана, пока компилируются шейдеры. Лог:
`user/log/linux_run.log` и лог эмулятора `user/log/shad_log.txt`.

**KDE Plasma:** ярлыки на рабочем столе Plasma запускает, только если они исполняемые,
иначе открывает их в текстовом редакторе. Команда `desktop` это настраивает; при первом
двойном клике Plasma один раз спросит — ответьте «Выполнить».

**Старые ярлыки Wine:** если Windows-установщик запускался под Wine, он оставил свои ярлыки
«NHL 24» (они запускают старый лаунчер через Wine и больше не работают). Убрать их:

```bash
./nhl24-linux.py desktop --remove-wine
```

## Wine (Windows-версия) — не работает

`nhl24_new.exe` (v0.8.3) под wine-staging 11.20 + DXVK 3.1.1 с префиксом Windows 7 и
Windows 10 всегда падает сразу после старта потока `Content Mounter`:

```
[Debug] <Critical> (Game:Main) mutex.cpp:13 lambda: Assertion Failed!
```

Нативная сборка этого не делает — похоже на разницу в синхронизации потоков у Wine.

## Свой архив для Linux

`make-linux-repack.sh` собирает архив `NHL24PC-Linux_<версия>.tar.gz` из Windows-репака
(берутся `tools/config_default.json`, мод сборных `tools/mods/`, `user/input_config/`, README)
и собранного эмулятора, плюс скрипты, инструкции и исходники (патч, SOURCE.txt, LICENSE):

```bash
./make-linux-repack.sh NHL24PC_V3_2.zip ~/src/nhl24pc 3.2-linux1
```

Файлов игры в архиве нет. Скрипт показывает, какая версия glibc и какие библиотеки нужны
эмулятору: бинарник, собранный на свежем дистрибутиве, не запустится на более старом.

## Устаревшее: лаунчер репака под Wine

До появления нативных скриптов PowerShell-лаунчер репака запускался под Wine для настроек,
а `nhl24.sh` после него запускал нативную сборку; `patch_launcher.sh` добавлял лаунчеру
ссылки на сборки .NET, нужные PowerShell 7. Оставлено для справки, вместо этого —
`nhl24-launcher.py`.

## Авторы

Сигнатуры патчей, настройки и дизайн лаунчера — из репака NHL24PC от LEVASTAK;
эмулятор — форк shadPS4 nhl24pc от HajDEO.
