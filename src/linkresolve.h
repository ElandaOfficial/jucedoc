
#pragma once

#include "specs.h"

#include <classdef.h>
#include <md5.h>
#include <memberdef.h>
#include <namespacedef.h>
#include <juce_core/juce_core.h>

#include <string_view>

inline juce::String getAnchor(const MemberDef &member)
{
    QCString memAnchor = member.name();
    if (!member.argsString().isEmpty()) memAnchor += member.argsString();
    
    memAnchor.prepend(juce::String(member.definition().data()).replace("juce::", "").toRawUTF8());
    
    if (member.argumentList().hasParameters())
    {
        char buf[20];
        qsnprintf(buf,20,"%d:",(int)member.argumentList().size());
        buf[19]='\0';
        memAnchor.prepend(buf);
    }
    
    if (!member.requiresClause().isEmpty())
    {
        memAnchor += " " + member.requiresClause();
    }
    
    uchar md5_sig[16];
    char sigStr[33];
    MD5Buffer((const unsigned char *)memAnchor.data(),memAnchor.length(),md5_sig);
    MD5SigToString(md5_sig,sigStr);
    
    return "a" + juce::String(sigStr);
}

inline juce::String sanitiseUrl(std::string_view name)
{
    name.remove_prefix(6);
    return juce::String(name.data()).replace("_", "__").replace("::", "_1_1");
}

inline juce::String getUrlFromEntity(EntityType entityType, const Definition &def)
{
    juce::String output;
    
    if (entityType == EntityType::Namespace)
    {
        output << "namespace" << sanitiseUrl(def.qualifiedName().data()) << ".html";
    }
    else if (entityType == EntityType::Class)
    {
        output << static_cast<const ClassDef&>(def).compoundTypeString().lower().str()
               << sanitiseUrl(def.qualifiedName().data()) << ".html";
    }
    else
    {
        const auto &member = static_cast<const MemberDef&>(def);
        const auto *parent = member.getOuterScope();
        
        if (const auto *ns = dynamic_cast<const NamespaceDef*>(parent))
        {
            const juce::String namespace_name = sanitiseUrl(ns->qualifiedName().data());
            output << "namespace" << namespace_name << ".html"
                   << "#" << getAnchor(member);
        }
        else if (const auto *cs = dynamic_cast<const ClassDef*>(parent))
        {
            const juce::String class_name = sanitiseUrl(cs->qualifiedName().data());
            output << cs->compoundTypeString().lower().str() << class_name << ".html"
                   << "#" << getAnchor(member);
        }
    }
    
    return output;
}
