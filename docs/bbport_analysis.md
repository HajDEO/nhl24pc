# bbport (deadinside28/bloodborne_pc) – detailná analýza

Analýza z 2026-10-07, repozitár na HEAD `4d4d208` (release 0.3, 2026-10-06). Čítané len zo zdrojov
(nič sa nebuildovalo ani nespúšťalo; Windows host bez Pythonu). Odkazy na súbory sú relatívne ku koreňu
repozitára. Účel: pochopiť projekt ako predlohu pre podobný port inej PS4 hry (NHL 24, pozri časť 9).

---

## 1. Čo to je v jednej vete

bbport je „Wine + DXVK pre jednu hru“: pôvodný x86-64 `eboot.bin` Bloodborne (CUSA03173 v1.09) sa
**offline** prevedie na plochý pamäťový obraz, do ktorého sa natívne prilinkujú `libc.prx` a
`libSceFios2.prx` z dumpu; malý C loader obraz namapuje a skočí doň; ~6 000 riadkov C runtime (HLE)
nahrádza PS4 systémové knižnice, ktoré hra importuje; grafika ide cez vendorované a silne upravené
video jadro shadPS4 (`libbbgpu.so`) s vlastným temporálnym upscalingom (FSR 3.1 / FSR 4 / FSR 4.1.1 /
TAA). Žiadna CPU emulácia, žiadny JIT.

Kľúčové čísla:

| Čo | Hodnota |
|---|---|
| Vlastný C kód (loader + runtime) | ~6 100 riadkov (`src/`), z toho `import_names.inc` 750 riadkov |
| Offline skripty | ~1 340 riadkov Python (`scripts/`) |
| GPU shim (nové C++) | ~5 200 riadkov (`gpu/shim/`) |
| Vendorované shadPS4 | ~107 000 riadkov, commit `b4e7ae7`, 293 značiek `bbport:` v 76 súboroch |
| Launcher (GTK4) | ~1 430 riadkov Python |
| Testy | 92 Python testov, 5 C testov, 7 C++ GPU testov; žiadne CI |
| Komunitné patche | 61 skupín v `patches/Bloodborne.xml` (3 185 zápisov) |
| Env premenné | ~150 `BB_*` |
| História | 50 commitov, 2026-09-25 až 10-06; 44/50 so spoluautorom Claude; jeden autor, ~3 týždne |
| Výkon (RX 7800 XT) | 26 FPS (1 GPU vlákno) → 145–150 FPS 1080p; ~90 FPS 4K FSR 4 Balanced |
| Licencia | GPL-2.0-or-later (kvôli shadPS4) |

---

## 2. Architektúra – päť vrstiev

```
dump hry (eboot.bin, sce_module/*.prx, param.sfo)
   │  scripts/prepare.py        SELF → ELF → plochý obraz + relokácie + mená importov (BBPROBE2)
   │  scripts/link_modules.py   + libc.prx + libSceFios2.prx natívne (BBPROBE5), fs:→gs: prepis
   │  scripts/patches.py        XML patche → out/patches.bin (BBPATCH2)
   │  scripts/content_profile.py, scripts/mods.py
   ▼
out/boot-linked.bin ──► src/probe.c (bb-probe, -no-pie)
                           mmap pod 1 TiB, relokácie s prioritou HLE > natívny export > trap,
                           TLS/GS TCB, DT_INIT modulov, skok na entry hry
                           │
          ┌────────────────┴───────────────────┐
          ▼                                    ▼
   src/runtime_*.c (HLE)                 gpu/libbbgpu.so (DT_NEEDED)
   kernel, memory, thread, mutex,        gpu/shim/*   – C ABI (bbgpu.h, 9 funkcií), pamäťové modely,
   rwlock, sema, time, rtc, file,                      overlay, nastavenia, diagnostika
   savedata, content, audio, ajm,        gpu/shadps4  – video_core + shader_recompiler + GnmDriver/
   pad, services                                       VideoOut/equeue/AvPlayer HLE (PM4 → Vulkan)
```

Riešenie importov: pre každý importovaný symbol `NID#lib#mod` loader volá `runtime_resolve()`
(`src/runtime.c:201-259`), ktorý reťazí moduly runtime a nakoniec `bbgpu_resolve()`. Čo sa nenájde,
dostane 32-bajtový trampolín: pri prvom volaní vypíše NID, meno, offset volajúceho a skončí s kódom 20.
**Žiadne automatické „return 0“** – to je hlavná disciplína projektu.

Pamäťový layout (všetko pod 1 TiB, lebo hra pakuje ukazovatele do 40 bitov – zistené na zámku
`0x20368c0`): obraz hry a zásobníky v `[0x8_0000_0000, 0x10_0000_0000)`, pamäť hry (direct + flexible)
v `[0x10_0000_0000, 0xfc_0000_0000)`; direct memory = jeden riedky memfd 5056 MiB (ako retail PS4),
flexible 448 MiB, stránky 16 KiB (`src/runtime_memory.c`).

Vlákna GPU strany: `shadPS4:GpuCommandProcessor` (stupeň A, dekóduje PM4), `bb:DrawRec` (stupeň B,
viaže zdroje a nahráva drawy), `bb:DrawScan` + `bb:DrawPrepN` (špekulatívna príprava drawov,
SCHED_IDLE), `bb:VkRecorder0..2` (zápis Vulkan príkazov), `bb:Copy0..3`, `bb:GpuSignals` (poctivé
fence), `shadPS4:PresentThread` (vblank 480 Hz), `bb:Present`, `bb:window`, `bb:kernel_service`.

Výstupné kódy: 1 chyba loadera, 20 prvý nepodporovaný import, 21 nepodporovaný režim („STOP“),
22 stack protector, 23 GPU assert / debug výnimka, 128+signál pád.

---

## 3. Subsystémy

### 3.1 Loader a offline príprava obrazu
Súbory: `scripts/prepare.py`, `scripts/link_modules.py` (a starší `link_libc.py`, ktorého výstup sa už
nepoužíva), `scripts/game_check.py`, `src/probe.c`, `src/import_names.inc`, `run.sh`.

- `prepare.py` číta SELF (magic `4F 15 3D 1D`, u16 počet segmentov na offsete 24, 32-bajtové
  záznamy, ELF hlavička za tabuľkou). Berie len „blocked“ (0x800) nešifrované segmenty, uloží PT_LOAD
  a PT_SCE_RELRO na ich vaddr do obrazu (vaddr 0 = offset 0; PS4 báza 0x400000 sa v obraze nepoužíva),
  rozparsuje PT_SCE_DYNLIBDATA cez DT_SCE_* tagy a prevedie relokácie na 3 druhy: 0 relatívna,
  1 funkčný import, 2 dátový import. Podporuje len typy 1/6/7/8 (+16 DTPMOD64 v PRX).
- `link_modules.py` pridá PRX moduly za obraz (64 KiB zarovnanie), zlúči relokácie a tabuľku importov,
  páruje exporty podľa (NID, knižnica+verzia, modul+verzia), **prepíše každý `mov rax, fs:[0]`
  (9 bajtov) na `gs:`**, lebo FS vlastní glibc. Iné formy prístupu k TLS by prešli bez povšimnutia.
- `probe.c` (541 riadkov): validácia limitov (obraz ≤ 512 MiB, ≤ 16 modulov, TLS moduly 2..7, TLS
  ≤ 1 MiB/modul), trampolíny, PROT_NONE stránky pre nevyriešené dátové importy, aplikácia patchov
  **po relokáciách a pred mprotect** (s prebázovaním pointerov v patchoch), FreeBSD variant-II TCB cez
  `arch_prctl(ARCH_SET_GS)`, volanie `DT_INIT` modulov, `enter_on_stack()` na 8 MiB zásobník s
  konvenciou PS4 crt (`rdi=&{argc,argv}`, `rsi=exit callback`).
- `game_check.py` pinuje SHA-256 načítaného obrazu (CUSA03173 1.09); `BB_SKIP_GAME_CHECK=1` obíde.
- `import_names.inc`: 747 párov `NID#lib#mod → meno` (mená zo shadPS4 aerolib). Hlavička tvrdí
  „len diagnostika“, ale `runtime_lookup()` na ňom stojí. **Generátor v repozitári nie je.**
- Dôležité poradie: `bbgpu_init()` (okno, Vulkan device, registrácia Gnm/VideoOut NIDov) beží
  **pred** riešením relokácií (`probe.c:452-464` vs `480-494`), lebo `bbgpu_resolve` musí mať tabuľku
  symbolov plnú. Lenivá inicializácia GPU pri prvom `sceGnm*` volaní bez prestavby nejde.

### 3.2 HLE runtime – jadro (kernel, pamäť, vlákna, synchronizácia, čas)
`src/runtime.c`, `runtime_kernel.c`, `runtime_memory.c` (851 r.), `runtime_thread.c`, `runtime_mutex.c`,
`runtime_rwlock.c`, `runtime_sema.c`, `runtime_time.c`, `runtime_rtc.c`.

- Dva štýly rozlíšenia NIDov: staršie moduly (mutex, thread, rwlock, sema, time, content, memory)
  porovnávajú **celý reťazec `NID#lib#mod`** natvrdo (celkom **126 miest**: 89 `strcmp` + 37-prvková
  tabuľka v `runtime_thread.c:325-349`); novšie (kernel, file, services, ajm, audio, pad, rtc,
  savedata) hľadajú podľa mena cez `import_names.inc` (391 záznamov). Pre inú hru treba prvé prepísať,
  druhé stačí regenerovať tabuľku.
- Pamäť: VMA tabuľka pod rwlock s generačným počítadlom (vlákna GPU majú cache regiónov bez zámku),
  fronta hookov map/unmap/invalidate pre GPU stranu, druhý „backing“ pohľad na memfd pre zápisy GPU
  (obchádza ochranu stránok), `store_once` (jednotlivé atomické stores, lebo glibc `memcpy` 4–7 bajtov
  robí dva prekrývajúce sa zápisy → prepísal uvoľnený label), write-watch bitmapa pre nový model,
  dma-buf chunk alokátor (256 MiB).
- Vlákna: host pthread + TCB s GS; priority/afinita/politika sa len zaznamenávajú, nevynucujú.
  Semafory od nuly (FIFO namiesto prioritného poradia), rwlock s registrom držiteľov pre PS4 chybové
  kódy, mutexy nad glibc so statickou inicializáciou (handle 0/1).
- Hodiny CLOCK_MONOTONIC, TSC kalibrácia 20 ms, RTC tiky od roku 1.
- Neimplementované, ale importované (nikdy sa nezavolali): `sceKernelGetModuleInfoFromAddr`,
  `sceKernelMlock`, `msync`, `__Ux86_64_setcontext`, `_is_signal_return`, `sigreturn`,
  `__elf_phdr_match_addr`, `__pthread_cxa_finalize` → **C++ výnimky a signal return by skončili kódom 20**.
  Chýbajú event flagy, `sceKernelLoadStartModule`/`dlsym` (Bloodborne ich nepotrebuje).

### 3.3 HLE runtime – súbory, save, obsah, audio, pad, služby
`runtime_file.c`, `runtime_savedata.c`, `runtime_content.c`, `runtime_audio.c`, `runtime_ajm.c`,
`runtime_pad.c`, `runtime_services.c` (236 exportov).

- Súbory: 16 mount pointov (`/app0`,`/hostapp` → dump, read-only; `/temp0`,`/download0`,`/data` → user;
  `/savedataN` dynamicky), FreeBSD `stat` (120 B), dirent, errno; Fios2 je natívny, HLE je len
  syscall vrstva pod ním. Pred `read()` do GPU-sledovaných stránok sa stránky „dotknú“ (inak EFAULT).
- SaveData: adresáre pod `user/savedata/1/<title>/`, `param.bin` je surová štruktúra (nie SFO),
  RDONLY mount sa nevynucuje. Obsahuje **Bloodborne „sound hack“** (bajt 0x204E v
  `SPRJ0005/userdata0010`).
- AppContent: offline profil (`out/content.bin`, SKU + USER_DEFINED_PARAM), **žiadne DLC**; nenulový
  service label = exit 21. `sceSysmoduleLoadModule` je len refcount, nič sa nenačítava.
- Audio: SDL3 streamy, iba 48 kHz, pacing „raz za periódu buffera“ ladený na FMOD; ATRAC9 cez
  LibAtrac9 synchrónne pri `sceAjmBatchStartBuffer`; MP3/AAC = exit 21.
- Pad: jeden SDL3 gamepad alebo klávesnica, remap cez `bbport.ini`, touchpad emulácia, rumble,
  skriptovaný vstup (`BB_PAD_FILE`) a nahrávanie/replay trasy (`BB_PAD_RECORD/REPLAY`).
- Služby: jeden používateľ, PSN odhlásený, sieť „odpojený kábel“, dialógy sa „dokončia“ okamžite bez UI,
  ImeDialog cez klávesnicu v okne, trofeje logované, PlayGo všetko lokálne. Neznámy
  `sceSystemServiceParamGetInt` id = exit 21 (zámer).

### 3.4 GPU shim a dva pamäťové modely
`gpu/bbgpu.h`, `gpu/shim/*`, `gpu/CMakeLists.txt`, `gpu/VENDOR.txt`.

- C ABI je 9 funkcií: `bbgpu_register_kernel`, `bbgpu_init`, `bbgpu_resolve`, `bbgpu_handle_fault`,
  `bbgpu_dump_guest_writes`, `bbgpu_text_input_begin/poll`, `bbgpu_overlay_captures_input`,
  `bbgpu_symbol_count`. Opačným smerom ~25 `runtime_*` symbolov vyriešených cez
  `--allow-shlib-undefined` (zoznam de facto v `tests/test_gpu_runtime_stubs.cpp`).
- Shim nahrádza shadPS4 hlavičky (`core/memory.h`, `address_space.h`, `signals.h`, `libs.h`,
  `emulator_settings.h` → všetky nastavenia z `BB_*`, logging, tracy, imgui) 7–78-riadkovými
  náhradami, aby sa video jadro skompilovalo mimo emulátora.
- **Model A (default, „0.2“)**: pamäť hry na rovnakých host adresách, GPU má kópie vo VRAM, zápisy CPU
  sa sledujú mprotect-om 4 KiB stránok (regióny 4 MiB), SIGSEGV → `bbgpu_handle_fault` → okno 256 KiB
  sa odomkne. Voliteľne userfaultfd (`BB_UFFD=1`, na Decku padalo).
- **Model B (`BB_GUEST_IN_PLACE=1`, experimentálny, 0.3)**: direct memory sa alokuje po 256 MiB
  chunkoch ako Vulkan host-visible pamäť exportovaná ako **dma-buf** a mmapnutá na adresy hry; GPU číta
  pamäť na mieste (Onion), Garlic dáta dostávajú VRAM kópiu, bloky nepoužité 60 s sa degradujú,
  textúry nepoužité 20 s sa uvoľnia. Bez ochrany stránok: o zápisoch CPU sa GPU strana dozvie z
  obalených libc `memcpy/memset`, z čítania súborov a z **int3 hookov v kóde hry** (alokátor GPU
  rozsahov, TLSF heap, memcpy v loaderoch zdrojov – `gpu/shim/bbport_guest_hooks.cpp`, všetko
  natvrdo offsety Bloodborne). Fence sa zapisujú až po skutočnom dokončení GPU („honest labels“).
  NVIDIA nevie mmapnúť dma-buf s nenulovým offsetom → automatický návrat do modelu A.
- Diagnostika na jednu chybu (pád `0x263b8e7`, poškodenie free-listu 48-bajtových label objektov):
  `bbport_free_check.cpp` (1 090 r.), `bbport_write_log.cpp`, `bbport_heap_sites.cpp`.
- ImGui overlay (Insert / L3+R3), `bbport.ini` nastavenia ako atomiky, „Apply and restart“ =
  `execlp("bash","run.sh")`.

### 3.5 Zmeny vo video jadre shadPS4 (okrem upscaleru)
- **Dvojstupňová draw pipeline**: stupeň A dekóduje PM4, udržiava registre a kontrolný súčet, vyberá
  pipeline (prípadne z predpripraveného drawu od workerov), kopíruje malé konštanty do 32 MiB
  ConstantRing a balí DrawPacket (špinavé bloky registrov + user data) do 16 MiB SPSC ringu; stupeň B
  (`bb:DrawRec`) vlastní texture/buffer cache, scheduler, upscaler. Operácie stupňa A, ktoré sa dotknú
  stavu B, najprv „draininú“ (`PipelinedOpcode` v `liverpool.cpp:337` vymenúva, čo drain nepotrebuje).
- **Viacvláknový zápis Vulkan príkazov**: closures v 128 KiB chunkoch, segmenty 64 KiB = vlastné
  command buffery, asynchrónny submit z recorderov; prvý veľký zisk (26 → 64 FPS).
- **Memoizácie**: RenderStateMemo (90 % drawov pokračuje v otvorenom render passe, +9 %),
  TextureSetMemo (XXH3 hashov T# descriptorov, 32 768 slotov, +5–7 %).
- **Copy-shader HLE**: compute shader hry s hashom `0xfefebf9f` (~57 dispatchov/snímku, ~1024 regiónov)
  sa číta na CPU a vykoná jedným dispatchom `buffer_multi_copy.comp` (2,1 → 0,6 ms/snímku).
- **Fence/poradie**: `work_retired` (GPU idle až keď dopadli všetky zápisy do pamäte hry, nie „všetko
  dekódované“ – príčina 13–17-minútového pádu), `pending_fences` pre `WaitRegMem`, atomický
  `StoreLabel`, kópia DCB pri submite (hra prepisovala command buffer pred dekódovaním).
- Prezentácia: `bb:Present` oddelené od vblank vlákna; `BB_FRAMES_AHEAD=1` (rozptyl snímky 9,9 →
  0,9 ms pri 4K). GC textúr v sekundách, uvoľňovanie VRAM blokov, VMA bloky 64 MiB.
- Diagnostika: breadcrumbs (VK_AMD_buffer_marker) pomenujú draw/dispatch, v ktorom GPU visí;
  GPU profiler; frame capture; BB_TOGGLE_FILE 64-bitová maska vypínačov optimalizácií za behu;
  LTO + PGO (+4 %).
- `gnmdriver.cpp` (3 450 r.) je stále shadPS4 HLE: `sceGnm*` serializujú PM4 do buffera hry, ktorý sa
  potom dekóduje. Sľúbený „DXVK-style preklad GnmDriver → Vulkan bez PM4“ **v repozitári neexistuje**.
  Navyše Bloodborne linkuje Gnmx staticky a veľa PM4 zapisuje inline, takže čistý preklad na úrovni
  `sceGnm*` exportov by aj tak nestačil. Eboot importuje 50 GnmDriver NIDov; shadPS4 HLE registruje 254.

### 3.6 Temporálny upscaling a pohybové vektory
`vk_temporal_upscaler.cpp` (2 017 r.), `vk_camera_motion.cpp`, `vk_object_motion.cpp`,
`motion_history.h`, `vk_scene_resolution.cpp`, `vk_fsr4.cpp`, `fsr411/`, `host_shaders/*.comp`,
`tools/fsr4cap/`.

- Bloodborne nemá velocity buffer ani matice predchádzajúcej snímky. Kamera: v G-buffer passe sa
  hľadá **864-bajtový constant buffer** podľa číselného podpisu (`[0]=3000` far, `[1]=1/3000`,
  `[4],[5]` rozlíšenie), z neho view (floaty 8–19), inverzná view (180–191), projekcia (52/57/62/63);
  `camera_motion.comp` rekonštruuje pohyb z hĺbky. Objekty: rekompilátor emituje variant vertex
  shaderu, ktorý ukladá clip pozície do ringu a číta minulé; drawy sa párujú podľa hashu VS, vertex
  streamov, index buffera, rozsahu a poradia výskytu; skeletony podľa veľkosti bone-palette buffera
  (≥640 B postava, 96–639 B násobok 48 = zbraň/prop).
- Jitter sa **nevkladá do konštánt hry**, ale posunom viewportu drawov nad scénovou hĺbkou (Halton,
  fázy 8·(output/render)², 8..256). Drawy s ≤6 indexmi sa považujú za fullscreen a nejitterujú (sprity
  nedostanú jitter – známy zvyšok).
- Dve topológie: 1080p/TAA „HDR cesta“ (upscale na RGBA16F pred post-combine CS `0x9a9cf8a9`) a
  „škálovaná cesta“ (posledný RGBA8 target pred prvým Scaleform UI drawom `0x34e8a281` sa upscaluje,
  UI a display pass sa presmerujú na výstupné rozlíšenie). Znížené rozlíšenie buď patchom imm32
  v eboote pri štarte (4 adresy), alebo „live“ proxy targetmi (drahšie – hra si myslí, že je 1080p a
  kreslí 8× viac svetiel; default vypnuté).
- FSR 3.1 cez FireBurn/FSR-Vulkan; FSR 4 v07 INT8 (assety z Q2RTX) s bit-exaktným prepisom post
  passu cez LDS (2,9 → 0,8 ms 4K) a opravou race v passe 11; **FSR 4.1.1 = nahratie AMD DX12 DLL pod
  vkd3d-proton (hook vtables, 29 dispatchov, DXIL → SPIR-V cez dxil-spirv) a bit-exaktný replay vo
  Vulkane** (`fsr411.cpp`, 785 r.). Natívne TAA (`taa.comp`) s validáciou hĺbky v priestore
  predošlej kamery, RGBA32F história.
- Hashy shaderov **nie sú** XXH3 emulátora, ale Sony `OrbShdr` footer hash v binárke shaderu
  (`regs_shader.h:17-20, 245-252`) → dajú sa získať offline zo shaderov hry, bez behu.

### 3.7 Patche a mody
- `scripts/patches.py` kompiluje shadPS4/GoldHEN XML (typy bytes/bytes16/32/64/float/utf8/utf16;
  `mask` nepodporuje) do `out/patches.bin`; adresy sú PS4 VA – 0x400000; `probe.c:243-275` aplikuje
  po relokáciách a prebázuje 8-bajtové zápisy do relokačných slotov (60/90 FPS++ menia pointre).
- `isEnabled` v zabudovanom XML sa ignoruje; výber je podľa mena cez tabuľky FPS_PRESETS/EFFECTS/
  MODEL_LOD/RESOLUTION_TEMPLATE v `patches.py`. Default sada: Uncap FPS++, Sprint Fix, Intel Black
  Tonemap Fix na Intel CPU, rozlíšenie podľa presetu.
- Mody: `scripts/mods.py` stavia symlinkový overlay `out/mod-game-*` (case-insensitive párovanie,
  posledný mod vyhráva), `run.sh` ho dá ako `--app0`; runtime o modoch nevie. Len voľné súbory.

### 3.8 Build, launcher, balenie
- `build.sh` (75 r.): CMake/Ninja `libbbgpu.so` (C++23, LTO, voliteľne PGO), `libatrac9.a`,
  `bb-probe` jedným `cc -std=c11 -O2 -no-pie -rdynamic` volaním, `bb-gpu-capabilities`; `--test`.
  Bez Nixu treba GCC, CMake, Ninja, Python 3, glslang, SDL3, Vulkan, fmt, Boost, magic_enum,
  robin-map, xxhash, VMA, spirv-cross, zydis, ffmpeg; `shell.nix` to dodá automaticky.
- `run.sh` (177 r.) je „kompilátor konfigurácie“: mods → prepare → link → content → patches →
  rozhodnutie patch vs live rozlíšenie → env defaulty → `build.sh` → `exec bb-probe`. Pri každom
  spustení aj reštarte z menu beží znova.
- Nastavenia v troch vrstvách: launcher `settings.json` → `BB_*` env; `bbport.ini` (čítajú Python,
  bash, C aj C++); env overrides v `BbSettings::Load`.
- Launcher GTK4/libadwaita (RU kľúče, EN preklad), AppImage cez Nix (`packaging/default.nix`,
  `nix bundle`), statický `bbport-entry.c` odstraňuje Steam `LD_PRELOAD`, `bbport_vulkan.py` nájde
  hostiteľský NVIDIA ICD. Všetko Linux-only; `_WIN32` vetvy v `probe.c` sú mŕtve.

### 3.9 Testy a nástroje
- C testy (`tests/test_runtime.c`, `test_sema.c`, `test_pad.c`, `test_file_mods.c`, `test_content.c`)
  volajú HLE exporty cez `runtime_resolve("<NID>#lib#mod")` a kontrolujú presné Orbis chybové kódy.
- Python testy so syntetickými SELF/ELF/param.sfo/BBPROBE obrazmi; `test_probe.py` spúšťa reálny
  loader s ručne poskladaným x86 kódom a kontroluje kódy 20/1/132; `test_run_settings.py` prejde
  `run.sh` vrátane 3 reštartov s falošným probe.
- GPU testy headless (`Vulkan::Instance(0,false)`, Lavapipe stačí): TAA, camera motion, proxy
  targety, SPIR-V backend rekompilátora.
- Nástroje: `tools/ab.sh` (A/B jedného toggle bitu v jednom procese), `restart.sh`/`press.sh`
  (skriptovaný vstup do levelu), `soak*.sh`, `bench.sh`, `load_watch.sh`, `dump_view.py`,
  `got_import.py` (GOT slot → import), `host_import_probe.c`/`sparse_host_probe.c` (merania
  pamäťových modelov), `bridge_helper.c` (Wine polovica prototypu pre XeSS/DLSS; Linux polovica
  v repozitári nie je), `fsr4cap/` (nahrávanie FSR 4.1.1).
- Netestované: samotná hra (všetko syntetické), PM4/GCN preklad na reálnych shaderoch, cache,
  page tracking, model B, audio, savedata, launcher UI, balenie. Výkon sa nikde neasertuje.

### 3.10 História a metóda práce
- Chronológia: pred-git prototyp „native_probe“ (loader + libc natívne + 7 modulov runtime, zastavil
  sa na `sceSysmoduleLoadModule(0xb4)`) → 25.9. prepis pamäte, 13 vlákien hry, vendorovanie shadPS4,
  audio, 26→64 FPS → 26.–29.9. kamera MV, jitter, FSR 3.1, ImGui, objektové MV, sebakritický audit →
  30.9.–1.10. jedna ~21-hodinová session (35 commitov: dvojstupňová pipeline, memo, pád 0x263b8e7,
  1440p/2160p, launcher, AppImage, FSR 4 LDS, FSR 4.1.1 replay, publikovanie) → 1.–3.10. (0.2) TAA
  audit (8 chýb), live rozlíšenie demotované, present vlákno, mody → 4.–6.10. (0.3) model B,
  uvoľňovanie VRAM, breadcrumbs, druhá príčina 0x263b8e7, oprava vertikálnej osi kamery.
- Metóda: každá optimalizácia má toggle bit, rozhodnutia podľa in-process A/B (reštarty kolíšu
  ±2–5 FPS), perf + rdtsc, skriptovaný vstup na reprodukciu, `taskset` ako emulácia Decku, numerická
  metrika blikania namiesto „vyzerá lepšie“, bit-exaktné overenie FSR prepisov, denné CHANGES dokumenty
  s rozlíšením „v kóde / spustené / overené obrazom“; slepé uličky zostávajú zapísané.
- Poradie, v ktorom sa hra zastavovala na importoch (= poradie implementácie runtime): Sysmodule/
  AppContent → Ajm → Fios2 → SaveData → Rtc → Pad → AudioOut → AvPlayer → VideoOut → GnmDriver.

---

## 4. Generické vs. špecifické pre Bloodborne

| Vrstva | Znovupoužiteľné priamo | Treba regenerovať / nájsť nanovo |
|---|---|---|
| Offline príprava | parse SELF, dyn tagy, relokácie, BBPROBE formát, linker PRX, fs→gs prepis, NID hash | zoznam modulov (`DEFAULT_MODULES`), vzory `fs:` prístupov, `game_check` konštanty, hint list |
| Loader | celý `probe.c` okrem reťazca „Bloodborne“ a 1920×1080 | limity (512 MiB, 16 modulov, TLS 2..7) overiť |
| Runtime jadro | memfd model, VMA, hooky, TCB/GS, sync primitíva, hodiny, libc glue | 126 literálnych NID miest, `import_names.inc`, text range `0x50d96dc`, libc offset `0x56e0000` |
| Runtime I/O | mount tabuľka, FreeBSD ABI, SaveData layout, SDL3 audio/pad, Ajm parser, offline služby | sound hack, 3 AppContent NIDy, whitelist SystemService param, FMOD pacing, len ATRAC9, žiadne DLC |
| GPU shim | C ABI, náhrady hlavičiek, model A komplet, infraštruktúra modelu B, copy pool, overlay, settings | int3 hooky (`guest_hooks`), `heap_sites`, `free_check`, DynamicWriters rozsahy, base `0x800000000` |
| Video core | dvojstupňová pipeline, recordery, memo, fence infra, present, GC, cache, breadcrumbs, PGO | hash copy shaderu, heuristiky passov (MRT≥5, RGBA16F, 1920×1080, 960×540), BB_SUBMIT_AHEAD=2 |
| Upscaling | hook architektúra, camera_motion.comp, object motion mechanizmus, FSR 3.1/4/4.1.1, TAA, proxy | podpis 864 B, offsety matíc, hashy `9a9cf8a9`/`34e8a281`/5 movie VS, 4 adresy rozlíšenia, far 3000 |
| Patche/mody | XML kompilátor, BBPATCH2, prebázovanie, patches.json, overlay engine | celé `Bloodborne.xml`, tabuľky mien, imm32 adresy, GAME_FOLDERS |
| Build/launcher | build.sh, run.sh kostra, 3-vrstvové nastavenia, GTK kostra, Nix/AppImage, ICD discovery | názvy, defaulty, Deck 720p, rozlíšenia |
| Testy/nástroje | všetky vzory (contract testy cez NID, syntetické obrazy, headless GPU, ab.sh, toggle) | NID suffixy v testoch, fixtures s hashmi a konštantami |

Ako autor získal špecifické znalosti (v repozitári nie sú RE poznámky, len nástroje, ktoré to
umožnili): slučka „spusti → STOP na importe → implementuj“ (exit 20 + `got_import.py`);
`BB_CAPTURE_TRIGGER` frame analyzer (shadery a targety každého passu → hashy a heuristiky);
`BB_TRACE_SIZE` (sledovanie bufferov danej veľkosti → nájdenie 864 B konštánt); `BB_FRAME_STATS`
atribúcia čakaní a SIGPROF sampler podľa guest RIP (horúce funkcie hry, alokátor);
`PageManager::ReportFaultSites` (guest RIPy, ktoré zapisujú do GPU pamäte → miesta memcpy
v loaderoch zdrojov); disassembler zjavne použitý, ale nedokumentovaný.

---

## 5. Stav a známe problémy (k 2026-10-06)

- Experimentálny, hrateľný; dôkladne testovaný na jednom stroji (RX 7800 XT, RADV). Celý playthrough
  neoverený.
- Model B môže padať; NVIDIA → fallback; prekryv snímok leakuje heap pri K ≥ 8 (strážený počítadlom).
- GPU hang v indirect dispatch particle shaderu `2da7fe60` – lokalizovaný, neopravený.
- Pád `0x263b8e7` opravený dvakrát, na Decku neoverený, 1 z 10 behov na PC bez príčiny (k 3.10.).
- FSR 4.1.1 race pri šírkach nedeliteľných 64 (zdedené od AMD); GTX 1060 + FSR 4 = čierne okno.
- Occlusion queries syntetické, `IT_SET_PREDICATION` neimplementované (Bloodborne ich nepoužíva –
  **pre Frostbite to bude reálna diera**).
- Stupeň B je CPU limit (~85–90 % jadra); „DXVK-style“ preklad bez kódu; async compute, frame gen,
  DLSS/XeSS, HDR nezačaté.
- Drobnosti z kódu: in-game `Save()` prepíše `bbport.ini` len známymi kľúčmi (pravdepodobne zahodí
  `key.*/pad.*` bindingy – odvodené z kódu, nespustené); `run.sh` stále volá nepotrebný
  `link_libc.py`; `apply_patches` nemá unit test; docs miestami zaostávajú za kódom (DEVELOPMENT_LOG
  opisuje prototyp, ROADMAP z 26.9.).

---

## 6. Lekcie pre druhý port (čo si odniesť)

1. **Explicitné kontrakty, žiadne tiché stuby.** Každý nevyriešený import zastaví proces s menom
   a offsetom volajúceho. Vďaka tomu sa boot dá bisektovať subsystém po subsystéme.
2. **Natívna libc.prx + Fios2.prx z dumpu** namiesto reimplementácie – HLE potom stačí na úrovni
   syscallov. Overiť, že `_init_env` je naozaj `RET` (capability bit), inak je HLE libc nesprávne.
3. **Všetko pod 1 TiB** (image, zásobníky, host heap cez `-no-pie` + `mallopt`): 40-bitové pakovanie
   pointerov a GPU descriptory to vyžadujú.
4. **Fence znamená „GPU dopísalo“, nie „dekódované“**; zápisy do pamäte hry jedným atomickým store.
   Oba pády trvali týždeň, kým sa našli – mať od začiatku write log s intentmi (nie na racing path).
5. **Nerobiť fork/join per draw**; škálujú len rozdelenia vlastníctva (stupeň A/B). Memoizovať
   render state a sady textúr.
6. **Každá optimalizácia má vypínač za behu + A/B v jednom procese**; reštarty sú príliš šumné.
7. **Čítanie súborov do chránených stránok** treba predchytiť dotykom (EFAULT nejde do SIGSEGV handlera).
8. **Pipeline cache**: pri nezhode profilu prebudovať, nie vypnúť na session (zdedená chyba shadPS4).
9. **Hashy shaderov brať z OrbShdr footera** – dajú sa získať offline zo súborov hry.
10. **Live škálovanie rozlíšenia je drahé**, keď si hra myslí, že kreslí natívne; patch imm32 pri
    štarte je lacnejší. Hra zarovnáva výšky targetov na 8 – heuristiky podľa veľkosti musia tolerovať.
11. **Overovať obrazom a metrikou**, nie FPS: „220 FPS“ bolo FSR, ktoré vôbec nebežalo.
12. Zapisovať slepé uličky s číslami – polovica dokumentov sú odmietnuté experimenty, ušetria čas.

---

## 7. Odhad náročnosti pre inú PS4 hru (všeobecne)

- Dni: regenerácia `import_names.inc` (treba napísať generátor: NID = base64(sha1(meno+salt))[:8]
  reverzne; suffixy z DT_SCE_IMPORT_LIB/MODULE id), prepis 126 literálnych NID miest na name lookup,
  `game_check`, prvý boot po `main`.
- Týždne: nové HLE kontrakty podľa zoznamu importov (každá EA hra importuje širokú Np/Http/dialóg
  plochu), AppContent s DLC, viac padov/používateľov, event flagy, prípadne C++ výnimky/unwinder.
- Neznáme: všetko hashované/offsetované (passy, konštanty kamery, hooky alokátora) – treba znova
  zreverzovať; pre hru s velocity bufferom odpadá väčšina motion-vector práce.

---

## 8. Čo bbport neumožňuje bez prestavby

- Runtime načítavanie PRX (`sceKernelLoadStartModule`, `sceKernelDlsym`) – linker je offline a
  statický; moduly nad rámec zoznamu by museli byť prilinkované vopred (ak ich hra načítava
  dynamicky z `/app0`, treba runtime linker alebo offline prilinkovať všetky).
- Windows ako host – memfd, mprotect/userfaultfd, dma-buf, SIGTRAP ucontext, SCHED_IDLE; `_WIN32`
  vetvy sú torzo. Reálne je to Linux (natívne alebo WSL2 nestačí kvôli Vulkanu a dma-buf).
- Hry so šifrovanými/komprimovanými SELF segmentmi (odmieta ich).
- Hry s TLS prístupmi inými než `mov rax, fs:[0]`, s relokáciami mimo typov 1/6/7/8/16, s > 16
  modulmi alebo > 6 TLS modulmi, s obrazom > 512 MiB.

---

## 9. Prvý pohľad na NHL 24 dump (`NHL_PC/GAME_FILE/uroot`) voči bbportu

Fakty z hlavičiek (nič sa nespúšťalo):

| | Bloodborne (bbport) | NHL 24 (CUSA37933) |
|---|---|---|
| Engine | Dantelion2 + Gnmx staticky | Frostbite „Gen4b“; `libSceGnm_lto` a `libSceGpuAddress_lto` staticky (18 výskytov) |
| eboot | 91 MB obraz, 8 SELF segmentov | 187 MB, 10 SELF segmentov, 5 „blocked“ plaintext (žiadny encrypted/compressed flag), ELF `ET_SCE_DYNEXEC`, 12 PH, entry 0x80 → **prepare.py by to mal prijať**, limit 512 MiB stačí |
| Importy | 686 unikátnych, 42 modulov | ~850 scoped symbolov (hrubý grep), dyn tabuľky v plaintexte; knižnice vrátane NpWebApi, NpTus, NpTrophy, NpUtility, SigninDialog, WebBrowserDialog, SharePlay, Remoteplay, VideoRecording, Videodec2, Usbd, Random, Ssl, PlayGo, SaveData(+Dialog), Pad, Rtc, UserService, SystemService, Sysmodule, VideoOut, Voice… |
| sce_module | libc, libSceFios2 (+5 nepoužité) | libc, **libSceFios2** (rovnaká dvojica ako bbport linkuje), libSceNpToolkit2 (834 KB), libSceFace, libSceHand, libSceS3DConversion |
| Vlastné PRX hry | žiadne | `Engine.Render.Core2.PlatformPs4.prx` (607 KB), `Engine.BuildInfo.prx` → Frostbite načítava moduly za behu – **bbport nemá runtime loader PRX** |
| DLC | žiadne (Old Hunters zlúčené) | `SERVICE_ID_ADDCONT_ADD_1..7` v param.sfo → AppContent s add-onmi, bbport tu končí exit 21 |
| SDK | ~2015 | SYSTEM_VER 10.50 (2023) → iná libc.prx, procparam layout a `_init_env` treba znova overiť |
| Velocity buffer | nie (rekonštrukcia MV) | Frostbite má vlastný TAA jitter a velocity target → iný prístup k upscalingu (zachytiť existujúce MV, nie počítať) |
| Occlusion/predication | nepoužíva | Frostbite ich typicky používa → treba doimplementovať v `liverpool.cpp` |
| Dáta | 31 GB voľných súborov dvdroot_ps4 | 33 GB superbundly (.toc/.sb/cas) → mody cez overlay nedávajú zmysel, Fios2 vs. vlastné VFS treba overiť |
| Host | Linux + Nix | Windows 11 bez Pythonu → na vývoj treba Linux (natívne alebo dual boot); WSL2 nestačí pre dma-buf/Vulkan cestu |

Predbežné poradie práce, ak sa pôjde bbport cestou:

1. Linuxové prostredie, klon bbportu s `--recursive`, `bash build.sh --test` na overenie toolchainu.
2. Spustiť `scripts/prepare.py` na NHL eboot (očakávať úpravy: relokačné typy, SKU/param.sfo) →
   `out/analysis.json` dá presný zoznam 42+ knižníc a importov; `link_modules.py` s libc + Fios2
   (a rozhodnúť, či prilinkovať aj libSceNpToolkit2 a Engine.*.prx offline, alebo napísať runtime
   `sceKernelLoadStartModule`).
3. Generátor `import_names.inc` z `analysis.json` + aerolib; prepísať 126 literálnych NID miest na
   name lookup.
4. Boot slučka „exit 20 → implementuj“ až po `main` a prvý `sceGnmSubmit*`; očakávať väčšiu Np/Http/
   dialóg plochu, AppContent DLC, viac padov, event flagy, možno C++ výnimky.
5. GPU: libbbgpu bez zmien by mal zobraziť prvé snímky (PM4 je PM4); potom predication/occlusion,
   nové heuristiky passov a až potom upscaling – ideálne napojiť Frostbite velocity buffer.
6. Až nakoniec model B (hooky alokátora treba zreverzovať nanovo) a launcher/balenie.

Otvorené otázky, ktoré rozhodne až `analysis.json`/`link.json` z reálneho behu: počet TLS modulov,
relokačné typy v NHL eboote a PRX, či libc.prx má `_init_env == RET`, veľkosť direct memory, ktorú si
hra pýta, a či NHL používa Fios2 alebo vlastný Frostbite VFS nad `sceKernel*`.
