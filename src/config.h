
#pragma once

#include <string_view>
#include <juce_core/juce_core.h>

#include <venum.h>

struct AppInfo
{
    static constexpr std::string_view urlJuceGitRepo  = "https://github.com/juce-framework/JUCE";
    static constexpr std::string_view urlJuceDocsBase = "https://docs.juce.com/";
    static constexpr std::string_view nameLogger      = "Main";
    
    static constexpr int              defaultPageCacheSize = 30;
    static constexpr std::string_view defaultBranch        = "develop";
};

struct AppConfig
{
public:
    struct Commit
    {
        juce::String name;
        juce::String date;
    };
    
    //==================================================================================================================
    static AppConfig& getInstance() noexcept
    {
        static AppConfig instance;
        return instance;
    }
    
    //==================================================================================================================
    Commit       currentCommit;
    juce::String branchName    { AppInfo::defaultBranch.data() };
    int          pageCacheSize { AppInfo::defaultPageCacheSize };
    bool         cloneOnStart  { false };
};

struct Colours
{
    enum : std::uint32_t
    {
        List  = 0xFDFD97,
        Find  = 0x9EC1CF,
        Show  = 0xCC99C9,
        Help  = 0x9EE09E,
        Fail  = 0xFF6663,
        About = 0xFEB144
    };
};

VENUM_CREATE_ASSOC
(
    ID(AppIcon),
    VALUES
    (
        (EntityNamespace)("https://i.imgur.com/DOO2fsx.jpg"),
        (EntityClass)    ("https://i.imgur.com/ch2huAW.jpg"),
        (EntityEnum)     ("https://i.imgur.com/Uo6MlVV.jpg"),
        (EntityFunction) ("https://i.imgur.com/jdEoJsZ.jpg"),
        (EntityField)    ("https://i.imgur.com/eJYjsUK.jpg"),
        (EntityTypeAlias)("https://i.imgur.com/zG6ANlU.jpg"),
        
        (LogoJuce)  ("https://i.imgur.com/RS0ZYKS.jpg"),
        (LogoGitHub)("https://i.imgur.com/7ldZ5G4.jpg")
    ),
    BODY
    (
    public:
        std::string_view getUrl() const noexcept { return url; }
    
    private:
        std::string_view url;
    
        //==============================================================================================================
        constexpr AppIconConstant(std::string_view name, int ordinal, std::string_view url)
            : VENUM_BASE(name, ordinal),
              url(url)
        {}
    )
)

#if __JETBRAINS_IDE__
#   define anon (void) 0;
#else
#   define anon (void)0; auto JUCE_CONCAT(anon, __LINE__) =
#endif
