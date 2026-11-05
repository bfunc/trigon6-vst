// Decides which parameters go out as NRPN and when: only values that differ from what the synth last got,
// at most one message per parameter every `minIntervalMs` (a turning knob sends its latest value, not every step),
// and at most `maxPerSecond` messages in total (token bucket), so a burst never floods a 31.25 kbaud DIN link.
#pragma once

#include <vector>

namespace t6
{

class SendQueue
{
public:
    struct Config
    {
        double minIntervalMs = 20.0;  // per parameter
        double maxPerSecond = 200.0;  // all parameters; 200 NRPN x 12 bytes = 2400 B/s, DIN carries 3125 B/s
        double burst = 16.0;          // messages that may go out at once after a quiet spell
    };

    explicit SendQueue (size_t size) : SendQueue (size, Config {}) {}
    SendQueue (size_t size, Config cfg);

    void setValue (size_t index, int value);    // the value the plugin wants the synth to have
    void markSynced (size_t index, int value);  // the synth already has this value (e.g. it sent it): don't send
    void markAllDirty();                        // send every parameter again ("Send all")

    // Calls emit(index, value) for each parameter due at time nowMs (a monotonic clock, ms).
    template <typename Emit>
    void collect (double nowMs, Emit&& emit)
    {
        refill (nowMs);
        const size_t n = wanted.size();
        for (size_t k = 0; k < n && tokens >= 1.0; ++k)
        {
            const size_t i = (cursor + k) % n;
            if (! isDue (i, nowMs))
                continue;
            sent[i] = wanted[i];
            lastSentMs[i] = nowMs;
            tokens -= 1.0;
            emit (i, wanted[i]);
        }
        cursor = (cursor + 1) % (n ? n : 1);  // rotate so no parameter is always last in line
    }

    bool pending() const;

private:
    static constexpr int kUnknown = -1;  // NRPN values are never negative

    bool isDue (size_t i, double nowMs) const
    {
        return wanted[i] != sent[i] && nowMs - lastSentMs[i] >= cfg.minIntervalMs;
    }
    void refill (double nowMs);

    Config cfg;
    std::vector<int> wanted, sent;
    std::vector<double> lastSentMs;
    double tokens, lastRefillMs = -1.0;
    size_t cursor = 0;
};

} // namespace t6
