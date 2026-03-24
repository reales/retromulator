# Retromulator

A standalone app and DAW plugin (AAX / VST3 / AU) that emulates classic hardware synthesizers, samplers, and keyboards by running their original firmware on cycle-accurate CPU and DSP emulators.

[![Retromulator](https://www.discodsp.com/img/retromulator.webp)](https://www.discodsp.com/retromulator/)

[![Download for Windows](https://img.shields.io/badge/Windows-Download-blue?style=for-the-badge&logo=windows)](https://www.discodsp.com/download/?id=36) &nbsp; [![Download for macOS](https://img.shields.io/badge/macOS-Download-blue?style=for-the-badge&logo=apple)](https://www.discodsp.com/download/?id=35) &nbsp; [![Download for Linux](https://img.shields.io/badge/Linux-Download-blue?style=for-the-badge&logo=linux&logoColor=white)](https://www.discodsp.com/download/?id=34) &nbsp; [![Buy License $29](https://img.shields.io/badge/Buy_License-$29-green?style=for-the-badge&logo=fastly)](https://discodsp.onfastspring.com/retromulator/)

## Hardware Cores

| Hardware | Emulation |
|---|---|
| Access Virus A / B / C | Motorola DSP 56300 cycle-accurate |
| Access Virus TI | Motorola DSP 56300 cycle-accurate |
| Akai S1000 | SFZero v3.0.0 sample engine |
| Clavia Nord Lead / Rack 2x | Motorola DSP 56300 cycle-accurate |
| Roland JP-8000 (JE-8086) | Motorola DSP 56300 cycle-accurate |
| Waldorf microQ | Motorola DSP 56300 cycle-accurate |
| Waldorf Microwave XT | Motorola DSP 56300 cycle-accurate |
| Wurlitzer 200A (OpenWurli) | Physical modeling synthesis |
| Yamaha DX7 | HD6303R + YM21280 EGS + YM21290 OPS (VDX7) |
| Yamaha OPL3 / YMF262 | Nuked OPL3 v1.8 |

Most DSP-based synths require their original ROM firmware to run (not included). ROMs are loaded from the application support folder at runtime. The microQ can run with an embedded fallback ROM. The Akai S1000, OpenWurli, and OPL3 cores are ROM-free.

The **Akai S1000** sampler loads SF2, SFZ, ZBP, and ZBB sample banks, as well as Akai ISO/BIN/CUE disk images, via the [SFZero](https://github.com/reales/retromulator/tree/main/Modules/SFZero) MIT-licensed engine with 8-point sinc interpolation, extended SFZ/SF2 opcode support, auto-slice drum mapping, CC20 global tuning, and discoDSP Bliss sampler format.

The **Wurlitzer 200A (OpenWurli)** is a physical model of the Wurlitzer 200A electric piano with 64-voice polyphony, tremolo, speaker character modeling, velocity curves, and 2x oversampling.

The **Yamaha OPL3** emulates the YMF262 FM synthesis chip (18 channels, 4-operator) using the Nuked OPL3 engine. It loads SBI patch files with bank navigation via folder hierarchy, pitch bend, and voice stealing.

## How it differs from Gearmulator

Retromulator is built on top of the open-source emulation engines from [Gearmulator](https://github.com/dsp56300/gearmulator) by dsp56300. Gearmulator ships as standalone applications and open-source plugins built with CMake. Retromulator packages the same engines into a polished single-plugin experience using JUCE, with a unified rack-style UI, DAW state persistence, bank/patch browsing, focused on preset playing.

The emulation cores (dsp56300, mc68k, h8s, synthLib and all synth-specific libraries) are from Gearmulator. The DX7 emulation is ported from VDX7, a separate project (see Credits below). The OPL3 emulation uses Nuked OPL3 by Nuke.YKT. The Akai S1000 sampler uses the SFZero module, an MIT-licensed JUCE sample engine maintained by discoDSP. The Wurlitzer 200A (OpenWurli) is a physical model fully ported by discoDSP.

## Credits

- **[dsp56300](https://github.com/dsp56300)** — DSP56300 emulator, Virus TI / microQ / XT / Nord N2X / JE-8086 engines, GPL v3
- All contributors to [github.com/dsp56300/gearmulator](https://github.com/dsp56300/gearmulator)
- **chiaccona** — [VDX7](https://github.com/chiaccona/VDX7), cycle-accurate Yamaha DX7 emulation (HD6303R CPU, EGS, OPS), GPL v3
- **Nuke.YKT** — [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3), cycle-accurate YMF262 emulation, LGPL v2.1
- **Steve Folta** — original [SFZero](https://github.com/stevefolta/SFZero) SFZ/SF2 sample player, MIT license
- **Leo Olivers** — SFZero JUCE module port
- **discoDSP** — [SFZero v3.0.0](https://github.com/reales/retromulator/tree/main/Modules/SFZero), 8-point sinc interpolation, Bliss format, extended opcode support, MIT license
- **Joshua Price** — [OpenWurli](https://github.com/hal0zer0/openwurli) Wurlitzer 200A physical model

JUCE integration, UI, AAX/AU/VST3 plumbing is developed separately.

## License

The emulation engine source code in this repository is licensed under the **GNU General Public License v3.0** — see [LICENSE.txt](LICENSE.txt). The SFZero module is licensed under the **MIT License** — see [Modules/SFZero/LICENSE](Modules/SFZero/LICENSE). The Nuked OPL3 engine is licensed under the **GNU Lesser General Public License v2.1**.
