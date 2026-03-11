# Retromulator

A DAW plugin (AAX / VST3 / AU) that emulates classic hardware synthesizers by running their original firmware on cycle-accurate CPU and DSP emulators.

## Hardware Cores

| Hardware | Emulation | Format |
|---|---|---|
| Access Virus A / B / C | Motorola DSP 56300 cycle-accurate | AAX, VST3, AU |
| Access Virus TI / TI2 / Snow | Motorola DSP 56300 cycle-accurate | AAX, VST3, AU |
| Waldorf microQ | Motorola DSP 56300 cycle-accurate | AAX, VST3, AU |
| Waldorf Microwave II / XT | Motorola DSP 56300 cycle-accurate | AAX, VST3, AU |
| Clavia Nord Lead / Rack 2x | Motorola DSP 56300 cycle-accurate | AAX, VST3, AU |
| Roland JP-8000 (JE-8086) | Motorola DSP 56300 cycle-accurate | AAX, VST3, AU |
| Yamaha DX7 | HD6303R + YM21280 EGS + YM21290 OPS (VDX7) | AAX, VST3, AU |
| Akai S1000 | SFZero v3.0.0 sample engine (MIT) | AAX, VST3, AU |

The DSP-based synths require their original ROM firmware to run (not included). ROMs are loaded from the application support folder at runtime. The Akai S1000 sampler loads SF2, SFZ, ZBP, and ZBB sample banks via the [SFZero](https://github.com/discoDSP/SFZero) MIT-licensed engine with 8-point sinc interpolation, extended SFZ/SF2 opcode support, and discoDSP Bliss sampler format.

## How it differs from Gearmulator

Retromulator is a **plugin wrapper** built on top of the open-source emulation engines from [Gearmulator](https://github.com/dsp56300/gearmulator) by dsp56300. Gearmulator ships as standalone applications and open-source plugins built with CMake. Retromulator packages the same engines into a polished single-plugin experience using JUCE, with a unified rack-style UI, DAW state persistence, bank/patch browsing, focused on preset playing.

The emulation cores (dsp56300, mc68k, h8s, synthLib and all synth-specific libraries) are unchanged from Gearmulator. The DX7 emulation is based on VDX7, a separate project (see Credits below). The Akai S1000 sampler uses the SFZero module, an MIT-licensed JUCE sample engine maintained by discoDSP.

## Credits

The emulation engines powering Retromulator are the work of the Gearmulator project:

- **[dsp56300](https://github.com/dsp56300)** — project lead, DSP56300 emulator, Virus TI / microQ / XT / Nord N2X / JE-8086 engines
- All contributors to [github.com/dsp56300/gearmulator](https://github.com/dsp56300/gearmulator)
- **chiaccona** — [VDX7](https://github.com/chiaccona/VDX7), cycle-accurate Yamaha DX7 emulation (HD6303R CPU, EGS, OPS), GPL v3
- **Steve Folta** — original [SFZero](https://github.com/stevefolta/SFZero) SFZ/SF2 sample player, MIT license
- **Leo Olivers** — SFZero JUCE module port
- **discoDSP** — [SFZero v3.0.0](https://github.com/discoDSP/SFZero), 8-point sinc interpolation, Bliss format, extended opcode support, MIT license

This plugin wrapper (JUCE integration, UI, AAX/AU/VST3 plumbing) is developed separately and is not affiliated with or endorsed by the Gearmulator project.

## License

The emulation engine source code in this repository is licensed under the **GNU General Public License v3.0** — see [LICENSE.txt](LICENSE.txt). The SFZero module is licensed under the **MIT License** — see [Modules/SFZero/LICENSE](Modules/SFZero/LICENSE).
