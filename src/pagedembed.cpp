
#include "pagedembed.h"

#include "config.h"
#include "linkresolve.h"

// Doxygen
#include <namespacedef.h>
#include <classlist.h>
#include <groupdef.h>
// Sleepy-Discord
#include <sleepy_discord/embed.h>
// STL
#include <cmath>

namespace
{
    template<bool VIsFunc>
    bool hasMemberSpecs(const MemberDef &member, const Filter &filter)
    {
        const ArgumentList &args = member.argumentList();
    
        if (const auto &linkage = filter.get<Filter::Linkage>();
            !linkage.empty() && !linkage.contains(Linkage::values[static_cast<int>(member.isExternal())]))
        {
            return false;
        }
        
        if (const auto &vtype = filter.get<Filter::VType>();
            !vtype.empty() && !vtype.contains(VarType::values[args.refQualifier()]))
        {
            return false;
        }
    
        {
            const int qualification = static_cast<int>(args.constSpecifier())
                                      + (static_cast<int>(args.volatileSpecifier()) * 2);
            
            if (const auto &quals = filter.get<Filter::Quals>();
                !quals.empty() && !quals.contains(TypeQualifier::values[qualification]))
            {
                return false;
            }
        }
        
        if (const auto &flag = filter.get<Filter::Constexpr>();
            flag.isSet() && (member.isConstExpr() != flag.getValue()))
        {
            return false;
        }
        
        const Ownership ownership = dynamic_cast<NamespaceDef*>(member.getOuterScope()) ? Ownership::Free
                                                                    : member.isFriend() ? Ownership::Friend
                                                                    : member.isStatic() ? Ownership::Static
                                                                                        : Ownership::Member;
        
        if (const auto &mtype = filter.get<Filter::MType>();
            !mtype.empty() && !mtype.contains(ownership))
        {
            return false;
        }
        
        if constexpr (VIsFunc)
        {
            if (const auto &virtualness = filter.get<Filter::Virtual>();
                !virtualness.empty() && !virtualness.contains(Virtualness::values[member.virtualness()]))
            {
                return false;
            }
        }
        
        return true;
    }
    
    bool searchForBaseDfs(const std::vector<std::string_view> &bases, const ClassDef &clazz)
    {
        for (const auto &base : clazz.baseClasses())
        {
            const ClassDef &base_def = *base.classDef;
            
            for (const auto &base_name : bases)
            {
                if (base_name == base_def.qualifiedName().data())
                {
                    return true;
                }
            }
            
            if (!base_def.baseClasses().empty())
            {
                if (searchForBaseDfs(bases, base_def))
                {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    bool classHasBase(const ClassDef &clazz, const std::vector<juce::String> &bases)
    {
        if (bases.empty())
        {
            return true;
        }
        
        std::vector<std::string_view> nested;
        std::vector<std::string_view> locals;
        
        for (const auto &base_name : bases)
        {
            if (base_name[0] != '@')
            {
                nested.emplace_back(base_name.toRawUTF8());
            }
            else
            {
                std::string_view view(base_name.toRawUTF8());
                view.remove_prefix(1);
                
                locals.emplace_back(view);
            }
        }
        
        for (const auto &base : clazz.baseClasses())
        {
            const ClassDef &base_def = *base.classDef;
            
            for (const auto &vec : { &nested, &locals })
            {
                for (const auto &base_name : *vec)
                {
                    if (base_name == base_def.qualifiedName().data())
                    {
                        return true;
                    }
                }
            }
            
            if (!nested.empty())
            {
                if (searchForBaseDfs(nested, base_def))
                {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    bool matches(const juce::String &term, std::string_view data, bool ignoreCase = false)
    {
        return term.isEmpty() || juce::String(data.data()).matchesWildcard("*" + term + "*", ignoreCase);
    }
}

//**********************************************************************************************************************
// region PagedEmbed
//======================================================================================================================
PagedEmbed::PagedEmbed(const sld::Snowflake<sld::Channel> &channelId, Filter filter)
    : filter(std::move(filter)), channelId(channelId)
{}

//======================================================================================================================
void PagedEmbed::applyListWithFilter(const CacheMap &cache, const juce::String &term)
{
    // Filters
    const std::string_view            &class_path = filter.get<Filter::CPath>().toRawUTF8();
    const venum::VenumSet<EntityType> &types      = filter.get<Filter::Entity>();
    
    if (types.contains(EntityType::Namespace))
    {
        for (const auto &def : cache[EntityType::Namespace])
        {
            const auto &ns_def = static_cast<const NamespaceDef&>(def.get());
            
            if (ns_def.qualifiedName().startsWith(class_path.data())
                && ::matches(term, ns_def.localName().data()))
            {
                (void) resultCache.emplace_back(EntityType::Namespace, ns_def);
            }
        }
    }
    
    if (types.contains(EntityType::Class))
    {
        const venum::VenumSet<CompoundType> &ctypes = filter.get<Filter::CType>();
        const std::vector<juce::String>     &bases  = filter.get<Filter::Bases>();
        
        for (const auto &def : cache[EntityType::Class])
        {
            const auto &cs_def = static_cast<const ClassDef&>(def.get());
            if (cs_def.qualifiedName().startsWith(class_path.data())
                && ::matches(term, cs_def.localName().data())
                && ::classHasBase(cs_def, bases))
            {
                if (ctypes.contains(CompoundType::valueOf(cs_def.compoundTypeString().str(), true)))
                {
                    (void) resultCache.emplace_back(EntityType::Class, cs_def);
                }
            }
        }
    }
    
    if (types.contains(EntityType::Enum))
    {
        // Filters
        const venum::VenumSet<CompoundType> &ctypes = filter.get<Filter::CType>();
        
        for (const auto &def : cache[EntityType::Enum])
        {
            const auto &en_def = static_cast<const MemberDef&>(def.get());
            
            if (en_def.qualifiedName().startsWith(class_path.data())
                && ::matches(term, en_def.localName().data()))
            {
                if (en_def.isEnumStruct() ? ctypes.contains(CompoundType::EnumClass)
                                          : ctypes.contains(CompoundType::Enum))
                {
                    (void) resultCache.emplace_back(EntityType::Enum, en_def);
                }
            }
        }
    }
    
    if (types.contains(EntityType::Function))
    {
        for (const auto &def : cache[EntityType::Function])
        {
            const auto &fn_def = static_cast<const MemberDef&>(def.get());
            
            if ((fn_def.qualifiedName().startsWith(class_path.data()))
                && ::hasMemberSpecs<true>(fn_def, filter)
                && ::matches(term, fn_def.localName().data()))
            {
                (void) resultCache.emplace_back(EntityType::Function, fn_def);
            }
        }
    }
    
    if (types.contains(EntityType::Field))
    {
        for (const auto &def : cache[EntityType::Field])
        {
            const auto &var_def = static_cast<const MemberDef&>(def.get());
            
            if ((var_def.qualifiedName().startsWith(class_path.data()))
                && ::hasMemberSpecs<false>(var_def, filter)
                && ::matches(term, var_def.localName().data()))
            {
                (void) resultCache.emplace_back(EntityType::Field, var_def);
            }
        }
    }
    
    if (types.contains(EntityType::TypeAlias))
    {
        for (const auto &def : cache[EntityType::TypeAlias])
        {
            const auto &aka_def = static_cast<const MemberDef&>(def.get());
            
            if (aka_def.qualifiedName().startsWith(class_path.data())
                && ::matches(term, aka_def.localName().data()))
            {
                (void) resultCache.emplace_back(EntityType::TypeAlias, aka_def);
            }
        }
    }
    
    if (const auto &sort_type = filter.get<Filter::Sort>())
    {
        if (sort_type == SortType::Asc)
        {
            std::sort(resultCache.begin(), resultCache.end(), [](const auto &a, const auto &b)
            {
                return a.second.get().qualifiedName() < b.second.get().qualifiedName();
            });
        }
        else if (sort_type == SortType::Desc)
        {
            std::sort(resultCache.begin(), resultCache.end(), [](const auto &a, const auto &b)
            {
                return b.second.get().qualifiedName() < a.second.get().qualifiedName();
            });
        }
    }
}

//======================================================================================================================
sld::Snowflake<sld::Channel> PagedEmbed::getChannelId() const noexcept { return channelId; }
sld::Snowflake<sld::Message> PagedEmbed::getMessageId() const noexcept { return messageId; }
int PagedEmbed::getMaxItemsPerPage()                    const noexcept { return maxItemsPerPage; }
int PagedEmbed::getNumItems()                           const noexcept { return resultCache.size(); }

int PagedEmbed::getMaxPages() const noexcept
{
    return std::max<int>(1, std::ceil(resultCache.size() / static_cast<double>(maxItemsPerPage)));
}

//======================================================================================================================
void PagedEmbed::setMaxItemsPerPage(int newMaxItemsPerPage) noexcept { maxItemsPerPage = newMaxItemsPerPage; }

void PagedEmbed::setMessageId(sld::Snowflake<sld::Message> parMessageId) { std::swap(messageId, parMessageId); }

//======================================================================================================================
void PagedEmbed::prevPage() noexcept
{
    currentPage = std::clamp(currentPage - 1, 0, getMaxPages() - 1);
}

void PagedEmbed::nextPage() noexcept
{
    currentPage = std::clamp(currentPage + 1, 0, getMaxPages() - 1);
}

void PagedEmbed::setPage(PageAction action) noexcept
{
    if (action == PageAction::Back)
    {
        prevPage();
    }
    else if (action == PageAction::Forward)
    {
        nextPage();
    }
}

//======================================================================================================================
void PagedEmbed::reset()
{
    resultCache.clear();
}

//======================================================================================================================
sld::Embed PagedEmbed::toEmbed(const juce::String &title, std::uint32_t colour) const
{
    const int num_items   = std::min<int>(resultCache.size() - (currentPage * maxItemsPerPage), maxItemsPerPage);
    const int start_index = currentPage * maxItemsPerPage;
    
    sld::Embed embed;
    embed.title = title.toRawUTF8();
    embed.color = colour;
    embed.fields.reserve(3 + num_items);
    embed.fields.emplace_back("Page",  std::to_string(currentPage + 1) + "/" + std::to_string(getMaxPages()), true);
    embed.fields.emplace_back("Items",
                              std::to_string(start_index + static_cast<int>(maxItemsPerPage > 0)) + " to "
                                  + std::to_string(start_index + num_items),
                              true);
    embed.fields.emplace_back("Of", std::to_string(getNumItems()), true);
    
    for (int i = start_index; i < (start_index + num_items); ++i)
    {
        const ResultDef &def = resultCache[i];
        
        juce::String field_desc;
        field_desc << "**Path:** " << def.second.get().qualifiedName().data() << "\n"
                   << "**Type:** " << def.first->name()               .data() << "\n";
        
        juce::String module = "All";
    
        const Definition *group_def = &def.second.get();
    
        if (dynamic_cast<const MemberDef*>(&def.second.get()))
        {
            group_def = def.second.get().getOuterScope();
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
        
        field_desc << "**Module:** " << module << "\n";
        
        if (const Definition *scope = def.second.get().getOuterScope())
        {
            field_desc << "**Parent:** " << scope->qualifiedName().data() << "\n";
        }
        else
        {
            field_desc << "**Parent:** None\n";
        }
        
        juce::String description;
        
        if (def.second.get().hasBriefDescription() && !def.second.get().briefDescription().isEmpty())
        {
            description = juce::String(def.second.get().briefDescription().data());
        }
        else if (def.second.get().hasDocumentation() && !def.second.get().documentation().isEmpty())
        {
            const juce::String doc = def.second.get().documentation().data();
    
            description = juce::String(doc).substring(0, 60);
            
            if (doc.length() > 60)
            {
                description << "...";
            }
        }
        
        if (description.isEmpty() || description == "/internal")
        {
            description = "No docs available";
        }
        
        const AppConfig    &config = AppConfig::getInstance();
        const juce::String doc_url = AppInfo::urlJuceDocsBase.data() + config.branchName + "/";
        field_desc << "**Doc:** " << description << "\n"
                   << "[Go to official docs](" << doc_url << ::getUrlFromEntity(def.first, def.second) << ")";
        field_desc = field_desc.substring(0, std::min(1024, field_desc.length()));
    
        embed.footer.iconUrl = AppIcon::LogoGitHub.getUrl();
        embed.timestamp      = config.currentCommit.date.toStdString();
        embed.footer.text    = config.currentCommit.name.substring(0, 9).toStdString()
                               + " (" + config.branchName.toStdString() + ")";
    
    
        embed.fields.emplace_back(def.second.get().localName().data(), field_desc.toRawUTF8(), false);
    }
    
    return embed;
}
//======================================================================================================================
// endregion PagedEmbed
//**********************************************************************************************************************
