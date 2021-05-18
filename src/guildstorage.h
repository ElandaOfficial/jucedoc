
#pragma once

#include <sleepy_discord/snowflake.h>
#include <juce_core/juce_core.h>

//======================================================================================================================
namespace SleepyDiscord { struct Server; }

//======================================================================================================================
template<class T, class ...TL>
class GuildStorage
{
public:
    template<class Ret>
    static Ret& get(const SleepyDiscord::Snowflake<SleepyDiscord::Server> &id)
    {
        lock.enterRead();
        
        if (auto it = getDataFor(id); it != guilds.end())
        {
            lock.exitRead();
            return std::get<Ret>(it->second);
        }
    
        lock.exitRead();
        
        throw std::logic_error("No storage found for server with the id '" + id.string() + "'");
    }
    
    template<class ...TArgs>
    static bool createFor(const SleepyDiscord::Snowflake<SleepyDiscord::Server> &id, TArgs &&...args)
    {
        {
            lock.enterRead();
            
            if (getDataFor(id) == guilds.end())
            {
                lock.exitRead();
                
                lock.enterWrite();
                guilds.emplace_back(id, std::make_tuple(std::forward<TArgs>(args)...));
                lock.exitWrite();
                
                return true;
            }
            else
            {
                lock.exitRead();
            }
        }
        
        return false;
    }
    
private:
    using DataType  = std::tuple<T, TL...>;
    using GuildList = std::vector<std::pair<SleepyDiscord::Snowflake<SleepyDiscord::Server>, DataType>>;
    
    //==================================================================================================================
    static typename GuildList::iterator getDataFor(const SleepyDiscord::Snowflake<SleepyDiscord::Server> &id)
    {
        return std::find_if(guilds.begin(), guilds.end(), [&id](auto &&data) { return data.first == id; });
    }
    
    //==================================================================================================================
    inline static GuildList guilds;
    inline static juce::ReadWriteLock lock;
};
