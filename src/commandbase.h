
#pragma once

#include "specs.h"

// JUCE
#include <juce_core/juce_core.h>
// venum
#include <venum.h>
// STL
#include <string_view>

//======================================================================================================================
class JuceDocClient;

namespace SleepyDiscord
{
    class Message;
}

//======================================================================================================================
class CommandBase
{
public:
    using DefVec   = std::vector<std::reference_wrapper<const Definition>>;
    using CacheMap = venum::VenumMap<EntityType, DefVec>;
    
    //==================================================================================================================
    CommandBase(JuceDocClient &client) : client(client) {}
    virtual ~CommandBase() = default;
    
    //==================================================================================================================
    virtual std::string_view getName()        const noexcept = 0;
    virtual std::string_view getDescription() const noexcept = 0;
    virtual std::string_view getEmoteName()   const noexcept = 0;
    virtual std::string_view getUsage()       const noexcept = 0;
    virtual std::string_view getPermission()  const noexcept = 0;
    
    //==================================================================================================================
    virtual bool execute(const SleepyDiscord::Message &msg, const juce::StringArray &args) = 0;

protected:
    JuceDocClient &client;
};
