// The app's settings file, one instance shared by every page (so pages don't overwrite each other's keys).
// Pages hold a juce::SharedResourcePointer<AppSettings>; the file closes when the last page goes.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <memory>

struct AppSettings
{
    AppSettings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Trigon6 Control";
        o.filenameSuffix = ".settings";
        o.folderName = "Trigon6 Control";
        o.osxLibrarySubFolder = "Application Support";
        file = std::make_unique<juce::PropertiesFile> (o);
    }
    std::unique_ptr<juce::PropertiesFile> file;
};

// Documents\Trigon6 Control\<sub>
inline juce::File documentsFolder (const char* sub)
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Trigon6 Control").getChildFile (sub);
}
