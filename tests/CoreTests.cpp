// Core tests: plain C++, no framework, no JUCE. Exit code 0 = all passed.

#include "core/Nrpn.h"
#include "core/Packed.h"
#include "core/ParamTable.h"
#include "core/SendQueue.h"
#include "core/Sysex.h"
#include "ui/PanelLayout.h"

#include <cstdio>
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
    CHECK ((requestEditBuffer() == std::vector<uint8_t> { 0xF0, 0x01, 0x2D, 0x06, 0xF7 }));
    CHECK ((requestProgram (9, 99) == std::vector<uint8_t> { 0xF0, 0x01, 0x2D, 0x05, 9, 99, 0xF7 }));

    std::vector<uint8_t> prog (1024);
    for (size_t i = 0; i < prog.size(); ++i)
        prog[i] = (uint8_t) (255 - i % 256);
    auto packed = packMsBit (prog.data(), prog.size());

    std::vector<uint8_t> edit { 0xF0, 0x01, 0x2D, 0x03 };
    edit.insert (edit.end(), packed.begin(), packed.end());
    edit.push_back (0xF7);
    auto got = parseProgramDump (edit.data(), edit.size());
    CHECK (got && *got == prog);
    // without F0/F7, as JUCE's getSysExData() gives it
    got = parseProgramDump (edit.data() + 1, edit.size() - 2);
    CHECK (got && *got == prog);

    std::vector<uint8_t> stored { 0xF0, 0x01, 0x2D, 0x02, 3, 42 };
    stored.insert (stored.end(), packed.begin(), packed.end());
    stored.push_back (0xF7);
    got = parseProgramDump (stored.data(), stored.size());
    CHECK (got && *got == prog);

    edit[2] = 0x2C;  // another DSI synth
    CHECK (! parseProgramDump (edit.data(), edit.size()));
    std::vector<uint8_t> shortMsg { 0xF0, 0x01, 0x2D, 0x03, 0x00, 0xF7 };
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
    CHECK (indexOfNrpn (22) == -1);

    // ranges from the manual (1.1) that the plugin must get right
    CHECK (t[(size_t) indexOfNrpn (29)].max == 164);   // cutoff
    CHECK (t[(size_t) indexOfNrpn (36)].max == 254);   // filter env amount
    CHECK (t[(size_t) indexOfNrpn (3)].max == 1400);   // osc 2 pitch
    CHECK (t[(size_t) indexOfNrpn (98)].min == 30 && t[(size_t) indexOfNrpn (98)].max == 250);  // BPM
    CHECK (formatValue (t[(size_t) indexOfNrpn (36)], 0) == "-127");
    CHECK (formatValue (t[(size_t) indexOfNrpn (36)], 254) == "+127");
    CHECK (formatValue (t[(size_t) indexOfNrpn (50)], 13) == "Spring");

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

int main()
{
    testNrpnEncode();
    testNrpnDecode();
    testPacked();
    testSysex();
    testSendQueue();
    testTable();
    if (failures)
    {
        std::printf ("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf ("all tests passed\n");
    return 0;
}
