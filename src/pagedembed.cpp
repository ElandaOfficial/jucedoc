
#include "pagedembed.h"
#include "config.h"

#include <namespacedef.h>
#include <classlist.h>
#include <sleepy_discord/embed.h>

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
    
    juce::String sanitiseUrl(std::string_view name)
    {
        name.remove_prefix(6);
        return juce::String(name.begin(), name.size()).replace("_", "__").replace("::", "_1_1");
    }
    
    juce::String getUrlFromEntity(EntityType entityType, const Definition &def)
    {
        juce::String output;
        
        if (entityType == EntityType::Namespace)
        {
            output = "namespace";
        }
        else if (entityType == EntityType::Class)
        {
            output = static_cast<const ClassDef&>(def).compoundTypeString().lower().data();
        }
        
        return output + sanitiseUrl(def.qualifiedName().data());
    }
}

//**********************************************************************************************************************
// region PagedEmbed
//======================================================================================================================
PagedEmbed::PagedEmbed(const sld::Snowflake<sld::Channel> &channelId, Filter filter)
    : filter(std::move(filter)), channelId(channelId)
{}

//======================================================================================================================
void PagedEmbed::applyListWithFilter(const CacheMap &cache)
{
    // Filters
    const std::string_view            &class_path = filter.get<Filter::CPath>().toRawUTF8();
    const venum::VenumSet<EntityType> &types      = filter.get<Filter::Entity>();
    
    if (types.contains(EntityType::Namespace))
    {
        for (const auto &def : cache[EntityType::Namespace])
        {
            const auto &ns_def = static_cast<const NamespaceDef&>(def.get());
            
            if (ns_def.qualifiedName().startsWith(class_path.data()))
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
            
            if (cs_def.qualifiedName().startsWith(class_path.data()))
            {
                if (ctypes.contains(CompoundType::valueOf(cs_def.compoundTypeString().str(), true)))
                {
                    if (!bases.empty())
                    {
                        const BaseClassList &base_classes = cs_def.baseClasses();
                        const auto it = std::find_if(base_classes.begin(), base_classes.end(),
                            [&bases](const BaseClassDef &baseClass)
                            {
                                return std::find_if(bases.begin(), bases.end(), [&baseClass](const juce::String &name)
                                {
                                    return baseClass.classDef->qualifiedName() == name.toRawUTF8();
                                }) != bases.end();
                            });
                        
                        if (it == base_classes.end())
                        {
                            continue;
                        }
                    }
                    
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
            
            if (en_def.qualifiedName().startsWith(class_path.data()))
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
                && ::hasMemberSpecs<true>(fn_def, filter))
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
                && ::hasMemberSpecs<false>(var_def, filter))
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
            
            if (aka_def.qualifiedName().startsWith(class_path.data()))
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
            
            if (ns_def.qualifiedName().startsWith(class_path.data()) && ns_def.localName().contains(term.toRawUTF8()))
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
            
            if (cs_def.qualifiedName().startsWith(class_path.data()) && cs_def.localName().contains(term.toRawUTF8()))
            {
                if (ctypes.contains(CompoundType::valueOf(cs_def.compoundTypeString().str(), true)))
                {
                    if (!bases.empty())
                    {
                        const BaseClassList &base_classes = cs_def.baseClasses();
                        const auto it = std::find_if(base_classes.begin(), base_classes.end(),
                             [&bases](const BaseClassDef &baseClass)
                             {
                                 return std::find_if(bases.begin(), bases.end(), [&baseClass](const juce::String &name)
                                 {
                                     return baseClass.classDef->qualifiedName() == name.toRawUTF8();
                                 }) != bases.end();
                             });
                        
                        if (it == base_classes.end())
                        {
                            continue;
                        }
                    }
                    
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
            
            if (en_def.qualifiedName().startsWith(class_path.data()) && en_def.localName().contains(term.toRawUTF8()))
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
                && ::hasMemberSpecs<true>(fn_def, filter) && fn_def.localName().contains(term.toRawUTF8()))
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
                && ::hasMemberSpecs<false>(var_def, filter) && var_def.localName().contains(term.toRawUTF8()))
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
            
            if (aka_def.qualifiedName().startsWith(class_path.data()) && aka_def.localName().contains(term.toRawUTF8()))
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
    embed.fields.reserve(2 + num_items);
    embed.fields.emplace_back("Page",  std::to_string(currentPage + 1) + "/" + std::to_string(getMaxPages()), true);
    embed.fields.emplace_back("Items",
                              std::to_string(start_index + static_cast<int>(maxItemsPerPage > 0)) + " to "
                                  + std::to_string(start_index + num_items),
                              true);
    
    for (int i = start_index; i < (start_index + num_items); ++i)
    {
        const ResultDef &def = resultCache[i];
        
        juce::String field_desc;
        field_desc << "Path: " << def.second.get().qualifiedName().data() << "\n"
                   << "Type: " << def.first->name()           .data() << "\n";
        
        if (const Definition *scope = def.second.get().getOuterScope())
        {
            field_desc << "Parent: " << scope->qualifiedName().data() << "\n";
        }
        else
        {
            field_desc << "Parent: [root]\n";
        }
        
        if (def.second.get().hasBriefDescription() && !def.second.get().briefDescription().isEmpty())
        {
            field_desc << "Doc: " << juce::String(def.second.get().briefDescription().data()) << "\n";
        }
        else if (def.second.get().hasDocumentation() && !def.second.get().documentation().isEmpty())
        {
            const juce::String doc = def.second.get().documentation().data();
            
            field_desc << "Doc: " << juce::String(doc).substring(0, 60);
            
            if (doc.length() > 60)
            {
                field_desc += "...";
            }
            
            field_desc += "\n";
        }
        else
        {
            field_desc << "Doc: No docs available \n";
        }
        
        field_desc << "[Go to official docs](" << AppConfig::urlJuceDocsBase.data()
                                               << ::getUrlFromEntity(def.first, def.second) << ".html)";
        
        embed.fields.emplace_back(def.second.get().localName().data(), field_desc.toRawUTF8(), false);
    }
    
    return embed;
}

//======================================================================================================================
// endregion PagedEmbed
//**********************************************************************************************************************
