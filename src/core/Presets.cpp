#include "Presets.h"

#include "Sysex.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace t6
{

namespace
{
bool printable (uint8_t c) { return c >= 32 && c <= 126; }

std::string trimRight (std::string s)
{
    while (! s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}
} // namespace

std::string programName (const std::vector<uint8_t>& data)
{
    if (data.size() >= kNameOffset + kNameLength)
    {
        std::string s;
        bool text = true;
        for (size_t i = 0; i < kNameLength && text; ++i)
        {
            const uint8_t c = data[kNameOffset + i];
            if (c == 0)
                break;
            text = printable (c);
            s += (char) c;
        }
        if (text)
            return trimRight (s);
    }
    return {};
}

std::string programType (const std::string& name)
{
    std::vector<std::string> words;
    std::string w;
    for (char c : name + " ")
    {
        if (std::isalnum ((unsigned char) c))
            w += (char) std::tolower ((unsigned char) c);
        else if (! w.empty())
        {
            words.push_back (w);
            w.clear();
        }
    }
    struct Type
    {
        const char* type;
        std::vector<const char*> prefixes;  // a word that starts with one of these
        std::vector<const char*> exact;     // a whole word
    };
    static const std::vector<Type> types {
        { "Bass",    { "bass", "subs" }, { "sub" } },
        { "Keys",    { "key", "piano", "clav", "organ", "drawbar", "rhodes", "wurli", "kord" }, { "ep" } },
        { "Brass",   { "brass", "sax", "horn", "trumpet" }, {} },
        { "Strings", { "string", "stwing", "violin", "cello" }, {} },
        { "Choir",   { "choir", "chorale", "vox", "voice", "vocal", "vocoder" }, {} },
        { "Pad",     { "pad", "atmos", "sweep", "swell", "drone" }, {} },
        { "Lead",    { "lead", "solo" }, {} },
        { "Pluck",   { "pluck", "koto", "biwa", "harp", "pizz" }, {} },
        { "Bell",    { "bell", "chime", "mallet", "marimba", "bonang", "gamelan" }, {} },
        { "Arp/Seq", { "arp", "seq" }, {} },
        { "Drum",    { "kick", "drum", "perc", "snare", "cowbell" }, {} },
        { "FX",      { "fx", "sci", "siren", "noise" }, {} },
    };
    for (const auto& t : types)
        for (const auto& word : words)
        {
            for (const char* p : t.prefixes)
                if (word.rfind (p, 0) == 0)
                    return t.type;
            for (const char* e : t.exact)
                if (word == e)
                    return t.type;
        }
    return {};
}

std::string programLabel (int bank, int program)
{
    char buf[16];
    std::snprintf (buf, sizeof buf, "%c %03d", bank < kUserBanks ? 'U' : 'F', (bank % kUserBanks) * kProgramsPerBank + program);
    return buf;
}

std::string sanitizeFileName (const std::string& name)
{
    std::string out;
    for (char c : name)
    {
        const auto u = (unsigned char) c;
        out += (u < 32 || std::string ("<>:\"/\\|?*").find (c) != std::string::npos) ? '_' : c;
    }
    while (! out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    return out;
}

std::string presetFileName (int bank, int program, const std::string& name)
{
    auto label = programLabel (bank, program);
    label.erase (std::remove (label.begin(), label.end(), ' '), label.end());
    const auto clean = sanitizeFileName (name);
    return label + (clean.empty() ? "" : " " + clean) + ".syx";
}

std::string cacheFileName (int bank, int program)
{
    char buf[32];
    std::snprintf (buf, sizeof buf, "b%d-p%03d.syx", bank, program);
    return buf;
}

std::optional<std::pair<int, int>> parseCacheFileName (const std::string& fileName)
{
    int bank = -1, program = -1;
    char tail[8] = {};
    if (std::sscanf (fileName.c_str(), "b%d-p%d.%4s", &bank, &program, tail) != 3 || std::string (tail) != "syx")
        return std::nullopt;
    if (bank < 0 || bank >= kBankCount || program < 0 || program >= kProgramsPerBank)
        return std::nullopt;
    return std::make_pair (bank, program);
}

std::optional<StoredProgram> parseStoredProgram (const uint8_t* msg, size_t size, uint8_t id)
{
    const uint8_t* body = msg;
    size_t n = size;
    if (n > 0 && body[0] == 0xF0)
    {
        ++body;
        --n;
    }
    if (n > 0 && body[n - 1] == 0xF7)
        --n;
    if (n < 5 || body[0] != kDsiId || body[1] != id || body[2] != 0x02)
        return std::nullopt;
    auto data = parseProgramDump (body, n, id);
    if (! data)
        return std::nullopt;
    StoredProgram p;
    p.bank = body[3];
    p.program = body[4];
    p.sysex.reserve (n + 2);
    p.sysex.push_back (0xF0);
    p.sysex.insert (p.sysex.end(), body, body + n);
    p.sysex.push_back (0xF7);
    p.data = std::move (*data);
    p.name = programName (p.data);
    return p;
}

const char* statusName (PresetStatus s)
{
    switch (s)
    {
        case PresetStatus::Empty:   return "";
        case PresetStatus::Cached:  return "cached";
        case PresetStatus::Fetched: return "read";
        case PresetStatus::Updated: return "updated";
        case PresetStatus::Error:   return "no reply";
    }
    return "";
}

bool storeFetched (PresetEntry& entry, const StoredProgram& program)
{
    const bool had = ! entry.sysex.empty();
    const bool changed = had && entry.sysex != program.sysex;
    if (changed)
        entry.status = PresetStatus::Updated;
    else if (entry.status != PresetStatus::Updated)
        entry.status = PresetStatus::Fetched;  // an earlier change this session stays visible
    entry.sysex = program.sysex;
    entry.name = program.name;
    return changed || ! had;
}

void FetchQueue::enqueue (int bank, int program, bool front)
{
    if (! busy())
        done = count = failures = 0;
    const auto job = std::make_pair (bank, program);
    if (waiting == job || std::find (jobs.begin(), jobs.end(), job) != jobs.end())
        return;
    if (front)
        jobs.push_front (job);
    else
        jobs.push_back (job);
    ++count;
}

void FetchQueue::cancel()
{
    jobs.clear();
    waiting.reset();
}

FetchQueue::Step FetchQueue::poll (double nowMs)
{
    if (waiting)
    {
        if (nowMs - sentMs < timeoutMs)
            return {};
        if (tries <= retries)
        {
            ++tries;
            sentMs = nowMs;
            return { Step::Send, waiting->first, waiting->second };
        }
        const auto job = *waiting;
        waiting.reset();
        ++done;
        ++failures;
        return { Step::Failed, job.first, job.second };
    }
    if (jobs.empty())
        return {};
    waiting = jobs.front();
    jobs.pop_front();
    tries = 1;
    sentMs = nowMs;
    return { Step::Send, waiting->first, waiting->second };
}

bool FetchQueue::received (int bank, int program)
{
    if (waiting != std::make_pair (bank, program))
        return false;
    waiting.reset();
    ++done;
    return true;
}

} // namespace t6
