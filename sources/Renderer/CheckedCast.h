/*
 * CheckedCast.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_CHECKED_CAST_H
#define LLGL_CHECKED_CAST_H


#include "../Platform/Debug.h"

#if LLGL_ENABLE_CHECKED_CAST
#   if LLGL_EXCEPTIONS_SUPPORTED
#       include <typeinfo>
#   else
#       include <LLGL/TypeInfo.h>
#       include "../Core/Assertion.h"
#   endif
#endif


namespace LLGL
{


#if LLGL_ENABLE_CHECKED_CAST

template <typename TDst, typename TSrc>
inline TDst& ObjectCast(TSrc& obj)
{
    #if LLGL_EXCEPTIONS_SUPPORTED

    try
    {
        return dynamic_cast<TDst&>(obj);
    }
    catch (const std::bad_cast&)
    {
        LLGL_DEBUG_BREAK();
        throw;
    }

    #else // LLGL_EXCEPTIONS_SUPPORTED

    return dynamic_cast<TDst&>(obj);

    #endif // /LLGL_EXCEPTIONS_SUPPORTED
}

template <typename TDst, typename TSrc>
inline TDst ObjectCast(TSrc* obj)
{
    if (obj == nullptr)
        return nullptr;

    #if LLGL_EXCEPTIONS_SUPPORTED

    try
    {
        TDst objInstance = dynamic_cast<TDst>(obj);
        if (!objInstance)
            throw std::bad_cast();
        return objInstance;
    }
    catch (const std::bad_cast&)
    {
        LLGL_DEBUG_BREAK();
        throw;
    }

    #else // LLGL_EXCEPTIONS_SUPPORTED

    TDst objInstance = dynamic_cast<TDst>(obj);
    LLGL_ASSERT(objInstance != nullptr);
    return objInstance;

    #endif // /LLGL_EXCEPTIONS_SUPPORTED
}

#else // LLGL_ENABLE_CHECKED_CAST

template <typename TDst, typename TSrc>
inline TDst ObjectCast(TSrc&& obj)
{
    return static_cast<TDst>(obj);
}

#endif // /LLGL_ENABLE_CHECKED_CAST

#define LLGL_CAST(TYPE, OBJ) \
    ObjectCast<TYPE>(OBJ)


} // /namespace LLGL


#endif



// ================================================================================
