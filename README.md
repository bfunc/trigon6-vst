# Trigon6 Control

A VST3 plugin (plus a standalone app) for Windows that controls a Sequential Trigon-6 from a DAW (built for FL Studio).
Every program parameter of the synth is a plugin parameter you can automate. When one changes, the plugin sends it
to the synth as an NRPN.

Status: **stage 1**. The parameters, NRPN output on the plugin's MIDI out, the MIDI channel and the panel UI work.
Still to do: direct output to a hardware port, reading NRPNs back from the synth, and Get patch over SysEx
(see [Roadmap](#roadmap)).

## Download

Every push to `main` builds a zip on GitHub Actions: **Releases → latest**. It contains `Trigon6 Control.vst3`,
`Trigon6 Control.exe` (the standalone app) and `VERSION.txt`.

Install the VST3 by copying `Trigon6 Control.vst3` to `C:\Program Files\Common Files\VST3`, or to any folder that
FL Studio scans, then rescan plugins in FL.

## Synth settings (Globals)

| Global | Set to | Why |
|---|---|---|
| Param Rcv | **NR** | The plugin sends NRPN. CC covers only some parameters and only 0–127. |
| Param Xmit | **NR** | The synth reports knob moves back. This becomes useful in stage 4. |
| MIDI Channel | the same channel as in the plugin (or All) | |
| MIDI Out | **USB** | when the synth is connected by USB |
| MIDI SysEx | **USB** | for Get patch (stage 5) |

## FL Studio

1. Add **Trigon6 Control** as a generator (Channel rack → + → Trigon6 Control). It makes no sound itself.
2. Open the plugin wrapper's ⚙ (Plugin options) and set **MIDI output port** to a free number, e.g. 1.
3. Open Options → MIDI settings. Select the Trigon-6 in **Output**, and set its **Port** to the same number (1).
4. In the plugin, set **MIDI channel** to the synth's channel.
5. Turn a knob in the plugin. The synth follows it. FL automation clips on the plugin's parameters work the same way.

To play the synth's sound, add a separate **MIDI Out** channel (or route the audio in through your interface).
That part is ordinary FL routing.

**Send all** pushes every parameter to the synth. A project that loads does **not** send anything: the synth keeps its
own sound until you press Send all or move a control.

## How sending works

- Each change goes out as 4 CCs: 99 (parameter MSB), 98 (parameter LSB), 6 (value MSB), 38 (value LSB).
- Only values that differ from what the synth last got are sent.
- Each parameter is sent at most once every 20 ms. While you turn a knob, the latest value goes out.
- Across all parameters the limit is 200 NRPN per second (2400 bytes/s). The DIN MIDI cable carries 3125 bytes/s.

## Parameter table

The table is `src/core/ParamTable.cpp`, the single source of truth. The parameters come from the NRPN table in the
**Trigon-6 User's Guide 1.1** (Appendix B).

Notes on the manuals:

- **Operation Manual 1.2 prints the wrong table.** Its program parameters belong to a Prophet-6 (2 oscillators, sub osc,
  HP filter). Version 1.1 has the Trigon-6 table: NRPN 0–106, Osc Pitch 0–1400, Cutoff 0–164 and so on.
- Some names in 1.1 are misprinted and are corrected in the plugin: NRPN 20 "Osc 1 Sync" is *Sync Osc 2*, and NRPN 21
  "Osc 1 Keyboard" is *Osc 3 Keyboard*.
- The manual's table has no NRPN for **Feedback◂▸Drive**, **Noise** or **LFO Shape**. The panel shows these as
  "NRPN ?". Stage 4 will find their numbers from what the synth sends.
- NRPN 63 "Distortion" is the Distortion knob in the Master section.
- The manual does not number every list. These value orders are guesses to check on the synth: Osc 3 Octave (Lo first?)
  and Key Mode.
- Left out on purpose: Sequencer Rec On (106), the program name and the sequencer steps.

## Build

Only Windows is built. GitHub Actions (`.github/workflows/build.yml`) configures with CMake, builds with MSVC, runs the
tests and packs the zip.

To build locally you need Visual Studio 2022 with the C++ workload, and CMake 3.22 or newer:

```
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

JUCE 8.0.4 is downloaded by CMake (FetchContent).

## Layout of the code

```
src/core/ParamTable.*   the parameters: NRPN, id, name, section, range, kind (source of truth)
src/core/Nrpn.*         NRPN encode / decode
src/core/Packed.*       DSI "packed MS bit" (1024 bytes <-> 1171 MIDI bytes)
src/core/Sysex.*        requests (edit buffer, program, device inquiry) and the program dump parser
src/core/SendQueue.*    what to send when: only changes, rate-limited
src/ui/PanelLayout.*    where each parameter sits on the panel
src/PluginProcessor.*   JUCE: parameters (APVTS) from the table, NRPN out in processBlock
src/PluginEditor.*      JUCE: the panel
tests/CoreTests.cpp     NRPN coding, packed format, SysEx, send queue, table and layout checks
```

`src/core` and `src/ui/PanelLayout` do not use JUCE, so the tests build in seconds.

## Roadmap

1. ✅ Parameters, NRPN out, MIDI channel, rate limit, panel UI.
2. MIDI output: the plugin's MIDI out (FL) or a hardware/USB port picked in the plugin (`juce::MidiOutput`).
3. Panel polish.
4. Feedback: NRPNs from the synth update the plugin without being sent back. A monitor shows unknown NRPN numbers
   (Feedback/Drive, Noise, LFO Shape).
5. Get patch: request the edit buffer and unpack it. An explore mode writes hex dumps and compares two of them, to find
   where each parameter sits in the 1024 bytes.

## Licence

AGPLv3 (JUCE's open-source licence). See `LICENSE`.
