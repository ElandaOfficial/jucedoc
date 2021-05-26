
#include "commands.h"

#include "config.h"
#include "entitydefinition.h"
#include "jucedocclient.h"
#include "linkresolve.h"

// Doxygen
#include <definition.h>
#include <memberdef.h>
// sleepy-discord
#include <sleepy_discord/sleepy_discord.h>

#if JUCE_DEBUG
#   define JD_DBG(MSG) client.getLogger().debug(MSG)
#else
#   define JD_DBG(MSG) (void) 0
#endif

//======================================================================================================================
namespace sld = SleepyDiscord;

//======================================================================================================================
namespace
{
    std::string toCodeBlock(const juce::String &blockType, const juce::String text)
    {
        return "```" + blockType.toStdString() + "\n" + text.trimCharactersAtEnd("\n ").toStdString() + "\n```";
    }
    
    //==================================================================================================================
    bool executeListCommand(const sld::Message &msg, JuceDocClient &client, const juce::StringArray &args, bool isList)
    {
        if (!isList && args.isEmpty())
        {
            return false;
        }
    
        juce::StringArray filters = args;
        juce::String      query;
    
        if (!isList)
        {
            if (filters.isEmpty())
            {
                return false;
            }
        
            std::swap(query, filters.getReference(0));
            filters.remove(0);
        }
        
        Filter filter;
        
        try
        {
            filter = Filter::createFromInput(filters);
        }
        catch (std::invalid_argument &ex)
        {
            if (std::string_view(ex.what()).empty())
            {
                return false;
            }
        
            client.sendMessage(msg.channelID, ex.what());
            return true;
        }
    
        PagedEmbed embed(msg.channelID, filter);
        embed.setMaxItemsPerPage(5);
    
        juce::String  title;
        std::uint32_t colour;
    
        if (isList)
        {
            embed.applyListWithFilter(client.getCache(), "");
            title  = "Entity List";
            colour = Colours::List;
        }
        else
        {
            embed.applyListWithFilter(client.getCache(), query);
            title  = "Symbols found for: " + query;
            colour = Colours::Find;
        }
    
        JD_DBG("Generating embed with filters: " + filter.toString().toStdString());
        
        client.sendMessage(msg.channelID, "", embed.toEmbed(title, colour), sld::TTS::Default, {
            [&client, embed, sid = msg.serverID](sld::ObjectResponse<sld::Message> response) mutable
            {
                if (embed.getMaxPages() <= 1)
                {
                    return;
                }
            
                if (response.error())
                {
                    JD_DBG("Error sending message: " + response.text);
                    return;
                }
            
                const sld::Message msg = response;
                (void) client.applyData<JuceDocClient::EmbedCacheBuffer>(sid, [&client, &embed, &msg](auto &&cache)
                {
                    JD_DBG("Added embed to cache for message: '" + msg.ID.string() + "'");
                
                    embed.setMessageId(msg.ID);
                    cache.push(std::move(embed));
                
                    client.addReaction(msg.channelID, msg.ID, PagedEmbed::emoteLastPage.data());
                    client.addReaction(msg.channelID, msg.ID, PagedEmbed::emoteNextPage.data());
                });
            }
        });
        
        return true;
    }
}

// CommandList
//======================================================================================================================
bool CommandList::execute(const sld::Message &msg, const juce::StringArray &args)
{
    return ::executeListCommand(msg, client, args, true);
}

// CommandFind
//======================================================================================================================
bool CommandFind::execute(const sld::Message &msg, const juce::StringArray &args)
{
    return ::executeListCommand(msg, client, args, false);
}

// CommandShow
//======================================================================================================================
bool CommandShow::execute(const sld::Message &msg, const juce::StringArray &args)
{
    if (args.size() != 1)
    {
        return false;
    }
    
    const juce::String     &query = args.getReference(0);
    const EntityDefinition def    = EntityDefinition::createFromSymbolPath(query, client.getCache());
    
    if (!def)
    {
        client.sendMessage(msg.channelID, "Sorry, I could not find any entity for: " + query.toStdString() + ". :/");
        return true;
    }
    
    const AppConfig    &config = AppConfig::getInstance();
    const juce::String doc_url = AppInfo::urlJuceDocsBase.data() + config.branchName + "/";
    const EntityType   type    = def.getType();
    
    sld::Embed embed;
    embed.color          = Colours::Show;
    embed.author.iconUrl = AppIcon::values[type->ordinal()]->getUrl();
    embed.author.name    = type->name();
    embed.title          = def->name().str() + (type == EntityType::Function ? "()" : "");
    embed.url            = (doc_url + getUrlFromEntity(type, *def)).toRawUTF8();
    embed.description    = def.getDocumentation().toRawUTF8();
    embed.timestamp      = config.currentCommit.date.toStdString();
    embed.footer.iconUrl = AppIcon::LogoGitHub.getUrl();
    embed.footer.text    = config.currentCommit.name.substring(0, 9).toStdString()
                           + " (" + config.branchName.toStdString() + ")";
    
    if (const juce::String definition = def.getDefinition().toStdString(); !definition.isEmpty())
    {
        embed.fields.emplace_back("Definition", ::toCodeBlock("cpp", definition));
    }
    
    embed.fields.emplace_back("Details", ::toCodeBlock("yaml", "Path:   " + def->qualifiedName().str()     + "\n"
                                                               + "Module: " + def.getModule().toStdString()  + "\n"
                                                               + "Parent: " + def.getParent().toStdString()));
    
    if (type == EntityType::Function)
    {
        if (const auto *commands = def.getCommands(EntityDefinition::CommandIds::param); commands)
        {
            juce::String param_list;
            
            for (const auto &[name, val] : *commands)
            {
                param_list << name << ": " << val << "\n";
            }
            
            embed.fields.emplace_back("Parameters", ::toCodeBlock("yaml", param_list));
        }
    }
    else if (type == EntityType::Enum)
    {
        const MemberDef *const member = static_cast<const MemberDef*>(def.get());
        
        if (const MemberList &enumerators = member->enumFieldList(); !enumerators.empty())
        {
            juce::String enum_list;
            
            for (const auto &enumerator : member->enumFieldList())
            {
                enum_list << enumerator->name().data() << ": "
                          << (enumerator->hasBriefDescription() ? enumerator->briefDescription()
                                                                : enumerator->documentation()).data() << "\n";
            }
            
            embed.fields.emplace_back("Enumerators", ::toCodeBlock("yaml", enum_list));
        }
    }
    
    if (const auto *examples = def.getCommands(EntityDefinition::CommandIds::code); examples)
    {
        juce::String example_list;
        
        for (int i = 0; i < examples->size(); ++i)
        {
            example_list << "Example " << (i + 1) << ::toCodeBlock("cpp", (*examples)[i].value);
        }
        
        embed.fields.emplace_back("Examples", example_list.toStdString());
    }
    
    if (const auto *references = def.getCommands(EntityDefinition::CommandIds::see); references)
    {
        juce::String reference_list;
        
        for (const auto &[_, name] : *references)
        {
            reference_list << name << ", ";
        }
        
        embed.fields.emplace_back("See also", reference_list.trimCharactersAtEnd(", ").toStdString());
    }
    
    const juce::File &dir_temp = client.getDirTemp();
    
    if (const juce::File cs_file = def.generateGraph(dir_temp, dir_temp); !cs_file.getFullPathName().isEmpty())
    {
        embed.image.url = "attachment://" + cs_file.getFileName().toStdString();
        (void) client.uploadFile(msg.channelID, cs_file.getFullPathName().toStdString(), "", embed, {
            [cs_file](auto&&) { (void) cs_file.deleteFile(); }
        });
    }
    else
    {
        (void) client.sendMessage(msg.channelID, "", embed);
    }
    
    return true;
}

// CommandAbout
//======================================================================================================================
bool CommandAbout::execute(const SleepyDiscord::Message &msg, const juce::StringArray&)
{
    sld::Embed embed;
    embed.title = "About";
    embed.color = Colours::About;
    
    embed.description = "Hello, my name is JuceDoc.\nI was developed by "
                        "[Elanda](https://github.com/ElandaOfficial)"
                        " and I would love to guide you through the JUCE documentation database.\n\n"
                        "Just ask me something and I will respond to your request. If you need directions you can"
                        " always call me by simply tagging my name. :slight_smile:\n\n"
                        "If there is any other question that I can't help you with, don't mind tagging my creator.\n"
                        "Wish you best luck and a great day, have fun developing. :wink:";
    
    embed.author.iconUrl = AppIcon::LogoJuce.getUrl().data();
    embed.author.name    = "JuceDoc";
    
    const AppConfig &config = AppConfig::getInstance();
    embed.timestamp         = config.currentCommit.date.toStdString();
    embed.footer.iconUrl    = AppIcon::LogoGitHub.getUrl();
    embed.footer.text       = config.currentCommit.name.substring(0, 9).toStdString()
                              + " (" + config.branchName.toStdString() + ")";
    
    // Repos
    juce::String repo_string;
    repo_string << "[JuceDoc](https://github.com/ElandaOfficial/jucedoc)\n"
                << "[JUCE](" << AppInfo::urlJuceGitRepo.data() << ")\n"
                   "[JUCE Docs Commit](" << AppInfo::urlJuceGitRepo.data() << "/tree/"
                                         << config.currentCommit.name << ")";
    embed.fields.emplace_back("Repositories", repo_string.toRawUTF8());
    
    // Other sites
    juce::String url_string;
    url_string << "[JUCE Website](https://juce.com/)\n"
               << "[JUCE Forum](https://forum.juce.com/)\n"
               << "[JUCE " << config.branchName.toUpperCase() << " Docs](" << AppInfo::urlJuceDocsBase.data()
                                                              << config.branchName << "/index.html)\n"
               << "[ElandaSunshine Discord](https://discord.gg/jzRyAtnJBc)";
    embed.fields.emplace_back("Other links", url_string.toRawUTF8());
    
    client.sendMessage(msg.channelID, "", embed);
    return true;
}

// CommandFilters
//======================================================================================================================
bool CommandFilters::execute(const SleepyDiscord::Message &msg, const juce::StringArray&)
{
    sld::Embed embed;
    embed.title = "Filters";
    embed.color = Colours::About;
    
    embed.description = "Here is a list of all filters that you can apply to a query.";
    
    embed.author.iconUrl = AppIcon::LogoJuce.getUrl().data();
    embed.author.name    = "JuceDoc";
    
    const AppConfig &config = AppConfig::getInstance();
    embed.timestamp         = config.currentCommit.date.toStdString();
    embed.footer.iconUrl    = AppIcon::LogoGitHub.getUrl();
    embed.footer.text       = config.currentCommit.name.substring(0, 9).toStdString()
                              + " (" + config.branchName.toStdString() + ")";
    
    juce::var filters = juce::JSON::fromString(Filter().toString());
    
    if (juce::DynamicObject *root = filters.getDynamicObject())
    {
        for (const auto &[name, value] : root->getProperties())
        {
            embed.fields.emplace_back(name.toString().toRawUTF8(), value.toString().toRawUTF8());
        }
    }
    
    client.sendMessage(msg.channelID, "", embed);
    return true;
}
