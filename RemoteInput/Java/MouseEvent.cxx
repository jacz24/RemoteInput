//
//  MouseEvent.cxx
//  RemoteInput
//
//  Created by Brandon on 2020-01-09.
//  Copyright © 2020 XIO. All rights reserved.
//

#include "MouseEvent.hxx"
#include <utility>

namespace java
{
    MouseEvent::MouseEvent(JNIEnv* env, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t x, std::int32_t y, std::int32_t clickCount, bool popupTrigger, std::int32_t button) noexcept : AWTEvent(), env(env), cls(nullptr)
    {
        this->cls = env->FindClass("java/awt/event/MouseEvent");
        env->DeleteLocalRef(std::exchange(this->cls, static_cast<jclass>(env->NewGlobalRef(this->cls))));

        static jmethodID methodId = env->GetMethodID(cls, "<init>", "(Ljava/awt/Component;IJIIIIZI)V");
        self = env->NewObject(cls, methodId, source->get(), id, when, modifiers, x, y, clickCount, popupTrigger, button);
        env->DeleteLocalRef(std::exchange(self, static_cast<jclass>(env->NewGlobalRef(self))));
    }

    MouseEvent::~MouseEvent() noexcept
    {
        env->DeleteGlobalRef(cls);
        env->DeleteGlobalRef(self);
    }

    void MouseEvent::Dispatch(JNIEnv* env, Component* receiver, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t x, std::int32_t y, std::int32_t clickCount, bool popupTrigger, std::int32_t button, bool is_system_generated) noexcept
    {
        jobject event = Build(env, "java/awt/event/MouseEvent", [&](jclass cls) {
            static jmethodID methodId = env->GetMethodID(cls, "<init>", "(Ljava/awt/Component;IJIIIIZI)V");
            return env->NewObject(cls, methodId, source->get(), id, when, modifiers, x, y, clickCount, popupTrigger, button);
        });

        if (event)
        {
            AWTEvent::SetSystemGenerated(env, event, is_system_generated);
            receiver->dispatchEvent(event);
            env->DeleteLocalRef(event);
        }
    }

    void MouseEvent::Post(JNIEnv* env, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t x, std::int32_t y, std::int32_t clickCount, bool popupTrigger, std::int32_t button, bool is_system_generated) noexcept
    {
        jobject event = Build(env, "java/awt/event/MouseEvent", [&](jclass cls) {
            static jmethodID methodId = env->GetMethodID(cls, "<init>", "(Ljava/awt/Component;IJIIIIZI)V");
            return env->NewObject(cls, methodId, source->get(), id, when, modifiers, x, y, clickCount, popupTrigger, button);
        });

        if (event)
        {
            AWTEvent::Post(env, event, is_system_generated);
            env->DeleteLocalRef(event);
        }
    }
}