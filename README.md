# Retromulator

A DAW plugin (VST3 / AU) that emulates classic hardware synthesizers by running their original firmware on cycle-accurate CPU and DSP emulators.

## What it emulates

| Hardware | Format |
|---|---|
| Access Virus A / B / C | VST3, AU |
| Access Virus TI / TI2 / Snow | VST3, AU |
| Waldorf microQ | VST3, AU |
| Waldorf Microwave II / XT | VST3, AU |
| Clavia Nord Lead / Rack 2x | VST3, AU |
| Roland JP-8000 (JE-8086) | VST3, AU |
| Yamaha DX7 (VDX7) | VST3, AU |

Each synth requires its original ROM firmware to run (not included). ROMs are loaded from the application support folder at runtime.

## How it differs from Gearmulator

Retromulator is a **plugin wrapper** built on top of the open-source emulation engines from [Gearmulator](https://github.com/dsp56300/gearmulator) by dsp56300. Gearmulator ships as standalone applications and open-source plugins built with CMake. Retromulator packages the same engines into a polished single-plugin experience using JUCE, with a unified rack-style UI, DAW state persistence, bank/patch browsing, focused on preset playing.

The emulation cores (dsp56300, mc68k, h8s, synthLib and all synth-specific libraries) are unchanged from Gearmulator. The DX7 emulation is based on VDX7, a separate project (see Credits below).

## Credits

The emulation engines powering Retromulator are the work of the Gearmulator project:

- **[dsp56300](https://github.com/dsp56300)** — project lead, DSP56300 emulator, Virus TI / microQ / XT / Nord N2X / JE-8086 engines
- All contributors to [github.com/dsp56300/gearmulator](https://github.com/dsp56300/gearmulator)
- **chiaccona** — [VDX7](https://github.com/chiaccona/VDX7), cycle-accurate Yamaha DX7 emulation (HD6303R CPU, EGS, OPS), GPL v3

This plugin wrapper (JUCE integration, UI, AU/VST3 plumbing) is developed separately and is not affiliated with or endorsed by the Gearmulator project.

## License

The emulation engine source code in this repository is licensed under the **GNU General Public License v3.0** — see [LICENSE.txt](LICENSE.txt).
