# NHL24 PC на Linux

NHL24 PC работает как нативное Linux-приложение. Wine, PowerShell и Windows-лаунчер не нужны.
Linux-пакет содержит эмулятор, Tk-лаунчер, консольный установщик, чистую переносимую конфигурацию и
актуальный патч NHL 24. PKG игры нужно получить самостоятельно с принадлежащей вам копии.

Поддерживаются:

- CUSA37933 (США) и CUSA37934 (Европа);
- базовая игра 01.00;
- обновление 01.60.

## Требования

- x86-64 Linux и процессор уровня x86-64-v3 (Intel Haswell или AMD Excavator и новее);
- видеокарта с Vulkan 1.3 и актуальным драйвером;
- минимум 8 ГБ ОЗУ, рекомендуется 16 ГБ;
- около 40 ГБ свободного места;
- Python 3.8 или новее;
- Tk 8.6 для графического лаунчера (`python3-tk` в Debian/Ubuntu, `tk` в Arch);
- CMake 3.24 или новее для сборки из исходников.

Зависимости сборки такие же, как у shadPS4. Примеры для популярных дистрибутивов:

Debian/Ubuntu:

```bash
sudo apt install build-essential clang clang-tools git cmake ninja-build libasound2-dev libpulse-dev \
  libopenal-dev libssl-dev zlib1g-dev libedit-dev libudev-dev libevdev-dev libsdl2-dev \
  libjack-dev libsndio-dev libvulkan-dev vulkan-validationlayers libpng-dev uuid-dev python3-tk
```

В Ubuntu 22.04 штатный CMake 3.22 слишком старый. Установите актуальную версию CMake или собирайте в
контейнере/дистрибутиве поновее.

Fedora:

```bash
sudo dnf install clang git cmake ninja-build alsa-lib-devel pipewire-jack-audio-connection-kit-devel \
  openal-soft-devel openssl-devel libevdev-devel systemd-devel libXext-devel libXcursor-devel \
  libXi-devel libXrandr-devel vulkan-devel vulkan-validation-layers libpng-devel libuuid-devel \
  python3-tkinter
```

Arch Linux:

```bash
sudo pacman -S base-devel clang git cmake ninja sdl2 vulkan-validation-layers libpng util-linux python tk
```

## Сборка

Клонируйте репозиторий вместе с подмодулями и соберите текущую ветку:

```bash
git clone --recursive https://github.com/HajDEO/nhl24pc.git
cd nhl24pc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel "$(nproc)"
```

Результат — `build/nhl24`. Никакой дополнительный патч применять не нужно.

## Переносимый Linux-пакет

После сборки выполните из корня репозитория:

```bash
./docs/linux/make-linux-repack.sh
```

Скрипт создаёт `NHL24-PC-Linux-v<версия>.tar.gz` только из файлов этого репозитория. Другой бинарник
или путь архива можно указать явно:

```bash
./docs/linux/make-linux-repack.sh /путь/к/nhl24 /путь/к/NHL24-PC-Linux.tar.gz
```

Распакуйте архив в доступную для записи папку. Не разделяйте `nhl24`, Python-скрипты и папку `user/`.

Готовый артефакт workflow называется `nhl24-pc-ubuntu-24.04-x64-...`, потому что его базовая система
совместимости — Ubuntu 24.04. Для старых дистрибутивов соберите проект из исходников. Файл
`BUILD-INFO.txt` внутри архива содержит точный commit и требования к glibc/libstdc++.

## Установка и запуск

Графический лаунчер:

```bash
./nhl24-launcher.py
```

Нажмите **Установить PKG** и выберите PKG базовой игры. После завершения той же кнопкой установите
обновление 01.60. Установщик проверяет тип пакета и регион; обновление распаковывается в оверлей
`game-UPDATE/`. Для запуска нажмите **ИГРАТЬ**.

То же из терминала:

```bash
./nhl24-linux.py install /путь/к/base.pkg
./nhl24-linux.py update /путь/к/update.pkg
./nhl24-linux.py play
```

Оба пакета можно установить одной командой:

```bash
./nhl24-linux.py install /путь/к/base.pkg /путь/к/update.pkg
```

Другие команды:

- `status` показывает версию игры и состояние патчей;
- `settings` открывает текстовое меню настроек;
- `remove-update` возвращает базовую версию;
- `desktop` пересоздаёт ярлыки в меню приложений и на рабочем столе;
- `desktop --remove-wine` также удаляет старые ярлыки Wine.

Графический лаунчер показывает только настройки, реализованные в текущих исходниках: разрешение
рендера, полноэкранный/оконный режим, HDR, VSync, лимит кадров, FSR, безопасное чтение с GPU,
диагностику, русский текст игры и клавиатуру. Режим PS4 Pro отключён, потому что для этой игры он пока
нестабилен.

Базовая игра находится в `game/`, обновление — в `game-UPDATE/`, настройки и сохранения — в `user/`.
При первом запуске несколько минут возможны рывки или чёрный экран, пока компилируются шейдеры.

Логи находятся в `user/log/`:

- `shad_log.txt` — основной лог эмулятора;
- `linux_run.log` — вывод запуска;
- `extract.log` и `install-gui.log` — установка PKG.

## Запуск из исходников

Скрипты можно запускать прямо из `docs/linux/`. Они автоматически находят `build/nhl24` и создают
чистую конфигурацию из `release/user/config.json`:

```bash
./docs/linux/nhl24-launcher.py
```

В этом режиме данные игры и переносимые настройки хранятся рядом с бинарником:
`build/game/`, `build/game-UPDATE/` и `build/user/`. Это совпадает с поиском файлов относительно эмулятора. Для обычной
игры удобнее переносимый архив.

## Примечания

- В Wayland установите XWayland/`xrandr`, если автоопределение экрана возвращает 1920x1080.
- Бинарник, собранный в новом дистрибутиве, может требовать более новые glibc/libstdc++. Упаковочный
  скрипт сохраняет обе найденные версии в `BUILD-INFO.txt` и выводит список динамических библиотек.
- Не публикуйте файлы игры, PKG, сохранения или кэш шейдеров.
