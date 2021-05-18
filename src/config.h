
#pragma once

#include <string_view>

struct AppConfig
{
    static constexpr std::string_view urlJuceDocsBase = "https://docs.juce.com/develop/";
    static constexpr std::string_view nameLogger      = "Main";
    
    static constexpr int defaultPageCacheSize = 30;
};

#if __JETBRAINS_IDE__
#   define anon (void)0;
#else
#   define anon (void)0; auto JUCE_CONCAT(anon, __LINE__) =
#endif
