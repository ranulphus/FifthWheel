# Testing

| What | Command | Passes when |
|---|---|---|
| DOS in 86Box | `make loopa CARD=g450` (or g200, g400) | `HX-DONE 0`: the frames ran, the snapshot was written |
| DOS against Mesa | a DOS `-test -fixed -frames 120 -shot 119:F0` run and `build/headless/fwheel-hl` with the same options, compared with the harness's `imgcmp.py` | within conformance tolerances (RGB565 quantisation, tolerance 24, 0.5%, edges excluded) |
| Sound | `make suite CHECKS="sb16 sbpro"` (`tools/suite.sh`): `-soundtest` (each sound in turn) through an SB16 and an SB Pro 2 (8-bit) with `--wav` | every sound's tones are in the recording (`wavcheck.py`), and `HX-TEST audio`: no chunk was played as silence (SDL patch 0005) |
| Flourishes' caps | `-test -fixed -fxtest` (headless or DOS): every flourish requested far past its pool for 2 s | `HX-TEST fx-caps`: at most 16 coins, 64 confetti and 32 dust, each pool filled |
| Budget | any run: `FW-STAT` at the end | the triangles and draws a frame (mean and most); see `docs/perf.md` |
| Menus | `make suite CHECKS=menus`: keys typed in Loop A from the title set the detail to high, drive, pause and quit from the pause menu; a second run reads `FWHEEL.CFG` | `FW-MENU` lines for each step and `FW-CFG detail high` in the second run |
| Leaving the machine usable | `make suite CHECKS="exit dglexit crash ctrlc ctrlbreak"`: QUIT on the title, DOS-GL's `DGL_EXIT_AFTER` (exit() inside a swap), `FW_CRASH` (a #UD while driving), Ctrl-C, Ctrl-Break; each with the SB16 playing, VECCHK saving the vectors before | afterwards SBCHK finds the SB's DMA stopped, VECCHK the vectors as saved, KEYWAIT reads a key through the BIOS; the crash shows `DGL-FAULT` and a career saved before it loads afterwards as it was |
| Memory | `make suite CHECKS=mem`: 86Box with 32 MB, the tour with sound | `FW-MEM` (written at every exit): the heap within 20 MB (about 7 MB) |
| Screen sizes | `make suite CHECKS=modes`: a frame in each size the game offers (`FW-MODES`: DOS-GL's, with room for a Z buffer and a back buffer), and the headless build's at the same size | each pair within conformance tolerances |
| Cards agree | `make suite CHECKS=auto` on each card: the tour's `FW-HASH` lines over 1,800 ticks | the same on every card run so far |
| The release | `make release`, then `make suite CHECKS=release`: the ZIP unpacked alone in `C:\FW` and run there with the harness's CWSDPMI off the PATH | it starts (its own CWSDPMI), plays with sound and ends |
| GL subset | `make glcheck` (in `make tests-host`): every `gl*` call in the game and the kit against the functions DOS-GL defines | none missing; every Loop A run also fails on a `DGL-STUB` or `DGL-GLERR` line |
| Unit tests | `make tests-host` (the host's compiler; no screen) | the governor's steps, the presets and `BUDGET.CFG`, settings, saves (a damaged one refused, a half-written one recovered), the shop's rules |
| Shop | `make suite CHECKS=shop`: keys typed in Loop A open the garage, buy two items with $600, leave, and quit from the pause menu; a second run loads the career | both bought, and `FW-CAREER loaded` with them fitted and $50 left |
| Joystick | `make suite CHECKS=joy`: in one Loop A run, `-calibrate` with 86Box's virtual joystick moved at each step (anchored on its `FW-CAL` lines), then `-joylog` with the saved file | `HX-TEST calibrate PASS`, `FW-CFG loaded`, and half left, full forward and button 1 read as steering 0.4-0.5, the accelerator and the handbrake |
| Joystick, headless | `DGK_JOY=TICK:axis:N:V,...` scripts a joystick (`FW_CFG=FILE` for the settings): `-calibrate`, then `-joylog` | the same, for a stick and for a wheel with pedals |
| Sound, headless | `DGK_WAV=FILE build/headless/fwheel-hl -test -fixed -nodraw -soundtest`: the mixer's output in step with the virtual clock | the same samples every run; the tones are there |
| Linux | `xvfb-run build/linux/fwheel -test -fixed -frames 120 -shot 119:F0L -nosound` in the dev container | the frame equals the headless one |
| Autopilot | `-test -fixed -autopilot -laps 1 -hash` (DOS in Loop A with `--file build/data/YARD.PAK`, or headless) | a lap with no damage (`FW-LAP`, `HX-TEST laps`) |
| World | `make data-check` | seed 1's pack hash equals `data/golden/world.sha`, built at -O0 and -O2 |
| World pictures | `make shots CARD=g450` (`tools/shots.sh`): `-tourshots 10`, a frame with the HUD (`-frames 61 -shot 60:H0`) and the reversing camera (`-dockpose -frames 121 -shot 120:H1`) on DOS in Loop A and headless, compared with `imgcmp.py` | the ten poses along the tour and at two depots, the HUD frame and the docking frame, within conformance tolerances |
| Jobs | `-test -fixed -autojob` (headless, or DOS in Loop A: `make loopa ARGS="-test -fixed -autojob -hash" LOOPA_TIMEOUT=1800`); `-job D:T:B` picks the job (depots and bay from 0) | `HX-TEST job PASS`: coupled, hauled and docked at grade OK or better (`FW-JOB done grade=...`) |
| Every job | `make jobsweep` (headless, every depot's trailer to every other depot's three bays; `out/jobsweep/results.txt`) | every job passes |
| Replays | record with `-record FILE`, play with `-replay FILE` on the same platform | `HX-TEST replay PASS`: every second's state hash as recorded |

What 86Box cannot show is on the bench checklist (`docs/bench.md`).

86Box's speed says nothing about real hardware: performance numbers come
from real silicon and the bench only.
