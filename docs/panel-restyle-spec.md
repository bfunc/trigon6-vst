# Panel restyle: the look of the hardware

Reference design: https://claude.ai/artifact/KtZqu5rc83Wg5K7333CxfA (visual spec, not code to port).
The reference is the Trigon-6 itself, not any other editor's look.

## Look
- Black panel (#0e0e0f to #151516), thin grey section lines (#5a5a5e), light wooden end cheeks left and right.
- Section titles in a wide display face (Michroma, letter-spaced); control labels small bold caps.
- Black knobs with tick marks, a white pointer line and the value under them.
- Square dark buttons that glow orange-red when on.
- Small red LEDs for choices: LFO shape, arp mode and octaves (a column), oscillator octave and filter keyboard (a row).
- Red seven-segment-style windows (Share Tech Mono) for effect types, glide mode, key mode, clock divide.
- Top bar: title, sound status, MIDI CH, GET SOUND, SEND ALL, NRPN count; tabs Connection / Panel / Presets.

## Layout (as on the synth)
- Left column: LFO, Aftertouch, PolyMod.
- Centre: Oscillators 1-3 in rows. Osc 3 saw is ONE button with two LEDs (saw falling / ramp rising).
- Right column: Filter, Amplifier, Effects A/B.
- Bottom strip: Glide / Unison, Arp / Clock, Master (distortion, vintage, pan spread, volume, PW range, key mode).

## Rules
- Fonts must be free to bundle (SIL OFL), compiled in as BinaryData (assets/fonts, licences beside them).
- Keep all behaviour: NRPN out, incoming NRPN updates, Get sound, Presets.
- English UI only.
