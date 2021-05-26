
#pragma once

class CommandAbout : public CommandBase
{
public:
    using CommandBase::CommandBase;
    
    //==================================================================================================================
    std::string_view getName()        const noexcept override { return "About"; }
    std::string_view getDescription() const noexcept override { return "Display information about this bot."; }
    std::string_view getEmoteName()   const noexcept override { return "information_source"; }
    std::string_view getUsage()       const noexcept override { return "about"; }
    std::string_view getPermission()  const noexcept override { return "cmd.user.about";  }
    
    //==================================================================================================================
    bool execute(const SleepyDiscord::Message &msg, const juce::StringArray &args) override;
};
