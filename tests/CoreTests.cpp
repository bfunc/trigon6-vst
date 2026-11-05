// Core tests: plain C++, no framework, no JUCE. Exit code 0 = all passed.

#include "core/Connection.h"
#include "core/Globals.h"
#include "core/Nrpn.h"
#include "core/Packed.h"
#include "core/PortMatch.h"
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
    CHECK (indexOfNrpn (37) == -1);  // a gap in the numbering

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
    // device inquiry reply: F0 7E 7F 06 02 01 2D 01 00 00 <0jjj nnnn> F7 -> version 1.2
    const std::vector<uint8_t> inq { 0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x01, 0x2D, 0x01, 0x00, 0x00, 0x21, 0xF7 };
    auto info = parseDeviceInquiryReply (inq.data(), inq.size());
    CHECK (info && info->isTrigon6 && info->versionMajor == 1 && info->versionMinor == 2);
    CHECK (info->raw == "7E 7F 06 02 01 2D 01 00 00 21");
    CHECK (info->manufacturer == 0x01 && info->family == 0x2D);
    // another model byte: requests and parsing follow the id they are given
    CHECK ((requestGlobals (0x3A) == std::vector<uint8_t> { 0xF0, 0x01, 0x3A, 0x0E, 0xF7 }));
    CHECK (classifySysex (inq.data(), inq.size()) == SysexKind::DeviceInquiryReply);
    CHECK (classifySysex (inq.data() + 1, inq.size() - 2) == SysexKind::DeviceInquiryReply);  // JUCE strips F0/F7

    // global dump: 25 values as LS, MS nibbles
    std::vector<uint8_t> gd { 0xF0, 0x01, 0x2D, 0x0F };
    for (int i = 0; i < 25; ++i)
    {
        const int v = i == 2 ? 0x1F : i;  // value 31 at index 2 -> nibbles F, 1
        gd.push_back ((uint8_t) (v & 0x0F));
        gd.push_back ((uint8_t) (v >> 4));
    }
    gd.push_back (0xF7);
    auto g = parseGlobalDump (gd.data(), gd.size());
    CHECK (g && g->size() == 25 && (*g)[2] == 31 && (*g)[24] == 24);
    CHECK (classifySysex (gd.data(), gd.size()) == SysexKind::GlobalDump);
    gd.erase (gd.begin() + 5);
    CHECK (! parseGlobalDump (gd.data(), gd.size()));

    const auto rg = requestGlobals();
    CHECK (rg == (std::vector<uint8_t> { 0xF0, 0x01, 0x2D, 0x0E, 0xF7 }));
    const std::vector<uint8_t> eb { 0xF0, 0x01, 0x2D, 0x03, 0x00, 0xF7 };
    CHECK (classifySysex (eb.data(), eb.size()) == SysexKind::EditBufferDump);
}

static void testGlobals()
{
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
    CHECK (globalValueName (*globalByNrpn (1025), 15) == "+3");   // transpose: raw 12 = 0
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
    s.globalsDump.assign (25, 0);
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

int main()
{
    testSysexReplies();
    testGlobals();
    testConnection();
    testPortMatch();
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
