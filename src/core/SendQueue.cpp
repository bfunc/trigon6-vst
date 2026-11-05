#include "SendQueue.h"

#include <algorithm>

namespace t6
{

SendQueue::SendQueue (size_t size, Config c)
    : cfg (c), wanted (size, kUnknown), sent (size, kUnknown), lastSentMs (size, -1.0e12), tokens (c.burst)
{
}

void SendQueue::setValue (size_t index, int value) { wanted[index] = value; }

void SendQueue::markSynced (size_t index, int value) { wanted[index] = sent[index] = value; }

void SendQueue::markAllDirty() { std::fill (sent.begin(), sent.end(), kUnknown); }

bool SendQueue::pending() const
{
    for (size_t i = 0; i < wanted.size(); ++i)
        if (wanted[i] != sent[i])
            return true;
    return false;
}

void SendQueue::refill (double nowMs)
{
    if (lastRefillMs >= 0.0 && nowMs > lastRefillMs)
        tokens = std::min (cfg.burst, tokens + (nowMs - lastRefillMs) * cfg.maxPerSecond / 1000.0);
    lastRefillMs = nowMs;
}

} // namespace t6
