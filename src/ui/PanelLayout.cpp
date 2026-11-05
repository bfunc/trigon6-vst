#include "PanelLayout.h"

namespace t6ui
{

const std::vector<SectionLayout>& panelLayout()
{
    // Three bands like the synth: modulation + effects on top, the voice in the middle, performance at the bottom.
    static const std::vector<SectionLayout> layout {
        // top band
        { "Poly Mod", 0.00f, 0.00f, 0.20f, 0.27f,
          { { "pmod_osc3_amount", "pmod_fenv_amount" },
            { "pmod_dest_freq1", "pmod_dest_freq2", "pmod_dest_freq3", "pmod_dest_filter" },
            { "pmod_dest_pw1", "pmod_dest_pw2", "pmod_dest_fdbk", "" } } },
        { "Aftertouch", 0.20f, 0.00f, 0.20f, 0.27f,
          { { "at_amount" },
            { "at_dest_freq1", "at_dest_freq2", "at_dest_freq3", "at_dest_fxa_mix" },
            { "at_dest_filter", "at_dest_amp", "at_dest_lfo", "at_dest_fxb_mix" } } },
        { "Misc", 0.40f, 0.00f, 0.25f, 0.27f,
          { { "vintage", "pan_spread", "program_volume" },
            { "key_mode", "pitch_wheel_range", "unison_voices" } } },
        { "Effects", 0.65f, 0.00f, 0.35f, 0.27f,
          { { "fx_on", "fxa_type", "fxa_param1", "fxa_param2", "fxa_mix", "fxa_sync" },
            { "", "fxb_type", "fxb_param1", "fxb_param2", "fxb_mix", "fxb_sync" } } },

        // middle band
        { "Arp / Clock", 0.00f, 0.27f, 0.22f, 0.25f,
          { { "arp_on", "arp_octaves", "arp_mode", "arp_hold" },
            { "seq_on", "clock_divide", "bpm" } } },
        { "LFO", 0.00f, 0.52f, 0.22f, 0.30f,
          { { "lfo_freq", "?Shape", "lfo_amount", "lfo_sync" },
            { "lfo_dest_freq1", "lfo_dest_freq2", "lfo_dest_freq3", "lfo_dest_filter" },
            { "lfo_dest_pw1", "lfo_dest_pw2", "lfo_dest_pw3", "lfo_dest_amp" } } },
        { "Oscillators", 0.22f, 0.27f, 0.46f, 0.55f,
          { { "osc1_octave", "osc2_sync", "osc1_tri", "osc1_saw", "osc1_pulse", "osc1_pw", "osc1_volume", "?Fdbk/Drive" },
            { "osc2_octave", "osc2_pitch", "osc2_tri", "osc2_saw", "osc2_pulse", "osc2_pw", "osc2_volume", "?Noise" },
            { "osc3_octave", "osc3_pitch", "osc3_tri", "osc3_saw", "osc3_pulse", "osc3_pw", "osc3_volume", "osc3_keyboard" } } },
        { "Filter", 0.68f, 0.27f, 0.32f, 0.30f,
          { { "cutoff", "resonance", "fenv_amount", "filter_2pole", "filter_keyboard" },
            { "fenv_attack", "fenv_decay", "fenv_sustain", "fenv_release", "fenv_velocity" } } },
        { "Amplifier", 0.68f, 0.57f, 0.32f, 0.25f,
          { { "vca_attack", "vca_decay", "vca_sustain", "vca_release", "vca_velocity" } } },

        // bottom band
        { "Master", 0.00f, 0.82f, 0.22f, 0.18f,
          { { "distortion", "unison_on" } } },
        { "Glide", 0.22f, 0.82f, 0.46f, 0.18f,
          { { "glide_on", "glide_rate", "glide_mode" } } },
    };
    return layout;
}

} // namespace t6ui
