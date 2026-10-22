//  JNI_Common.hxx
//  RemoteInput
//
//  Created by Brandon on 2022-12-02.
//  Copyright © 2022 XIO. All rights reserved.
//

#ifndef JNI_COMMON_HXX_INCLUDED
#define JNI_COMMON_HXX_INCLUDED

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#endif

#include <jni.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

//A Java exception that a JNI call leaves stays pending: the next JNI call with it pending is undefined, and -Xcheck:jni
//warns at a call after a Call*Method that nobody checked (Simba-Reflection #63). Clear it, and give the zero value.
template<typename T>
inline T ClearException(JNIEnv* env, T result) noexcept
{
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return T{};
    }
    return result;
}

#endif //JNI_COMMON_HXX_INCLUDED
