# NHL_PC – natívny PC port NHL 24 (PS4, CUSA37933)

## Diagnostický log (build 62, 2026-10-12)

Každý beh `nhl24.exe` zapisuje popri `shad_log.txt` do `nhlport\build\user\log\` tri súbory
(modul `src/common/diag_log.{h,cpp}`, zapnutý predvolene; `NHLPORT_DIAG=0` ho vypne,
`NHLPORT_DIAG_FRAMES=0` vypne CSV po snímkach, `NHLPORT_DIAG_INTERVAL=<s>` mení periódu súhrnov):

- `nhl24_diag.log` – hlavička behu (hra, verzia portu, CPU, OS, GPU + ovládač, konfigurácia okna /
  rozlíšenia / readbackov, aktívne `NHLPORT_*` premenné), potom udalosti s časovou značkou:
  `resolution` (zmena rozlíšenia scény = najväčší depth target v GPU snímke), `window`
  (zmena okna / swapchainu), `videoout` (registrácia bufferov, flip rate, `GetResolutionStatus`),
  `compile` (každá kompilácia pipeline / shaderu s hashom a časom), `stutter` (snímka > 100 ms alebo
  > 2,5× kĺzavý medián, s rozpisom príčin: kompilácie, GPU čakania, readbacky, nové obrazy,
  invalidácie, I/O, CPU čas vlákien, page faulty), `glitch` (zahodené draw cally = chýbajúca
  geometria), `guest` (vlákna hry, načítané moduly), `marker` (značka používateľa) a každých 10 s
  `summary` (fps, 1 % low, GPU busy, kompilácie, readbacky, cache textúr, I/O, audio, rozlíšenia,
  CPU vlákien, pamäť RAM / VRAM / hosťa, nové varovania podľa miesta v kóde). Na konci `final`:
  percentily snímok, histogram, 24 najdlhších snímok s príčinami, značky, celkové súčty, CPU po
  vláknach za celý beh, top 40 varovaní.
- `nhl24_frames.csv` – jeden riadok na prezentovanú snímku (64 stĺpcov: časy, draws / dispatches,
  zahodené draw cally (`skip_bind` = len grafické draw cally; compute dispatche nahradené skratkou
  emulátora sú zvlášť v `cs_meta_clear` / `cs_image_copy` / `cs_image_clear` / `cs_hle`, udalosť
  `compute-shortcut` + zoznam v `final` hovorí, ktorý shader a ktorý cieľ – v menu je to 7×
  za snímku shader `0xe7e7ed01` = HTILE/CMASK clear depth a colour targetov), kompilácie + ms, GPU čakania + stall ms, readbacky + KB + čakanie,
  odhad GPU busy a latencia submit→dokončenie, obrazy +/− a živé, expand / depth-recreate / overlap,
  invalidácie cpu / inline / gpu, re-uploady, rozlíšenie scény, počet veľkostí render targetov,
  render passy, VideoOut buffer, okno, flip backlog, čítania súborov + KB, audio, CPU ms procesu
  a kľúčových vlákien (GPU vlákno, PresentThread, GameSimulationLoop, RenderLoop, Job*), page
  faulty, príznaky `S C W R X I F G T D P A Q M` – legenda je v hlavičke `nhl24_diag.log`).
- `nhl24_threads.csv` – každú sekundu CPU % každého vlákna procesu (názvy vlákien hry sú vidieť).

Značka „teraz som videl glitch“: klávesa **F1** (`hotkey_diag_mark` v `user\input_config\global.ini`)
alebo súbor `user\captures\diagmark` s poznámkou; zapíše udalosť `marker`, označí ďalšiu snímku
príznakom `M` a uloží presnú snímku hry do `user\screenshots`. `tools\run_nhl24.ps1` kopíruje
všetky tri súbory do `logs\nhl24-<stamp>.*`; vyhodnotenie:

```powershell
python tools\diag_report.py logs\nhl24-<stamp>.nhl24_frames.csv
```

(štatistika snímok, tabuľka zásekov s príčinami, verdikt GPU / GPU-vlákno / joby, časová os
rozlíšení, indikátory glitchov, CPU vlákien, udalosti a značky s okolitými snímkami).

**Počítanie referencií metadát CMASK / FMASK / HTILE (oprava 2026-10-12 13:25).** `LinkMeta`
inkrementovalo `refs` len keď sa `info.meta_info.*_addr` obrazu líšil od adresy; obrazy vytvorené
z registrov colour / depth bufferu adresu už mali, prvé prepojenie sa nezarátalo (`refs 0` vo
väčšine záznamov zoznamu „compute dispatches replaced…“) a `UnlinkMeta` pri zmazaní ľubovoľného
vlastníka záznam zmazal ako upstream. Teraz sa prepojenie eviduje v `Image::meta_links` (per typ),
`LinkMeta` započíta každé nové prepojenie, `DeleteImage` odpojí len to, čo obraz naozaj prepojil,
a záznam zanikne až s posledným vlastníkom (`NHLPORT_META_ERASE=1` = upstream, mazanie pri prvom).
Draw log má nový riadok `meta-link f<n> @ <adresa> type <t> refs <r> image @ <adresa obrazu>`.
Meranie: `tools\measure_nhl24.ps1 -Label <x>` (spustí hru cez `run_nhl24.ps1`, v pevných časoch od
prvej snímky vytvorí `captures\screenshot` / `captures\frametrace`, zavrie okno cez WM_CLOSE a
skopíruje draw log a screenshoty do `logs\`; pozor, `frametrace` = N zapíše `drawlog_<N-1>.txt`),
blikanie: `tools\shot_stats.ps1 -Dir logs\shots-<stamp>-<x>` (snímka, ktorá sa líši od oboch
susedov, kým susedia sa zhodujú). 60 s intro + titulná obrazovka, draw log 150 snímok v okne
prechodu intro → menu (10–14 s, jediné miesto, kde sa obrazy rušia):

| | pred (`logs\nhl24-20261012-131959.*`) | po (`…-132704.*`; kontrola `…-133151.*`) |
|---|---|---|
| zoznam „compute dispatches replaced…“: záznamy s `refs 0` / `refs ≥ 1` | 39 / 4 (43 párov) | 0 / 94 (94 párov); kontrola 0 / 21 |
| draw log `meta-unlink` spolu (zrušených obrazov v okne) | 122 (1507) | 134 (1289); kontrola 40 (598) |
| z toho `erase 1` (záznam zmazaný) | 122 = 100 % | 70 = 52 %; kontrola 40 (všetky posledný vlastník) |
| z toho `erase 0` (záznam prežil, má iného vlastníka) | 0 | 64 |
| `meta-new` (nový záznam = všetky slice „cleared“ = falošný clear pre ostatných) | 164 | 90; kontrola 32 |
| `refs` pri `meta-unlink` | vždy 0 | 0–8 (`meta-link` 1–9) |

Screenshoty: intro 30 + titulná obrazovka 30 pred aj po – statické, 0 blikajúcich snímok; po navyše
prechod intro → menu 60 (plynulé stmievanie intra) a výber tímov 44 (`PLAY NOW – SELECT TEAM`, po
stlačení krížika = klávesa `n`) – 0 blikajúcich snímok, zmeny len pri prepnutí panelu UI. V prvom
behu „po“ hra opustila titulnú obrazovku sama v ~33. s (načítanie súborov, nové obrazy); kontrolný beh
bez vstupu ostal na titulnej obrazovke do 62. s ako pred zmenou, takže to nesúvisí so zmenou. Výkon
bez zmeny (kontrola 33,4 fps priemer, 99 % 34,2 ms vs. pred 33,2 fps, 34,6 ms). Samostatný beh na
výber tímov (`logs\nhl24-20261012-134532.*`, `shots-…-teamsel4`): krížik v 40. s (titulná obrazovka)
→ dialóg „SIGN IN REQUIRED“, v 50. s (OK) → hlavné menu s PLAY NOW, v 60. s → výber tímov; 24 snímok
od +64 s, 0 blikajúcich, kompletné UI (`tools\measure_nhl24.ps1 -PressAt "40,50,60" -Shot4At "0,0,64"
-Shot4Frames 30 -CloseAt 74`; série dlhšie než ~100 snímok sa zaseknú a zablokujú ďalšie triggery).
Prestrihy pred zápasom (`logs\nhl24-20261012-135152.*` a `…-135652.*`, `shots-…-cut1` / `cut2`): na výbere
tímov trojuholník (klávesa `c`) = CPU vs CPU, zápas sa spustí sám (`-Keys "40:n,50:n,60:n,66:c"
-ShotsAt "100,108,…" -ShotFrames 30 -CloseAt 186`); nahrávanie „STARTING GAME“ ~+97–110 s (prvý beh,
druhý s teplou cache rýchlejšie), potom prelet arénou → wipe „EA SPORTS“ → nástup na buly → hra. 198 + 157
snímok z nahrávania, preletu, wipu, bulov, oslavy gólu, opakovania a hry: 0 blikajúcich snímok, 0 udalostí
`glitch`; svetlé snímky medzi tmavými sú strihy (wipe EA SPORTS). Známe: svietiace tyče divákov.

## Finálny build v0.8.1 (2026-10-12 noc) a známy problém s prestrihmi

**Build**: `nhlport\build\nhl24.exe` v0.8.1 (Release + PDB, `tools\build_nhlport.ps1 -DebugInfo`), `launcher.exe`
(fallback verzie 0.8.1; obrázky launchera `background.png`, `btn.png`, `ps4_pad.png`, `ico.png` a `launcher.ico`
sú teraz v `launcher\`, `build_launcher.ps1` ich kopíruje k exe). Diagnostika je vypnutá (opt-in `NHLPORT_DIAG=1`,
viď v0.8.0 nižšie); `dump_shaders` v `user\config.json` je `false`.

**Zmeny od v0.8.0:**
- `nhl24.exe` je linkovaný ako Windows GUI aplikácia (`/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup` v `CMakeLists.txt`),
  takže pri spustení (aj dvojklikom) sa už neotvára konzolové okno s logom; log ostáva v `user\log\shad_log.txt`
  a presmerovanie stdout/stderr v `toolsun_nhl24.ps1` funguje ďalej.
- Ikona okna hry (titulok, taskbar, Alt-Tab) sa na Windows berie priamo z ikonového zdroja `nhl24.exe`
  (`SetWindowIconFromExe` v `sdl_window.cpp`: `WM_SETICON` + ikony triedy okna), takže má rovnaké 16/32 px rámce
  ako `launcher.exe` (titulok dostáva ten istý 32 px rámec ako WinForms launcher); predtým SDL dostalo 256 px PNG a
  Windows ho zmenšoval sám. Ikony v exe sú bajtovo zhodné
  (`launcher.ico` = `src\dist
hl24.ico`, 6 veľkostí 16–256 px); ak Explorer ukazuje starú, je to jeho cache
  (`ie4uinit -show`; `toolsuild_nhlport.ps1` ju po úspešnom builde obnoví sám). Overené cez shell API
  (`SHGetFileInfo`, `IShellItemImageFactory` 256 px): rovnaké hashe pre oba exe.
- `BufferCache::InlineData` (zápisy labelov/fence cez WRITE_DATA): hodnota sa zapíše do hosťovskej kópie aj priamo do
  rezidentnej GPU arény (`vkCmdUpdateBuffer`) a stránka sa už neoznačuje „CPU-modified“ – pôvodne by nasledujúce
  nahratie stránky prepísalo GPU-zapísané rozsahy v tej istej stránke (NHL 24 má labely vedľa počítadiel dlaždíc).
  Správne, ale pruhy v prestrihoch to nerieši (viď nižšie).
- Klávesa **F1** (diag): v `input_handler.h` chýbala položka `ControllerOutput(HOTKEY_DIAG_MARK)` v tabuľke výstupov
  (`output_count` 43 → 44), väzba z `global.ini` ukazovala do prázdna. F1 (alebo súbor `captures\capture` s poznámkou)
  teraz = značka + screenshot + draw log 3 snímok + raw dump obrazov naviazaných na UI kompozit (oba varianty
  `0x2c569595` / `0x5bb1bf02`, `NHLPORT_DIAG_F1_CS=hex,hex`) + dump GDS (`captures\gds_f<n>.bin`); dump čaká na
  aktívny draw log, aby dumpnutá snímka bola v logu. Nové riadky draw logu: `clobber`, `buf-inval`, `readback`,
  `inline`, `dispi-args`, `gds`, `gds-fill`, `gds-copy`, `gds-after`, `srt=[…]` (obsah SRT po flattening) –
  všetko len pri zapnutom trace. Nástroje: `tools\capture_report.py` (súhrn záchytu + PNG), `tools\ui_dump_map.py`
  (ASCII mapa UI vrstvy).
- Upratané: `logs\` (screenshotové série, RenderDoc záznamy, raw dumpy, draw logy, logy > 20 MB; ostali textové logy
  a `logs\stripe-20261012\` s dôkazmi k problému nižšie), `build\user\{captures,screenshots,log,shader\dumps,temp}`,
  staré build logy v koreni, `logs\nhl24_build39_nosym.exe`. Odkazy na `logs\shots-*` v starších sekciách README už
  nemajú súbory.

**Známy problém – čierne obdĺžniky v prestrihoch (nevyriešené).** V prestrihoch s kamerou v dynamickom rozlíšení
(1600×904, 1624×912, 1656×936 – mení sa každú snímku) občas chýbajú celé stĺpce 64-px dlaždíc osvetlenej scény
(čierne; napr. 1600×904 stĺpce od x = 896 do konca alebo 896–1472). V hre (scéna 1920×1080, stabilná) sa to nedeje.
Čo sa zistilo (záchyty F1, `logs\stripe-20261012\`):
- Chyba je už v tonemapovanej scéne pred UI kompozitom; UI vrstva aj kompozit (`0x5bb1bf02`, konštanty 1656×936 →
  1920×1080) sú v poriadku.
- Reťazec: depth pyramída (`0x45f5267d`) → hĺbkové hranice dlaždíc (`0xa5821966`) → culling svetiel na dlaždicu
  (`0xeab78653`, `0xf85763dc`: tabuľka 8160 B {offset, počet} + zoznamy indexov) → klasifikácia (`0x5fddf9ec`, obrazy
  25×15 R32G32/R16Uint) → kompaktovanie (`0x172839d0` ×3 tried, `0xccd3f3b4`, `0xe7e4752c` ×3; 1 vlákno = stĺpec,
  GDS atomiky `ds_add_rtn` na počítadlá GDS[(ud_2>>16)+0/4/8/12], zápis `(x<<16)|y` do 2 KB zoznamov s kapacitou 512
  z V#) → `0x55714361` (count×mult → indirect args) → indirect lighting dispatche (`0x91b69bbf`, `0x8939d227`,
  `0xde55468f`, `0xc6941c20`, `0xa8d2345`, `0xe93b3ce6`, tg 8×8, ballot len na uniformitu).
- Vylúčené: synchronizačné skratky forku (`NHLPORT_READBACK_DRAIN=1 NHLPORT_WRITEDATA_FAULT=1 NHLPORT_GPU_IDLE_US=0
  NHLPORT_PENDING_POLL_US=0` – chyba trvá), prepis GPU dát hosťovskou stránkou (trace `clobber` prázdny), zdroj
  argumentov indirect dispatchov (GPU kópia), kapacity v SRT (CPU konštanty 512/376), GDS počítadlá a ich reset
  (`gds-after`: 375 = 25×15 dlaždíc, triedy 360 + 15, výplne 0 pred každým kompaktovaním, kópie GDS→pamäť na GPU),
  nesúlad obrazov klasifikácie v texture cache (rovnaké id u zapisovateľa aj čitateľa), CMASK/UI.
- Ostáva: lighting dispatche dostanú všetky dlaždice, ale pre dotknuté stĺpce vyprodukujú čiernu → buď zlé vstupy
  osvetlenia pre tieto dlaždice (zoznamy svetiel / hĺbkové hranice z obrazov, ktoré sa pri každej zmene rozlíšenia
  vytvárajú nanovo), alebo chyba prekladu niektorého z týchto compute shaderov (64-vláknové skupiny = 2 subgroupy na
  NVIDII, shared memory + bariéry). Ďalší krok: pri F1 uložiť aj tabuľku svetiel na dlaždicu (`0x111ad13d00`, 8160 B,
  16 B/dlaždicu, mriežka 30×17 max) a hĺbkové hranice (`0x111ad3afc0`, 32 KB) z chybnej snímky (`captures\dumpbuffer`
  s riadkami `hexaddr size` funguje aj ručne) a porovnať stĺpce; SPIR-V shaderov je v `build\user\cache\CUSA37933\
  0x<hash>_0.spv` (`spirv-dis` z Vulkan SDK), GCN + SRT program v `build\user\shader\dumps` po recompile s
  `dump_shaders` (cache súbor treba zmazať).

## Zníženie nárokov na CPU (build 18:05, v0.8.0)

Profil vlákien (`tools\profile_gpu_thread.ps1`, 1200 vzoriek po 2 ms, výber tímov) a CPU po vláknach z diag logu
ukázali, že emulátor pálil dve celé jadrá bez užitočnej práce:

- **GPU vlákno (`shadPS4:GpuCommandProcessor`)** bolo na 94–100 % aj v menu: 63 % vzoriek v slučke čakania
  (`ProcessGraphics` 23 % – yield vo `WaitRegMem`, `Process` 11 % – round-robin front + zámky,
  `ProcessCompute<0>` 11 %, `SpinDiag::Tick` 4,5 %, `WaitRegMem::Test` 4 %), skutočná práca (texture cache,
  pipeline) ~3 %. Oprava (`liverpool.cpp`): počítadlo spracovaných paketov; keď kolo cez všetky fronty nič
  nespracuje (všetky úlohy čakajú na label alebo GPU), vlákno najprv pár kôl čaká `_mm_pause`, potom
  `submit_cv.wait_for` s timeoutom `NHLPORT_GPU_IDLE_US` (predvolene 1000 µs; nový submit/príkaz ho zobudí
  hneď, zápis labelu hosťovského vlákna sa zachytí po timeoute; `0` = pôvodné točenie).
- **Vlákno `GpuSchedPriorityPendingOpsRunner`** (kópie readbackov do pamäte hry, screenshoty) bolo v hre na
  92–96 % jadra: `vkWaitSemaphores` ovládača NVIDIA čaká aktívne. Oprava (`vk_scheduler.cpp`): tick GPU sa
  testuje cez `IsFree`/`Refresh` a medzi testami sa spí `NHLPORT_PENDING_POLL_US` (predvolene 500 µs; `0` =
  pôvodné čakanie).
- **SDL časovače**: štyri 4 ms časovače (jeden na slot ovládača, 1000 prebudení/s) nahradil jeden, ktorý
  prejde všetky štyri sloty (`sdl_window.cpp`); SDL app name je „NHL24 PC“.
- Nálezy bez zmeny: `SDL_joystick` vlákno SDL bolo pri pripojenom ovládači na 71 % (polling HID v SDL –
  prípadne hint `SDL_JOYSTICK_THREAD=0`), `ImGuiTextureManager` 6–13 % v hre napriek blokovaniu v ntdll
  (pravdepodobne chyba priradenia v diag CSV), hosťovské Job0–5 vlákna 45–64 % sú kód hry.

| | pred (build 16:21) | po (build 18:05) |
|---|---|---|
| menu / výber tímov: GPU vlákno | 94–100 % | 9–15 % |
| menu: CPU procesu spolu | 185–199 % | 93–135 % |
| menu: fps / 99 % | 30,0 / 34,2–34,6 ms | 30,0 / 34,2–34,6 ms |
| zápas: GPU vlákno / PendingOps | 90–99 % / 92–96 % | 89–93 % / vlákno sa v zozname neobjavuje (< 2 %) |
| zápas: CPU procesu spolu | 531–578 % | 349–414 % |
| zápas (hra, ~850–1050 draws): fps, avg, 99 % | 39,5–45,8 fps, 21,8–25,3 ms, 34,6–35,1 ms | 51,4 fps, 19,5 ms, 34,5 ms (jedno 10 s okno; predchádzajúce okná boli nahrávanie a prestrihy s kompiláciou pipeline) |

Merané na Ryzen 5 4500 (6 jadier), RTX 5060; logy `logs\nhl24-20261012-162102.*` (pred) a diag log behu 18:05
(`logs\cpu_before_*.txt` = výpis „pred“). GPU vlákno v zápase ostáva vyťažené (skutočná práca + čakanie na
submity hry v 1 ms krokoch); ďalší krok by bol profil GPU vlákna priamo v zápase.

## Finálny build v0.8.0 (2026-10-12 večer)

- **Verzia** `0.8.0` v `src/common/port_version.h` (aj číselne `NHLPORT_VERSION_MAJOR/MINOR/PATCH`);
  CMake ju číta a vkladá do resource súboru `src/dist/shadps4.rc` – exe má popis **NHL24 PC**
  (to zobrazuje Správca úloh namiesto „shadPS4“), produkt NHL24 PC, verziu súboru 0.8.0.0, názov
  `nhl24.exe` a ikonu `src/dist/nhl24.ico` (rovnaká ako launcher; `launcher\launcher.ico` z `ico.png`).
  Ikona okna hry je vložený `src/resources/nhl24.png` (vždy, nie `icon0.png` hry). Launcher berie
  verziu z version resource `nhl24.exe` (fallback 0.8.0).
- **Diagnostika je vypnutá**: `nhl24_diag.log`, `nhl24_frames.csv`, `nhl24_threads.csv`, triggery
  `captures\screenshot|frametrace|dumpimage|dumpbuffer|diagmark|trigger` (RenderDoc), značka F1, perf
  štatistika flipov v logu a draw log sa aktivujú len s premennou prostredia **`NHLPORT_DIAG=1`**
  (`tools\run_nhl24.ps1` ju nastavuje sám, takže meranie cez `measure_nhl24.ps1` funguje ďalej).
  `dump_shaders` v `user\config.json` je `false`. Ladiace premenné `NHLPORT_CLEAR_DEBUG`,
  `NHLPORT_SAFE_AREA` a prepínače správania (`NHLPORT_GPU_CLOCK`, …) ostávajú ako opt-in.
- **Vyčistené**: `nhlport\build_prof`, `nhlport\build_run59`, 15× `build\nhl24_run*.exe`,
  `build\user\shader\dumps`, `build\user\captures`, `build\user\screenshots`, `build\user\log`
  (spolu ~4,5 GB). V `nhlport\build` ostáva `nhl24.exe`, `nhl24.pdb` (symboly pre pády, nemusí sa
  distribuovať), `launcher.exe`, obrázky launchera a `user\` (config, ovládanie, uložené pozície,
  `cache\` s pipeline cache – zrýchľuje štart, dá sa zmazať).
- Build: `tools\build_nhlport.ps1 -DebugInfo` (Release + PDB), `launcher\build_launcher.ps1`.

## Balík „NHL24 PC“ (stav k 2026-10-08 popoludní)

- `nhlport\build\nhl24.exe` – spustí hru bez parametrov: hľadá `game\eboot.bin` vedľa seba,
  `..\GAME_FILE\uroot\eboot.bin`, `..\..\GAME_FILE\uroot\eboot.bin` alebo cestu v `user\game_path.txt`;
  záplatu `user\patches\nhl24.xml` použije automaticky. Názov okna „NHL24 PC v0.2.0“
  (`src/common/port_version.h`). Predvolené sú najlepšie známe nastavenia (fixný GPU časovač,
  readbacky cez transfer frontu, streamované textúry, mip memo); premenné prostredia
  `NHLPORT_GPU_CLOCK=real|stall|flat`, `NHLPORT_READBACK_DRAIN=1`, `NHLPORT_STREAMED=0`,
  `NHLPORT_MIP_MEMO=0`, `NHLPORT_GPU_INVAL_SMALL=1`, `NHLPORT_META_ERASE=1`, `NHLPORT_WRITEDATA_FAULT=1`
  vracajú pôvodné správanie pre porovnanie.
- `nhlport\build\launcher.exe` (zdroj `launcher\Launcher.cs`, build `launcher\build_launcher.ps1`
  systémovým `csc.exe`; obrázky `background.png`, `btn.png`, `ps4_pad.png` z koreňa projektu sa kopírujú
  vedľa exe). **Nový vzhľad 2026-10-12 večer** (okno 1180x885 s pozadím arény, podľa návrhu):
  prepínač *Jednoduchý / Rozšírený* vpravo hore, pod logom verzia portu a verzia hry z `param.sfo`.
  *Jednoduchý*: panel „Kontrola pripravenosti“ (súbory hry + verzia (+ `-UPDATE`), uložené pozície,
  grafická karta a VRAM cez WMI/registry, ovládanie), tlačidlo „Otvoriť priečinok“, panel „Rýchle
  nastavenia“ (priečinok hry + Prehľadávať, „Inštalovať z PKG…“ s informáciou, aký PKG je potrebný,
  výstupné rozlíšenie, režim zobrazenia, upscaler (vypnutý / FSR / FSR + RCAS) a zaostrenie, VSync,
  jazyk launchera SK/EN – prvé spustenie je po anglicky, kým sa neuloží iný jazyk), dolná lišta so
  súhrnom nastavení, „Uložiť“ a tlačidlom PLAY (`btn.png`). Ikona exe: `launcher\launcher.ico`
  (vygenerovaná z `ico.png`, 256–16 px; build ju vloží cez `/win32icon`).
  *Rozšírený*: pôvodné karty Ovládanie (klávesy nad DualShockom, gamepad 1:1) a Zobrazenie; obe
  strany sú zrkadlené. `launcher.exe --advanced` štartuje v Rozšírenom. Zapisuje `user\config.json`
  (GPU + `Launcher.language`), `user\input_config\default.ini` a `user\game_path.txt` (len ak je
  priečinok hry iný než predvolené `game\` / `..\GAME_FILE\uroot`).
- **Inštalácia z PKG** (`nhl24.exe --extract-pkg <súbor> [--extract-to <priečinok hry>] [--pkg-info]`,
  kód `src/core/file_format/pkg.{h,cpp}`, `pkg_type.cpp`, `pkg_extract.{h,cpp}`,
  `src/core/crypto/{crypto.cpp,keys.h}` – prenesené z odstráneného kódu shadPS4 / shadPS4Plus,
  kryptografia prepísaná na libressl: RSA PKCS#1 v1.5 (dk3, ekpfs), SHA-256, AES-128-CBC,
  HMAC-SHA256, AES-128-XTS po 0x1000 B sektoroch, PFSC + zlib). Výstup na stdout po riadkoch:
  `pkg: content_id=… title_id=… category=… kind=base|patch|dlc app_ver=… version=… flags="…" size=… title="…"`,
  `target: <priečinok>`, `files: <n> bytes: <b>`, `progress <hotové súbory> <súbory> <hotové B> <B> <názov>`,
  `done` alebo `error: <dôvod>`. Základná hra (kategória `gd`) ide do priečinka hry, aktualizácia
  (`gp`) do `<priečinok hry>-UPDATE` (overlay, ktorý emulátor montuje nad /app0); DLC sa odmietne.
  Launcher najprv spustí `--pkg-info` (len hlavička + `param.sfo`, nič sa nezapisuje), overí title ID
  CUSA37933 a druh balíka, zobrazí potvrdenie (druh, názov, verzia, veľkosť, cieľ) a až potom
  extrakciu s priebehom (MB a súbory, tlačidlo Zrušiť = ukončenie procesu). Potrebný je PKG hry
  NHL 24, title ID **CUSA37933** (základná hra `UP0006-CUSA37933_00-NHLPS4HOCKEY2024` v01.00 –
  taká je aj dump v `GAME_FILE\uroot`; aktualizačný PKG je voliteľný). Extrakcia bola otestovaná len
  na chybovej ceste (ne-PKG súbor) – na tomto PC nie je žiadny `.pkg`; prvý skutočný PKG treba
  prejsť launcherom a skontrolovať `game\eboot.bin` + `game\sce_sys\param.sfo`.
- Presné snímky obrazu hry: súbor `user\captures\screenshot` s číslom N uloží nasledujúcich N
  snímok 1920x1080 do `user\screenshots` (bez oneskorenia displeja).
- Otvorené: čierny pás 54 riadkov (5 %) na spodku obrazu počas hry (hra ho kreslí sama – v pauze
  a prestrihoch nie je; „safe area“ hlásená hre je 1.0, overené v kóde hry pri
  `sceSystemServiceGetDisplaySafeAreaInfo`); video textúra (Y 1920x1080 + UV 960x540 R8) sa nahráva
  počas zápisu dekodérom → čierne bloky v prehrávači; občasné blikanie v prestrihoch; biele tyče divákov.

## Stav k 2026-10-12 (zostavenie 13:25, počítanie referencií metadát)

- **Build vyzerá dobre** (overené screenshotmi a diag logom, viď „Diagnostický log“): intro, titulná
  obrazovka, hlavné menu, výber tímov, nahrávanie zápasu, predzápasové prestrihy (prelet arénou, wipe
  EA SPORTS, nástup na buly), prestrihy v hre (oslava gólu, opakovania, buly) aj hra – 0 blikajúcich
  snímok, 0 udalostí `glitch`.
- **Čierny pás dole a deformácie na okrajoch obrazu – vyriešené (kalibrácia obrazovky v hre, viď rozbor
  nižšie).** Pôvodné meranie na
  `logs\shots-20261012-135652-cut2` (`tools\shot_crop.ps1` = orez a zväčšenie okraja, výrezy v
  `…\crops`): v hre je pod ukazovateľom skóre čierny pás riadkov 1026–1079 (54 riadkov = 5 %); v
  prestrihoch a opakovaniach, kde je HUD skrytý, je čierny pás preč, ale spodných 5 riadkov (1075–1079)
  a pravých ~15 stĺpcov (1905–1919) ukazuje cudzí obsah (farby ukazovateľa skóre / zvislý zlatý prúžok),
  t. j. niečo sa skladá s posunom alebo mierkou. Stopa: scéna sa v hre kreslí v 2048x1024 (udalosti
  `resolution` striedajú 1920x1080 ↔ 2048x1024) a 1080 − 1024 = 56 ≈ 54 riadkov pásu – podozrenie na
  zlé mapovanie scény s inou internou výškou do 1080p výstupu (viewport / scissor / veľkosť obrazu pri
  aliasovaní 1920x1080 a 2048x1024 cieľov na jednej adrese), nie na hru samotnú.

### Čierny pás – rozbor 2026-10-12 popoludní (VYRIEŠENÉ: kalibrácia obrazovky v nastaveniach hry)

**Záver (16:40, overil používateľ):** pás aj „prúžky“ spôsobovalo nastavenie **kalibrácie obrazovky v
samotnej hre** (čerstvý profil v emulátore má okraj 95 %); po nastavení v menu hry je obraz v poriadku.
Emulátor obraz skladá správne. Pôvodný rozbor: **pás nie je chyba skladania obrazu v emulátore – čierny
prvok pod lištou skóre kreslí samotná hra.** Všetky dole uvedené logy sú v `logs\` (behy `band2` = pred, `band3` = s alfou 0,
`band5` = červený clear, `safe95b` = ratio safe area 0,95); nástroje v `tools\`.

- **Stopa 2048x1024 je falošná.** V draw logu zo zápasu (`nhl24-20261012-142610.drawlog_band2.txt`,
  60 snímok, `tools\band_passes.py`) je 2048x1024 len D16 tieňová mapa s 2 vrstvami (kaskády) a BC
  textúry; udalosť `resolution` sleduje najväčší depth target vrátane tieňov (2112x2112 = ďalšia tieňová
  mapa). Scéna je D32S8 1920x1080 s dynamickým rozlíšením (1600x904 … 1920x1080).
- **Ako sa skladá obraz.** Compute 0x1e09202e číta CMASK UI cieľa (R8G8B8A8 1920x1080 @ 0x111c3f0000,
  CMASK 0x111cbe8000) ako bajtový buffer a z nibble každej 8x8 dlaždice vyrobí 1 bit na 4x4 blok
  („zapísané od fast clearu“); composite 0x2c569595 (30x1080 skupín po 64) potom počíta
  `out = scéna·(1 − ui.a) + ui.rgb` len v blokoch s bitom 1, inak `out = scéna` (IR v
  `nhlport\build\user\shader\dumps\cs_0x000000002c569595.irprogram.txt`). Emulátor podsúva CMASK ako
  „všetko zapísané“ (`BufferCache::SynchronizeMemoryFromImage`), takže composite mieša UI vrstvu všade –
  to je neškodné, lebo nekreslené pixely UI cieľa majú alfu 0.
- **Čo je v UI cieli (dumpy `captures\dumpimage`, `tools\ui_dump_rows.py`).** V zápase: riadky 0–1025
  priehľadné (0,0,0,0) alebo prvky HUD, lišta skóre v riadkoch ~996–1025, riadky 1026–1079 presne
  (0,0,0,255). Beh s `NHLPORT_CLEAR_DEBUG=1` (fast clear takéhoto cieľa = (1,0,0,0)) ukázal, že plátno
  (riadky < 1026) ostáva v hodnote clearu (255,0,0,0) – Rime nekreslí žiadny „čistiaci“ quad – a riadky
  1026–1079 majú (0,0,0,255) / (17,0,0,255): **takmer nepriehľadný čierny prvok cez celú šírku nakreslí
  hra** (pri pravom okraji x≈1905–1919 sú pod ním ďalšie prvky). Registrová hodnota fast clearu UI cieľa
  je (0,0,0,0), nie (0,0,0,1), ako tvrdí odsek o builde 45 nižšie; dump `img_0x111c3f0000_…_f5220.raw`
  z 8. 10. má rovnaké riadky 1026+, takže „oprava alfa 0“ z buildu 45 nikdy reálne nič nezmenila
  (a v strome už ani nebola – ostal len nepoužitý príznak `shader_read`).
- **Geometria.** 30 UI draws na snímku sú `drawi` (indexované; staršie skripty hľadali len `draw`), všetky
  s viewportom aj scissorom 1920x1080; vertex shader 0x7d0cbcc8 počíta `NDC.y = c13·y + c15` s
  `c15 = −0,8519`, t. j. lokálny počiatok lišty je presne na riadku 1000, spodok lišty 1026 = 1080·0,95.
  Na PS4 je podľa používateľa spodok HUD-u na okraji obrazovky – hra teda v emulátore ukladá rozloženie
  o 5 % výšky vyššie (zvislo; vodorovne je lišta celých 1920 px).
- **Safe area vylúčená.** Hra volá `sceSystemServiceGetDisplaySafeAreaInfo` raz (Job1) a `ratio` zapisuje do
  RimeUISystem+0x90/+0x94 (pri chybe návratu použije 0,9; vedľa je `SplitScreenMarginPercent` 1,0).
  Emulátor vracia 1,0; s `NHLPORT_SAFE_AREA=0.95` (nová premenná v `systemservice.cpp`) je trojica
  (0,95; 0,95; 1,0) v pamäti hry (`tools\memscan.ps1 -Ptr 0x3f8000003f733333`), ale pás ostal rovnaký
  (riadky 1026–1079). V pamäti sú aj tabuľky (0,8 | 0,9 | 0,95 | 1,0) → vyzerá to na vlastné nastavenie
  hry (v eboot-e sú typy `ProfileOptionDataInt/Float/Enum` a Rime vlastnosť `LayoutWithSafeZone`;
  `HudScale` patrí k ladiacemu PerfHudSettings) s predvolenou hodnotou 0,95 v čerstvom profile emulátora; profil `savedata\CUSA37933\Profile0\DATA` konštantu 0,95 neobsahuje (môže byť
  index/percento). **Ďalší krok:** v menu hry (Settings → Visual / Screen) nájsť nastavenie veľkosti
  obrazovky alebo HUD-u a nastaviť 100 %, resp. porovnať so screenshotom z PS4; prípadne vystopovať
  rozloženie cez `LayoutWithSafeZone`.
- **Prestrihy a okraje.** Dump UI cieľa z prestrihu (`captures\img_0x111c3f0000_…_f6811.raw`, beh 15:20)
  je celoplošný stmavovací prekryv (3,4,5, α 0,84, pri pravom okraji 0,97) – stmievanie strihu kreslené
  hrou; „cudzí obsah“ v spodných riadkoch a pravých stĺpcoch sú prvky HUD-u pod lištou (v dumpe band5 majú
  riadky 1040–1079 pri x=1912 hodnoty (40,40,40) a (201,200,199)). Nie je to posun ani mierka v emulátore.
- **Čo sa menilo v strome (necommitnuté):** `texture_cache.h` `MarkMetaShaderRead`/`IsMetaShaderRead`
  (diag log ukazuje „read by shaders true“), `buffer_cache.cpp` (značenie + LOG_INFO),
  `vk_rasterizer.{h,cpp}` `Rasterizer::ColorClearValue` (hodnota z registrov; `NHLPORT_CLEAR_DEBUG=1`
  = červený clear pre ciele čítané shaderom; draw log `meta-clear-shader-read f<n> @ <cmask> target
  <addr> clear <word0>`), `perf_counters.h` + `driver.cpp` + `vk_rasterizer.cpp` (`captures\dumpimage`
  riadok `addr:N` počíta draw-y až od nasledujúcej GPU snímky), `systemservice.cpp` (`NHLPORT_SAFE_AREA`).
  Skripty: `tools\measure_nhl24.ps1` kotvy `T<s>` (prvé `-> 2112x2112`) / `G<s>` (prvé `-> 2048x1024`),
  položky `shot`, `trace`, `dump=<hex>[:N]`, `-TraceAt G22`, `-CloseAt G50`, kopíruje všetky nové
  `drawlog_*.txt` a `img_*.raw`; `tools\band_passes.py`, `tools\ui_dump_rows.py`, `tools\band_report.ps1`
  (výrezy každej série, sonda riadkov 1050/1077 a stĺpca 1912, blikanie, dumpy, diag/draw log).
- **Merania:** pred (band2, 65 snímok) pás v 13/13 sondách, 0 blikajúcich; s alfou 0 (band3, 115 snímok)
  pás v 23/23 sondách, 0 blikajúcich, 0 `glitch`; ratio 0,95 (safe95b, 20 snímok) pás 1026–1079; finálny
  strom (final, 53 snímok, `logs\shots-20261012-162102-final`): séria bez HUD (nástup na buly) bez pásu
  a bez cudzieho obsahu v spodných riadkoch / pravom okraji (rozdiely 0,8), séria v hre s pásom 1026–1079,
  0 blikajúcich, 0 `glitch`. Zápas sa
  v behoch po 15:00 spúšťa sám ~67–77 s po prvej snímke (trojuholník už netreba; neskorý trojuholník
  otvorí pauzu „SELECT SIDES“).

## Stav k 2026-10-08

- **Blikanie vo výbere tímov vyriešené** (overené: 0 z 200 screenshotov, predtým 16–20 z 200).
  Reťazec príčin: fence zápisy `RELEASE_MEM` na compute fronte (a `WRITE_DATA`) sa zapisovali
  obyčajným zápisom do stránok sledovaných cache → page fault → readback s úplným vyprázdnením
  GPU fronty v náhodnom bode snímky → hra merala kolísavý čas GPU → dynamické rozlíšenie
  preskakovalo každé ~2 snímky → cache textúr prestavovala desiatky obrazov za snímku → občasné
  poškodenie UI cieľa uprostred passu (ľavý panel nahradený rozmazanou arénou). Oprava: fence
  zápisy idú cez `Rasterizer::InlineData` (backing view + oprava GPU kópie), `PM4CmdReleaseMem::SignalFence`
  dostal `write_mem` callback. Po oprave: zmeny rozlíšenia 1/600 snímok (predtým ~150/600).
- **Ďalšie zmeny v cache textúr**: CMASK/HTILE metadáta majú počítanie referencií (`LinkMeta` /
  `UnlinkMeta`; upstream mazal záznam pri zrušení ľubovoľného obrazu s rovnakou adresou a nový
  záznam znamenal falošný clear); pri zlúčení naviazaného subresource do väčšieho obrazu sa jeho
  obsah kopíruje; log „alias pick“ a „incompatible view“.
- **Časovač GPU** (`NHLPORT_GPU_CLOCK=stall`, zapnuté v `tools/run_nhl24.ps1` cez env): časové
  značky `GpuClock64` nezahŕňajú čakania emulátora (readbacky, kompilácia pipeline), aby
  dynamické rozlíšenie hry nereagovalo na jitter emulátora. Experiment, v zápase „o kúsok lepšie“.
- **Readbacky bez vyprázdnenia fronty** (build 33): kópia GPU→CPU beží na druhej fronte rodiny
  graphics a čaká len na submisiu, ktorá dáta zapísala (`gpu_write_ticks`); `NHLPORT_READBACK_DRAIN=1`
  vráti pôvodné `Finish()`. Zostávajúce readbacky (0,6–3/snímku) sú 8-bajtové CPU fence zápisy
  vlákien Job do stránok s malými GPU výstupmi (DMA výplne 4–32 B, bloky 0x804 B).
- **Zamrznutie hry v zápase (beh 39) – príčina a oprava (build 40)**: GPU vlákno pripravuje
  snímku pri flip NOP-e (`SubmitFlip → PrepareFrame → GetRenderFrame`) a čaká na fence
  `present_done` snímky vybratej z voľného radu. Prezentačné vlákno pritom tú istú snímku znova
  prezentovalo (`DrawLastFrame`, kým `ImGui::Core::MustKeepDrawing()` – napr. toast „Saved
  screenshot“), resetovalo jej fence pod čakajúcim vláknom a GPU vlákno sa už nezobudilo
  (hra potom čaká na EOP: `GfxEopQueue waited 120s`). Upstream je bezpečný len preto, že u neho
  príprava aj prezentácia bežia v jednom vlákne. Oprava: naposledy prezentovaná snímka ostáva
  rezervovaná a do voľného radu ide až predchádzajúca (`Presenter::Present`). Diagnostika
  zaseknutí: `lldb --batch -p <pid> -o "thread backtrace all" -o detach` (lldb potrebuje
  `python3.dll` z Python312 na PATH), `tools/pe_func_strings.py` (mená funkcií bez PDB cez
  `.pdata` + reťazce `__func__`), `tools/thread_names.ps1`; od buildu 40 má Release build PDB
  (`tools/build_nhlport.ps1 -DebugInfo`, `tools/symbolize_frames.py`). Strážcovia čakaní logujú
  každé čakanie nad 2–5 s (`timeline wait`, `GPU command queue`, `GetRenderFrame: waited`,
  `readback waited`).
- **Čierny pás (54 riadkov) – príčina a oprava (build 45)**: finálny compute shader
  (hash 0x2c569595, pokrýva celých 1080 riadkov) mieša UI vrstvu (R8G8B8A8 1920×1080) do
  tonemapovanej scény len tam, kde bitová maska 4×4 blokov hovorí „UI prítomné“; tú masku si hra
  počíta ďalším compute shaderom (hash 0x1e09202e) **z CMASK metadát UI cieľa**. Hra UI cieľ
  fast-clearuje na nepriehľadnú čiernu (0,0,0,1) a Rime kreslí len do svojho plátna (riadky
  0–1025 v zápase, ~1078 v prestrihoch), takže na konzole ostanú nekreslené bloky v CMASK
  „cleared“ a composite ich preskočí. Emulátor podsúva shaderom CMASK ako „všetko nakreslené“
  (`BufferCache::SynchronizeMemoryFromImage`), composite preto primieša nepriehľadnú čiernu
  = pás. Potvrdené dumpom obrazov (UI cieľ má v riadkoch 1026+ presne (0,0,0,255), scéna má
  obsah po riadok 1079). Oprava: metadáta čítané shaderom dostanú `shader_read`
  (`MarkMetaShaderRead`) a fast clear takého cieľa (`EliminateFastClear` aj loadOp clear
  v `BeginRendering`) použije alfu 0, takže nekreslené bloky sa composite-u javia ako prázdne.
  Nástroje: `captures/dumpimage` (hex adresy po riadkoch) → raw dump naviazaných obrazov pri
  najbližšom draw/dispatch (`captures/img_<addr>_<w>x<h>_<fmt>_f<n>.raw`), `tools/raw2png.py`,
  `tools/band_rows.py`, `tools/composite_consts.py`; shader dumpy cez `dump_shaders` +
  `spirv-dis` (Vulkan SDK). RenderDoc: API trigger nefunguje, F12 (SendKeys) zachytí 1–2 GB,
  ale replay potrebuje 12–14 GB RAM – na tomto stroji nepoužiteľné.
- **Rozbité texty v menu (build 44 → oprava v builde 46)**: text sa vzorkuje zo stránok glyfov
  256×256 R8, ktoré hra plní cez WRITE_DATA; od buildu 25 idú tieto zápisy cez backing view
  (bez page-faultu) a informovali len buffer cache, takže cache textúr držala staré stránky.
  Skôr to maskovalo každosnímkové nahrávanie „streamovaných“ obrazov; prísnejšie pravidlo
  povyšovania (build 41: fault v ≥ 8 rôznych snímkach, nie BC, ≤ 8 MB) to odkrylo. Oprava:
  `Rasterizer::InlineData` po zápise cez backing volá aj `texture_cache.InvalidateMemory`.
  Pravidlo: každý zápis emulátora, ktorý obchádza ochranu stránok, musí informovať obe cache.
- **Streamované textúry a snapshot (buildy 40–43)**: snapshot pamäte streamovaných obrazov pri
  `SubmitGfx` (proti trhaniu videa) spôsobil v builde 40 1 fps – hra robí ~8 submitov na snímku
  a „streamovaných“ bolo 288 obrazov (85+ MB) vrátane bežných BC textúr. Teraz: kópia len pre
  obrazy nahrané v posledných 2 snímkach, ≤ 4 MB, rozpočet 24 MB, najviac raz medzi dvoma
  nahraniami; riadok `snapshots in this window` v logu každých 5 s.
- **Diagnostika** (všetko vypnuté, kým sa nezapne súborom): `nhlport/build/user/captures/frametrace`
  s číslom N → 600 riadkov `frame trace` (počítadlá na snímku) + draw-log N snímok v pamäti,
  zapísaný do `captures/drawlog_<n>.txt` (riadky `dl`, `tc+`/`tc-`, `meta-*`, `refresh`,
  `cpu-inval`/`gpu-inval`). Nástroje: `tools/burst_shots.ps1`, `tools/flicker_capture.sh`,
  `tools/frametrace_analyze.py`, `tools/drawlog_analyze.py`, `tools/drawlog_compare.py`,
  `tools/drawlog_refresh.py`. Build objektov počas behu hry: `tools/build_nhlport.ps1 -Targets a.obj,b.obj`;
  bežiaci exe sa dá premenovať (`mv shadps4.exe shadps4_runN.exe`) a nový build sa zlinkuje bez
  zavretia hry.
- **Otvorené**: prestrihové scény v zápase občas preblikávajú (nezachytené detektorom – jediný
  nájdený „preblik“ v úvode je úmyselný glitch prechod loga EA SPORTS); diváci držia netextúrované
  biele svietiace tyče; „Device lost“ pády; výkon v zápase 38–48 fps (GPU vlákno 77 %, GPU 15–19 %).

## Stav k 2026-10-07 noc

- **Hrateľné v skate režime**: ľad, aréna aj hráč sa kreslia, hra reaguje na vstup (beh 11 so živou
  záplatou, screenshot `logs/screen_run11_patched_192835.png`). Potrebné boli tri veci:
  1. `0007`: obyčajný `prepareFlip` vyvolá okrem interného GfxFlip aj prerušenie **GfxEop** – Frostbite
     zapisuje všetky GPU fence-y bez prerušenia a vlákno `GfxEopEventLoop` sa budí na EOP prerušení,
     ktoré na reálnej PS4 generuje samotný flip. V shadPS4 toto vlákno čakalo donekonečna.
  2. `0007`: `sceGnmSubmitAndFlipCommandBuffers` čaká, kým prezentácia nie je viac než 2 flipy za CPU
     (emulácia latencie flipu; RenderLoop hry toleruje najviac 3 rozpracované flipy).
  3. **Herná záplata** `game_patches/nhl24.xml` (shadPS4 MemoryPatcher, `-p nhl24.xml` v `tools/run_nhl24.ps1`):
     ring „rplyCompress“ (kompresia replay blokov po 128 KB) plánuje joby, ktoré emulovaný job systém
     nikdy nedokončí; po 64 blokoch sa ring otočí, Job3 sa točí vo `WaitJob`, `GameSimulationLoop` stojí
     a kreslí sa len HUD nad čiernym svetom. Záplata prepne `je`→`jmp` na eboot+0x64acb70, čím hra
     komprimuje synchronne vlastnou cestou. Prečo job systém tieto joby nedokončí (ostatné joby bežia),
     je otvorené – pozri `patches/README.txt`.
- Diagnostika v emulátore (ostáva zapnutá): točiace sa GPU čakania > 2 s, nekonečné čakania na frontách
  udalostí (každých 5 s), debug riadky o flipoch (`Log.filter = "Lib.VideoOut:Debug"` v config.json).
- Nástroje na analýzu živého procesu (PowerShell + capstone): `tools/threads.ps1` (všetky vlákna
  s menami HLE funkcií cez GOT eboot-u z `analysis/eboot.bin/jmprel.txt`), `tools/pm4scan.ps1` (sken
  pamäte hry na PM4 pakety a značky flipov), `tools/findeq.ps1` (fronty udalostí v pamäti emulátora),
  `tools/disasm2.py` (disassembler s rozlíšením PLT thunkov na mená importov).

## Stav k 2026-10-07 večer

- Fork shadPS4 (`nhlport/`, vetva `nhl24`, báza upstream `144591e5`) sa na tomto PC zostaví
  (`tools/build_nhlport.ps1`: clang-cl 23 + Ninja + VS 2022 Build Tools; oprava `HAVE_ENDIAN_H=0`
  pre libressl s LLVM 23) a **NHL 24 bootuje a kreslí prvé 3D snímky** (ľadová plocha, svetelný
  stĺp úvodnej scény), načíta runtime PRX moduly (`Engine.Render.Core2.PlatformPs4.prx`,
  `Engine.BuildInfo.prx`, `libSceNpToolkit2.prx`), beží s ~3,4 GB RAM. Snímky: `logs/screen_*.png`.
- Opravy vo forku (commity `e5c4ed6b`, `9df799cf`, patche v `patches/`): 16-bitové čítanie
  EXEC/VCC_HI/M0/SCC v rekompilátore, LibcInternal → `libc.prx` fallback, preskočenie dialógu
  „Save Migration“, `CopyImageWithBuffer` bez assertu pri rozdielnom počte vrstiev/mipov (prvý GPU pád).
- Ďalšie opravy (commity `3df17227`, `15a4f1a8`): HLE `libSceJson2` (sce::Json nad nlohmann/json),
  Audio3d bed prijíma 1–16 kanálov, CMASK metadáta čítané compute shadermi sa prezentujú ako
  „expanded“. Výsledok: **hra sa dostane do menu s korektným obrazom**, Audio3d a CMask chyby
  zmizli. Potom padá na `Device lost during submit` (GPU chyba v jednom z príkazov); diagnostika
  beží s `vkcrash_diagnostic_enabled` + markermi v `nhlport/build/user/config.json`.
- Zostávajúce drobnosti: `Parser::parse` dostáva prázdny buffer (chýbajúci konfiguračný súbor
  v `/download0`/`/temp0`), stuby `sceKernelTruncate/Chmod/Utimes/GetPageTableStats/StopUnloadModule`.

Cieľ: spustiteľný `.exe`, ktorý natívne vykoná PS4 kód NHL 24 na Windows
(x86-64 priamo na CPU, PS4 systémové knižnice nahradené, grafika cez Vulkan), podľa vzoru
[bbport](bbport_analysis.md) (Bloodborne na Linuxe).

## Čo vieme o dumpe (`GAME_FILE/uroot`)

| | |
|---|---|
| Titul | UP0006-CUSA37933_00-NHLPS4HOCKEY2024, verzia dumpu 1.60 (podľa issues), SDK/FW **10.50** (`sdk_version 0x10508001`) |
| eboot.bin | SELF 187 MB, 10 segmentov (5 plaintext „blocked“), ELF `ET_SCE_DYNEXEC`, 12 PH, entry 0x80, obraz 198 MB, modul `NHLGame.Main_Ps4_retail` |
| Relokácie | 452 814 RELATIVE, 5 767 R_X86_64_64, 827 JUMP_SLOT (žiadne TLS typy v eboote) |
| TLS | 8 B init / 1456 B |
| Importy eboot | 849 (57 DT_NEEDED modulov) – vrátane `sceKernelLoadStartModule` / `sceKernelDlsym` |
| Vlastné PRX hry | `Engine.Render.Core2.PlatformPs4.prx` (Frostbite render backend: 113 GnmDriver + 98 kernel + 62 libc importov, 41 exportov), `Engine.BuildInfo.prx` – načítavané **za behu** (nie sú v DT_NEEDED) |
| sce_module | libc, libSceFios2, libSceNpToolkit2 (2727 exportov), libSceFace, libSceHand, libSceS3DConversion – všetky SELF, plaintext |
| HLE plocha | **905 unikátnych NID-ov**, ktoré musí dodať hostiteľ (libkernel 191, GnmDriver 113, LibcInternal 54, Posix 45, Np*/Http/Net ~250, SaveData 15, PlayGo 14, VideoOut 14, Voice 13, Ajm 11, Audio3d 10, AppContent 10 vrátane DLC mountov…) |
| Dáta | 33 GB Frostbite superbundly (Gen4b), DLC služby 1..7 v param.sfo |

Kompletné výstupy: [`analysis/`](analysis/) (`overview.txt`, `hle_surface.txt`, `link_plan.txt`,
`shadps4_coverage.txt`, per-súbor `summary.json` / `imports.txt` / `exports.txt` / `relocs.txt`).
Generuje ich `tools/analyze.ps1` (C# analyzátor kompilovaný .NET 4.8, bez inštalácií).

## Čo vieme o shadPS4 a NHL 24

- V kompatibilitnej databáze má NHL 24 status **nothing** na všetkých OS (issues #2712, #2505,
  #2500; verzie 0.15–0.16, 2026). Príčina v logu: rekompilátor shaderov,
  `translate.cpp GetSrc: unexpected operand 126` (= GCN `EXEC_LO` ako 32-bit zdroj).
  **V aktuálnom main (2026-10-07) je `ExecLo/ExecHi` v `GetSrc` už ošetrené**; 16-bitový variant
  `GetSrc16` ho stále nemá. Čiže hra na shadPS4 prejde celým CPU štartom až po prvé shadery.
- shadPS4 už implementuje **821 z 905** potrebných NID-ov (`analysis/shadps4_coverage.txt`).
  Chýba 84: 36 libSceLibcInternal (volajú ich dodané PRX; riešenie: presmerovať na rovnaké NID-y
  v hernej `libc.prx`, ako robí bbport, alebo firmware `libSceLibcInternal.sprx`), 21 `libSceJson2`
  (sce::Json C++ API – treba napísať), 17 libkernel drobností (atexit počítadlá, sanitizer,
  `sceKernelStopUnloadModule`, `GetPageTableStats`, `Utimes`, `Chmod`, `Truncate`…), 7 NpManager,
  2 VideoRecording, 1 Coredump.
- shadPS4 na Windows: VEH namiesto SIGSEGV, TLS cez `TlsAlloc` + patchovanie `fs:` inštrukcií,
  pamäť cez `VirtualAlloc2`/`MapViewOfFile3` nad jedným file mappingom (obdoba memfd modelu),
  runtime načítavanie PRX (`sceKernelLoadStartModule`), event flagy, AIO, Audio3d (čiastočne),
  AppContent s DLC mountmi. Jadro bez GUI (SDL frontend), build: CMake + Ninja + **clang-cl**,
  C++23, 45 submodulov, CI beží na VS2026 runneri.

## Rozhodnutie o základe

| Variant | Za | Proti |
|---|---|---|
| **A. Fork shadPS4 ako jednohrový Windows exe** (odporúčané) | Hra už bootuje po GPU; 91 % HLE hotové; Windows loader/TLS/pamäť existujú; bbport ukázal, že GPU jadro shadPS4 sa dá špecializovať a zrýchliť | Veľký cudzí kód (C++23, 45 submodulov), GPL-2.0+ |
| B. bbport prístup (offline obraz + vlastný loader) na Linuxe | Najmenší vlastný kód, overená cesta | Linux-only; chýba runtime PRX loader, event flagy, AIO, Audio3d, DLC; všetko by sa písalo nanovo |
| C. Vlastný Windows loader od nuly | Plná kontrola | Mesiace práce pred prvým snímkom; reinvencia shadPS4 Windows vrstvy |

## Míľniky (variant A)

1. **Toolchain** (Windows): VS 2022/2026 Build Tools (MSVC + Windows SDK), LLVM (clang-cl),
   CMake, Ninja, Python 3. Repo `shadPS4` rekurzívne (~2 GB so submodulmi), build ~30–60 min
   na Ryzen 5 4500.
2. **Prvý beh**: zostaviť shadPS4 main, spustiť NHL 24, uložiť log → zistiť ďalšiu prekážku
   po oprave ExecLo (očakávané: `GetSrc16`, Audio3d, LibcInternal stuby, Json2).
3. **Fork `nhlport`**: opraviť blokery (rekompilátor, LibcInternal → libc.prx, Json2, stuby),
   odstrániť nepotrebné (GUI, updater, Discord), zabudovať konfiguráciu (cesta k dumpu,
   nastavenia) → `NHL24.exe`, ktorý sa spúšťa bez emulátorového UI (model bbport launcheru).
4. **Hrateľnosť**: pad, zvuk (Audio3d!), save, offline Np stuby (ako bbport „kábel odpojený“),
   DLC/addcont mounty, patche (framerate, rozlíšenie) cez shadPS4 XML formát.
5. **Výkon** (prevzať z bbportu): dvojstupňová GPU pipeline, memoizácie, FSR 3.1/4 s
   Frostbite velocity bufferom, Windows ekvivalent in-place pamäte.

## Štruktúra repozitára

```
GAME_FILE/uroot/           PS4 dump (nie je súčasťou repozitára)
analysis/                  výstupy analyzátora (eboot, PRX, HLE plocha, pokrytie shadPS4)
tools/Ps4Analyze.cs        analyzátor SELF/ELF/PRX (C# 5, .NET 4.8)
tools/analyze.ps1          build + spustenie analyzátora (jmprel.txt = JUMP_SLOT relokácie s menami)
game_patches/nhl24.xml     herná záplata pre shadPS4 MemoryPatcher (synchronna replay kompresia)
patches/                   git format-patch série forku nhlport (0001-0007)
third_party/shadps4/       aerolib.inl (NID → meno, GPL-2.0+)
bbport_analysis.md         detailná analýza predlohy (bbport)
```

Spustenie analýzy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\analyze.ps1
```
