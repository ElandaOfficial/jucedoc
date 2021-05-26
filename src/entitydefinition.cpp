
#include "entitydefinition.h"

#include "linkresolve.h"

// Doxygen
#include <classdef.h>
#include <dotclassgraph.h>
#include <groupdef.h>
#include <memberdef.h>
#include <textstream.h>
// GraphViz
#include <graphviz/gvc.h>
// STL
#include <regex>

namespace
{
    void reduceDoubleSpacesAndRemoveNewLines(juce::String &input)
    {
        juce::String output;
        bool was_space = false;
        
        for (auto c : input)
        {
            if (was_space && juce::CharacterFunctions::isWhitespace(c))
            {
                continue;
            }
            
            if (c != '\n')
            {
                output += c;
            }
            
            was_space = juce::CharacterFunctions::isWhitespace(c);
        }
        
        std::swap(input, output);
    }
    
    //==================================================================================================================
    juce::String getDefinitionString(const Definition &definition)
    {
        juce::String output;
        
        if (const MemberDef *member = dynamic_cast<const MemberDef*>(&definition))
        {
            output << juce::String(member->definition().data()).replace("juce::", "");
        
            if (member->argumentList().hasParameters())
            {
                output << "(";
            
                const ArgumentList &arg_list = member->argumentList();
            
                for (auto it = arg_list.begin(); it != arg_list.end(); ++it)
                {
                    const int index = std::distance(arg_list.begin(), it);
                
                    if (member->isDefine())
                    {
                        output << it->type.data();
                    
                        if (index < (arg_list.size() - 1))
                        {
                            output << ",";
                        }
                    }
                    else
                    {
                        output << it->attrib.data();
                    
                        if (it->type != "...")
                        {
                            output << "\n    " << juce::String(it->type.data()).replace(" *", "*").replace(" &", "&");
                        }
                    
                        if (!it->name.isEmpty() || it->type == "...")
                        {
                            output << (it->name.isEmpty() ? "\n    " + it->type.str() : " " + it->name.str());
                        }
    
                        output << it->array.data();
                    
                        if (!it->defval.isEmpty())
                        {
                            output << " = " << it->defval.data();
                        }
                    
                        if (index < (arg_list.size() - 1))
                        {
                            output << ",";
                        }
                    }
                }
            
                if (!arg_list.empty())
                {
                    output << "\n";
                }
    
                output << ")" << member->extraTypeChars().data();
            
                if (arg_list.constSpecifier())
                {
                    output << " const ";
                }
            
                if (arg_list.volatileSpecifier())
                {
                    output << " volatile ";
                }
            
                if (arg_list.refQualifier() != RefQualifierNone)
                {
                    output << (arg_list.refQualifier() == RefQualifierLValue ? "&" : "&&");
                }
    
                output << arg_list.trailingReturnType().data();
            }
        
            if (member->hasOneLineInitializer())
            {
                if (member->isDefine())
                {
                    output << "   ";
                }
    
                output << member->initializer().data();
            }
        
            if (!member->isNoExcept())
            {
                output << member->excpString().data();
            }
        }
        
        return output;
    }
    
    //==================================================================================================================
    juce::String getModule(const Definition &definition)
    {
        juce::String     module     = "All";
        const Definition *group_def = &definition;
    
        if (dynamic_cast<const MemberDef*>(&definition))
        {
            group_def = definition.getOuterScope();
        }
        
        if (const auto &groups = group_def->partOfGroups(); !groups.empty() && group_def->localName() != "juce")
        {
            module.clear();
            
            if (groups[0]->localName().contains("-"))
            {
                module << groups[0]->groupTitle().rawData() << " ("
                       << juce::String(groups[0]->localName().data()).upToFirstOccurrenceOf("-", false, true)
                       << ")";
            }
            else
            {
                module << groups[0]->groupTitle().rawData();
            }
        }
        else
        {
            module = "Unknown";
        }
        
        return module;
    }
    
    //==================================================================================================================
    template<class ...TVecs>
    std::tuple<const EntityDefinition::DefVec*, const Definition*> findDefinition(const juce::String &term,
                                                                                  const TVecs &...vecs)
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
    
        return std::make_tuple(static_cast<const EntityDefinition::DefVec*>(nullptr),
                               static_cast<const Definition*>(nullptr));
    }
}

//**********************************************************************************************************************
// region EntityDefinition
//======================================================================================================================
EntityDefinition EntityDefinition::createFromSymbolPath(const juce::String &symbolPath,
                                                        const venum::VenumMap<EntityType, DefVec> &cache)
{
    const DefVec &list_namespace = cache[EntityType::Namespace];
    const DefVec &list_class     = cache[EntityType::Class];
    const DefVec &list_enum      = cache[EntityType::Enum];
    const DefVec &list_func      = cache[EntityType::Function];
    const DefVec &list_var       = cache[EntityType::Field];
    const DefVec &list_alias     = cache[EntityType::TypeAlias];
    
    const juce::String last_name = symbolPath.fromLastOccurrenceOf("::", false, true);
    const Definition   *def      = nullptr;
    EntityType         type;
    
    if (!last_name.isEmpty())
    {
        // Likely a function, namespace or variable
        if (juce::CharacterFunctions::isLowerCase(last_name[0]))
        {
            const std::tuple<const DefVec *, const Definition *> found = ::findDefinition(symbolPath, list_func,
                                                                                                      list_namespace,
                                                                                                      list_var,
                                                                                                      list_class,
                                                                                                      list_enum,
                                                                                                      list_alias);
        
            if (const auto *const vec = std::get<0>(found))
            {
                type = std::find_if(cache.begin(), cache.end(), [vec](auto &&entry)
                {
                    return vec == &entry.second;
                })->first;
                def = std::get<1>(found);
            }
        }
            // Likely a class, enum, type-alias or constructor
        else if (juce::CharacterFunctions::isUpperCase(last_name[0]))
        {
            const std::tuple<const DefVec *, const Definition *> found = ::findDefinition(symbolPath, list_class,
                                                                                                      list_enum,
                                                                                                      list_alias,
                                                                                                      list_func,
                                                                                                      list_namespace,
                                                                                                      list_var);
        
            if (const auto *const vec = std::get<0>(found))
            {
                type = std::find_if(cache.begin(), cache.end(),
                    [vec](auto &&entry)
                    {
                        return vec == &entry.second;
                    }
                )->first;
                def = std::get<1>(found);
            }
        }
    }
    // Single word path, can only be one of the juce namespaces but let's search anyway for future compatibility
    else
    {
        const std::tuple<const DefVec*, const Definition*> found = ::findDefinition(symbolPath, list_namespace,
                                                                                                list_class,
                                                                                                list_enum,
                                                                                                list_func,
                                                                                                list_var,
                                                                                                list_alias);
        
        if (const auto *const vec = std::get<0>(found))
        {
            type = std::find_if(cache.begin(), cache.end(),
                [vec](auto &&entry)
                {
                    return vec == &entry.second;
                }
            )->first;
            def = std::get<1>(found);
        }
    }
    
    if (!def)
    {
        for (const auto &class_def : list_class)
        {
            const ClassDef &cs_def = static_cast<const ClassDef&>(class_def.get());
            
            if (const MemberList *const list = cs_def.getMemberList(MemberListType_constructors))
            {
                for (const auto &constructor : *list)
                {
                    if (constructor->qualifiedName() == symbolPath.toRawUTF8())
                    {
                        def  = constructor;
                        type = EntityType::Function;
                        break;
                    }
                }
            }
        }
    }
    
    return (!def ? EntityDefinition() : EntityDefinition(def, type));
}

//======================================================================================================================
const Definition& EntityDefinition::operator*()  const noexcept { return *definition; }
const Definition* EntityDefinition::operator->() const noexcept { return definition;  }

//======================================================================================================================
EntityDefinition::operator bool() const noexcept { return definition != nullptr; }

//======================================================================================================================
juce::File EntityDefinition::generateGraph(const juce::File &inputDir, const juce::File &outputDir) const
{
    if (const ClassDef *class_def = dynamic_cast<const ClassDef*>(definition))
    {
        if (class_def->baseClasses().empty())
        {
            return {};
        }
        
        const juce::String class_name = sanitiseUrl(class_def->qualifiedName().data(), true);
        const juce::File   dot_file   = inputDir.getChildFile(class_def->compoundTypeString().lower().data()
                                                              + class_name + "__inherit__graph.dot");
        
        if (!dot_file.exists())
        {
            TextStream text_stream;
            DotClassGraph cgraph(class_def, GraphType::Inheritance);
            cgraph.writeGraph(text_stream, GraphOutputFormat::GOF_BITMAP, EmbeddedOutputFormat::EOF_Html,
                              inputDir.getFullPathName().toRawUTF8(), "", "");
        }
    
        const juce::File img_file = outputDir.getChildFile(class_name + ".jpg");
        std::array<char*, 5> gv_args {
            strdup("dot"), strdup("-Tjpg"), strdup("-Gbgcolor=white"),
            strdup(dot_file.getFullPathName().toRawUTF8()), strdup(("-o" + img_file.getFullPathName()).toRawUTF8())
        };
        
        std::unique_ptr<GVC_t, void (*)(GVC_t *)> ctx(gvContext(), [](GVC_t *ptr) { gvFreeContext(ptr); });
        gvParseArgs(ctx.get(), gv_args.size(), gv_args.data());
        
        FILE *in = fopen(dot_file.getFullPathName().toRawUTF8(), "r");
        
        Agraph_t *const g = agread(in, 0);
        {
            gvLayoutJobs(ctx.get(), g);
            gvRenderJobs(ctx.get(), g);
            
            gvFreeLayout(ctx.get(), g);
            agclose(g);
        }
    
        for (const auto &ptr : gv_args)
        {
            free(ptr);
        }
    
        return (img_file.exists() ? img_file : juce::File{});
    }
    
    return {};
}

//======================================================================================================================
const EntityDefinition::CommandList* EntityDefinition::getCommands(std::string_view name) const
{
    auto it = commandMap.find(name.data());
    return (it != commandMap.end() ? &it->second : nullptr);
}

//======================================================================================================================
EntityType          EntityDefinition::getType()          const noexcept { return entityType; }
const juce::String& EntityDefinition::getDocumentation() const noexcept { return docString;  }
const juce::String& EntityDefinition::getDefinition()    const noexcept { return defString;  }
const juce::String& EntityDefinition::getModule()        const noexcept { return module;     }
const juce::String& EntityDefinition::getParent()        const noexcept { return parent;     }
const Definition*   EntityDefinition::get()              const noexcept { return definition; }

//======================================================================================================================
EntityDefinition::EntityDefinition(const Definition *def, EntityType type)
    : definition(def), entityType(type),
      docString(juce::String(def->briefDescription().data()).trimCharactersAtEnd("\n ")),
      defString(::getDefinitionString(*def)),
      module(::getModule(*def)),
      parent(def->getOuterScope() ? def->getOuterScope()->qualifiedName().data() : "")
{
    parseAndExtractCommands();
}

//======================================================================================================================
void EntityDefinition::parseAndExtractCommands()
{
    enum MatchId
    {
        All,
        Docs,
        Command,
        CommandArgs,
        CodeBlock
    };
    
    //==================================================================================================================
    const std::string documentation = definition->documentation().str();
    juce::String      desc;
    
    if (documentation.find("@") != std::string::npos)
    {
        const std::regex regex(R"REX(([^@]+)|)REX"                                         // Match documentation
                               R"REX((?:@((?!code)[^@]+?)\s+?([^@]+?)(?=^\s*$|@|\Z))|)REX" // Match normal commands
                               R"REX((?:@(?:code)\s([^@]+?)(?:@endcode)))REX");            // Match code blocks
        int example_count    = 0;
        std::string::const_iterator it = documentation.begin();
        std::smatch m;
        
        while (std::regex_search(it, documentation.end(), m, regex))
        {
            // Just documentation
            if (m[MatchId::Docs].matched)
            {
                desc << m.str(MatchId::Docs);
            }
            // Normal commands
            else if (m[MatchId::Command].matched)
            {
                const juce::String command = m.str(MatchId::Command);
                juce::String       data    = m.str(MatchId::CommandArgs);
    
                if (command == "param")
                {
                    juce::String name = data.upToFirstOccurrenceOf(" ", false, true);
                    juce::String docs = data.fromFirstOccurrenceOf(" ", false, true).trim();
                    
                    ::reduceDoubleSpacesAndRemoveNewLines(docs);
                    commandMap["param"].emplace_back(DoxygenCommandEntry{ std::move(name), std::move(docs) });
                }
                else if (command == "see" || command == "seealso")
                {
                    commandMap["see"].emplace_back(DoxygenCommandEntry{ juce::String{}, std::move(data)});
                }
            }
            // Code block commands
            else if (m[MatchId::CodeBlock].matched)
            {
                commandMap["code"].emplace_back(DoxygenCommandEntry { juce::String(), m.str(MatchId::CodeBlock) });
                desc << "_See example " << ++example_count << "_";
            }
            
            it = m.suffix().first;
        }
    }
    else
    {
        desc = juce::String(documentation).trim();
    }
    
    docString << ((!docString.isEmpty() && !desc.isEmpty()) ? "\n\n" : "") << desc.trimCharactersAtEnd("\n ") << "\n";
}
//======================================================================================================================
// endregion EntityDefinition
//**********************************************************************************************************************
