#include "ParamTable.h"

#include <unordered_map>

namespace t6
{

const char* sectionName (Section s)
{
    switch (s)
    {
        case Section::Oscillators: return "Oscillators";
        case Section::Mixer:       return "Mixer";
        case Section::Filter:      return "Filter";
        case Section::FilterEnv:   return "Filter Env";
        case Section::AmpEnv:      return "Amp Env";
        case Section::Lfo:         return "LFO";
        case Section::PolyMod:     return "Poly Mod";
        case Section::Aftertouch:  return "Aftertouch";
        case Section::Effects:     return "Effects";
        case Section::ArpSeq:      return "Arp / Seq";
        case Section::Misc:        return "Misc";
    }
    return "";
}

namespace
{
using S = Section;

ParamDef num (int nrpn, const char* id, const char* name, S s, int min, int max)
{
    return { nrpn, id, name, s, min, max, Kind::Int, 0, {} };
}

ParamDef bip (int nrpn, const char* id, const char* name, S s, int max, int center)
{
    return { nrpn, id, name, s, 0, max, Kind::Bipolar, center, {} };
}

ParamDef onOff (int nrpn, const char* id, const char* name, S s)
{
    return { nrpn, id, name, s, 0, 1, Kind::Bool, 0, {} };
}

ParamDef pick (int nrpn, const char* id, const char* name, S s, std::vector<std::string> names)
{
    return { nrpn, id, name, s, 0, (int) names.size() - 1, Kind::Choice, 0, std::move (names) };
}

const std::vector<std::string> octaves { "-2", "-1", "0", "+1", "+2" };
const std::vector<std::string> fxA { "Off", "BBD", "DDL", "Chorus", "PH1", "PH2", "PH3", "Ring", "FL1", "FL2" };

std::vector<ParamDef> build()
{
    // Source: Trigon-6 User's Guide 1.1, Appendix B, "Program Parameter Data".
    // (Manual 1.2 prints the Prophet-6 table there by mistake: 2 oscillators, sub osc, HP filter.)
    // Choice orders follow the manual's lists; marked (?) where the manual does not give the numbering.
    // Missing from the manual's table (to find via incoming NRPN, stage 4): Feedback<>Drive, Noise, LFO Shape.
    // Left out on purpose: 106 Sequencer Rec On (a mode switch, not a sound setting), name, sequencer steps.
    return {
        // Oscillators
        pick (0, "osc1_octave", "Osc 1 Octave", S::Oscillators, octaves),
        pick (1, "osc2_octave", "Osc 2 Octave", S::Oscillators, octaves),
        pick (2, "osc3_octave", "Osc 3 Octave", S::Oscillators, { "Lo", "-2", "-1", "0", "+1", "+2" }), // (?) order
        bip  (3, "osc2_pitch", "Osc 2 Pitch", S::Oscillators, 1400, 700),
        bip  (4, "osc3_pitch", "Osc 3 Pitch", S::Oscillators, 1400, 700),
        onOff (5, "osc1_saw", "Osc 1 Saw", S::Oscillators),
        onOff (6, "osc2_saw", "Osc 2 Saw", S::Oscillators),
        onOff (7, "osc3_saw", "Osc 3 Saw", S::Oscillators), // manual: "Osc 3 Saw Fall"
        onOff (8, "osc1_tri", "Osc 1 Tri", S::Oscillators),
        onOff (9, "osc2_tri", "Osc 2 Tri", S::Oscillators),
        onOff (10, "osc3_tri", "Osc 3 Tri", S::Oscillators),
        onOff (11, "osc1_pulse", "Osc 1 Pulse", S::Oscillators),
        onOff (12, "osc2_pulse", "Osc 2 Pulse", S::Oscillators),
        onOff (13, "osc3_pulse", "Osc 3 Pulse", S::Oscillators),
        num  (14, "osc1_pw", "Osc 1 PW", S::Oscillators, 0, 127),
        num  (15, "osc2_pw", "Osc 2 PW", S::Oscillators, 0, 127),
        num  (16, "osc3_pw", "Osc 3 PW", S::Oscillators, 0, 127),
        onOff (20, "osc2_sync", "Sync Osc 2", S::Oscillators),          // manual: "Osc 1 Sync"
        onOff (21, "osc3_keyboard", "Osc 3 Keyboard", S::Oscillators),  // manual: "Osc 1 Keyboard"; only Osc 3 has it

        // Mixer
        num  (17, "osc1_volume", "Osc 1 Volume", S::Mixer, 0, 127),
        num  (18, "osc2_volume", "Osc 2 Volume", S::Mixer, 0, 127),
        num  (19, "osc3_volume", "Osc 3 Volume", S::Mixer, 0, 127),
        num  (63, "distortion", "Distortion", S::Mixer, 0, 127),

        // Filter
        num  (29, "cutoff", "Cutoff", S::Filter, 0, 164),
        num  (30, "resonance", "Resonance", S::Filter, 0, 127),
        pick (31, "filter_keyboard", "Filter Keyboard", S::Filter, { "Off", "Half", "Full" }),
        onOff (33, "filter_2pole", "2 Pole", S::Filter),

        // Filter envelope
        bip  (36, "fenv_amount", "FEnv Amount", S::FilterEnv, 254, 127),
        num  (40, "fenv_attack", "FEnv Attack", S::FilterEnv, 0, 127),
        num  (42, "fenv_decay", "FEnv Decay", S::FilterEnv, 0, 127),
        num  (44, "fenv_sustain", "FEnv Sustain", S::FilterEnv, 0, 127),
        num  (46, "fenv_release", "FEnv Release", S::FilterEnv, 0, 127),
        onOff (32, "fenv_velocity", "FEnv Velocity", S::FilterEnv),

        // Amp envelope
        num  (41, "vca_attack", "Amp Attack", S::AmpEnv, 0, 127),
        num  (43, "vca_decay", "Amp Decay", S::AmpEnv, 0, 127),
        num  (45, "vca_sustain", "Amp Sustain", S::AmpEnv, 0, 127),
        num  (47, "vca_release", "Amp Release", S::AmpEnv, 0, 127),
        onOff (48, "vca_velocity", "Amp Velocity", S::AmpEnv),

        // LFO
        num  (64, "lfo_freq", "LFO Freq", S::Lfo, 0, 254),
        onOff (66, "lfo_sync", "LFO Sync", S::Lfo),
        num  (68, "lfo_amount", "LFO Amount", S::Lfo, 0, 255),
        onOff (69, "lfo_dest_freq1", "LFO > Freq 1", S::Lfo),
        onOff (70, "lfo_dest_freq2", "LFO > Freq 2", S::Lfo),
        onOff (71, "lfo_dest_freq3", "LFO > Freq 3", S::Lfo),
        onOff (72, "lfo_dest_pw1", "LFO > PW 1", S::Lfo),
        onOff (73, "lfo_dest_pw2", "LFO > PW 2", S::Lfo),
        onOff (74, "lfo_dest_pw3", "LFO > PW 3", S::Lfo),
        onOff (75, "lfo_dest_filter", "LFO > Filter", S::Lfo),
        onOff (76, "lfo_dest_amp", "LFO > Amp", S::Lfo),

        // Poly Mod
        bip  (86, "pmod_fenv_amount", "PolyMod Filter Env", S::PolyMod, 254, 127),
        bip  (87, "pmod_osc3_amount", "PolyMod Osc 3 Amt", S::PolyMod, 254, 127),
        onOff (88, "pmod_dest_freq1", "PolyMod > Freq 1", S::PolyMod),
        onOff (89, "pmod_dest_freq2", "PolyMod > Freq 2", S::PolyMod),
        onOff (90, "pmod_dest_freq3", "PolyMod > Freq 3", S::PolyMod),
        onOff (91, "pmod_dest_pw1", "PolyMod > PW 1", S::PolyMod),
        onOff (92, "pmod_dest_pw2", "PolyMod > PW 2", S::PolyMod),
        onOff (93, "pmod_dest_fdbk", "PolyMod > Fdbk", S::PolyMod),
        onOff (94, "pmod_dest_filter", "PolyMod > Filter", S::PolyMod),

        // Aftertouch
        bip  (77, "at_amount", "AT Amount", S::Aftertouch, 254, 127),
        onOff (78, "at_dest_freq1", "AT > Freq 1", S::Aftertouch),
        onOff (79, "at_dest_freq2", "AT > Freq 2", S::Aftertouch),
        onOff (80, "at_dest_freq3", "AT > Freq 3", S::Aftertouch),
        onOff (81, "at_dest_filter", "AT > Filter", S::Aftertouch),
        onOff (82, "at_dest_amp", "AT > Amp", S::Aftertouch),
        onOff (83, "at_dest_lfo", "AT > LFO", S::Aftertouch),
        onOff (84, "at_dest_fxa_mix", "AT > FX Mix A", S::Aftertouch),
        onOff (85, "at_dest_fxb_mix", "AT > FX Mix B", S::Aftertouch),

        // Effects
        onOff (51, "fx_on", "FX On", S::Effects),
        pick (49, "fxa_type", "FX A Type", S::Effects, fxA),
        num  (53, "fxa_mix", "FX A Dry/Wet", S::Effects, 0, 127),
        num  (55, "fxa_param1", "FX A Param 1", S::Effects, 0, 127),
        num  (57, "fxa_param2", "FX A Param 2", S::Effects, 0, 127),
        onOff (59, "fxa_sync", "FX A Sync", S::Effects),
        pick (50, "fxb_type", "FX B Type", S::Effects,
              { "Off", "BBD", "DDL", "Chorus", "PH1", "PH2", "PH3", "Ring", "FL1", "FL2", "Hall", "Room", "Plate", "Spring" }),
        num  (54, "fxb_mix", "FX B Dry/Wet", S::Effects, 0, 127),
        num  (56, "fxb_param1", "FX B Param 1", S::Effects, 0, 127),
        num  (58, "fxb_param2", "FX B Param 2", S::Effects, 0, 127),
        onOff (60, "fxb_sync", "FX B Sync", S::Effects),

        // Arpeggiator / sequencer / clock
        onOff (102, "arp_on", "Arp On", S::ArpSeq),
        pick (100, "arp_mode", "Arp Mode", S::ArpSeq, { "Up", "Down", "Up/Down", "Random", "Assign" }),
        pick (101, "arp_octaves", "Arp Octaves", S::ArpSeq, { "1", "2", "3" }),
        onOff (104, "arp_hold", "Arp Hold", S::ArpSeq),
        pick (103, "clock_divide", "Clock Divide", S::ArpSeq,
              { "Half", "Qtr", "8th", "8th D", "8th S", "8th T", "16th", "16th S", "16th T", "32nd" }),
        num  (98, "bpm", "BPM", S::ArpSeq, 30, 250),
        onOff (105, "seq_on", "Seq On", S::ArpSeq),

        // Misc
        num  (34, "program_volume", "Program Volume", S::Misc, 0, 100),
        num  (35, "pan_spread", "Pan Spread", S::Misc, 0, 127),
        num  (27, "vintage", "Vintage", S::Misc, 0, 127),
        onOff (25, "glide_on", "Glide On", S::Misc),
        num  (23, "glide_rate", "Glide Rate", S::Misc, 0, 127),
        pick (24, "glide_mode", "Glide Mode", S::Misc, { "FR", "FRA", "FT", "FTA" }),
        onOff (95, "unison_on", "Unison On", S::Misc),
        num  (96, "unison_voices", "Unison Voices", S::Misc, 0, 6),
        pick (97, "key_mode", "Key Mode", S::Misc, { "Low", "Low R", "High", "High R", "Last", "Last R" }), // (?) order
        num  (26, "pitch_wheel_range", "Pitch Wheel Range", S::Misc, 0, 12),
    };
}
} // namespace

const std::vector<ParamDef>& paramTable()
{
    static const std::vector<ParamDef> table = build();
    return table;
}

int indexOfNrpn (int nrpn)
{
    static const std::unordered_map<int, int> byNrpn = []
    {
        std::unordered_map<int, int> m;
        const auto& t = paramTable();
        for (int i = 0; i < (int) t.size(); ++i)
            m[t[(size_t) i].nrpn] = i;
        return m;
    }();
    auto it = byNrpn.find (nrpn);
    return it == byNrpn.end() ? -1 : it->second;
}

std::string formatValue (const ParamDef& p, int value)
{
    switch (p.kind)
    {
        case Kind::Bool:
            return value ? "On" : "Off";
        case Kind::Choice:
        {
            const int i = value - p.min;
            return i >= 0 && i < (int) p.choices.size() ? p.choices[(size_t) i] : std::to_string (value);
        }
        case Kind::Bipolar:
        {
            const int d = value - p.center;
            return (d > 0 ? "+" : "") + std::to_string (d);
        }
        case Kind::Int:
            break;
    }
    return std::to_string (value);
}

} // namespace t6
