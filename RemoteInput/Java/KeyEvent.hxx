//
//  KeyEvent.hxx
//  RemoteInput
//
//  Created by Brandon on 2020-01-09.
//  Copyright © 2020 XIO. All rights reserved.
//

#ifndef KeyEvent_HXX_INCLUDED
#define KeyEvent_HXX_INCLUDED

#include "JNI_Common.hxx"
#include "AWTEvent.hxx"
#include "Component.hxx"

namespace java
{
    class KeyEvent final : public AWTEvent
    {
    private:
        JNIEnv* env;
        jclass cls;

    public:
        KeyEvent(JNIEnv* env, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t keyCode, jchar keyChar, std::int32_t keyLocation) noexcept;
        virtual ~KeyEvent() noexcept;

        static void Dispatch(JNIEnv* env, Component* receiver, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t keyCode, jchar keyChar, std::int32_t keyLocation, bool is_system_generated = false) noexcept;

        //nativeKey: the Windows virtual key of a press or a release, which sets the four private fields a native event
        //carries (Simba-Reflection #206); 0 leaves them 0, as a KEY_TYPED has them.
        static void Post(JNIEnv* env, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t keyCode, jchar keyChar, std::int32_t keyLocation, std::int32_t nativeKey = 0, bool is_system_generated = true) noexcept;

        enum KeyCodes : std::int32_t
        {
            KEY_TYPED = 400,
            KEY_PRESSED = 401,
            KEY_RELEASED = 402,

            VK_UNDEFINED = 0x0,
            CHAR_UNDEFINED = 0xFFFF,
            KEY_LOCATION_UNKNOWN = 0x0,
            KEY_LOCATION_STANDARD = 0x1,
            KEY_LOCATION_LEFT = 0x2,
            KEY_LOCATION_RIGHT = 0x3,
            KEY_LOCATION_NUMPAD = 0x4
        };
    };
}

#endif /* KeyEvent_HXX_INCLUDED */
