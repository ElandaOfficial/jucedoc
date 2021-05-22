
#pragma once

#include "config.h"
#include "specs.h"

#include "pagedembed.h"
#include "guildstorage.h"

#include <venum.h>

#include <namespacedef.h>

#include <sleepy_discord/websocketpp_websocket.h>
#include <juce_core/juce_core.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <bitset>
#include <stack>
#include <string_view>
#include <unordered_map>
#include <variant>

//======================================================================================================================
namespace sld = SleepyDiscord;

//======================================================================================================================
namespace std
{
    template<class T>
    struct hash<sld::Snowflake<T>>
    {
        std::size_t operator()(const sld::Snowflake<T> &slid) const noexcept { return slid.number(); }
    };
}

//======================================================================================================================
VENUM_CREATE_ASSOC
(
    ID(Command),
    VALUES
    (
        (List)("Lists all symbols of a certain category.", "notepad_spiral", "list | [filter]"),
        (Find)("Searches for a portion of text through the entire juce database.\nAsterisk-wildcards supported.", "mag",
               "find <query> | [filter]"),
        (Show)("Tries to find a certain symbol and gives a detailed overview if it was found.", "symbols",
               "show <query> | [filter]"),
        
        (Refresh)("Clone the latest commit and recompile the juce architecture.", "recycle", "refresh [commit]")
    ),
    BODY
    (
    public:
        constexpr std::string_view getDescription() const noexcept { return description;    }
        constexpr std::string_view getEmote()       const noexcept { return emote;          }
        constexpr std::string_view getUsage()       const noexcept { return usage;          }
        constexpr std::string_view getRoleString()  const noexcept { return allowedRoleIds; }
        
        std::vector<sld::Snowflake<sld::Role>> getRoles() const
        {
            juce::StringArray id_list;
            id_list.addTokens(allowedRoleIds.data(), ",", "\"");
            
            std::vector<sld::Snowflake<sld::Role>> result;
            result.reserve(id_list.size());
            
            for (const auto &id : id_list)
            {
                if (!id.isEmpty())
                {
                    result.emplace_back(id.toStdString());
                }
            }
            
            return result;
        }
    
    private:
        std::string_view description;
        std::string_view emote;
        std::string_view usage;
        std::string_view allowedRoleIds;
        
        //==============================================================================================================
        constexpr CommandConstant(std::string_view name, int ordinal, std::string_view description,
                                  std::string_view emote, std::string_view usage, std::string_view allowedRoleIds = "")
            : VENUM_BASE(name, ordinal),
              description(description), emote(emote), usage(usage), allowedRoleIds(allowedRoleIds)
        {}
    )
)

//======================================================================================================================
class JuceDocClient : public sld::DiscordClient
{
public:
    struct Options
    {
        juce::String branch;
        int pageCacheSize;
        bool cloneOnStart;
    };
    
    //==================================================================================================================
    JuceDocClient(const juce::String &token, juce::String clientId, Options options);
    
    //==================================================================================================================
    void onMessage (sld::Message) override;
    void onReady   (sld::Ready)   override;
    void onError   (SleepyDiscord::ErrorCode, std::string) override;
    void onServer  (sld::Server) override;
    void onReaction(sld::Snowflake<sld::User>, sld::Snowflake<sld::Channel>, sld::Snowflake<sld::Message>,
                    sld::Emoji) override;
    
private:
    class EmbedCacheBuffer
    {
    public:
        explicit EmbedCacheBuffer(int);
        
        //==============================================================================================================
        void pop();
        void push(PagedEmbed&&);
        
        //==============================================================================================================
        PagedEmbed       *get(const sld::Snowflake<sld::Message>&);
        const PagedEmbed *get(const sld::Snowflake<sld::Message>&) const;
        
        //==============================================================================================================
        bool isFull()  const noexcept;
        bool isEmpty() const noexcept;
        
        //==============================================================================================================
        std::size_t capacity() const noexcept;
        std::size_t size()     const noexcept;
    
    private:
        std::vector<std::pair<sld::Snowflake<sld::Message>, PagedEmbed>> buffer;
        std::size_t head {};
        std::size_t tail {};
    };
    
    //==================================================================================================================
    using Storage = GuildStorage<EmbedCacheBuffer>;
    using DefVec  = std::vector<std::reference_wrapper<const Definition>>;
    
    //==================================================================================================================
    std::vector<std::unique_ptr<NamespaceDef>> namespaces;
    venum::VenumMap<EntityType, DefVec>        defCache;
    
    Options      options;
    juce::String clientId;
    
    std::shared_ptr<spdlog::logger> logger { spdlog::stdout_color_mt(AppConfig::nameLogger.data()) };
    
    juce::File dirRoot { juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() };
    juce::File dirJuce;
    juce::File dirDocs;
    
    bool busy { false };
    
    //==================================================================================================================
    template<class T, class Fn>
    bool applyData(const sld::Snowflake<sld::Server> &id, Fn &&func)
    {
        try
        {
            std::forward<Fn>(func)(Storage::get<T>(id));
            return true;
        }
        catch (std::exception &ex)
        {
#if JUCE_DEBUG
            logger->template debug(ex.what());
#endif
        }
        
        return false;
    }
    
    //==================================================================================================================
    juce::String getActivator() const;
    
    //==================================================================================================================
    void notifyBusy(const sld::Message&);
    
    //==================================================================================================================
    void showHelpPage  (sld::Message&, const juce::String&);
    bool processCommand(int, sld::Message&, juce::StringArray&);
    
    //==================================================================================================================
    void initDoxygenEngine();
    void parseDoxygenFiles();
    void createFileStructure();
    
    //==================================================================================================================
    void cloneRepository() const;
};
