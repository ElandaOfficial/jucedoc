
#pragma once

#include <juce_core/juce_core.h>

void addDoxygenGroup(const juce::File &path, const juce::String &groupName);
void processSourceFiles(const juce::File &searchDir, const juce::File &outputDir);
