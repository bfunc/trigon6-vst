// The type the user set for a program (Bass, Pad, ...). The synth stores no type, so it lives in the settings file,
// keyed by slot, together with the name it was set for: when the slot later holds another sound, the type is dropped.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "core/Presets.h"

namespace presetTypes
{
inline juce::String key (int bank, int program)
{
    return "type." + juce::String (t6::cacheFileName (bank, program)).upToLastOccurrenceOf (".", false, false);
}

// "" when none is set, or it was set for another sound in this slot.
inline juce::String user (juce::PropertiesFile& f, int bank, int program, const std::string& name)
{
    const auto v = f.getValue (key (bank, program));
    return v.fromFirstOccurrenceOf ("\n", false, false) == juce::String (name) ? v.upToFirstOccurrenceOf ("\n", false, false)
                                                                               : juce::String();
}

// type "" goes back to the type from the name.
inline void set (juce::PropertiesFile& f, int bank, int program, const std::string& name, const juce::String& type)
{
    if (type.isEmpty())
        f.removeValue (key (bank, program));
    else
        f.setValue (key (bank, program), type + "\n" + juce::String (name));
    f.saveIfNeeded();
}

inline juce::String shown (juce::PropertiesFile& f, int bank, int program, const std::string& name)
{
    return juce::String (t6::shownType (user (f, bank, program, name).toStdString(), name));
}
} // namespace presetTypes
