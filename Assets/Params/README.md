# Parameter descriptions

One JSON per synth core, each describing the 128 host-automatable parameter
slots that `source/plugin/ParameterPool.cpp` binds when that core is selected.

The files are listed as binary resources in `Retromulator.jucer`, so Projucer
compiles them into `BinaryData` and they are linked into the plugin. No
install-time file copying is needed on any platform. A file placed in
`<dataFolder>/Params/` overrides the built-in table of the same name without a
rebuild.

## Format

Parsed by `source/jucePluginLib/parameterdescriptions.cpp`:

- `parameterdescriptions`: 128 entries, `name` is the permanent slot id
  (`slot_000`..`slot_127`), `index` is the slot number. `displayName` is what
  the host shows; an empty one marks an unused slot, which hosts hide.
- `valuelists`: value-to-text tables referenced by each entry's `toText`.
- `controllerMap`: how a slot reaches the engine — `cc` (control change),
  `pp` (poly pressure, Virus page B) or `nrpn` (a core-native parameter index,
  used for DX7 voice parameters and N2X performance parameters).

Slot ids and the slot count must never change: they are hashed into the
AudioUnit/VST3 parameter ids, so altering them invalidates saved automation.

Named slots are packed contiguously from index 8; slots 0-7 are a fixed global
block (Volume, Pan, Mod Wheel, Expression, Sustain, Portamento, Breath, Foot),
left empty where a core uses that CC for its own parameter.

Data for the Virus, MicroQ, XT, N2X and JE-8086 tables derives from the
gearmulator project's editor parameter descriptions.
