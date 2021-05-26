
#pragma once

class CommandFind : public CommandBase
{
public:
    using CommandBase::CommandBase;
    
    //==================================================================================================================
    std::string_view getName() const noexcept override { return "Find"; }
    
    std::string_view getDescription() const noexcept override
    {
        return "Searches for a portion of text through the entire juce database. (wildcards supported)";
    }
    
    std::string_view getEmoteName()   const noexcept override { return "mag"; }
    std::string_view getUsage()       const noexcept override { return "find <query> [filters]"; }
    std::string_view getPermission()  const noexcept override { return "cmd.user.find";  }
    
    //==================================================================================================================
    bool execute(const SleepyDiscord::Message &msg, const juce::StringArray &args) override;
};
