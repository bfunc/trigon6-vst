#include "PresetDiff.h"

namespace t6
{

std::vector<size_t> differingParams (const std::vector<int>& preset, const std::vector<int>& now)
{
    std::vector<size_t> out;
    if (preset.empty())
        return out;
    for (size_t i = 0; i < preset.size() && i < now.size(); ++i)
        if (preset[i] != now[i])
            out.push_back (i);
    return out;
}

std::string diffText (const ParamDef& p, int preset, int now)
{
    std::string s = "preset " + formatValue (p, preset) + ", now " + formatValue (p, now);
    if (p.kind == Kind::Int || p.kind == Kind::Bipolar)
    {
        const int d = now - preset;
        s += std::string (", \xCE\x94 ") + (d > 0 ? "+" : d < 0 ? "-" : "") + std::to_string(d < 0 ? -d : d);
    }
    return s;
}

} // namespace t6
