
#include "jucedocclient.h"

#include "filter.h"
#include "linkresolve.h"
#include "processsourcefiles.h"

#include <dir.h>
#include <doxygen.h>
#include <outputgen.h>
#include <parserintf.h>
#include <classdef.h>
#include <filedef.h>
#include <util.h>
#include <classlist.h>
#include <config.h>
#include <filename.h>
#include <membername.h>
#include <groupdef.h>
#include <dotclassgraph.h>

#include <filesystem>

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
    
    //==================================================================================================================
    template<class ...TVecs>
    std::tuple<const DefVec*, const Definition*> findDefinition(const juce::String &term, const TVecs &...vecs)
    {
        for (const auto &vec : { &vecs... })
        {
            for (const auto &def : *vec)
            {
                if (def.get().qualifiedName() == term.toRawUTF8())
                {
                    return std::make_tuple(vec, &def.get());
                }
            }
        }
        
        return std::make_tuple(static_cast<const DefVec*>(nullptr), static_cast<const Definition*>(nullptr));
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
JuceDocClient::JuceDocClient(const juce::String &parToken, juce::String parClientId, Options options)
    : sld::DiscordClient(parToken.toStdString()),
      options(std::move(options)), clientId(std::move(parClientId)),
      dirJuce(dirRoot.getChildFile(this->options.branch)), dirDocs(dirJuce.getChildFile("docs/doxygen"))
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
        (void) Storage::createFor(guild.ID, EmbedCacheBuffer(options.pageCacheSize));
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
    
    logger->info("JuceDoc is now ready to be used.");
}

void JuceDocClient::onError(sld::ErrorCode, const std::string errorMessage) { logger->error(errorMessage); }

void JuceDocClient::onServer(sld::Server server)
{
    (void) Storage::createFor(server.ID, EmbedCacheBuffer(options.pageCacheSize));
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
                embed.color       = Colours::Help;
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
    juce::String query;
    
    if (commandId != Command::List.ordinal())
    {
        if (args.isEmpty())
        {
            return false;
        }
        
        std::swap(query, args.getReference(0));
        args.remove(0);
    }
    
    if (commandId == Command::Show)
    {
        const DefVec &list_namespace = defCache[EntityType::Namespace];
        const DefVec &list_class     = defCache[EntityType::Class];
        const DefVec &list_enum      = defCache[EntityType::Enum];
        const DefVec &list_func      = defCache[EntityType::Function];
        const DefVec &list_var       = defCache[EntityType::Field];
        const DefVec &list_alias     = defCache[EntityType::TypeAlias];
        
        const juce::String last_name = query.fromLastOccurrenceOf("::", false, true);
        const Definition   *def      = nullptr;
        EntityType         type;
        
        if (last_name.isEmpty())
        {
            const std::tuple<const DefVec*, const Definition*> found
                = ::findDefinition(query, list_namespace, list_class, list_enum, list_func, list_var, list_alias);
            
            if (const auto *const vec = std::get<0>(found))
            {
                type = std::find_if(defCache.begin(), defCache.end(), [vec](auto &&entry)
                {
                    return vec == &entry.second;
                })->first;
                def = std::get<1>(found);
            }
        }
        else
        {
            // Likely a function, variable or namespace
            if (juce::CharacterFunctions::isLowerCase(last_name[0]))
            {
                const std::tuple<const DefVec*, const Definition*> found
                    = ::findDefinition(query, list_func, list_namespace, list_var, list_class, list_enum, list_alias);
                
                if (const auto *const vec = std::get<0>(found))
                {
                    type = std::find_if(defCache.begin(), defCache.end(), [vec](auto &&entry)
                    {
                        return vec == &entry.second;
                    })->first;
                    def = std::get<1>(found);
                }
            }
            else
            {
                const std::tuple<const DefVec*, const Definition*> found
                    = ::findDefinition(query, list_class, list_enum, list_alias, list_func, list_namespace, list_var);
                
                if (const auto *const vec = std::get<0>(found))
                {
                    type = std::find_if(defCache.begin(), defCache.end(), [vec](auto &&entry)
                    {
                        return vec == &entry.second;
                    })->first;
                    def = std::get<1>(found);
                }
            }
        }
        
        if (!def)
        {
            sendMessage(message.channelID, "Sorry, I could not find any entity for: " + query.toStdString() + ". :/");
            return true;
        }
        
        sld::Embed embed;
        embed.title = def->name().data();
        embed.url   = (AppConfig::urlJuceDocsBase.data() + options.branch + "/"
                       + getUrlFromEntity(type, *def)).toRawUTF8();
        embed.color = Colours::Show;
        
        juce::String description;
        description << (def->documentation().isEmpty()
                           ? (def->briefDescription().isEmpty() ? "No description": def->briefDescription())
                           : def->documentation()).data();
        
        juce::String definition;
        
        if (const MemberDef *member = dynamic_cast<const MemberDef*>(def))
        {
            definition << member->definition().data();
            
            if (member->argumentList().hasParameters())
            {
                definition << "(";
                
                const ArgumentList &arg_list = member->argumentList();
                
                for (auto it = arg_list.begin(); it != arg_list.end(); ++it)
                {
                    const int index = std::distance(arg_list.begin(), it);
                    
                    if (member->isDefine())
                    {
                        definition << it->type.data();
                        
                        if (index < (arg_list.size() - 1))
                        {
                            definition << ",";
                        }
                    }
                    else
                    {
                        definition << it->attrib.data();
                        
                        if (it->type != "...")
                        {
                            definition << juce::String(it->type.data()).replace(" *", "*").replace(" &", "&");
                        }
                        
                        if (!it->name.isEmpty() || it->type == "...")
                        {
                            if (it->name.isEmpty())
                            {
                                definition << it->type.data();
                            }
                            else
                            {
                                definition << " " << it->name.data();
                            }
                        }
                        
                        definition << it->array.data();
                        
                        if (!it->defval.isEmpty())
                        {
                            definition << " = " << it->defval.data();
                        }
    
                        if (index < (arg_list.size() - 1))
                        {
                            definition << ",";
                        }
                    }
                }
                
                definition << ")" << member->extraTypeChars().data();
                
                if (arg_list.constSpecifier())
                {
                    definition << " const ";
                }
                
                if (arg_list.volatileSpecifier())
                {
                    definition << " volatile ";
                }
                
                if (arg_list.refQualifier() != RefQualifierNone)
                {
                    definition << (arg_list.refQualifier() == RefQualifierLValue ? "&" : "&&");
                }
                
                definition << arg_list.trailingReturnType().data();
            }
            
            if (member->hasOneLineInitializer())
            {
                if (member->isDefine())
                {
                    definition << "   ";
                }
                
                definition << member->initializer().data();
            }
            
            if (!member->isNoExcept())
            {
                definition << member->excpString().data();
            }
        }
        
        description << (!definition.isEmpty() ? ("\n```c++\n" + definition + "\n```") : "");
        embed.description = description.toRawUTF8();
    
        juce::String module = "All";
        
        const Definition *group_def = def;
        
        if (dynamic_cast<const MemberDef*>(def))
        {
            group_def = def->getOuterScope();
        }
        
        if (group_def)
        {
            if (const auto &groups = group_def->partOfGroups(); !groups.empty() && group_def->localName() != "juce")
            {
                module.clear();
                module << groups[0]->groupTitle().rawData() << " ("
                       << juce::String(groups[0]->localName().data()).upToFirstOccurrenceOf("-", false, true) << ")";
            }
        }
        else
        {
            module = "Unknown";
        }
        
        juce::String parent = "**Parent:** None\n";
        
        if (const Definition *scope = def->getOuterScope())
        {
            parent.clear();
            parent << "**Parent:** " << scope->qualifiedName().data() << "\n";
        }
        
        embed.fields = {
            { "Details",
              (juce::String("**Path:**") + def->qualifiedName().data() + "\n"
               "**Type:**"               + type->name().data()         + "\n"
               "**Module:**"             + module                      + "\n"
               "**Parent:**"             + parent                      + "\n").toRawUTF8()
            }
        };
        
        if (type == EntityType::Class)
        {
            const ClassDef &clazz = static_cast<const ClassDef&>(*def);
            
            DotClassGraph cgraph(&clazz, GraphType::Inheritance);
    
            TextStream text_stream;
            cgraph.writeGraph(text_stream, GraphOutputFormat::GOF_BITMAP, EmbeddedOutputFormat::EOF_Html,
                              dirRoot.getFullPathName().toRawUTF8(), "graph.png", "");
        }
        else
        {
            sendMessage(message.channelID, "", embed);
        }
    }
    else if (commandId == Command::Refresh)
    {
    
    }
    else
    {
        Filter filter;
    
        try
        {
            filter = Filter::createFromInput(args);
        }
        catch (std::invalid_argument &ex)
        {
            if (std::string_view(ex.what()).empty())
            {
                return false;
            }
        
            sendMessage(message.channelID, ex.what());
            return true;
        }
    
        PagedEmbed embed(message.channelID, filter);
        embed.setMaxItemsPerPage(5);
        
        juce::String  title;
        std::uint32_t colour;
        
        if (commandId == Command::List)
        {
            embed.applyListWithFilter(defCache, "");
            title  = "Entity List";
            colour = Colours::List;
        }
        else if (commandId == Command::Find)
        {
            embed.applyListWithFilter(defCache, query);
            title  = "Symbols found for: " + query;
            colour = Colours::Find;
        }
        else
        {
            return true;
        }
    
        JD_DBG("Generating embed with filters: " + filter.toString().toStdString());
    
        sendMessage(message.channelID, "", embed.toEmbed(title, colour), sld::TTS::Default, {
            [this, embed, sid = message.serverID](sld::ObjectResponse<sld::Message> response) mutable
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

                (void) applyData<EmbedCacheBuffer>(sid, [this, &embed, &msg](auto &&cacheBuffer)
                {
                    JD_DBG("Added embed to cache for message: '" + msg.ID.string() + "'");
    
                    embed.setMessageId(msg.ID);
                    cacheBuffer.push(std::move(embed));
    
                    addReaction(msg.channelID, msg.ID, PagedEmbed::emoteLastPage.data());
                    addReaction(msg.channelID, msg.ID, PagedEmbed::emoteNextPage.data());
                });
            }
        });
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
    
    processSourceFiles(juce::File("../../modules"), build_folder);
}

//======================================================================================================================
void JuceDocClient::cloneRepository() const
{
    const juce::String &branch = options.branch;
    
    dirJuce.deleteRecursively();
    logger->info("Cloning latest juce " + branch.toStdString() + " commit...");
    
    juce::ChildProcess cloneProcess;
    cloneProcess.start("git clone --branch " + branch + " https://github.com/juce-framework/juce.git " + branch);
    cloneProcess.waitForProcessToFinish(-1);
}
//======================================================================================================================
// endregion JuceDocClient
//**********************************************************************************************************************
//======================================================================================================================
//======================================================================================================================
// endregion JuceDocClient
//**********************************************************************************************************************
