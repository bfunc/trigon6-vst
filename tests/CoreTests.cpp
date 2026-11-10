// Core tests: plain C++, no framework, no JUCE. Exit code 0 = all passed.

#include "core/Connection.h"
#include "core/Globals.h"
#include "core/Nrpn.h"
#include "core/Packed.h"
#include "core/PortMatch.h"
#include "core/PresetDiff.h"
#include "core/Presets.h"
#include "core/ParamTable.h"
#include "core/SendQueue.h"
#include "core/SoundSync.h"
#include "core/Sysex.h"
#include "ui/PanelLayout.h"

#include <cstdio>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace t6;

static int failures = 0;
#define CHECK(cond)                                                      \
    do                                                                   \
    {                                                                    \
        if (! (cond))                                                    \
        {                                                                \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static void testNrpnEncode()
{
    // cutoff (29) = 164: value MSB 1, LSB 36
    auto c = encodeNrpn (29, 164);
    CHECK (c[0].controller == 99 && c[0].value == 0);
    CHECK (c[1].controller == 98 && c[1].value == 29);
    CHECK (c[2].controller == 6 && c[2].value == 1);
    CHECK (c[3].controller == 38 && c[3].value == 36);

    // osc pitch 1400 = 10 * 128 + 120
    c = encodeNrpn (3, 1400);
    CHECK (c[2].value == 10 && c[3].value == 120);

    // a global parameter above 127: 1026 = 8 * 128 + 2
    c = encodeNrpn (1026, 0);
    CHECK (c[0].value == 8 && c[1].value == 2 && c[2].value == 0 && c[3].value == 0);

    // raw bytes on channel 16
    auto b = encodeNrpnBytes (16, 36, 254);
    const uint8_t want[12] = { 0xBF, 99, 0, 0xBF, 98, 36, 0xBF, 6, 1, 0xBF, 38, 126 };
    for (int i = 0; i < 12; ++i)
        CHECK (b[(size_t) i] == want[i]);
    CHECK (encodeNrpnBytes (1, 0, 0)[0] == 0xB0);
}

static void testNrpnDecode()
{
    NrpnDecoder d;
    for (int param : { 0, 29, 106, 1024, 1088 })
        for (int value : { 0, 1, 127, 128, 254, 1400 })
        {
            std::optional<NrpnEvent> got;
            for (auto cc : encodeNrpn (param, value))
                if (auto e = d.feed (cc.controller, cc.value))
                    got = e;
            CHECK (got && got->param == param && got->value == value);
        }

    // other CCs in between are ignored; a value LSB before any parameter number is ignored
    NrpnDecoder d2;
    CHECK (! d2.feed (38, 5));
    CHECK (! d2.feed (99, 0));
    CHECK (! d2.feed (7, 100));
    CHECK (! d2.feed (98, 45));
    CHECK (! d2.feed (6, 0));
    auto e = d2.feed (38, 99);
    CHECK (e && e->param == 45 && e->value == 99);
    // the synth may repeat only the value for the same parameter
    d2.feed (6, 1);
    e = d2.feed (38, 0);
    CHECK (e && e->param == 45 && e->value == 128);
}

// The "saved / edited" indicator: the current values against a stored program and the last PC file.
static void testSoundSync()
{
    const auto& table = t6::paramTable();
    std::vector<uint8_t> stored (t6::kProgramBytes, 0);
    t6::setProgramName (stored, "Bright Bass");
    std::vector<int> values;
    for (const auto& p : table)
    {
        const auto v = t6::valueInProgram (p, stored);
        values.push_back (v ? *v : p.min);
    }
    CHECK (t6::compareSound (stored, values, "Bright Bass").same());
    CHECK (t6::compareSound (stored, values, "").same());       // name unknown: not compared
    CHECK (t6::compareSound (stored, values, "Dark Bass").name);  // a name change counts

    // one parameter that the bytes hold, moved
    size_t k = 0;
    while (k < table.size() && ! t6::valueInProgram (table[k], stored))
        ++k;
    CHECK (k < table.size());
    auto edited = values;
    edited[k] = edited[k] == table[k].max ? table[k].min : edited[k] + 1;
    const auto d = t6::compareSound (stored, edited, "Bright Bass");
    CHECK (d.count() == 1 && d.params.size() == 1 && d.params[0] == k);

    t6::SyncInput in;
    CHECK (t6::syncStatus (in).state == t6::SyncState::NoSlot);
    in.slotKnown = true;
    in.bank = 0;
    in.program = 12;
    CHECK (t6::syncStatus (in).state == t6::SyncState::NotRead);
    in.checking = true;
    CHECK (t6::syncStatus (in).state == t6::SyncState::Checking);
    in.checking = false;
    in.slotData = stored;
    in.values = values;
    in.name = "Bright Bass";
    auto r = t6::syncStatus (in);
    CHECK (r.state == t6::SyncState::InSync && r.text == "Saved in U 012");
    in.values = edited;
    r = t6::syncStatus (in);
    CHECK (r.state == t6::SyncState::Edited && r.text == "Edited - 1 change");
    CHECK (r.tooltip.find (table[k].name) != std::string::npos);

    // saved to the PC with the edit: PC only; renamed in that file too: still PC only
    auto pc = stored;
    const auto at = (size_t) table[k].nrpn;
    pc[at] = (uint8_t) (table[k].nrpn2 < 0 ? edited[k] : pc[at]);
    if (table[k].nrpn2 < 0 && ! table[k].inverted)
    {
        in.pcData = pc;
        CHECK (t6::syncStatus (in).state == t6::SyncState::PcOnly);
        t6::setProgramName (in.pcData, "Brighter Bass");
        in.name = "Brighter Bass";
        r = t6::syncStatus (in);
        CHECK (r.state == t6::SyncState::PcOnly && r.diff.count() == 2);
    }

    // the synth does not send knob moves: the state stays, the text says it is not sure
    in.pcData.clear();
    in.name = "Bright Bass";
    in.values = values;
    in.seesSynthKnobs = false;
    r = t6::syncStatus (in);
    CHECK (r.state == t6::SyncState::InSync && r.text == "Saved in U 012 ?");

    // FetchQueue.pending: the slot being waited for, and queued ones
    t6::FetchQueue q;
    q.enqueue (0, 12);
    q.enqueue (0, 13);
    CHECK (q.pending (0, 12) && q.pending (0, 13) && ! q.pending (0, 14));
    q.poll (0.0);
    CHECK (q.pending (0, 12));
    q.received (0, 12);
    CHECK (! q.pending (0, 12) && q.pending (0, 13));
}

static void testSaveCore()
{
    std::vector<uint8_t> prog (kProgramBytes);
    for (size_t i = 0; i < prog.size(); ++i)
        prog[i] = (uint8_t) ((i * 37) & 0xFF);
    setProgramName (prog, std::string ("Bright Bass") + (char) 1 + " with a very long name");
    CHECK (programName (prog) == "Bright Bass  with a");
    setProgramName (prog, "Pad");
    CHECK (programName (prog) == "Pad" && prog[kNameOffset + 3] == ' ' && prog[kNameOffset + 19] == ' ');

    const auto pd = programDump (3, 42, prog);
    CHECK (pd.size() == 6 + kProgramPackedBytes + 1 && pd[3] == 0x02 && pd[4] == 3 && pd[5] == 42 && pd.back() == 0xF7);
    const auto sp = parseStoredProgram (pd.data(), pd.size(), kTrigon6Id);
    CHECK (sp && sp->bank == 3 && sp->program == 42 && sp->data == prog && sp->name == "Pad");
    const auto eb = editBufferDump (prog);
    CHECK (eb.size() == 4 + kProgramPackedBytes + 1 && eb[3] == 0x03);
    const auto back = parseProgramDump (eb.data() + 1, eb.size() - 2);
    CHECK (back && *back == prog);
    CHECK (programDump (0, 0, std::vector<uint8_t> (10)).empty());
    for (auto b : pd)
        if (b != 0xF0 && b != 0xF7)
            CHECK (b < 0x80);

    CHECK (isWritableSlot (0, 0) && isWritableSlot (4, 99));
    // every guessed type is in the list the user picks from
    for (const char* n : { "Sub Bass", "Rhodes", "Brass Section", "Strings", "Vox", "Warm Pad", "Solo Lead", "Koto",
                           "Bells", "Arp One", "Kick", "Siren" })
    {
        const auto t = programType (n);
        CHECK (! t.empty() && std::find (programTypes().begin(), programTypes().end(), t) != programTypes().end());
    }
    CHECK (programTypes().back() == "Other");
    CHECK (shownType ("", "Sub Bass") == "Bass" && shownType ("Lead", "Sub Bass") == "Lead" && shownType ("", "Mystery") == "Other");
    CHECK (! isWritableSlot (5, 0) && ! isWritableSlot (9, 99) && ! isWritableSlot (0, 100) && ! isWritableSlot (-1, 0));
    auto other = prog;
    other[200] ^= 1;
    other[300] ^= 2;
    CHECK (countDifferences (prog, prog) == 0 && countDifferences (prog, other) == 2 && countDifferences (prog, {}) == -1);
}

static void testPacked()
{
    CHECK (packedSize (1024) == 1171);
    CHECK (packedSize (7) == 8);
    CHECK (packedSize (8) == 10);

    // the manual's example packet: A..G with only A and G having the top bit
    const uint8_t data[7] = { 0x81, 0x02, 0x03, 0x04, 0x05, 0x06, 0xFF };
    auto p = packMsBit (data, 7);
    CHECK (p.size() == 8);
    CHECK (p[0] == 0x41);  // 0 G7 F7 E7 D7 C7 B7 A7 = 0100 0001
    CHECK (p[1] == 0x01 && p[7] == 0x7F);
    auto u = unpackMsBit (p.data(), p.size());
    CHECK (u == std::vector<uint8_t> (data, data + 7));

    // a full program: 1024 bytes of every value round-trip through 1171 7-bit bytes
    std::vector<uint8_t> prog (1024);
    for (size_t i = 0; i < prog.size(); ++i)
        prog[i] = (uint8_t) (i * 37 + 11);
    auto packed = packMsBit (prog.data(), prog.size());
    CHECK (packed.size() == 1171);
    bool sevenBit = true;
    for (auto b : packed)
        sevenBit = sevenBit && b < 0x80;
    CHECK (sevenBit);
    CHECK (unpackMsBit (packed.data(), packed.size()) == prog);
}

static void testSysex()
{
    CHECK ((requestEditBuffer() == std::vector<uint8_t> { 0xF0, 0x01, 0x39, 0x06, 0xF7 }));
    CHECK ((requestProgram (9, 99) == std::vector<uint8_t> { 0xF0, 0x01, 0x39, 0x05, 9, 99, 0xF7 }));

    std::vector<uint8_t> prog (1024);
    for (size_t i = 0; i < prog.size(); ++i)
        prog[i] = (uint8_t) (255 - i % 256);
    auto packed = packMsBit (prog.data(), prog.size());

    std::vector<uint8_t> edit { 0xF0, 0x01, 0x39, 0x03 };
    edit.insert (edit.end(), packed.begin(), packed.end());
    edit.push_back (0xF7);
    auto got = parseProgramDump (edit.data(), edit.size());
    CHECK (got && *got == prog);
    // without F0/F7, as JUCE's getSysExData() gives it
    got = parseProgramDump (edit.data() + 1, edit.size() - 2);
    CHECK (got && *got == prog);

    std::vector<uint8_t> stored { 0xF0, 0x01, 0x39, 0x02, 3, 42 };
    stored.insert (stored.end(), packed.begin(), packed.end());
    stored.push_back (0xF7);
    got = parseProgramDump (stored.data(), stored.size());
    CHECK (got && *got == prog);

    edit[2] = 0x2C;  // another DSI synth
    CHECK (! parseProgramDump (edit.data(), edit.size()));
    std::vector<uint8_t> shortMsg { 0xF0, 0x01, 0x39, 0x03, 0x00, 0xF7 };
    CHECK (! parseProgramDump (shortMsg.data(), shortMsg.size()));
}

static void testSendQueue()
{
    SendQueue::Config cfg;
    cfg.minIntervalMs = 20.0;
    cfg.maxPerSecond = 100.0;
    cfg.burst = 4.0;
    SendQueue q (3, cfg);
    std::vector<std::pair<size_t, int>> out;
    auto run = [&] (double t) { out.clear(); q.collect (t, [&] (size_t i, int v) { out.push_back ({ i, v }); }); };

    for (size_t i = 0; i < 3; ++i)
        q.markSynced (i, 10);
    run (0);
    CHECK (out.empty());  // nothing changed

    q.setValue (1, 11);
    run (1);
    CHECK (out.size() == 1 && out[0].first == 1 && out[0].second == 11);
    run (2);
    CHECK (out.empty());  // sent once only

    // a knob turning fast: within 20 ms only the latest value goes out, once
    q.setValue (1, 12);
    run (5);
    CHECK (out.empty());
    q.setValue (1, 13);
    run (21);
    CHECK (out.size() == 1 && out[0].second == 13);

    // a value the synth sent itself is not echoed back
    q.markSynced (2, 50);
    run (100);
    CHECK (out.empty());

    // Send all, limited by the burst (4) and then the rate (100/s = 1 per 10 ms)
    SendQueue big (10, cfg);
    for (size_t i = 0; i < 10; ++i)
        big.markSynced (i, (int) i);
    big.markAllDirty();
    size_t total = 0;
    big.collect (0, [&] (size_t, int) { ++total; });
    CHECK (total == 4);
    big.collect (10, [&] (size_t, int) { ++total; });
    CHECK (total == 5);
    for (double t = 20; t <= 200; t += 10)
        big.collect (t, [&] (size_t, int) { ++total; });
    CHECK (total == 10);
    CHECK (! big.pending());
}

static void testTable()
{
    const auto& t = paramTable();
    CHECK (t.size() > 90);
    std::set<int> nrpns;
    std::set<std::string> ids;
    for (const auto& p : t)
    {
        CHECK (nrpns.insert (p.nrpn).second);
        CHECK (ids.insert (p.id).second);
        CHECK (p.min < p.max);
        CHECK (p.max <= 16383);
        if (p.kind == Kind::Choice)
            CHECK ((int) p.choices.size() == p.max - p.min + 1);
        if (p.kind == Kind::Bipolar)
            CHECK (p.center > p.min && p.center < p.max);
        if (p.kind == Kind::Bool)
            CHECK (p.min == 0 && p.max == 1);
        CHECK (indexOfNrpn (p.nrpn) >= 0 && t[(size_t) indexOfNrpn (p.nrpn)].nrpn == p.nrpn);
    }
    CHECK (indexOfNrpn (37) == -1);  // a gap in the numbering

    // 2 Pole: the light is on when the synth's NRPN 33 is 0
    const auto& pole = t[(size_t) indexOfNrpn (33)];
    CHECK (pole.inverted && nrpnValues (pole, 1)[0].value == 0 && nrpnValues (pole, 0)[0].value == 1);
    CHECK (valueFromBytes (pole, 0, 0) == 1 && valueAfterNrpn (pole, 0, 33, 1) == 0);
    // Osc 2/3 Pitch: two bytes in a program dump (low at 3/4, high at 38/39)
    {
        std::vector<uint8_t> prog (kProgramBytes);
        prog[3] = 188; prog[38] = 2;   // 700, the centre
        prog[4] = 120; prog[39] = 5;   // 1400, the top
        prog[5] = 1;                   // Osc 1 Saw on
        prog[33] = 0;                  // 2 Pole: 0 = on
        CHECK (valueFromProgram (t[(size_t) indexOfNrpn (3)], prog) == 700);
        CHECK (valueFromProgram (t[(size_t) indexOfNrpn (4)], prog) == 1400);
        CHECK (valueFromProgram (t[(size_t) indexOfNrpn (5)], prog) == 1);
        CHECK (valueFromProgram (t[(size_t) indexOfNrpn (33)], prog) == 1);
        CHECK (valueFromProgram (t[(size_t) indexOfNrpn (3)], std::vector<uint8_t> (10)) == -1);
        for (const auto& p : t)
            CHECK (valueFromProgram (p, prog) >= 0);  // every parameter can be read from a dump now
    }

    // Unison Voices: 0-6 shows as 1-6, Chord
    const auto& uni = t[(size_t) indexOfNrpn (96)];
    CHECK (formatValue (uni, 0) == "1" && formatValue (uni, 5) == "6" && formatValue (uni, 6) == "Chord");

    // Osc 3 saw: one control on NRPN 7 (on) and 107 (rising), as the synth's button steps it
    const auto& saw = t[(size_t) indexOfNrpn (7)];
    CHECK (indexOfNrpn (107) == indexOfNrpn (7) && saw.nrpn2 == 107 && saw.kind == Kind::Choice);
    CHECK (formatValue (saw, 0) == "Off" && formatValue (saw, 1) == "Saw" && formatValue (saw, 2) == "Ramp");
    auto sends = [&] (int v) { std::vector<std::pair<int, int>> r; for (auto nv : nrpnValues (saw, v)) r.push_back ({ nv.nrpn, nv.value }); return r; };
    CHECK ((sends (0) == std::vector<std::pair<int, int>> { { 7, 0 }, { 107, 0 } }));
    CHECK ((sends (1) == std::vector<std::pair<int, int>> { { 7, 1 }, { 107, 0 } }));
    CHECK ((sends (2) == std::vector<std::pair<int, int>> { { 7, 1 }, { 107, 1 } }));
    // the synth's presses: off -> 7 = 1 -> saw; 107 = 1 -> ramp; 7 = 0 -> off; then 107 = 0 keeps it off
    int v = 0;
    v = valueAfterNrpn (saw, v, 7, 1);   CHECK (v == 1);
    v = valueAfterNrpn (saw, v, 107, 1); CHECK (v == 2);
    v = valueAfterNrpn (saw, v, 7, 0);   CHECK (v == 0);
    v = valueAfterNrpn (saw, v, 107, 0); CHECK (v == 0);
    CHECK (valueAfterNrpn (saw, 0, 107, 1) == 0);  // ramp alone does not switch the saw on
    CHECK (valueFromBytes (saw, 1, 1) == 2 && valueFromBytes (saw, 1, 0) == 1 && valueFromBytes (saw, 0, 1) == 0);
    // a plain parameter: one NRPN, the value as it is
    const auto& cutoff = t[(size_t) indexOfNrpn (29)];
    CHECK (nrpnValues (cutoff, 100).size() == 1 && nrpnValues (cutoff, 100)[0].nrpn == 29);
    CHECK (valueAfterNrpn (cutoff, 5, 29, 100) == 100 && valueFromBytes (cutoff, 42, 0) == 42);

    // ranges from the manual (1.1) that the plugin must get right
    CHECK (t[(size_t) indexOfNrpn (29)].max == 164);   // cutoff
    CHECK (t[(size_t) indexOfNrpn (36)].max == 254);   // filter env amount
    CHECK (t[(size_t) indexOfNrpn (3)].max == 1400);   // osc 2 pitch
    CHECK (t[(size_t) indexOfNrpn (98)].min == 30 && t[(size_t) indexOfNrpn (98)].max == 250);  // BPM
    CHECK (formatValue (t[(size_t) indexOfNrpn (36)], 0) == "-127");
    CHECK (formatValue (t[(size_t) indexOfNrpn (36)], 254) == "+127");
    CHECK (formatValue (t[(size_t) indexOfNrpn (50)], 13) == "Spring");
    // found from the synth's own NRPN output
    CHECK (t[(size_t) indexOfNrpn (22)].max == 127);   // noise
    CHECK (t[(size_t) indexOfNrpn (28)].max == 254 && t[(size_t) indexOfNrpn (28)].center == 127);  // feedback<>drive
    CHECK (formatValue (t[(size_t) indexOfNrpn (67)], 4) == "Random");
    // checked on the synth with the NRPN monitor (2026-11-05)
    CHECK (formatValue (t[(size_t) indexOfNrpn (97)], 1) == "High" && formatValue (t[(size_t) indexOfNrpn (97)], 3) == "Low R");
    CHECK (formatValue (t[(size_t) indexOfNrpn (2)], 0) == "Lo" && formatValue (t[(size_t) indexOfNrpn (2)], 5) == "+2");
    CHECK (t[(size_t) indexOfNrpn (28)].min == 0 && t[(size_t) indexOfNrpn (28)].max == 254);

    // the panel shows every parameter exactly once and names nothing unknown
    std::map<std::string, int> onPanel;
    for (const auto& s : t6ui::panelLayout())
        for (const auto& row : s.rows)
            for (const auto& cell : row)
                if (! cell.empty() && cell[0] != '?')
                    ++onPanel[cell];
    for (const auto& p : t)
    {
        CHECK (onPanel[p.id] == 1);
        if (onPanel[p.id] != 1)
            std::printf ("  panel count for %s: %d\n", p.id, onPanel[p.id]);
    }
    CHECK (onPanel.size() == t.size());
}

static void testSysexReplies()
{
    // device inquiry reply: F0 7E 7F 06 02 01 39 01 00 00 <0jjj nnnn> F7 -> version 1.2
    const std::vector<uint8_t> inq { 0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x01, 0x39, 0x01, 0x00, 0x00, 0x21, 0xF7 };
    auto info = parseDeviceInquiryReply (inq.data(), inq.size());
    CHECK (info && info->isTrigon6 && info->versionMajor == 1 && info->versionMinor == 2);
    CHECK (info->raw == "7E 7F 06 02 01 39 01 00 00 21");
    CHECK (info->manufacturer == 0x01 && info->family == 0x39);
    CHECK (toHex (inq.data(), inq.size(), 3) == "F0 7E 7F ...");
    // another model byte: requests and parsing follow the id they are given
    CHECK ((requestGlobals (0x3A) == std::vector<uint8_t> { 0xF0, 0x01, 0x3A, 0x0E, 0xF7 }));
    CHECK (classifySysex (inq.data(), inq.size()) == SysexKind::DeviceInquiryReply);
    CHECK (classifySysex (inq.data() + 1, inq.size() - 2) == SysexKind::DeviceInquiryReply);  // JUCE strips F0/F7

    // global dump: 24 values, one byte each (the start is what a Trigon-6 sent: 29 bytes in all)
    std::vector<uint8_t> gd { 0xF0, 0x01, 0x39, 0x0F, 0x0C, 0x32, 0x00, 0x03, 0x00, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00, 0x02 };
    for (int i = 13; i < 24; ++i)
        gd.push_back ((uint8_t) i);
    gd.push_back (0xF7);
    CHECK (gd.size() == 29);
    auto g = parseGlobalDump (gd.data(), gd.size());
    CHECK (g && g->size() == 24 && (*g)[0] == 12 && (*g)[1] == 50 && (*g)[8] == 1 && (*g)[23] == 23);
    CHECK (parseGlobalDump (gd.data() + 1, gd.size() - 2));  // JUCE strips F0/F7
    CHECK (classifySysex (gd.data(), gd.size()) == SysexKind::GlobalDump);
    gd.erase (gd.begin() + 5);
    CHECK (! parseGlobalDump (gd.data(), gd.size()));

    const auto rg = requestGlobals();
    CHECK (rg == (std::vector<uint8_t> { 0xF0, 0x01, 0x39, 0x0E, 0xF7 }));
    const std::vector<uint8_t> eb { 0xF0, 0x01, 0x39, 0x03, 0x00, 0xF7 };
    CHECK (classifySysex (eb.data(), eb.size()) == SysexKind::EditBufferDump);
}

static void testGlobals()
{
    for (const auto& g : globalTable())
    {
        const auto info = globalInfo (g);
        CHECK (! info.about.empty());
        // a value list names every value the synth shows (Alt Tuning's 1-16 share one line)
        if (! g.values.empty() && g.values.size() < 6)
            CHECK (info.values.size() >= g.values.size() - 1);
    }
    std::set<std::pair<int, int>> places;
    for (const auto& gl : globalTable())
    {
        CHECK (places.insert ({ gl.row, gl.button }).second);
        CHECK (gl.button >= 0 && gl.button <= 9 && (gl.row == 0 || gl.row == 1));
    }
    CHECK (places.size() == 20);
    CHECK (std::string (globalAt (0, 8)->name) == "MIDI SysEx");
    CHECK (std::string (globalAt (0, 9)->name) == "MIDI Out");
    const auto* xmit = globalByNrpn (kGlobalParamXmit);
    CHECK (xmit && globalValueName (*xmit, 2) == "NR" && globalValueName (*xmit, 9) == "raw 9");
    CHECK (globalValueName (*globalByNrpn (1024), 15) == "+3");   // transpose: raw 12 = 0
    CHECK (globalValueName (*globalByNrpn (kGlobalMidiChannel), 0) == "All");
}

static LinkSnapshot connectedSnapshot()
{
    LinkSnapshot s;
    s.nowMs = 10000;
    s.pluginChannel = 1;
    s.inName = s.outName = "Trigon 6 Module";
    s.inOpen = s.outOpen = true;
    s.probeSentMs = 5000;
    s.inquiryMs = 5100;
    s.lastAnyInMs = 5600;
    s.device.isTrigon6 = true;
    s.globalsMs = 5300;
    s.editBufferMs = 5600;
    s.editBufferOk = true;
    s.editBufferSize = 1171;
    // what the synth sent while its globals were changed: Xmit NR, Rcv NR, SysEx USB, Out USB, channel 1
    for (auto [n, v] : std::vector<std::pair<int, int>> { { 1029, 2 }, { 1030, 2 }, { 1032, 1 }, { 1033, 1 }, { 1026, 1 } })
    {
        s.globalNrpn[n] = v;
        s.globalNrpnMs[n] = 6000;
    }
    s.globalsDump.assign (kGlobalCount, 0);
    return s;
}

static LinkSnapshot portsOnly()
{
    LinkSnapshot s;
    s.nowMs = 1000;
    s.inName = s.outName = "Trigon 6 Module";
    s.inOpen = s.outOpen = true;
    return s;
}

static const CheckItem& step (const Report& r, size_t i) { return r.steps[i]; }
enum { kIn, kOut, kRx, kTx, kSysex, kKnobs, kRcv, kChannel };

static void testConnection()
{
    auto s = connectedSnapshot();
    auto rep = evaluate (s);
    CHECK (rep.allOk && rep.verdict == "Connected." && rep.highlightButton < 0);
    CHECK (rep.steps.size() == 8);

    {
        // SysEx is USB (the synth said so) and the inquiry came back, but no dump: the request was ignored
        auto q = connectedSnapshot();
        q.globalsMs = q.editBufferMs = -1;
        q.globalsDump.clear();
        q.device.isTrigon6 = false;
        q.device.manufacturer = 0x01;
        q.device.family = 0x3A;
        q.device.raw = "7E 00 06 02 01 3A";
        const auto r = evaluate (q);
        CHECK (step (r, kSysex).health == Health::Bad);
        CHECK (step (r, kSysex).detail.find ("did not answer") != std::string::npos);
        CHECK (! r.warnings.empty() && r.warnings[0].title == "Unexpected model ID");
    }

    // priority: the synth's NRPN, then proof, then the dump
    CHECK (readGlobal (s, 1032).source == Source::Nrpn);
    s.globalNrpn.erase (1032);
    CHECK (readGlobal (s, 1032).source == Source::Proof && readGlobal (s, 1032).raw == 1);  // a dump arrived: USB
    s.globalNrpn.erase (1030);  // Param Rcv now only in the dump, which says 0
    CHECK (readGlobal (s, 1030).source == Source::Dump);
    rep = evaluate (s);
    CHECK (step (rep, kRcv).health == (kDumpOrderVerified ? Health::Bad : Health::Warn));
    CHECK (! rep.allOk && rep.verdict.find ("Param Rcv") != std::string::npos);
    CHECK (rep.highlightRow == 0 && rep.highlightButton == 6);

    // nothing at all comes back: MIDI Out is the likely cause
    s = portsOnly();
    s.probeSentMs = 1000;
    s.nowMs = 1000 + kProbeTimeoutMs + 1;
    rep = evaluate (s);
    CHECK (step (rep, kRx).health == Health::Bad);
    CHECK (rep.highlightRow == 0 && rep.highlightButton == 9);
    CHECK (step (rep, kSysex).blocked && step (rep, kChannel).blocked);
    s.nowMs = 1100;  // still waiting
    CHECK (step (evaluate (s), kRx).health == Health::Unknown);

    // knob moves arrive but no SysEx answer: the note test tells "SysEx off" from "nothing reaches the synth"
    s = portsOnly();
    s.probeSentMs = 1000;
    s.nowMs = 5000;
    s.lastAnyInMs = s.lastNrpnMs = 2000;
    s.nrpnInCount = 3;
    s.lastNrpnChannel = 1;
    rep = evaluate (s);
    CHECK (step (rep, kRx).health == Health::Ok && step (rep, kTx).health == Health::Warn && rep.offerNoteTest);
    CHECK (step (rep, kSysex).health == Health::Warn);
    CHECK (readGlobal (s, 1033).source == Source::Proof && readGlobal (s, 1029).text == "NR or nAS");
    s.noteTest = NoteTest::Heard;
    rep = evaluate (s);
    CHECK (step (rep, kTx).health == Health::Ok && step (rep, kSysex).health == Health::Bad);
    CHECK (rep.highlightButton == 8);  // MIDI SysEx
    s.noteTest = NoteTest::NotHeard;
    CHECK (step (evaluate (s), kTx).health == Health::Bad);

    // panel moves come as CC: Param Xmit is CC
    s = connectedSnapshot();
    s.globalNrpn.erase (1029);
    s.paramCcInCount = 4;
    CHECK (readGlobal (s, 1029).raw == 1 && step (evaluate (s), kKnobs).health == Health::Bad);

    // the synth is on channel 5 (seen), the plugin on 1
    s = connectedSnapshot();
    s.globalNrpn.erase (1026);
    s.nrpnInCount = 1;
    s.lastNrpnChannel = 5;
    rep = evaluate (s);
    CHECK (! rep.allOk && rep.suggestChannel == 5 && step (rep, kChannel).health == Health::Bad);
    s.globalNrpn[1026] = 0;  // the synth says All
    CHECK (evaluate (s).allOk);

    // port busy: the verdict is about the port, later steps wait
    s = connectedSnapshot();
    s.inOpen = false;
    rep = evaluate (s);
    CHECK (! rep.allOk && rep.verdict.find ("Input port") == 0 && step (rep, kRx).blocked);
    s = connectedSnapshot();
    s.inName.clear();
    s.inOpen = false;
    CHECK (evaluate (s).verdictFix.find ("USB cable") != std::string::npos);

    // through the host nothing can be proven by round trip; it is not required then
    s = connectedSnapshot();
    s.hostOutput = true;
    s.outOpen = false;
    s.outName.clear();
    s.inquiryMs = s.globalsMs = s.editBufferMs = -1;
    s.probeSentMs = -1;
    rep = evaluate (s);
    CHECK (rep.allOk && rep.offerNoteTest);

    // warnings
    s = connectedSnapshot();
    s.echoCount = 3;
    s.globalNrpn[1027] = 2;  // MIDI Clock In
    s.globalNrpnMs[1027] = 6000;
    rep = evaluate (s);
    CHECK (rep.allOk && rep.warnings.size() == 2);

    CHECK (! isParameterCc (1) && ! isParameterCc (99) && ! isParameterCc (64) && ! isParameterCc (123));
    CHECK (isParameterCc (102) && isParameterCc (74));
}

static void testPortMatch()
{
    CHECK (normalizePortName ("Trigon 6 Module") == "trigon 6 module");
    CHECK (normalizePortName ("2- Trigon 6 Module") == "trigon 6 module");
    CHECK (normalizePortName ("Trigon 6 Module [0]") == "trigon 6 module");
    CHECK (normalizePortName ("Trigon 6 Module 1") == "trigon 6 module");
    CHECK (normalizePortName ("  MIDIIN2  (Trigon 6) ") == "midiin2 (trigon 6)");

    const std::vector<std::string> ports { "Microsoft GS Wavetable Synth", "3- Trigon 6 Module" };
    auto p = pickPort (ports, "");
    CHECK (p.index == 1 && ! p.savedMissing);
    p = pickPort (ports, "Trigon 6 Module");  // saved before the synth moved to another USB socket
    CHECK (p.index == 1 && ! p.savedMissing);
    p = pickPort (ports, "Microsoft GS Wavetable Synth");
    CHECK (p.index == 0);
    p = pickPort (ports, "loopMIDI Port");  // gone: fall back to the Trigon, and say so
    CHECK (p.index == 1 && p.savedMissing);
    p = pickPort ({ "Microsoft GS Wavetable Synth" }, "");
    CHECK (p.index == -1);
}

static std::vector<uint8_t> storedDump (int bank, int program, const std::vector<uint8_t>& data)
{
    auto packed = packMsBit (data.data(), data.size());
    std::vector<uint8_t> m { 0xF0, 0x01, 0x39, 0x02, (uint8_t) bank, (uint8_t) program };
    m.insert (m.end(), packed.begin(), packed.end());
    m.push_back (0xF7);
    return m;
}

static void testPresets()
{
    // name at the guessed place, padded with spaces
    std::vector<uint8_t> data (1024, 0x7F);
    const std::string name = "Bright Bass";
    for (size_t i = 0; i < kNameLength; ++i)
        data[kNameOffset + i] = (uint8_t) (i < name.size() ? name[i] : ' ');
    CHECK (programName (data) == "Bright Bass");
    CHECK (kNameOffset == 116);
    // not text there: no name, even when other bytes look like text (sequencer notes are 60 = '<')
    std::vector<uint8_t> other (1024, 60);
    other[kNameOffset] = 200;
    CHECK (programName (other) == "");
    CHECK (programName (std::vector<uint8_t> (1024, 0)) == "");

    CHECK (programType ("Bamboo Bass") == "Bass");
    CHECK (programType ("Bass N Keys Please") == "Bass");
    CHECK (programType ("Classic EP") == "Keys");
    CHECK (programType ("Brassy Pad") == "Brass");
    CHECK (programType ("Low Atmos Pad") == "Pad");
    CHECK (programType ("Stringy Thingie") == "Strings");
    CHECK (programType ("Seq Syn Classic") == "Arp/Seq");
    CHECK (programType ("Keysync") == "Keys");
    CHECK (programType ("Why Now") == "");
    CHECK (programType ("Ripple") == "");  // "ep" only as a whole word

    CHECK (programLabel (0, 23) == "U 023");
    CHECK (programLabel (4, 99) == "U 499");
    CHECK (programLabel (5, 0) == "F 000");
    CHECK (programLabel (7, 12) == "F 212");
    CHECK (presetFileName (0, 23, "Bass: A/B?") == "U023 Bass_ A_B_.syx");
    CHECK (presetFileName (5, 1, "") == "F001.syx");
    CHECK (sanitizeFileName ("End. ") == "End");
    CHECK (cacheFileName (3, 42) == "b3-p042.syx");
    auto bp = parseCacheFileName ("b3-p042.syx");
    CHECK (bp && bp->first == 3 && bp->second == 42);
    CHECK (! parseCacheFileName ("b11-p042.syx"));
    CHECK (! parseCacheFileName ("U023 Bass.syx"));

    auto msg = storedDump (2, 7, data);
    auto sp = parseStoredProgram (msg.data(), msg.size(), 0x39);
    CHECK (sp && sp->bank == 2 && sp->program == 7 && sp->name == "Bright Bass" && sp->sysex == msg && sp->data == data);
    auto sp2 = parseStoredProgram (msg.data() + 1, msg.size() - 2, 0x39);  // JUCE strips F0/F7
    CHECK (sp2 && sp2->sysex == msg);
    CHECK (! parseStoredProgram (msg.data(), msg.size(), 0x2D));

    // cache status: new -> read; same again -> read; different -> updated, and it stays updated
    PresetEntry e;
    CHECK (storeFetched (e, *sp) && e.status == PresetStatus::Fetched);
    CHECK (! storeFetched (e, *sp) && e.status == PresetStatus::Fetched);
    PresetEntry cached { PresetStatus::Cached, "Bright Bass", msg };
    CHECK (! storeFetched (cached, *sp) && cached.status == PresetStatus::Fetched);
    data[0] = 1;
    auto changedMsg = storedDump (2, 7, data);
    auto sp3 = parseStoredProgram (changedMsg.data(), changedMsg.size(), 0x39);
    CHECK (storeFetched (cached, *sp3) && cached.status == PresetStatus::Updated);
    CHECK (! storeFetched (cached, *sp3) && cached.status == PresetStatus::Updated);

    // fetch queue: one at a time, a retry, then failure
    FetchQueue q (1000.0, 1);
    q.enqueue (0, 0);
    q.enqueue (0, 1);
    q.enqueue (0, 1);  // already waiting
    CHECK (q.total() == 2);
    auto st = q.poll (0);
    CHECK (st.kind == FetchQueue::Step::Send && st.program == 0);
    CHECK (q.poll (500).kind == FetchQueue::Step::None);  // still waiting
    CHECK (! q.received (0, 1));                          // not the one asked for
    CHECK (q.received (0, 0) && q.finished() == 1);
    st = q.poll (600);
    CHECK (st.kind == FetchQueue::Step::Send && st.program == 1);
    st = q.poll (1700);
    CHECK (st.kind == FetchQueue::Step::Send && st.program == 1);  // retry
    st = q.poll (2800);
    CHECK (st.kind == FetchQueue::Step::Failed && st.program == 1 && q.failed() == 1);
    CHECK (! q.busy() && q.finished() == 2);
    q.enqueue (1, 5);
    q.enqueue (1, 9, true);  // in front
    CHECK (q.total() == 2 && q.poll (0).program == 9);
    q.cancel();
    CHECK (! q.busy() && q.poll (0).kind == FetchQueue::Step::None);
}

// The preset marks: which parameters differ, and the tooltip text
static void testPresetDiff()
{
    const auto& table = paramTable();
    std::vector<int> preset, now;
    for (const auto& p : table)
        preset.push_back (p.kind == Kind::Choice ? p.min : p.min);
    now = preset;
    CHECK (differingParams ({}, now).empty());   // no preset: nothing differs
    CHECK (differingParams (preset, now).empty());
    const int cutoff = indexOfNrpn (29), reson = indexOfNrpn (30);
    CHECK (cutoff >= 0 && reson >= 0);
    now[(size_t) cutoff] = 100;
    now[(size_t) reson] = 3;
    const auto d = differingParams (preset, now);
    CHECK (d.size() == 2 && d[0] == (size_t) std::min (cutoff, reson) && d[1] == (size_t) std::max (cutoff, reson));
    now[(size_t) cutoff] = preset[(size_t) cutoff];
    CHECK (differingParams (preset, now).size() == 1);
    // sizes that do not match: only the common part is compared
    CHECK (differingParams (preset, std::vector<int> (3, 99)).size() <= 3);

    const auto& c = table[(size_t) cutoff];
    const std::string t = diffText (c, 80, 100);
    CHECK (t == "preset 80, now 100, \xCE\x94 +20");
    CHECK (diffText (c, 100, 80) == "preset 100, now 80, \xCE\x94 -20");
    CHECK (diffText (c, 99, 100) == "preset 99, now 100, \xCE\x94 +1");
    // a button or a choice: no delta
    for (const auto& p : table)
        if (p.kind == Kind::Bool)
        {
            const auto b = diffText (p, 0, 1);
            CHECK (b.find ("\xCE\x94") == std::string::npos);
            CHECK (b.find ("preset") == 0);
            break;
        }
    for (const auto& p : table)
        if (p.kind == Kind::Choice)
        {
            CHECK (diffText (p, p.min, p.min + 1).find ("\xCE\x94") == std::string::npos);
            break;
        }
    // a bipolar knob shows its own scale, the delta is in steps
    for (const auto& p : table)
        if (p.kind == Kind::Bipolar)
        {
            const auto b = diffText (p, p.center, p.center + 5);
            CHECK (b.find ("\xCE\x94 +5") != std::string::npos);
            break;
        }
}

int main()
{
    testPresets();
    testSysexReplies();
    testGlobals();
    testConnection();
    testPortMatch();
    testNrpnEncode();
    testNrpnDecode();
    testPacked();
    testSaveCore();
    testSoundSync();
    testSysex();
    testSendQueue();
    testTable();
    testPresetDiff();
    if (failures)
    {
        std::printf ("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf ("all tests passed\n");
    return 0;
}
