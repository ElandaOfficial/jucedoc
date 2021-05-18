
#include "jucedocclient.h"

namespace
{
    void setOption(const juce::ArgumentList &args, std::string_view name, juce::String &valueToSet,
                   juce::String defaultValue = "")
    {
        juce::String opt_syntax;
        opt_syntax << "-" << name[0] << "|--" << name.data();
        
        if (juce::String opt = args.getValueForOption(opt_syntax); !opt.isEmpty())
        {
            std::swap(valueToSet, opt);
        }
        else
        {
            std::swap(valueToSet, defaultValue);
        }
    }
    
    void setOption(const juce::ArgumentList &args, std::string_view name, bool &option)
    {
        option = args.containsOption(juce::String("--") + name.data());
    }
    
    void setOption(const juce::ArgumentList &args, std::string_view name, int &option, int defaultValue = 0)
    {
        juce::String value;
        setOption(args, name, value);
        
        const int int_val = value.getIntValue();
        option = int_val < 1 ? defaultValue : int_val;
    }
}

int main(int argc, char *argv[])
{
    juce::ArgumentList argument_list(argc, argv);
    JuceDocClient::Options options;
    
    juce::String client_token;
    juce::String client_id;
    
    // Bot info
    ::setOption(argument_list, "token", client_token, JUCEDOC_BOT_TOKEN);
    ::setOption(argument_list, "id",    client_id,    JUCEDOC_BOT_ID);
    
    // App info
    ::setOption(argument_list, "branch", options.branch,        "develop");
    ::setOption(argument_list, "pcsize", options.pageCacheSize, AppConfig::defaultPageCacheSize);
    ::setOption(argument_list, "clone",  options.cloneOnStart);
    
    JuceDocClient client(client_token, client_id, options);
    client.setIntents(sld::Intent::SERVER_MESSAGES | sld::Intent::SERVER_MESSAGE_REACTIONS);
    client.run();
    
    return 0;
}
