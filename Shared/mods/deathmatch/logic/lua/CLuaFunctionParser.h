/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *
 *  Multi Theft Auto is available from http://www.multitheftauto.com/
 *
 *****************************************************************************/
#pragma once 

#include <optional>
#include <variant>
#include <SharedUtil.Template.h>
#include "lua/CLuaFunctionParseHelpers.h"
#include "lua/LuaBasic.h"

template <bool, auto*>
struct CLuaFunctionParser
{
};

template <bool ErrorOnFailure, typename Ret, typename... Args, auto (*Func)(Args...)->Ret>
struct CLuaFunctionParser<ErrorOnFailure, Func>
{
    std::size_t iIndex = 1;
    bool        bFailed = false;
    std::string strError = "todo fix error message";

    // Pop should remove a T from the Lua Stack after verifying that it is a valid type
    // Pop may also throw a LuaArgumentError to indicate failure
    template <typename T>
    inline T Pop(lua_State* L, std::size_t& index)
    {
        if (!TypeMatch<T>(L, index))
        {
            // TODO: resolve error
            bFailed = true;
            return T{};
        }
        return PopUnsafe<T>(L, index);
    }

    // Special type matcher for variants. Returns -1 if the type does not match
    // returns n if the nth type of the variant matches
    template <typename T>
    inline int TypeMatchVariant(lua_State* L, std::size_t index)
    {
        if constexpr (std::is_same_v<T, std::variant<>>)
            return -1;
        else
        {
            using first_t = typename is_variant<T>::param1_t;
            using next_t = typename is_variant<T>::rest_t;
            if (TypeMatch<first_t>(L, index))
                return 0;
            else
            {
                int iResult = TypeMatchVariant<next_t>(L, index);
                if (iResult == -1)
                    return -1;
                return 1 + iResult;
            }
        }
    }

    // TypeMatch<T> should return true if the value on top of the Lua stack can be popped via
    // PopUnsafe<T>. This must accurately reflect the associated PopUnsafe. Note that TypeMatch
    // should only check for obvious type violations (e.g. false is not a string) but not
    // for internal type errors (passing a vehicle to a function expecting a ped)
    template <typename T>
    inline bool TypeMatch(lua_State* L, std::size_t index)
    {
        int iArgument = lua_type(L, index);
        // trivial types
        if constexpr (std::is_same_v<T, std::string>)
            return (iArgument == LUA_TSTRING || iArgument == LUA_TNUMBER);
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, short> ||
                      std::is_same_v<T, unsigned int> || std::is_same_v<T, unsigned short>)
            return (iArgument == LUA_TSTRING || iArgument == LUA_TNUMBER);
        if constexpr (std::is_same_v<T, bool>)
            return (iArgument == LUA_TBOOLEAN);

        // advanced types
        // Enums are represented as strings to Lua
        if constexpr (std::is_enum_v<T>)
            return iArgument == LUA_TSTRING;
        // std::optional is used for optional parameters
        // which may also be in the middle of a parameter list
        // therefore it is always valid to attempt to read an
        // optional
        if constexpr (is_specialization<T, std::optional>::value)
            return true;

        // std::vector is used for arrays built from tables
        if constexpr (is_2specialization<T, std::vector>::value)
            return iArgument == LUA_TTABLE;

        // std::unordered_map<k,v> is used for maps built from tables
        if constexpr (is_5specialization<T, std::unordered_map>::value)
            return iArgument == LUA_TTABLE;

        // CLuaFunctionRef is used for functions
        if constexpr (std::is_same_v<T, CLuaFunctionRef>)
            return iArgument == LUA_TFUNCTION;

        // lua_State* can be taken as argument anywhere
        if constexpr (std::is_same_v<T, lua_State*>)
            return true;

        // variants can be used by any of the underlying types
        // thus recursively use this function
        if constexpr (is_variant<T>::value)
            return TypeMatchVariant<T>(L, index) != -1;

        // Catch all for class pointer types, assume all classes are valid script entities
        // and can be fetched from a userdata
        if constexpr (std::is_pointer_v<T> && std::is_class_v<std::remove_pointer_t<T>>)
            return iArgument == LUA_TUSERDATA || iArgument == LUA_TLIGHTUSERDATA;
    }

    // Special PopUnsafe for variants
    template <typename T, std::size_t currIndex = 0>
    inline T PopUnsafeVariant(lua_State* L, std::size_t& index, int vindex)
    {
        // As std::variant<> cannot be constructed, we simply return the first value
        // in the error case. This is actually unreachable in the regular path,
        // due to TypeMatch making sure that vindex < is_variant<T>::count
        if constexpr (is_variant<T>::count == currIndex)
        {
            using type_t = typename is_variant<T>::param1_t;
            return type_t{};
        }
        else
        {
            // If we have reached the target index, pop the right value
            // else go to the next type
            if (vindex == currIndex)
            {
                using type_t = std::remove_reference_t<decltype(std::get<currIndex>(T{}))>;
                return PopUnsafe<type_t>(L, index);
            }
            else
            {
                return PopUnsafeVariant<T, currIndex + 1>(L, index, vindex);
            }
        }
    }

    template <typename T>
    inline T PopUnsafe(lua_State* L, std::size_t& index)
    {
        // trivial types are directly popped
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, int> || std::is_same_v<T, float> || std::is_same_v<T, double> ||
                      std::is_same_v<T, short> || std::is_same_v<T, unsigned int> || std::is_same_v<T, unsigned short> || std::is_same_v<T, bool>)
            return lua::PopTrivial<T>(L, index);
        else if constexpr (std::is_enum_v<T>)
        {
            // Enums are considered strings in Lua
            std::string strValue = lua::PopTrivial<std::string>(L, index);
            T           eValue;
            if (StringToEnum(strValue, eValue))
                return eValue;
            else
            {
                bFailed = true;
                // TODO: resolve error
                return static_cast<T>(0);
            }
        }
        else if constexpr (is_specialization<T, std::optional>::value)
        {
            // optionals may either type match the desired value, or be nullopt
            using param = typename is_specialization<T, std::optional>::param_t;
            if (TypeMatch<param>(L, index))
                return PopUnsafe<param>(L, index);
            else
                return std::nullopt;
        }

        else if constexpr (is_2specialization<T, std::vector>::value)            // 2 specialization due to allocator
        {
            using param = typename is_2specialization<T, std::vector>::param1_t;
            T vecData;
            lua_pushnil(L); /* first key */
            while (lua_next(L, index) != 0)
            {
                if (!TypeMatch<param>(L, -1) || !TypeMatch<int>(L, -2))
                {
                    bFailed = true;
                    // TODO: resolve error
                    return vecData;
                }

                std::size_t i = -1;
                vecData.emplace_back(PopUnsafe<param>(L, i));
                lua_pop(L, 1);            // drop value, keep key for lua_next
            }
            ++index;
            return vecData;
        }
        else if constexpr (is_5specialization<T, std::unordered_map>::value)
        {
            using key_t = typename is_5specialization<T, std::unordered_map>::param1_t;
            using value_t = typename is_5specialization<T, std::unordered_map>::param2_t;
            T map;
            lua_pushnil(L); /* first key */
            while (lua_next(L, index) != 0)
            {
                if (!TypeMatch<value_t>(L, -1) || !TypeMatch<key_t>(L, -2))
                {
                    bFailed = true;
                    // TODO: resolve error
                    return map;
                }

                std::size_t i = -2;
                auto        k = PopUnsafe<key_t>(L, i);
                auto        v = PopUnsafe<value_t>(L, i);
                map.emplace(std::move(k), std::move(v));
                lua_pop(L, 1);            // drop value, keep key for lua_next
            }
            ++index;
            return map;
        }
        else if constexpr (std::is_same_v<T, CLuaFunctionRef>)
        {
            return luaM_toref(L, index++);
        }
        else if constexpr (std::is_same_v<T, lua_State*>)
            return L;
        // variants can be used by any of the underlying types
        // thus recursively use this function
        else if constexpr (is_variant<T>::value)
        {
            int iMatch = TypeMatchVariant<T>(L, index);
            return PopUnsafeVariant<T>(L, index, iMatch);
        }

        // Catch all for class pointer types, assume all classes are valid script entities
        // and can be fetched from a userdata
        else if constexpr (std::is_pointer_v<T> && std::is_class_v<std::remove_pointer_t<T>>)
        {
            bool  isLightUserData = lua_type(L, index) == LUA_TLIGHTUSERDATA;
            void* pValue = lua::PopTrivial<void*>(L, index);
            using class_t = std::remove_pointer_t<T>;
            T result = nullptr;
            if (isLightUserData)
            {
                result = UserDataCast<class_t>((class_t*)0, pValue, L);
            }
            else
            {
                result = UserDataCast<class_t>((class_t*)0, *reinterpret_cast<void**>(pValue), L);
            }
            if (result == nullptr)
            {
                bFailed = true;
                // TODO: resolve error
                return nullptr;
            }
            return result;
        }
    }

    template <typename... Params>
    inline auto Call(lua_State* L, Params&&... ps)
    {
        if (bFailed)
        {
            return -1;
        }
        if constexpr (sizeof...(Params) == sizeof...(Args))
        {
            if constexpr (std::is_same_v<Ret, void>)
            {
                Func(std::forward<Params>(ps)...);
                return 0;
            }
            else
            {
                return lua::Push(L, Func(std::forward<Params>(ps)...));
            }
        }
        else
        {
            return Call(L, ps..., Pop<typename nth_element_impl<sizeof...(Params), Args...>::type>(L, iIndex));
        }
    }

    inline int operator()(lua_State* L, CScriptDebugging* pScriptDebugging)
    {
        int iResult = 0;
        try
        {
            iResult = Call(L);
        }
        catch (std::invalid_argument& e)
        {
            strError = e.what();
        }
        if (bFailed)
        {
            if constexpr (ErrorOnFailure)
            {
                luaL_error(L, strError.c_str());
            }
            else
            {
                pScriptDebugging->LogCustom(L, strError.c_str());
                lua_pushboolean(L, false);
            }
            return 1;
        }
        return iResult;
    }
};