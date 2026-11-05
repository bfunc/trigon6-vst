#include "PortMatch.h"

#include <cctype>

namespace t6
{

std::string normalizePortName (const std::string& name)
{
    std::string s;
    for (char c : name)
        s += (char) std::tolower ((unsigned char) c);

    auto trim = [&]
    {
        while (! s.empty() && s.back() == ' ')
            s.pop_back();
        while (! s.empty() && s.front() == ' ')
            s.erase (s.begin());
    };
    trim();

    // "2- name"
    size_t i = 0;
    while (i < s.size() && std::isdigit ((unsigned char) s[i]))
        ++i;
    if (i > 0 && i + 1 < s.size() && s[i] == '-' && s[i + 1] == ' ')
        s.erase (0, i + 2);

    // "name [0]"
    if (s.size() >= 3 && s.back() == ']')
    {
        const auto open = s.rfind ('[');
        if (open != std::string::npos)
            s.erase (open);
    }
    trim();

    // "name 0": a trailing number after a space
    size_t j = s.size();
    while (j > 0 && std::isdigit ((unsigned char) s[j - 1]))
        --j;
    if (j < s.size() && j > 0 && s[j - 1] == ' ')
        s.erase (j - 1);
    trim();

    // collapse double spaces
    std::string out;
    for (char c : s)
        if (! (c == ' ' && ! out.empty() && out.back() == ' '))
            out += c;
    return out;
}

PortPick pickPort (const std::vector<std::string>& names, const std::string& saved)
{
    PortPick p;
    if (! saved.empty())
    {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == saved)
                return { (int) i, false };
        const auto want = normalizePortName (saved);
        for (size_t i = 0; i < names.size(); ++i)
            if (normalizePortName (names[i]) == want)
                return { (int) i, false };
        p.savedMissing = true;
    }
    for (size_t i = 0; i < names.size(); ++i)
        if (normalizePortName (names[i]).find ("trigon") != std::string::npos)
        {
            p.index = (int) i;
            return p;
        }
    return p;
}

} // namespace t6
