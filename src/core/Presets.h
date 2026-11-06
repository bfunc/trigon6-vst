// Presets: the synth's stored programs. Names, labels and file names, the stored program dump,
// the cache status of each program, and the queue that fetches programs one at a time.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace t6
{

// 10 banks of 100 (all answer on a Trigon-6): banks 0-4 user (U 000-499), 5-9 factory (F 000-499).
constexpr int kBankCount = 10;
constexpr int kProgramsPerBank = 100;
constexpr int kUserBanks = 5;

// The name inside the 1024 unpacked program bytes: 20 characters at 116, padded with spaces
// (found in 1000 program dumps from a Trigon-6, 2026-11-05).
constexpr size_t kNameOffset = 116;
constexpr size_t kNameLength = 20;
std::string programName (const std::vector<uint8_t>& data);
// Puts a name into the program bytes: cut to 20, padded with spaces; characters the synth cannot show become spaces.
void setProgramName (std::vector<uint8_t>& data, const std::string& name);
// The characters a name may hold (printable ASCII).
bool isNameChar (char c);

// Factory banks (5-9) are never written.
bool isWritableSlot (int bank, int program);
// How many of the 1024 bytes differ (a program read back after writing it); -1 when the sizes differ.
int countDifferences (const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

// A type guessed from the name ("Bamboo Bass" -> "Bass"); "" when the name gives no hint.
// The synth itself stores no category.
std::string programType (const std::string& name);
// Every type programType can give, then "Other" (what the user can pick for a program without a type).
const std::vector<std::string>& programTypes();
// The type a filter sees: the one the user set, else the one from the name, else "Other".
std::string shownType (const std::string& userType, const std::string& name);

// "U 023", "F 412": how the synth shows a program.
std::string programLabel (int bank, int program);
// "U023 Bright Bass.syx": characters Windows does not allow become '_'.
std::string presetFileName (int bank, int program, const std::string& name);
std::string sanitizeFileName (const std::string& name);
// The cache keeps one file per program: "b3-p042.syx".
std::string cacheFileName (int bank, int program);
std::optional<std::pair<int, int>> parseCacheFileName (const std::string& fileName);

// A program dump as it arrived: F0 01 <id> 02 bank program <1171> F7 (F0/F7 may be missing).
struct StoredProgram
{
    int bank = -1, program = -1;
    std::vector<uint8_t> sysex;  // the whole message, F0 ... F7
    std::vector<uint8_t> data;   // 1024 unpacked bytes
    std::string name;
};
std::optional<StoredProgram> parseStoredProgram (const uint8_t* msg, size_t size, uint8_t id);

enum class PresetStatus
{
    Empty,    // never read
    Cached,   // from the cache on disk, not read from the synth this session
    Fetched,  // read from the synth this session, same as the cache (or new)
    Updated,  // read from the synth this session and different from the cache
    Error     // the synth did not answer
};
const char* statusName (PresetStatus s);

struct PresetEntry
{
    PresetStatus status = PresetStatus::Empty;
    std::string name;
    std::vector<uint8_t> sysex;
};

// Stores a dump just read from the synth. Returns true when it differs from what the entry held.
bool storeFetched (PresetEntry& entry, const StoredProgram& program);

// Fetches programs one at a time: a request goes out, the next waits for its reply or the timeout;
// a program that does not answer is asked once more, then reported as failed.
class FetchQueue
{
public:
    struct Step
    {
        enum Kind { None, Send, Failed } kind = None;
        int bank = -1, program = -1;
    };

    explicit FetchQueue (double timeoutMs = 1500.0, int retries = 1) : timeoutMs (timeoutMs), retries (retries) {}

    void enqueue (int bank, int program, bool front = false);  // a program already waiting is not added twice
    void cancel();
    Step poll (double nowMs);              // call often: what to do now
    bool received (int bank, int program);  // a dump arrived; true when it was the one being waited for

    bool busy() const { return waiting.has_value() || ! jobs.empty(); }
    int finished() const { return done; }
    int total() const { return count; }
    int failed() const { return failures; }

private:
    double timeoutMs;
    int retries;
    std::deque<std::pair<int, int>> jobs;
    std::optional<std::pair<int, int>> waiting;
    double sentMs = 0;
    int tries = 0;
    int done = 0, count = 0, failures = 0;
};

} // namespace t6
