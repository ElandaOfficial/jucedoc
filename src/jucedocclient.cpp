
#include "jucedocclient.h"

#include "commands.h"
#include "entitydefinition.h"
#include "filter.h"
#include "linkresolve.h"
#include "processsourcefiles.h"

// Doxygen
#include <classdef.h>
#include <classlist.h>
#include <config.h>
#include <dir.h>
#include <doxygen.h>
#include <filedef.h>
#include <filename.h>
#include <groupdef.h>
#include <membername.h>
#include <outputgen.h>
#include <parserintf.h>
#include <util.h>
// STL
#include <filesystem>
#include <regex>

#if JUCE_DEBUG
#   define JD_DBG(MSG) logger->debug(MSG)
#else
#   define JD_DBG(MSG) (void) 0
#endif

//======================================================================================================================
class ScopedWorkingDirectory
{
public:
    enum WDType { System = 1, Juce = 2 };
    
    explicit ScopedWorkingDirectory(const juce::File &newWd, int wdType)
        : wdType(wdType)
    {
        if ((wdType & WDType::System) == WDType::System)
        {
            oldWdSystem = std::filesystem::current_path().string();
            std::filesystem::current_path(newWd.getFullPathName().toStdString());
        }
        
        if ((wdType & WDType::Juce) == WDType::Juce)
        {
            oldWdJuce = juce::File::getCurrentWorkingDirectory();
            newWd.setAsCurrentWorkingDirectory();
        }
    }
    
    ~ScopedWorkingDirectory()
    {
        if ((wdType & WDType::System) == WDType::System)
        {
            std::filesystem::current_path(oldWdSystem);
        }
        
        if ((wdType & WDType::Juce) == WDType::Juce)
        {
            oldWdJuce.setAsCurrentWorkingDirectory();
        }
    }

private:
    int wdType;
    juce::File  oldWdJuce;
    std::string oldWdSystem;
};

//======================================================================================================================
namespace
{
    void copyList(const MemberList *src, JuceDocClient::DefVec &dest)
    {
        if (!src)
        {
            return;
        }
        
        for (const auto &member : *src)
        {
            (void) dest.emplace_back(*member);
        }
    }
    
    void traceAndDumpClassHierarchyDfs(JuceDocClient::CacheMap &defCache, const ClassDef &classDef)
    {
        JuceDocClient::DefVec &list_class     = defCache[EntityType::Class];
        JuceDocClient::DefVec &list_enum      = defCache[EntityType::Enum];
        JuceDocClient::DefVec &list_func      = defCache[EntityType::Function];
        JuceDocClient::DefVec &list_var       = defCache[EntityType::Field];
        JuceDocClient::DefVec &list_alias     = defCache[EntityType::TypeAlias];
        
        if (!classDef.isAnonymous())
        {
            list_class.emplace_back(classDef);
            
            if (const MemberList *list = classDef.getMemberList(MemberListType_enumMembers))
            {
                for (const auto &en_def : *list)
                {
                    (void) list_enum.emplace_back(*en_def);
                }
            }
    
            copyList(classDef.getMemberList(MemberListType_functionMembers), list_func);
            copyList(classDef.getMemberList(MemberListType_variableMembers), list_var);
            copyList(classDef.getMemberList(MemberListType_typedefMembers),  list_alias);
            
            for (const auto &def : classDef.getClasses())
            {
                traceAndDumpClassHierarchyDfs(defCache, *def);
            }
        }
    }
}

//**********************************************************************************************************************
// region JuceDocClient
//======================================================================================================================
//======================================================================================================================
//**********************************************************************************************************************
// region EmbedCacheBuffer
//======================================================================================================================
JuceDocClient::EmbedCacheBuffer::EmbedCacheBuffer(int cacheSize)
{
    buffer.resize(cacheSize);
}

//======================================================================================================================
void JuceDocClient::EmbedCacheBuffer::pop()
{
    if (!isEmpty())
    {
        buffer[tail].second.reset();
        tail = (tail + 1) % capacity();
    }
}

void JuceDocClient::EmbedCacheBuffer::push(PagedEmbed &&embed)
{
    if (isFull())
    {
        pop();
    }
    
    const sld::Snowflake<sld::Message> message_id = embed.getMessageId();
    buffer[head] = std::make_pair(message_id, std::move(embed));
    head = (head + 1) % capacity();
}

//======================================================================================================================
PagedEmbed *JuceDocClient::EmbedCacheBuffer::get(const sld::Snowflake<sld::Message> &messageId)
{
    auto it = std::find_if(buffer.begin(), buffer.end(), [messageId](auto &&page) { return messageId == page.first; });
    return it != buffer.end() ? &it->second : nullptr;
}

const PagedEmbed *JuceDocClient::EmbedCacheBuffer::get(const sld::Snowflake<sld::Message> &messageId) const
{
    auto it = std::find_if(buffer.begin(), buffer.end(), [messageId](auto &&page) { return messageId == page.first; });
    return it != buffer.end() ? &it->second : nullptr;
}

//======================================================================================================================
bool JuceDocClient::EmbedCacheBuffer::isFull()  const noexcept { return (head + 1) % capacity() == tail; }
bool JuceDocClient::EmbedCacheBuffer::isEmpty() const noexcept { return tail == head; }

//======================================================================================================================
std::size_t JuceDocClient::EmbedCacheBuffer::capacity() const noexcept
{
    return buffer.size();
}

std::size_t JuceDocClient::EmbedCacheBuffer::size() const noexcept
{
    return (head >= tail ? head - tail : head - tail + capacity());
}
//======================================================================================================================
// endregion EmbedCacheBuffer
//**********************************************************************************************************************
// region JuceDocClient
//======================================================================================================================
JuceDocClient::JuceDocClient(const juce::String &parToken, juce::String parClientId)
    : sld::DiscordClient(parToken.toStdString()),
      clientId(std::move(parClientId))
{
    dirRoot.setAsCurrentWorkingDirectory();

#if JUCE_DEBUG
    logger->set_level(spdlog::level::debug);
#else
    logger->set_level(spdlog::level::info);
#endif
}

//======================================================================================================================
void JuceDocClient::onMessage(SleepyDiscord::Message message)
{
    const juce::String content = message.content;
    
    const bool mobmen = content.startsWith("<@" + clientId + ">");
    const bool commen = content.startsWith("<@!" + clientId + ">"); 

    if (mobmen || commen)
    {
        if (busy)
        {
            notifyBusy(message);
            return;
        }
        
        const juce::String args_string = content.fromFirstOccurrenceOf((mobmen ? "<@" : "<@!") + clientId + ">", false, false).trim();
        juce::StringArray  args        = juce::StringArray::fromTokens(args_string, true);
        
        if (args.isEmpty())
        {
            sendMessage(message.channelID, "I am up and working thanks for looking after me, :robot:\n"
                                           "in case you need directions, tag me and type \"help\"!\n"
                                           "Have a JUCEY day :kiwi::wave:");
        }
        else
        {
            const juce::String cmd = args[0];
            
            if (cmd.equalsIgnoreCase("help"))
            {
                showHelpPage(message, args.size() > 1 ? args[1] : "");
            }
            else
            {
                const auto command = std::find_if(commands.begin(), commands.end(), [&cmd](auto &&cmdPtr)
                {
                    return cmd.equalsIgnoreCase(cmdPtr->getName().data());
                });
                
                if (command == commands.end())
                {
                    sendMessage(message.channelID, "I am so sorry, I don't know what **" + cmd.toStdString()
                                                   + "** means. :frowning:");
                    return;
                }
                
                args.remove(0);

                if (!(*command)->execute(message, args))
                {
                    sendMessage(message.channelID, "Wrong usage for **" + cmd.toStdString() + "**!\n"
                                                   "**Usage:** " + (*command)->getUsage().data());
                }
            }
        }
    }
}

void JuceDocClient::onReady(sld::Ready readyData)
{
    busy.store(true);
    logger->info("Initialising JuceDoc...");
    
    setupRepository();
    
    for (const auto &guild : getServers().vector())
    {
        (void) Storage::createFor(guild.ID, EmbedCacheBuffer(AppConfig::getInstance().pageCacheSize));
        JD_DBG("Registered server for id: " + guild.ID.string());
    }
    
    {
        // Dangerous, but necessary.
        // Since Doxygen uses the filesystem working directory, which is where our binary sits,
        // we need to set this to our juce docs directory
        anon ScopedWorkingDirectory(dirDocs, ScopedWorkingDirectory::System | ScopedWorkingDirectory::Juce);
        
        logger->info("Generating juce information...");
        createFileStructure();
        
        logger->info("Initialising doxygen engine...");
        initDoxygenEngine();
        
        logger->info("Parsing juce hierarchy...");
        parseDoxygenFiles();
    }
    
    logger->info("Preparing command provider...");
    commands.emplace_back(std::make_unique<CommandList>   (*this));
    commands.emplace_back(std::make_unique<CommandFind>   (*this));
    commands.emplace_back(std::make_unique<CommandShow>   (*this));
    commands.emplace_back(std::make_unique<CommandAbout>  (*this));
    commands.emplace_back(std::make_unique<CommandFilters>(*this));
    
    logger->info("JuceDoc is now ready to be used.");
    busy.store(false);
}

void JuceDocClient::onError(sld::ErrorCode, const std::string errorMessage)
{
    logger->error(errorMessage);
}

void JuceDocClient::onServer(sld::Server server)
{
    (void) Storage::createFor(server.ID, EmbedCacheBuffer(AppConfig::getInstance().pageCacheSize));
    JD_DBG("Registered new server for id: " + server.ID.string());
}

void JuceDocClient::onReaction(sld::Snowflake<sld::User> userId, sld::Snowflake<sld::Channel> channelId,
                               sld::Snowflake<sld::Message> messageId, sld::Emoji emote)
{
    if (getUser(userId).cast().bot)
    {
        return;
    }
    
    const sld::Message message = getMessage(channelId, messageId);
        
    applyData<EmbedCacheBuffer>(getChannel(channelId).cast().serverID, [this, &message, &emote, &userId](auto &&cache)
    {
        if (PagedEmbed *const embed = cache.get(message.ID))
        {
            removeReaction(message.channelID, message.ID, emote.name, userId);
            embed->setPage(emote.name == PagedEmbed::emoteLastPage ? PagedEmbed::PageAction::Back
                                                                   : PagedEmbed::PageAction::Forward);
            
            const sld::Embed pre_embed = message.embeds[0];
            editMessage(message, "", embed->toEmbed(pre_embed.title, pre_embed.color));
        }
    });
}

//======================================================================================================================
juce::String JuceDocClient::getActivator() const { return "<@!" + clientId + ">"; }

//======================================================================================================================
void JuceDocClient::notifyBusy(const sld::Message &msg)
{
    sendMessage(msg.channelID, "Sorry, I am busy right now setting up my office. :factory_worker:\n"
                               "Please come back at a later time and try again.");
}

//======================================================================================================================
void JuceDocClient::showHelpPage(sld::Message &message, const juce::String &cmd)
{
    if (cmd.isEmpty())
    {
        sld::Embed embed;
        embed.title       = ":notebook_with_decorative_cover: Command Help";
        embed.type        = "rich";
        embed.description = "A list of commands you can use:";
        embed.color       = Colours::Help;
        
        embed.fields.reserve(commands.size());
        
        for (const auto &command : commands)
        {
            const juce::String emote = command->getEmoteName().data();
            const juce::String title = (!emote.isEmpty() ? ":" + emote + ": " : "") + command->getName().data();
            embed.fields.emplace_back(title.toStdString(), command->getDescription().data(), false);
        }
        
        sendMessage(message.channelID, "", embed);
    }
    else
    {
        for (const auto &command : commands)
        {
            const juce::String name = command->getName().data();
            
            if (name.equalsIgnoreCase(cmd))
            {
                const juce::String emote       = command->getEmoteName().data();
                const juce::String emote_title = (!emote.isEmpty() ? ":" + emote + ": " : "")
                                                  + command->getName().data();
    
                sld::Embed embed;
                embed.title       = emote_title.toStdString() + "Help for '" + name.toStdString() + "'";
                embed.description = command->getDescription();
                embed.color       = Colours::Help;
                embed.fields      = {
                    { "Usage", command->getUsage().data(), false }
                };
                
                sendMessage(message.channelID, "", embed);
                return;
            }
        }
    }
}

//======================================================================================================================
void JuceDocClient::initDoxygenEngine()
{
    logger->info("Configurung doxygen...");
    initDoxygen();
    
    readConfiguration(0, nullptr);
    checkConfiguration();
    adjustConfiguration();
    
    Config_updateBool(GENERATE_HTML,          FALSE)
    Config_updateBool(GENERATE_LATEX,         FALSE)
    Config_updateBool(QUIET,                  TRUE )
    Config_updateBool(WARNINGS,               FALSE)
    Config_updateBool(WARN_IF_UNDOCUMENTED,   FALSE)
    Config_updateBool(WARN_IF_DOC_ERROR,      FALSE)
    Config_updateBool(WARN_IF_INCOMPLETE_DOC, FALSE)
    Config_updateBool(WARN_NO_PARAMDOC,       FALSE)
    
    Config_updateString(DOT_IMAGE_FORMAT,     "png")
    Config_updateBool  (HAVE_DOT,             true)
}

void JuceDocClient::parseDoxygenFiles()
{
    parseInput();
    logger->info("Creating entity map...");
    
    for (auto &ns_def : *Doxygen::namespaceLinkedMap)
    {
        if (!ns_def->isAnonymous() && !ns_def->qualifiedName().startsWith("std"))
        {
            (void) namespaces.emplace_back(ns_def.release()).get();
        }
    }
    
    DefVec &list_namespace = defCache[EntityType::Namespace];
    DefVec &list_class     = defCache[EntityType::Class];
    DefVec &list_enum      = defCache[EntityType::Enum];
    DefVec &list_func      = defCache[EntityType::Function];
    DefVec &list_var       = defCache[EntityType::Field];
    DefVec &list_alias     = defCache[EntityType::TypeAlias];
    
    for (const auto &ns_def : namespaces)
    {
        (void) list_namespace.emplace_back(*ns_def);
        
        if (const MemberList *list = ns_def->getMemberList(MemberListType_enumMembers))
        {
            for (const auto &en_def : *list)
            {
                (void) list_enum.emplace_back(*en_def);
            }
        }
    
        copyList(ns_def->getMemberList(MemberListType_functionMembers), list_func);
        copyList(ns_def->getMemberList(MemberListType_variableMembers), list_var);
        copyList(ns_def->getMemberList(MemberListType_typedefMembers),  list_alias);
        
        for (const auto &cs_def : ns_def->getClasses())
        {
            ::traceAndDumpClassHierarchyDfs(defCache, *cs_def);
        }
    }
    
    logger->info("Cached " + std::to_string(list_namespace.size()) + " namespaces, "
                           + std::to_string(list_class    .size()) + " classes, "
                           + std::to_string(list_enum     .size()) + " enums, "
                           + std::to_string(list_func     .size()) + " functions, "
                           + std::to_string(list_var      .size()) + " variables and "
                           + std::to_string(list_alias    .size()) + " type aliases.");
}

void JuceDocClient::createFileStructure()
{
    (void) dirTemp.createDirectory();
    
    const juce::File build_folder("./build");
    build_folder.deleteRecursively();
    
    processSourceFiles(juce::File("../../modules"), build_folder);
}

//======================================================================================================================
void JuceDocClient::setupRepository() const
{
    juce::ChildProcess gitProcess;
    AppConfig &config = AppConfig::getInstance();
    const juce::String &branch = config.branchName;
    
    if (config.cloneOnStart || !dirJuce.exists())
    {
        (void) dirJuce.deleteRecursively();
        logger->info("Cloning latest juce " + branch.toStdString() + " commit...");
        
        (void) gitProcess.start("git clone --branch " + branch + " " + AppInfo::urlJuceGitRepo.data()
                                + " juce/" + branch);
        (void) gitProcess.waitForProcessToFinish(-1);
    }
    
    const juce::String env = "env -C juce/" + branch + " ";
    bool  need_request     = config.currentCommit.name.isEmpty();
    
    if (!need_request)
    {
        (void) gitProcess.start(env + "git checkout " + config.currentCommit.name);
        
        if (!(need_request = gitProcess.readAllProcessOutput().startsWith("error")))
        {
            (void) gitProcess.start(env + "git log -1 --pretty=format:'%ct'");
            const juce::Time time(gitProcess.readAllProcessOutput().removeCharacters("'").getLargeIntValue() * 1000);
            config.currentCommit.date = time.formatted("%Y-%m-%d");
        }
    }
    
    if (need_request)
    {
        (void) gitProcess.start(env + "git log -1 --pretty=format:" R"('{"name":"%H","date":"%ct"}')");
        const juce::var json = juce::JSON::fromString(gitProcess.readAllProcessOutput().removeCharacters("'"));
        
        if (json.isObject())
        {
            if (const juce::DynamicObject *const root = json.getDynamicObject())
            {
                const juce::Time time(root->getProperty("date").toString().getLargeIntValue() * 1000);
                config.currentCommit.name = root->getProperty("name").toString();
                config.currentCommit.date = time.formatted("%Y-%m-%d");
            }
        }
    }
}
//======================================================================================================================
// endregion JuceDocClient
//**********************************************************************************************************************
//======================================================================================================================
//======================================================================================================================
// endregion JuceDocClient
//**********************************************************************************************************************
