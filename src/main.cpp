
#include "jucedocclient.h"

int main(int argc, char *argv[])
{
    juce::ArgumentList argument_list(argc, argv);
    JuceDocClient::Options options { false };
    
    juce::String token     = JUCEDOC_BOT_TOKEN;
    juce::String client_id = JUCEDOC_BOT_ID;
    
    if (argument_list.containsOption("--clone"))
    {
        options.cloneOnStart = true;
    }
    
    if (argument_list.containsOption("-t"))
    {
        juce::String t = argument_list.getValueForOption("-t");
        
        if (!t.isEmpty())
        {
            std::swap(token, t);
        }
    }
    
    if (argument_list.containsOption("-i"))
    {
        juce::String id = argument_list.getValueForOption("-i");
    
        if (!id.isEmpty())
        {
            std::swap(client_id, id);
        }
    }
    
    JuceDocClient client(token, client_id, options);
    client.setIntents(sld::Intent::SERVER_MESSAGES | sld::Intent::SERVER_MESSAGE_REACTIONS);
    client.run();
    
    return 0;
}
