
#pragma once

#include "filter.h"
#include "polyspan.h"

#include <sleepy_discord/snowflake.h>

#include <type_traits>
#include <algorithm>
#include <variant>

#include "namespacedef.h"

//======================================================================================================================
namespace sld = SleepyDiscord;

//======================================================================================================================
class Definition;
class NamespaceDef;
class ClassDef;
class MemberDef;

namespace SleepyDiscord
{
    struct Channel;
    struct Message;
    struct Embed;
}

//======================================================================================================================
class PagedEmbed
{
private:
    using ResultDef = std::pair<EntityType, std::reference_wrapper<const Definition>>;
    using ResultVec = std::vector<ResultDef>;
    using CacheMap  = venum::VenumMap<EntityType, std::vector<std::reference_wrapper<const Definition>>>;
    
public:
    static constexpr std::string_view emoteLastPage = u8"⬅️";
    static constexpr std::string_view emoteNextPage = u8"➡️";
    
    //==================================================================================================================
    enum class PageAction { Back, Forward };
    
    //==================================================================================================================
    PagedEmbed() = default;
    explicit PagedEmbed(const sld::Snowflake<sld::Channel> &channelId, Filter filter = {});
    
    //==================================================================================================================
    void applyListWithFilter(const CacheMap &cache);
    void applyListWithFilter(const CacheMap &cache, const juce::String &term);
    
    //==================================================================================================================
    sld::Snowflake<sld::Channel> getChannelId() const noexcept;
    sld::Snowflake<sld::Message> getMessageId() const noexcept;
    int getMaxItemsPerPage()                    const noexcept;
    int getNumItems()                           const noexcept;
    int getMaxPages()                           const noexcept;
    
    //==================================================================================================================
    void setMaxItemsPerPage(int newMaxItemsPerPage) noexcept;
    void setMessageId(sld::Snowflake<sld::Message> messageId);
    
    //==================================================================================================================
    void prevPage()          noexcept;
    void nextPage()          noexcept;
    void setPage(PageAction) noexcept;
    
    //==================================================================================================================
    void reset();
    
    //==================================================================================================================
    sld::Embed toEmbed(const juce::String &title, std::uint32_t = 0) const;
    
private:
    ResultVec resultCache;
    Filter    filter;
    
    sld::Snowflake<sld::Channel> channelId;
    sld::Snowflake<sld::Message> messageId;
    
    int maxItemsPerPage { 10 };
    int currentPage     {  0 };
};

