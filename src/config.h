
#pragma once

#include <string_view>

struct AppConfig
{
    static constexpr std::string_view urlJuceDocsBase = "https://docs.juce.com/";
    static constexpr std::string_view nameLogger      = "Main";
    
    static constexpr int defaultPageCacheSize = 30;
};

struct Colours
{
    enum : std::uint32_t
    {
        List = 0xFDFD97,
        Find = 0x9EC1CF,
        Show = 0xCC99C9,
        Help = 0x9EE09E,
        Fail = 0xFF6663,
        Plhd = 0xFEB144
    };
};

#if __JETBRAINS_IDE__
#   define anon (void)0;
#else
#   define anon (void)0; auto JUCE_CONCAT(anon, __LINE__) =
#endif
