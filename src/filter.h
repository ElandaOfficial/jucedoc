
#pragma once

#include "specs.h"

#include <juce_core/juce_core.h>
#include <jaut_core/jaut_core.h>

#include <variant>


class Filter
{
public:
    class Flag
    {
    public:
        bool getValue() const noexcept { return mask.test(1); }
        bool isSet()    const noexcept { return mask.test(0); }
        
        void setValue(bool value) noexcept
        {
            mask.set(0);
            mask.set(1, value);
        }
        
    private:
        std::bitset<2> mask;
    };

private:
    template<class TypeArray, class T1, class ...Ts>
    struct FilterDuplicates : FilterDuplicates<std::conditional_t<(std::is_same_v<T1, Ts> || ...),
                                                                  TypeArray, typename TypeArray::template add<T1>>,
                                              Ts...>
    {};
    
    template<class TypeArray, class T1>
    struct FilterDuplicates<TypeArray, T1>
    {
        using type = typename TypeArray::template add<T1>;
    };
    
    template<class Wrapper> struct FilterWrapper;
    
    template<template<class...> class Wrapper, class ...Ts>
    struct FilterWrapper<Wrapper<Ts...>>
    {
        using type = typename FilterDuplicates<jaut::TypeArray<>, Ts...>::type::template to<Wrapper>;
    };
    
    struct TypeMap : jaut::TypeArray <
                         venum::VenumSet<EntityType>,
                         venum::VenumSet<Ownership>,
                         venum::VenumSet<VarType>,
                         venum::VenumSet<CompoundType>,
                         venum::VenumSet<Linkage>,
                         venum::VenumSet<TypeQualifier>,
                         venum::VenumSet<Virtualness>,
                         
                         juce::String,
                         Flag,
                         
                         SortType
                     >
    {
        //==============================================================================================================
        static_assert(forAll<jaut::AllDefaultConstructible>(), "All types must be default constructible");
        
        //==============================================================================================================
        static constexpr std::array<std::string_view, TypeMap::size> names {
            "entity",
            "mtype",
            "vtype",
            "ctype",
            "linkage",
            "quals",
            "virtual",
            
            "cpath",
            "constexpr",
            
            "sort"
        };
        
        template<std::size_t I>
        static constexpr auto create() { return Option{ names[I], getDefault<I>() }; }
    
        template<std::size_t I>
        static constexpr at<I> getDefault()
        {
            using T = at<I>;
            
            if constexpr (jaut::sameTypeIgnoreTemplate_v<venum::VenumSet, T>)
            {
                return T::all();
            }
            else
            {
                return construct<I>();
            }
        }
    };
    
    struct Option
    {
        std::string_view name;
        FilterWrapper<TypeMap::to<std::variant>>::type value;
        
        //==============================================================================================================
        bool operator==(std::string_view otherName) const noexcept { return otherName == name; }
        bool operator!=(std::string_view otherName) const noexcept { return otherName != name; }
    
        //==============================================================================================================
        juce::String toString() const
        {
            juce::String output;
            output << name.data() << ":";
            
            output << std::visit([](auto &&val) -> juce::String
            {
                using T = std::decay_t<decltype(val)>;
                
                if constexpr (jaut::sameTypeIgnoreTemplate_v<venum::VenumSet, T>)
                {
                    const T &set = static_cast<const T&>(val);
                    juce::String output;
                    
                    for (const auto &str_opt : set)
                    {
                        output << str_opt->name().data() << ",";
                    }

                    return output.trimCharactersAtEnd(",");
                }
                else if constexpr (std::is_same_v<Flag, T>)
                {
                    return val.isSet() ? (val.getValue() ? "true" : "false") : "unset";
                }
                else if constexpr (   std::is_same_v<juce::String, T>
                                   || std::is_same_v<std::string,  T>
                                   || std::is_same_v<const char*,  T>)
                {
                    return val;
                }
                else if constexpr (venum::is_venum_type_v<T>)
                {
                    return val ? val->name().data() : "N/A";
                }
                else
                {
                    return std::to_string(val);
                }
                
            }, value);
    
            return output;
        }
    };
    
    //==================================================================================================================
    template<std::size_t ...I>
    static auto getArray(std::index_sequence<I...>)
        -> std::array<Option, TypeMap::size>
    {
        return { TypeMap::create<I>()... };
    }
    
    static auto initArray()
        -> std::array<Option, TypeMap::size>
    {
        return getArray(std::make_index_sequence<TypeMap::size>());
    }
    
    //==================================================================================================================
    std::array<Option, TypeMap::size> options { initArray() };
    
public:
    enum OptionId
    {
        Entity,
        MType,
        VType,
        CType,
        Linkage,
        Quals,
        Virtual,
        
        CPath,
        Constexpr,
        
        Sort,
        END
    };
    
    //==================================================================================================================
    using FilterMap = std::unordered_map<juce::String, juce::String>;
    
    //==================================================================================================================
    static FilterMap getFilterMap(const juce::StringArray &input)
    {
        std::unordered_map<juce::String, juce::String> output;
        output.reserve(input.size());
    
        for (const auto &arg : input)
        {
            if (!arg.matchesWildcard("*:*", true))
            {
                throw std::invalid_argument("Invalid option specification '" + arg.toStdString() + "'.\n"
                                            R"(Make sure to follow the option format "option:value" with no spaces!)");
            }
        
            output.emplace(arg.upToFirstOccurrenceOf(":", false, true), arg.fromFirstOccurrenceOf(":", false, true));
        }
    
        return output;
    }
    
    static Filter createFromInput(const juce::StringArray &input)
    {
        FilterMap map = getFilterMap(input);
        Filter filter;
        
        for (const auto &[name, value] : map)
        {
            auto it = std::find(filter.options.begin(), filter.options.end(), name.toLowerCase().toRawUTF8());
            
            if (it == filter.options.end())
            {
                throw std::invalid_argument("There is no filter option for **" + name.toStdString() + "**.");
            }
            
            std::visit([&filter, m_val = value, m_name = name](auto &&val)
            {
                using T = std::decay_t<decltype(val)>;
                
                auto &opt = filter.options[TypeMap::indexOf<T>].value;
                
                if constexpr (jaut::sameTypeIgnoreTemplate_v<venum::VenumSet, T>)
                {
                    juce::StringArray opt_values;
                    opt_values.addTokens(m_val, ",");
                    
                    using VType = jaut::getTypeAt_t<T, 0>;
                    
                    T &set = std::get<T>(opt);
                    set.clear();
                    
                    for (const auto &opt_val : opt_values)
                    {
                        VType constant;
                        
                        if constexpr (std::is_same_v<VType, VarType>)
                        {
                            for (const auto &v_type : VarType::values)
                            {
                                if (opt_val.equalsIgnoreCase(v_type->name().data())
                                 || opt_val.equalsIgnoreCase(v_type->getRepresentation().data()))
                                {
                                    constant = v_type;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            constant = VType::valueOf(opt_val.toRawUTF8(), true);
                        }
    
                        if (!constant)
                        {
                            throw std::invalid_argument("There is no " + m_name.toStdString() + " of type **"
                                                        + opt_val.toStdString() + "**!");
                        }
    
                        set.emplace(constant);
                    }
                }
                else if constexpr (std::is_same_v<Flag, T>)
                {
                    std::get<T>(opt);
                }
                else if constexpr (   std::is_same_v<juce::String, T>
                                   || std::is_same_v<std::string,  T>
                                   || std::is_same_v<const char*,  T>)
                {
                    opt = m_val;
                }
                else if constexpr (venum::is_venum_type_v<T>)
                {
                    opt = T::valueOf(m_val.toRawUTF8(), true);
                }
                else
                {
                    opt = static_cast<T>(juce::var(m_val));
                }
                
            }, it->value);
        }
        
        return filter;
    }
    
    //==================================================================================================================
    template<std::size_t Idx>
    const TypeMap::at<Idx>& get() const { return std::get<TypeMap::at<Idx>>(options[Idx].value); }
    
    //==================================================================================================================
    juce::String toString() const
    {
        juce::String output;
        
        for (const auto &opt : options)
        {
            output << opt.toString() << " ";
        }
        
        return output.trim();
    }
    
    //==================================================================================================================
    // Dev asserts
    static_assert(TypeMap::size == END,                               "Missing an index for a newly added option");
    static_assert(!TypeMap::names[TypeMap::names.size() - 1].empty(), "Didn't add names for each option");
};
