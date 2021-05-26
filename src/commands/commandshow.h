
#pragma once

class CommandShow : public CommandBase
{
public:
    using CommandBase::CommandBase;
    
    //==================================================================================================================
    std::string_view getName() const noexcept override { return "Show"; }
    
    std::string_view getDescription() const noexcept override
    {
        return "Tries to find a certain symbol and gives a detailed overview if it was found.";
    }
    
    std::string_view getEmoteName()   const noexcept override { return "symbols"; }
    std::string_view getUsage()       const noexcept override { return "find <symbol-path>"; }
    std::string_view getPermission()  const noexcept override { return "cmd.user.show";  }
    
    //==================================================================================================================
    bool execute(const SleepyDiscord::Message &msg, const juce::StringArray &args) override;
};
