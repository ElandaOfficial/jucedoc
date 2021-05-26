
#pragma once

#include "specs.h"

#include <juce_core/juce_core.h>

class Definition;

class EntityDefinition
{
public:
    struct DoxygenCommandEntry
    {
        juce::String name;
        juce::String value;
    };
    
    struct CommandIds
    {
        static constexpr std::string_view param = "param";
        static constexpr std::string_view see   = "see";
        static constexpr std::string_view code  = "code";
    };
    
    //==================================================================================================================
    using CommandList = std::vector<DoxygenCommandEntry>;
    using CommandMap  = std::unordered_map<juce::String, CommandList>;
    using DefVec      = std::vector<std::reference_wrapper<const Definition>>;
    
    //==================================================================================================================
    static EntityDefinition createFromSymbolPath(const juce::String &symbolPath,
                                                 const venum::VenumMap<EntityType, DefVec> &cache);
    
    //==================================================================================================================
    const Definition* operator->() const noexcept;
    const Definition& operator*()  const noexcept;
    
    //==================================================================================================================
    operator bool() const noexcept;
    
    //==================================================================================================================
    juce::File generateGraph(const juce::File &inputDir, const juce::File &outputDir) const;
    
    //==================================================================================================================
    const CommandList* getCommands(std::string_view name) const;
    
    //==================================================================================================================
    EntityType          getType()          const noexcept;
    const juce::String& getDocumentation() const noexcept;
    const juce::String& getDefinition()    const noexcept;
    const juce::String& getModule()        const noexcept;
    const juce::String& getParent()        const noexcept;
    const Definition*   get()              const noexcept;
    
private:
    const Definition *definition { nullptr };
    EntityType       entityType;
    
    // Command Data
    CommandMap   commandMap;
    juce::String docString;
    juce::String defString;
    
    // Details
    juce::String module;
    juce::String parent;
    
    //==================================================================================================================
    EntityDefinition() = default;
    explicit EntityDefinition(const Definition*, EntityType);
    
    //==================================================================================================================
    void parseAndExtractCommands();
};
