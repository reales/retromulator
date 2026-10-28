# Retromulator

A standalone app and DAW plugin (AAX / VST3 / AU) that emulates classic hardware synthesizers, samplers, and keyboards by running their original firmware on cycle-accurate CPU and DSP emulators.

[![Retromulator](https://www.discodsp.com/img/retromulator.webp)](https://www.discodsp.com/retromulator/)

[![Download for Windows](https://img.shields.io/badge/Windows-Download-blue?style=for-the-badge&logo=windows)](https://www.discodsp.com/download/?id=36) &nbsp; [![Download for macOS](https://img.shields.io/badge/macOS-Download-blue?style=for-the-badge&logo=apple)](https://www.discodsp.com/download/?id=35) &nbsp; [![Download for Linux](https://img.shields.io/badge/Linux-Download-blue?style=for-the-badge&logo=linux&logoColor=white)](https://www.discodsp.com/download/?id=34) &nbsp; [![Buy License $99](https://img.shields.io/badge/Buy_License-$99-green?style=for-the-badge&logo=fastly)](https://discodsp.onfastspring.com/retromulator/)

## Hardware Cores

| Hardware | Emulation |
|---|---|
| Access Virus A / B / C | Motorola DSP 56300 cycle-accurate |
| Access Virus TI | Motorola DSP 56300 cycle-accurate |
| Akai S1000 | SFZero v3.0.0 sample engine |
| Clavia Nord Lead / Rack 2x | Motorola DSP 56300 cycle-accurate |
| Commodore 64 SID 6581 / 8580 | reSID cycle-accurate |
| General Instrument AY-3-8910 / Yamaha YM2149 | Ayumi + Ym2149Synth voice engine |
| Hammond B3 + Leslie (OpenB3) | setBfree tonewheel and Leslie engine |
| Moog Minimoog Model D (CA-72) | Circuit model from SPICE simulations |
| Oberheim Matrix-1000 | 6809 firmware + CEM3396 voice model |
| Roland JP-8000 (JE-8086) | H8S CPU and 4x ESP ASIC cycle-accurate |
| Roland SC-88 / 88VL / 88Pro / SC-8850 / SC-55mkII | H8/500 and SH-2 cycle-accurate |
| Tracker modules (XM / MOD / S3M / IT) | Fasttracker 2 and Schism Tracker replayers |
| Waldorf microQ | Motorola DSP 56300 cycle-accurate |
| Waldorf Microwave XT | Motorola DSP 56300 cycle-accurate |
| Wurlitzer 200A (OpenWurli) | Physical modeling synthesis |
| Yamaha DX7 | HD6303R + YM21280 EGS + YM21290 OPS (VDX7) |
| Yamaha OPL3 / YMF262 | Nuked OPL3 v1.8 |

Most DSP-based synths require their original ROM firmware to run (not included). ROMs are loaded from the application support folder at runtime. The SC-88 family needs a full image set per board, kept together in its own folder and identified by content. The Matrix-1000 needs its 32 KB system EPROM, and the 64 KB patch EPROM for banks 2 to 9. The microQ can run with an embedded fallback ROM. The Akai S1000, OpenWurli, OpenB3, CA-72, OPL3, AY-3-8910, and Tracker cores are ROM-free. The files each core needs, with their sizes and hashes, are listed under [ROM Files](#rom-files).

The **Akai S1000** sampler loads SF2, SFZ, ZBP, and ZBB sample banks, as well as Akai ISO/BIN/CUE disk images, via the [SFZero](https://github.com/reales/retromulator/tree/main/Modules/SFZero) MIT-licensed engine with 8-point sinc interpolation, extended SFZ/SF2 opcode support, auto-slice drum mapping, CC20 global tuning, and discoDSP Bliss sampler format.

The **Hammond B3 (OpenB3)** is the setBfree tonewheel organ and Leslie engine by way of OpenB3, with upper, lower and pedal drawbars, percussion, vibrato and chorus, overdrive, three Leslie cabinets with convolution cabinet bodies, a plate reverb, a keyboard split and .fxp presets in Factory and User banks.

The **Minimoog Model D (CA-72)** is the CA-72 circuit model by Idle Foundry: every board of a 1972 Model D transcribed into SPICE netlists, with real-time models derived from those simulations. It runs at the host sample rate in LO, HI and ULTRA quality, with a 4x engine for offline renders, and opens its own front panel below the plugin bar. Presets are .fxp files in Factory and User banks, with .fxb bank import and export, and every panel control is a host parameter with a MIDI CC.

The **Wurlitzer 200A (OpenWurli)** is a physical model of the Wurlitzer 200A electric piano with 64-voice polyphony, tremolo, speaker character modeling, velocity curves, and 2x oversampling.

The **Yamaha OPL3** emulates the YMF262 FM synthesis chip (18 channels, 4-operator) using the Nuked OPL3 engine. It loads SBI patch files with bank navigation via folder hierarchy, pitch bend, and voice stealing.

The **Commodore 64 SID** emulates the MOS 6581 / 8580 chip using the reSID engine with 3-voice polyphony, oldest-voice stealing, and a full 50 Hz macro playback engine (wavetable, pulsetable, filtertable, speedtable). It loads GoatTracker `.sng` banks and `.ins` standalone instruments, with live MIDI control via CC 1 (vibrato depth), CC 22 (pitch bend range, 1–48 semitones), CC 64 (sustain pedal), CC 71 (resonance), CC 74 (cutoff), and CC 75 (pulse width).

The **AY-3-8910 / YM2149** emulates the 3-channel PSG used in the ZX Spectrum, Amstrad CPC, MSX, and Atari ST using the Ayumi engine, with a voice layer re-implementing the Ym2149Synth firmware: soft volume and pitch envelopes, glide, vibrato, detune, noise delay, and transpose. Voices are controlled via CC 1–11 and patches are stored in a user bank.

The **Tracker** core plays XM and MOD modules through the Fasttracker 2 replayer, and S3M and IT through the Schism Tracker player with Nuked OPL3 for Adlib instruments. The module owns the transport, driven by MIDI notes: play, stop, and step through a playlist, which can be shuffled or stop at its end. A song ends after five seconds of silence. Folders and .m3u playlists are expanded, the song tempo can follow the host, and a module or a whole playlist renders offline to WAV or MP3 320 with 512 tap sinc interpolation.

The **Roland SC-88 family** emulates the SC-88, SC-88VL, SC-88Pro, SC-8850 and SC-55mkII GS sound modules, running each board's own firmware on an H8/500 or SH-2 core with the Roland tone generator, effects and filter chips. Tone and drum kit names are read from the board ROM, and the GS parameter map is exposed to the host: per-part programs, variation banks (CC0), tone maps (CC32) and the rhythm part assignment. A standard MIDI file, or a playlist of them, can be played through the board and rendered offline to WAV or MP3 320, from the plugin or from the command line.

The **Oberheim Matrix-1000** runs the original firmware on an emulated 6809 board (82C54 timers, 6850 UART, S&H DAC, latches) driving six CEM3396 voice models with BLEP oscillators and 2x oversampling. The firmware calibrates the voices itself at first boot, and the result is kept as a snapshot so later boots are instant. ROM banks 2 to 9 are dumped from the patch EPROM into .syx files with the factory names. Every patch parameter is exposed to the host and mapped to a MIDI CC, and both LFOs can follow the host tempo, MIDI clock or a SysEx tempo.

The standalone app opens modules, .m3u playlists and .mid files handed over by the OS (double click, Open With, or a drop on the dock icon) and switches to the matching core.

The plugin has a MIDI output that can pass on what the emulated core sends (SysEx dumps, parameter changes), switched on with `midiOutCoreOutput="1"` in settings.xml.

A core can also run without its sound engine, as an editor for the real synth, with `editorOnly_<core name>="1"` in settings.xml (spaces in the name become underscores).

## ROM Files

Required ROM files per hardware core. The filename is not checked: any file of the correct size and content is accepted.

ROMs go in the `ROM` folder inside the Retromulator data folder, and the Roland SC-88 family sets (SC-55mkII, SC-88, SC-88VL, SC-88Pro, SC-8850) in its `88emu` folder:

- macOS: `~/Library/Application Support/discoDSP/Retromulator/`
- Windows: `Documents\discoDSP\Retromulator\`
- Linux: `~/Documents/discoDSP/Retromulator/`

The hashes are those of the images Retromulator is tested with. The Virus TI firmware varies in size between versions, so its hash is that of the 8,569,757 byte image tested; other versions may also work. The DX7 needs only its 16 KB firmware.

| Hardware Core | ROM File | Size | SHA-1 | MD5 |
|---|---|---|---|---|
| Access Virus A, B & C | `virus_c.bin` | 512 KB | `4c34546889d90522a8d85300c471f18339f88f72` | `e499bb4f988d18d22bbfd0343928380a` |
| Access Virus TI | `firmware.bin` | 6–9 MB | `fb60f58212f72047b46c72767b887397dd77264a` | `5d853f2ca304045a5458812504d5db95` |
| Clavia Nord Lead 2X | `nord_lead_2x.bin` | 512 KB | `549394349f7371e48ead57d8a0ec6b4010014d12` | `61f58146de0b566adf6d1acf6355718b` |
| Oberheim Matrix-1000 | `mtrx1kv120.bin` | 32 KB | `4c39f955e9bed110e25805cf076537634ff1ac7a` | `a8092cfa56189c5bb7c1775e7b4ca09b` |
|  | `m1000_patchrom.bin` | 64 KB | `20f413653f597313766f24d4e369c6b7768b6872` | `fa081871ed9120468577eefab72330b1` |
| Roland JP-8000 | `jp8000.bin` | 512 KB | `492c98abdeef078afd0f6a5e00fd5d4a858a8242` | `6a3b6a45817a0b038aa63d6277ea7d51` |
| Roland SC-55mkII | `rom1.bin` | 32 KB | `b91bb1d9dccffe831b7cfde7800a3fe32b2fbda6` | `4ca058f7db05f51e97bb30a162e9610a` |
|  | `rom2.bin` | 512 KB | `078cb5feea05e80bb9a1bb857a2163ee434fd053` | `63b24c7193ce34afefce9cec32ac39f0` |
|  | `waverom1.bin` | 2 MB | `96708cb21381c2fd03de4babbf7aea301c7594a6` | `30df645acf7f1b621d5f2891ea53e00b` |
|  | `waverom2.bin` | 1 MB | `4d91cdeaed048d653dbf846a221003c3a3f08279` | `f86e0433a11707a048a8a79e8d98a3be` |
| Roland SC-88 / SC-88VL | `ControlROMSC88.bin` | 512 KB | `3bc9a68703bd09459283b6b45d01d08feaffb744` | `0ac771782ea58a53af590ebdf140d517` |
|  | `ControlROMSC88VL.bin` | 512 KB | `3a20f8f8cefd0d5e1edb103046f6fe94bb73ac7a` | `e3c8e8e644534f2390958c6f9d553da5` |
|  | `PCM_IC_325.bin` | 2 MB | `860dcd9804ded4cd46aab38bfc3764a1ad7a6b65` | `6a92b7de3ac7b8205d29ec4497644beb` |
|  | `PCM_IC_326.bin` | 2 MB | `ac95c26c46c40aacb944f5d45634c93bee9e6d90` | `d98f4b255d3a7dc830d92c71c25ce2eb` |
|  | `PCM_IC_327.bin` | 2 MB | `d88bf13c3d74097991b783295d95ccfae2c9282d` | `c05b103d4db110b3962431173cc72967` |
|  | `PCM_IC_328.bin` | 2 MB | `03e70ae2efd190f41af24be01b1abaa84bfa93d9` | `bccc26c34cac0d5509e8efb645043b5b` |
| Roland SC-88Pro | `sc88pro_valid.bin` | 1 MB | `6d06f00a1f8195a76b3af600ca708003f9a3abdc` | `9d4c2f123b4451d8ee75c3b982760f28` |
|  | `Roland VE-GSPro_Sample_ROM_A_R01567167 301_Roland XP-GS_v1.01.BIN` | 8 MB | `5cc1700b3f41921ed81fe3b92ba9ec28bd6649c9` | `bd33b20bb5e8f51e436e141f81a75c14` |
|  | `Roland VE-GSPro_Sample_ROM_B_R01567178 302_Roland SC-GS_v1.00.BIN` | 8 MB | `78b37ce0e735ad2c3e51338e74219a103d7fadba` | `125ea2056dc208f04a141b3dcdffdb1b` |
|  | `Roland VE-GSPro_Sample_ROM_C_R01233667 314_Roland SC-GS_v1.00.BIN` | 4 MB | `e2ad5f0b2e5bdad84e42d080fc2e2fad523cb84b` | `48c3887c9a2a574b907242640fa0a320` |
| Roland SC-8850 | `8850CPU.bin` | 64 KB | `99b414c5129960e3e58af1fb147a1474ccccca84` | `efe1ffb0ccbe1b2ec454692c522494fc` |
|  | `8850.bin` | 1 MB | `e594de1f5be17c11ef4f4d7efd142053bbf44085` | `554d5997dcd9ce6fa0777092ff48f6fa` |
|  | `8850Flash.bin` | 2 MB | `d9af1c75b277de2258ed74982d7a754f60c0826e` | `06eee65647b66109efb01eabd6d71248` |
|  | `SC8850WaveROMUnscrambled.bin` | 32 MB | `f3ff7b1f57f6c1a393d0a22e6bd08554dc325573` | `e8eb6ac0e394997dfbc17064f9f4a70e` |
| Waldorf microQ | `micro_q.bin` | 512 KB | `2b5d3f57b8a43802f36a53ced4173285f70d5ae0` | `216d13d7222a9f6a548ab19d419aa101` |
| Waldorf Microwave XT | `xt.bin` | 256 KB | `781ceb74e15d48017bf327091729f3e127ec2a3a` | `b676e922e04841e304676f9b7f55fa41` |
| Yamaha DX7 | `dx7.bin` | 16 KB | `715dbb8e96a4df2a7f096b368334a7654860bb26` | `6580f668fa67e63c22ad2617a0aaebd7` |

## Credits

- **[dsp56300](https://github.com/dsp56300)** — DSP56300 emulator, Virus TI / microQ / XT / Nord N2X engines, GPL v3
- All contributors to [github.com/dsp56300/gearmulator](https://github.com/dsp56300/gearmulator)
- **Giulio Zausa** — Roland JP-8000 (JE-8086) and SC-88 family emulation
- **chiaccona** — [VDX7](https://github.com/chiaccona/VDX7), cycle-accurate Yamaha DX7 emulation (HD6303R CPU, EGS, OPS), GPL v3
- **Nuke.YKT** — [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3), cycle-accurate YMF262 emulation, LGPL v2.1
- **Dag Lem** — [reSID](https://en.wikipedia.org/wiki/ReSID), cycle-accurate MOS 6581 / 8580 SID emulation, GPL v2
- **Lasse Öörni / Cadaver** — [GoatTracker](https://sourceforge.net/projects/goattracker2/) `.sng` / `.ins` format reference, GPL v2
- **Olav Sorensen** — [Fasttracker 2 clone](https://github.com/8bitbubsy/ft2-clone), XM / MOD replayer, BSD 3-Clause
- **Schism Tracker contributors** — [Schism Tracker](https://github.com/schismtracker/schismtracker), S3M / IT player, GPL v2
- **Peter Sovietov** — [Ayumi](https://github.com/true-grue/ayumi), AY-3-8910 / YM2149 emulation, MIT license
- **Timothy Lamb** — [Ym2149Synth](https://github.com/trash80/Ym2149Synth), voice engine and soft envelope firmware, GPL v3
- **Dominic Mazzoni** — [libresample](https://github.com/minorninth/libresample), sinc resampling, BSD 3-Clause / LGPL dual
- **Jean-Marc Valin** — [Speex](https://www.speex.org/) arbitrary-rate resampler, BSD 3-Clause
- **Steve Folta** — original [SFZero](https://github.com/stevefolta/SFZero) SFZ/SF2 sample player, MIT license
- **Leo Olivers** — SFZero JUCE module port
- **discoDSP** — [SFZero v3.0.0](https://github.com/reales/retromulator/tree/main/Modules/SFZero), 8-point sinc interpolation, Bliss format, extended opcode support, MIT license; synthLib device adapters, bank and patch handling, offline WAV / MP3 render and host parameter pool, Oberheim Matrix-1000 emulation, GPL v3
- **Joshua Price** — [OpenWurli](https://github.com/hal0zer0/openwurli) Wurlitzer 200A physical model
- **Michele Perrone** — [OpenB3](https://github.com/michele-perrone/OpenB3) Hammond B3 and Leslie, AGPL v3, based on [setBfree](https://github.com/pantherb/setBfree) by Robin Gareus, GPL v2+
- **Idle Foundry** — [CA-72](https://github.com/idlefoundry/ca-72) Minimoog Model D circuit model and front panel, GPL v3 or later; ported to C++ / JUCE by discoDSP
- **Laurent de Soras** — [hiir](http://ldesoras.free.fr/prod.html#src_hiir) polyphase IIR halfband filters, WTFPL

JUCE integration, UI, AAX/AU/VST3 plumbing is developed separately.

## License

The emulation engine source code in this repository is licensed under the **GNU General Public License v3.0** (see [LICENSE.txt](LICENSE.txt)). Bundled third-party components keep their own licenses:

| Component | License | File |
|---|---|---|
| Ayumi | MIT | [source/ayumiLib/AYUMI_LICENSE.txt](source/ayumiLib/AYUMI_LICENSE.txt) |
| CA-72 | GPL v3 or later | [source/ca72Lib/LICENSE](source/ca72Lib/LICENSE) |
| Fasttracker 2 replayer | BSD 3-Clause | [source/trackerLib/ft2/LICENSE](source/trackerLib/ft2/LICENSE) |
| LAME MP3 encoder | LGPL v2 | [source/lameLib/COPYING](source/lameLib/COPYING) |
| libresample | BSD 3-Clause | [source/libresample/LICENSE-BSD.txt](source/libresample/LICENSE-BSD.txt) |
| Nuked OPL3 | LGPL v2.1 | |
| OpenB3 | AGPL v3 | [source/openB3Lib/LICENSE](source/openB3Lib/LICENSE) |
| reSID | GPL v2 | |
| Schism Tracker player | GPL v2 | [source/trackerLib/schism/COPYING](source/trackerLib/schism/COPYING) |
| SFZero | MIT | [Modules/SFZero/LICENSE](Modules/SFZero/LICENSE) |
| Speex resampler | BSD 3-Clause | |
