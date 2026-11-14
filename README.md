# Trigon6 Control

A VST3 plugin (plus a standalone app) for Windows that controls a Sequential Trigon-6 from a DAW (built for FL Studio).
Every program parameter of the synth is a plugin parameter you can automate. When one changes, the plugin sends it
to the synth as an NRPN.

Status: **stage 2**. The parameters, NRPN output (through the host or straight to the synth's USB port), the MIDI
channel, the panel UI and a Connection page work. Still to do: parameters following the synth's own knobs, and
Get patch over SysEx (see [Roadmap](#roadmap)).

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

## Is the sound saved? (top bar)

Next to the sound status the top bar says how the current sound compares with what is stored:

- **Saved in U 012** (green): the same as the slot it came from.
- **● Edited - 3 changes** (orange): differs from that slot. Hover for the list (Cutoff 64 -> 91). A new name counts.
- **● Saved on PC only** (blue): saved as a .syx on the PC, but the slot on the synth still holds the old version.
- **Slot unknown / U 012 not read / Checking U 012...** (grey): nothing to compare with yet.

The slot is the program picked last: on the synth's panel, with Load on the Presets tab, or the slot a sound was saved
into. Picking a program reads it again from the synth (only a read), so a change saved with the synth's own WRITE
button shows up; the plugin also reads the new sound (Get sound). A trailing **?** means the synth does not send its
knob moves (Param Xmit is not NR), so edits made on the synth can't be seen until Get sound.

## Distance from the preset (Panel tab)

The preset is the sound the plugin last read from the synth (Get sound, also after a program is picked) or last saved.
Until there is one, nothing is marked. Every value that differs from it shows it, always:

- **Knob**: a grey arc on the ring from the preset's value to the pointer (the longer, the further away), ending in a
  dot; a difference of one step still leaves a short arc. Double-click the knob to put the preset's value back (it goes
  to the synth). Hover: "preset 80, now 100, delta +20".
- **Button, switch, window**: a small grey dot at its corner. Click it to put the preset's value back.
- **LED lists, octave dials, the keyboard row**: the preset's choice shows as a grey LED while another one is lit.
- **Envelope displays** (filter, amp): the preset's shape is drawn dashed behind the current one; double-click puts it back.
- **Reset to preset** (top bar): every parameter back to the preset, sent to the synth. Grey when nothing differs.

## Connection page

The plugin opens on **Connection** unless the synth is fully connected (then it opens on **Panel**).

- **Send NRPN**: *Direct to the synth's port* (the default, in the standalone app and in a DAW) or *Through the host*
  (DAW MIDI out). A choice saved in a project stays as it was saved. Ports are picked by name; *Auto* takes the first port named "Trigon 6". A saved name still finds its
  port when Windows renames it ("2- Trigon 6 Module", "Trigon 6 Module [0]"). Ports reopen by themselves when the
  synth is switched on or plugged in. All plugin copies in one program share one connection.
- **Verdict**: one line with the most likely cause and the one thing to do, e.g. which Globals button to press.
- **Ladder**: ports, synth to computer, computer to synth, SysEx, knob moves (Param Xmit), Param Rcv, MIDI channel.
  Each step needs the ones above it; a step that can't be judged yet is dimmed. Only the first failing step shows
  its fix, so one cause doesn't light up the whole list.
- **Tests**: *Check* asks the synth who it is and requests its global settings and current sound (changes nothing).
  *Play test note* plays middle C; you answer *I heard it* / *No sound*. That tells "nothing reaches the synth" from
  "SysEx is off". *Use channel N* appears when the synth's channel differs from the plugin's.
- **Also note**: a MIDI loop (our NRPN come straight back), MIDI Clock = In (arp and sequencer wait for clock), Local
  Ctrl = Off, a shared connection.
- **Globals map**: both rows of the synth's buttons 0–9 with each setting's name, current value and the value it
  needs. A white frame marks the button the verdict is about.

Settings are known from three sources, in this order: what the synth sends when you change a global (Param Xmit
1029, Param Rcv 1030, MIDI SysEx 1032, MIDI Out 1033; values in the panel's order, e.g. Param Xmit 0 Off, 1 CC,
2 NR), what the traffic proves (knob moves over USB prove MIDI Out = USB and Param Xmit = NR; a dump proves SysEx =
USB), and the global dump. The dump's order is not checked yet (assumed NRPN order), so a value seen only there is
marked "?", never overrides proof, and a mismatch is only a warning. The page shows the dump's raw values.

Windows gives a MIDI port to one program at a time. If FL Studio (or the standalone app, or another program) has the
Trigon ports open, the page says the port is busy.

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

**Osc 1 Octave jumps to +2 when FL starts playing.** FL's MIDI Out sends something at transport start that the synth
takes as Osc 1 Octave (turning MIDI Out off stops it). Workaround, off by default: Connection page →
**Re-send Osc 1 Octave on play**. 10 ms after each start the plugin sends Osc 1 Octave again: the value from the last
Get sound, or the last change made in the plugin. It is saved with the project.

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
- The manual's table has no NRPN for **Noise**, **Feedback◂▸Drive** or **LFO Shape**. Their numbers were read from
  what the synth sends: Noise = 22 (0–127), Feedback◂▸Drive = 28 (0–254, centre 127, left = feedback, right = drive),
  LFO Shape = 67 (0–4: Triangle, Saw, Reverse Saw, Square, Random, in the manual's order).
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
src/core/Globals.*      the synth's global settings: button row/number, NRPN, value names, what the plugin needs
src/core/Connection.*   the diagnosis: ladder, verdict, warnings, from a snapshot of what the MIDI layer saw
src/core/PortMatch.*    finding a port by name when Windows renames it
src/midi/SynthLink.*    JUCE: opens the synth's ports, reconnects, sends NRPN/SysEx, records replies
src/core/PresetDiff.*   which parameters differ from the preset, the tooltip text
src/ui/PanelLayout.*    where each control sits on the panel (generated from docs/panel-mockup.html by tools/panel-geometry)
src/ui/PanelView.*      JUCE: the Panel tab, drawn on a fixed canvas and scaled to the window
src/PluginProcessor.*   JUCE: parameters (APVTS) from the table, NRPN out in processBlock
src/PluginEditor.*      JUCE: top bar, tabs
src/ui/ConnectionPage.* JUCE: the Connection page
src/ui/PresetsPage.*    JUCE: the Presets page (bank list, fetch, cache, .syx files)
src/ui/SavePanel.*      JUCE: Save the current sound to a user slot, a .syx file, or both; Rename a program (backup + read-back)
src/ui/AppSettings.h    the shared settings file, Documents folders
src/ui/PresetTypes.h    the type the user set per program (kept in the settings file)
src/core/Presets.*      program names and labels, file names, stored program dumps, fetch queue
tests/CoreTests.cpp     NRPN coding, packed format, SysEx, send queue, globals, diagnosis rules, port names, table and layout
tests/PanelSnapshot.cpp JUCE, local only (-DT6_SNAPSHOT=ON): draws the Panel tab to a PNG, or clicks every control
```

`src/core` and `src/ui/PanelLayout` do not use JUCE, so the tests build in seconds. `tests/ProcessorTest.cpp` (JUCE,
local only: `-DT6_PLAYTEST=ON`) checks the preset reference, the reset and the output mode default on the processor.

## Roadmap

1. ✅ Parameters, NRPN out, MIDI channel, rate limit, panel UI.
2. ✅ MIDI output: the plugin's MIDI out (FL) or the synth's port picked in the plugin. Connection page.
3. Panel polish.
4. Feedback: NRPNs from the synth update the plugin without being sent back. A monitor shows unknown NRPN numbers
   (for parameters still missing from the table).
5. Get patch: request the edit buffer and unpack it. An explore mode writes hex dumps and compares two of them, to find
   where each parameter sits in the 1024 bytes.

## Licence

AGPLv3 (JUCE's open-source licence). See `LICENSE`.
