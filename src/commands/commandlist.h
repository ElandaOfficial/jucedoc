
#pragma once

class CommandList : public CommandBase
{
public:
    using CommandBase::CommandBase;
    
    //==================================================================================================================
    std::string_view getName()        const noexcept override { return "List"; }
    std::string_view getDescription() const noexcept override { return "Lists all symbols of a certain category."; }
    std::string_view getEmoteName()   const noexcept override { return "notepad_spiral"; }
    std::string_view getUsage()       const noexcept override { return "list [filters]"; }
    std::string_view getPermission()  const noexcept override { return "cmd.user.list";  }
    
    //==================================================================================================================
    bool execute(const SleepyDiscord::Message &msg, const juce::StringArray &args) override;
};
