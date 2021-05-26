
#pragma once

#include "config.h"
#include "commandbase.h"
#include "specs.h"

#include "pagedembed.h"
#include "guildstorage.h"

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
class JuceDocClient : public sld::DiscordClient
{
public:
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
    using Storage  = GuildStorage<EmbedCacheBuffer>;
    using DefVec   = std::vector<std::reference_wrapper<const Definition>>;
    using CacheMap = venum::VenumMap<EntityType, DefVec>;
    
    //==================================================================================================================
    JuceDocClient(const juce::String &token, juce::String clientId);
    
    //==================================================================================================================
    void onMessage (sld::Message) override;
    void onReady   (sld::Ready)   override;
    void onError   (SleepyDiscord::ErrorCode, std::string) override;
    void onServer  (sld::Server) override;
    void onReaction(sld::Snowflake<sld::User>, sld::Snowflake<sld::Channel>, sld::Snowflake<sld::Message>,
                    sld::Emoji) override;
    
    //==================================================================================================================
    const CacheMap& getCache() const noexcept { return defCache; }
    
    //==================================================================================================================
    const juce::File &getDirRoot() const noexcept { return dirRoot; }
    const juce::File &getDirJuce() const noexcept { return dirJuce; }
    const juce::File &getDirDocs() const noexcept { return dirDocs; }
    const juce::File &getDirTemp() const noexcept { return dirTemp; }
    
    //==================================================================================================================
    spdlog::logger& getLogger() noexcept { return *logger; }
    
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
    
private:
    std::vector<std::unique_ptr<CommandBase>>  commands;
    std::vector<std::unique_ptr<NamespaceDef>> namespaces;
    CacheMap                                   defCache;
    
    juce::String clientId;
    
    std::shared_ptr<spdlog::logger> logger { spdlog::stdout_color_mt(AppInfo::nameLogger.data()) };
    
    juce::File dirRoot { juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() };
    juce::File dirTemp { dirRoot.getChildFile("temp") };
    juce::File dirJuce { dirRoot.getChildFile("juce/" + AppConfig::getInstance().branchName) };
    juce::File dirDocs { dirJuce.getChildFile("docs/doxygen") };
    
    bool busy { false };
    
    //==================================================================================================================
    juce::String getActivator() const;
    
    //==================================================================================================================
    void notifyBusy(const sld::Message&);
    
    //==================================================================================================================
    void showHelpPage(sld::Message&, const juce::String&);
    
    //==================================================================================================================
    void initDoxygenEngine();
    void parseDoxygenFiles();
    void createFileStructure();
    
    //==================================================================================================================
    void setupRepository() const;
};
