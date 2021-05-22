
#pragma once

#include <classdef.h>
#include <definition.h>
#include <memberdef.h>
#include <namespacedef.h>

#include <venum.h>

VENUM_CREATE(EntityType,
    Namespace,
    Class,
    Enum,
    Function,
    Field,
    TypeAlias
)

VENUM_CREATE(Ownership,
    Free,
    Member,
    Static,
    Friend
)

VENUM_CREATE(Linkage,
    Internal,
    External
)

VENUM_CREATE(CompoundType,
    Class,
    Struct,
    Union,
    Enum,
    EnumClass
)

VENUM_CREATE(TypeQualifier,
    Normal,
    Const,
    Volatile,
    ConstVolatile
)

VENUM_CREATE(Virtualness,
    False,
    True,
    Pure
)

VENUM_CREATE(SortType,
    Entity,
    Asc,
    Desc
)

VENUM_CREATE_ASSOC
(
    ID(VarType),
    VALUES
    (
        (Value)    (""),
        (LValueRef)("&"),
        (RValueRef)("&&")
    ),
    BODY
    (
    public:
        std::string_view getRepresentation() const noexcept { return sign; }
    
    private:
        std::string_view sign { "" };
    
        //==============================================================================================================
        constexpr VarTypeConstant(std::string_view name, int ordinal, std::string_view representation)
            : VENUM_BASE(name, ordinal), sign(representation)
        {}
    )
)
