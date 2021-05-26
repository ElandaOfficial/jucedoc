
#pragma once

class CommandFilters : public CommandBase
{
public:
    using CommandBase::CommandBase;
    
    //==================================================================================================================
    std::string_view getName()        const noexcept override { return "Filters"; }
    std::string_view getDescription() const noexcept override { return "Get a list of filters and their use."; }
    std::string_view getEmoteName()   const noexcept override { return "scissors"; }
    std::string_view getUsage()       const noexcept override { return "filters"; }
    std::string_view getPermission()  const noexcept override { return "cmd.user.filters";  }
    
    //==================================================================================================================
    bool execute(const SleepyDiscord::Message &msg, const juce::StringArray &args) override;
};
