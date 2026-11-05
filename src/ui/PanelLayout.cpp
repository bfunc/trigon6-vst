#include "PanelLayout.h"

namespace t6ui
{

const std::vector<SectionLayout>& panelLayout()
{
    // As on the synth: modulation on the left, the oscillators in the middle, filter, amp and effects on the
    // right, and a performance strip along the bottom. Every parameter appears exactly once (tests check it).
    static const std::vector<SectionLayout> layout {
        // left column
        { "LFO", 0.000f, 0.00f, 0.230f, 0.36f,
          { { "lfo_freq", "lfo_sync", "lfo_shape", "lfo_amount" },
            { "lfo_dest_freq1", "lfo_dest_freq2", "lfo_dest_freq3", "lfo_dest_pw1",
              "lfo_dest_pw2", "lfo_dest_pw3", "lfo_dest_filter", "lfo_dest_amp" } } },
        { "Aftertouch", 0.000f, 0.36f, 0.230f, 0.22f,
          { { "at_amount", "at_dest_freq1", "at_dest_freq2", "at_dest_freq3", "at_dest_filter" },
            { "", "at_dest_amp", "at_dest_lfo", "at_dest_fxa_mix", "at_dest_fxb_mix" } } },
        { "PolyMod", 0.000f, 0.58f, 0.230f, 0.24f,
          { { "pmod_osc3_amount", "pmod_fenv_amount", "pmod_dest_freq1", "pmod_dest_freq2", "pmod_dest_freq3" },
            { "", "", "pmod_dest_pw1", "pmod_dest_pw2", "pmod_dest_fdbk", "pmod_dest_filter" } } },

        // centre
        { "Oscillators", 0.230f, 0.00f, 0.350f, 0.82f,
          { { "osc1_octave", "osc2_sync", "osc1_tri", "osc1_saw", "osc1_pulse", "osc1_pw", "osc1_volume", "fdbk_drive" },
            { "osc2_octave", "osc2_pitch", "osc2_tri", "osc2_saw", "osc2_pulse", "osc2_pw", "osc2_volume", "noise" },
            { "osc3_octave", "osc3_pitch", "osc3_tri", "osc3_saw", "osc3_pulse", "osc3_pw", "osc3_volume", "osc3_keyboard" } } },

        // right column
        { "Filter", 0.580f, 0.00f, 0.420f, 0.34f,
          { { "cutoff", "resonance", "fenv_amount", "filter_2pole", "filter_keyboard" },
            { "fenv_attack", "fenv_decay", "fenv_sustain", "fenv_release", "fenv_velocity" } } },
        { "Amplifier", 0.580f, 0.34f, 0.420f, 0.18f,
          { { "vca_attack", "vca_decay", "vca_sustain", "vca_release", "vca_velocity" } } },
        { "Effects", 0.580f, 0.52f, 0.420f, 0.30f,
          { { "fxa_type", "fxa_param1", "fxa_param2", "fxa_mix", "fxa_sync", "fx_on" },
            { "fxb_type", "fxb_param1", "fxb_param2", "fxb_mix", "fxb_sync", "" } } },

        // bottom strip
        { "Glide / Unison", 0.000f, 0.82f, 0.230f, 0.18f,
          { { "glide_rate", "glide_mode", "glide_on", "unison_voices", "unison_on" } } },
        { "Arp / Clock", 0.230f, 0.82f, 0.350f, 0.18f,
          { { "arp_on", "arp_octaves", "arp_mode", "arp_hold", "seq_on", "bpm", "clock_divide" } } },
        { "Master", 0.580f, 0.82f, 0.420f, 0.18f,
          { { "distortion", "vintage", "pan_spread", "program_volume", "pitch_wheel_range", "key_mode" } } },
    };
    return layout;
}

} // namespace t6ui
