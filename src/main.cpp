
#include "jucedocclient.h"

namespace
{
    void setOption(const juce::ArgumentList &args, std::string_view name, juce::String &valueToSet)
    {
        juce::String opt_syntax;
        opt_syntax << "-" << name[0] << "|--" << name.data();
        
        if (juce::String opt = args.getValueForOption(opt_syntax); !opt.isEmpty())
        {
            std::swap(valueToSet, opt);
        }
    }
    
    void setOption(const juce::ArgumentList &args, std::string_view name, bool &option)
    {
        option = args.containsOption(juce::String("--") + name.data());
    }
    
    void setOption(const juce::ArgumentList &args, std::string_view name, int &option)
    {
        juce::String value;
        setOption(args, name, value);
        option = value.getIntValue();
    }
}

int main(int argc, char *argv[])
{
    juce::ArgumentList argument_list(argc, argv);
    
    juce::String client_token;
    juce::String client_id;
    
    // Bot info
    ::setOption(argument_list, "token", client_token);
    ::setOption(argument_list, "id",    client_id);
    
    if (client_token.isEmpty())
    {
        client_token = JUCEDOC_BOT_TOKEN;
    }
    
    if (client_id.isEmpty())
    {
        client_id = JUCEDOC_BOT_ID;
    }
    
    AppConfig &config = AppConfig::getInstance();
    
    // App info
    ::setOption(argument_list, "branch", config.branchName);
    ::setOption(argument_list, "pcsize", config.pageCacheSize);
    ::setOption(argument_list, "clone",  config.cloneOnStart);
    
    if (config.pageCacheSize < 1)
    {
        config.pageCacheSize = AppInfo::defaultPageCacheSize;
    }
    
    JuceDocClient client(client_token, client_id);
    client.setIntents(sld::Intent::SERVER_MESSAGES | sld::Intent::SERVER_MESSAGE_REACTIONS);
    client.run();
    
    return 0;
}
