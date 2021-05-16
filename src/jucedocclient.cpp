
#include "jucedocclient.h"

#include "filter.h"

#include <dir.h>
#include <doxygen.h>
#include <outputgen.h>
#include <parserintf.h>
#include <classdef.h>
#include <namespacedef.h>
#include <filedef.h>
#include <util.h>
#include <classlist.h>
#include <config.h>
#include <filename.h>
#include <membername.h>

#include <filesystem>

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
    using DefVec = std::vector<std::reference_wrapper<const Definition>>;
    
    //==================================================================================================================
    void copyList(const MemberList *src, DefVec &dest)
    {
        if (!src)
        {
            return;
        }
        
        for (const auto &member : *src)
        {
            if (member)
            {
                (void) dest.emplace_back(*member);
            }
        }
    }
    
    void traceAndDumpClassHierarchyDfs(venum::VenumMap<EntityType, DefVec> &defCache, const ClassDef &classDef)
    {
        DefVec &list_class     = defCache[EntityType::Class];
        DefVec &list_enum      = defCache[EntityType::Enum];
        DefVec &list_func      = defCache[EntityType::Function];
        DefVec &list_var       = defCache[EntityType::Field];
        DefVec &list_alias     = defCache[EntityType::TypeAlias];
        
        if (!classDef.isAnonymous())
        {
            list_class.emplace_back(classDef);
            
            if (const MemberList *list = classDef.getMemberList(MemberListType_enumMembers))
            {
                for (const auto &en_def : *list)
                {
                    if (en_def)
                    {
                        (void) list_enum.emplace_back(*en_def);
                    }
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
void JuceDocClient::EmbedCacheBuffer::pop()
{
    if (!isEmpty())
    {
        buffer[tail].second.reset();
        tail = (tail + 1) % capacity;
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
    head = (head + 1) % capacity;
}
//======================================================================================================================
PagedEmbed *JuceDocClient::EmbedCacheBuffer::get(const sld::Snowflake<sld::Message> &messageId)
{
    for (auto &[msg_id, embed] : buffer)
    {
        if (msg_id == messageId)
        {
            return &embed;
        }
    }
    
    return nullptr;
}

const PagedEmbed *JuceDocClient::EmbedCacheBuffer::get(const sld::Snowflake<sld::Message> &messageId) const
{
    for (auto &[msg_id, embed] : buffer)
    {
        if (msg_id == messageId)
        {
            return &embed;
        }
    }
    
    return nullptr;
}

//======================================================================================================================
bool JuceDocClient::EmbedCacheBuffer::isFull()  const noexcept { return (head + 1) % capacity == tail; }
bool JuceDocClient::EmbedCacheBuffer::isEmpty() const noexcept { return tail == head; }

//======================================================================================================================
std::size_t JuceDocClient::EmbedCacheBuffer::size() const noexcept
{
    return (head >= tail ? head - tail : head - tail + capacity);
}
//======================================================================================================================
// endregion EmbedCacheBuffer
//**********************************************************************************************************************
// region JuceDocClient
//======================================================================================================================
constexpr int JuceDocClient::getEmbedIdBaseForCommand(Command command) noexcept
{
    constexpr int magic_number = 30390340;
    return (command->ordinal() * 100000000) + magic_number;
}

//======================================================================================================================
JuceDocClient::JuceDocClient(const juce::String &parToken, juce::String parClientId, Options options)
    : sld::DiscordClient(parToken.toStdString()),
      options(options), clientId(std::move(parClientId))
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
    
    if (content.startsWith(getActivator()))
    {
        if (busy)
        {
            notifyBusy(message);
            return;
        }
        
        juce::StringArray args;
        args.addTokens(content.fromFirstOccurrenceOf(getActivator(), false, false).trim(), " ", "\"");
        
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
                const Command command = Command::valueOf(cmd.toLowerCase().toRawUTF8(), true);
                
                if (!command)
                {
                    sendMessage(message.channelID, "I am so sorry, I don't know what **" + cmd.toStdString()
                                                   + "** means. :frowning:");
                    return;
                }
                
                const std::vector<sld::Snowflake<sld::Role>> allowed_roles = command->getRoles();
                
                if (!allowed_roles.empty())
                {
                    const sld::ServerMember member = getMember(message.serverID, message.author).cast();
                    const std::vector<sld::Snowflake<sld::Role>> &roles = member.roles;
                    
                    for (const auto &role : allowed_roles)
                    {
                        if (std::find(roles.begin(), roles.end(), role) != roles.end())
                        {
                            args.remove(0);
                            
                            if (!processCommand(command->ordinal(), message, args))
                            {
                                sendMessage(message.channelID, "Wrong usage for **" + cmd.toStdString() + "**!\n"
                                                               "**Usage:** " + command->getUsage().data());
                            }
                            
                            return;
                        }
                    }
                    
                    sendMessage(message.channelID, "I see what you did there. :smirk:\n"
                                                   "Unfortunately you don't have the permission to use this command.");
                }
                else
                {
                    args.remove(0);
    
                    if (!processCommand(command->ordinal(), message, args))
                    {
                        sendMessage(message.channelID, "Wrong usage for **" + cmd.toStdString() + "**!\n"
                                                       "**Usage:** " + command->getUsage().data());
                    }
                }
            }
        }
    }
}

void JuceDocClient::onReady(sld::Ready readyData)
{
    anon juce::ScopedValueSetter(busy, true);
    logger->info("Initialising JuceDoc...");
    
    if (options.cloneOnStart || !dirJuce.exists())
    {
        cloneRepository();
    }
    
    for (const auto &guild : getServers().vector())
    {
        (void) Storage::createFor(guild.ID);
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
    
    logger->info("JuceDoc is now ready to be used.");
}

void JuceDocClient::onReaction(sld::Snowflake<sld::User> userId, sld::Snowflake<sld::Channel> channelId,
                               sld::Snowflake<sld::Message> messageId, sld::Emoji emote)
{
    if (getUser(userId).cast().bot)
    {
        return;
    }
    
    const sld::Message message = getMessage(channelId, messageId);
    EmbedCacheBuffer   &embeds = Storage::get<EmbedCacheBuffer>(message.serverID);
    
    if (PagedEmbed *const embed = embeds.get(messageId))
    {
        removeReaction(channelId, messageId, emote.name, userId);
        embed->setPage(emote.name == PagedEmbed::emoteLastPage ? PagedEmbed::PageAction::Back
                                                               : PagedEmbed::PageAction::Forward);
        editMessage(message, "", embed->toEmbed());
    }
}

void JuceDocClient::onError(sld::ErrorCode, const std::string errorMessage) { logger->error(errorMessage); }

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
        embed.color       = 0x00ff00;
        
        embed.fields.reserve(Command::values.size());
        
        for (const auto &command : Command::values)
        {
            const juce::String emote = command->getEmote().data();
            const juce::String title = (!emote.isEmpty() ? ":" + emote + ": " : "") + command->name().data();
            embed.fields.emplace_back(title.toStdString(), command->getDescription().data(), false);
        }
        
        sendMessage(message.channelID, "", embed);
    }
    else
    {
        for (const auto &command : Command::values)
        {
            const juce::String name = command->name().data();
            
            if (name.equalsIgnoreCase(cmd))
            {
                juce::String roles = "everyone";
                
                if (!command->getRoleString().empty())
                {
                    const std::vector<sld::Snowflake<sld::Role>> role_ids = command->getRoles();
                    
                    sld::Server server = getServer(message.serverID).cast();
                    roles = server.findRole(role_ids[0])->name;
                    
                    for (int i = 1; i < role_ids.size(); ++i)
                    {
                        roles << ", " << server.findRole(role_ids[i])->name;
                    }
                }
    
                const juce::String emote       = command->getEmote().data();
                const juce::String emote_title = (!emote.isEmpty() ? ":" + emote + ": " : "") + command->name().data();
    
                sld::Embed embed;
                embed.title       = emote_title.toStdString() + "Help for '" + name.toStdString() + "'";
                embed.description = command->getDescription();
                embed.color       = 0x00ff00;
                embed.fields      = {
                    { "Usage",        command->getUsage().data(), false },
                    { "Needed roles", roles.toStdString(),        false }
                };
                
                sendMessage(message.channelID, "", embed);
                return;
            }
        }
    }
}

bool JuceDocClient::processCommand(int commandId, sld::Message &message, juce::StringArray &args)
{
    const juce::String query = args[0];
    
    if (commandId != Command::List.ordinal())
    {
        args.remove(0);
    }
    
    Filter filter;
    
    try
    {
        filter = Filter::createFromInput(args);
    }
    catch (std::invalid_argument &ex)
    {
        if (juce::String(ex.what()).isEmpty())
        {
            return false;
        }
        
        sendMessage(message.channelID, ex.what());
        return true;
    }
    
    if (commandId == Command::List)
    {
        PagedEmbed embed(message.channelID, filter);
        embed.setMaxItemsPerPage(6);
        embed.applyListWithFilter(defCache);


#if JUCE_DEBUG
        logger->debug("Generating embed with filter: " + filter.toString().toStdString());
#endif
        
        sendMessage(message.channelID, "", embed.toEmbed(), sld::TTS::Default, {
        [this, paged = embed](sld::ObjectResponse<sld::Message> response) mutable
        {
            paged.setMessageId(response.cast().ID);
            Storage::get<EmbedCacheBuffer>(response.cast().serverID).push(std::move(paged));
            
            addReaction(response.cast().channelID, response.cast().ID, PagedEmbed::emoteLastPage.data());
            addReaction(response.cast().channelID, response.cast().ID, PagedEmbed::emoteNextPage.data());
        }});
    }
    else if (commandId == Command::Find)
    {
    
    }
    else if (commandId == Command::Show)
    {
    
    }
    else if (commandId == Command::Refresh)
    {
    
    }
    
    return true;
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
                if (en_def)
                {
                    (void) list_enum.emplace_back(*en_def);
                }
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
    const juce::File build_folder("./build");
    build_folder.deleteRecursively();
    juce::File("../../modules").copyDirectoryTo(build_folder);
}

//======================================================================================================================
void JuceDocClient::cloneRepository() const
{
    dirJuce.deleteRecursively();
    logger->info("Cloning latest juce develop commit...");
    
    juce::ChildProcess cloneProcess;
    cloneProcess.start("git clone --branch develop https://github.com/juce-framework/juce.git juce");
    cloneProcess.waitForProcessToFinish(-1);
}
//======================================================================================================================
// endregion JuceDocClient
//**********************************************************************************************************************
//======================================================================================================================
//======================================================================================================================
// endregion JuceDocClient
//**********************************************************************************************************************
