
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
        (void) createFor(id);
        Ret &result = std::get<Ret>(getDataFor(id)->second);
        lock.exitRead();
    
        return result;
    }
    
    static bool createFor(const SleepyDiscord::Snowflake<SleepyDiscord::Server> &id)
    {
        {
            lock.enterRead();
            
            if (getDataFor(id) == guilds.end())
            {
                lock.exitRead();
                
                lock.enterWrite();
                guilds.emplace_back(id, DataType{});
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
        for (typename GuildList::iterator it = guilds.begin(); it != guilds.end(); ++it)
        {
            if (it->first == id)
            {
                return it;
            }
        }
    
        return guilds.end();
    }
    
    //==================================================================================================================
    inline static GuildList guilds;
    inline static juce::ReadWriteLock lock;
};
