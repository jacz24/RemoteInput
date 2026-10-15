//
//  AWTEvent.cxx
//  RemoteInput
//
//  Created by Brandon on 2020-01-09.
//  Copyright © 2020 XIO. All rights reserved.
//

#include "AWTEvent.hxx"
#include "RIEventQueue.hxx"
#include <utility>

namespace java
{
    AWTEvent::AWTEvent() noexcept : self(nullptr)
    {
    }

    AWTEvent::~AWTEvent() noexcept
    {
    }

    AWTEvent::AWTEvent(jobject self) noexcept : self(self)
    {
    }

    AWTEvent::AWTEvent(AWTEvent&& other) noexcept : self(other.self)
    {
        other.self = nullptr;
    }

    AWTEvent& AWTEvent::operator = (AWTEvent&& other) noexcept
    {
        this->self = other.self;
        other.self = nullptr;
        return *this;
    }

    jobject AWTEvent::get() const noexcept
    {
        return self;
    }

    jobject AWTEvent::getSource(JNIEnv* env) const noexcept
    {
        jclass cls = env->FindClass("java/util/EventObject");
        if (cls)
        {
            jmethodID methodID = env->GetMethodID(cls, "getSource", "()Ljava/lang/Object;");
            jobject result = env->CallObjectMethod(this->self, methodID);
            env->DeleteLocalRef(cls);

            env->DeleteLocalRef(std::exchange(result, env->NewGlobalRef(result)));
            return result;
        }
        return nullptr;
    }

    void AWTEvent::SetSystemGenerated(JNIEnv* env, jobject event, bool is_system_generated)
    {
        if (is_system_generated)
        {
            // The mark that SunToolkit.postEvent puts on real OS input.
            jclass cls = env->FindClass("sun/awt/SunToolkit");
            if (cls)
            {
                static jmethodID methodId = env->GetStaticMethodID(cls, "setSystemGenerated", "(Ljava/awt/AWTEvent;)V");
                if (methodId)
                {
                    env->CallStaticVoidMethod(cls, methodId, event);
                }

                //Cleanup
                env->DeleteLocalRef(cls);
            }

            // A JVM without SunToolkit.setSystemGenerated posts the event unmarked.
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
            }
        }
    }

    void AWTEvent::Post(JNIEnv* env, jobject event, bool is_system_generated)
    {
        if (is_system_generated)
        {
            SetSystemGenerated(env, event, is_system_generated);
        }

        //Get Default Toolkit
        jclass toolkit_class = env->FindClass("java/awt/Toolkit");
        if (toolkit_class)
        {
            static jmethodID toolkit_methodId = env->GetStaticMethodID(toolkit_class, "getDefaultToolkit",
                                                                       "()Ljava/awt/Toolkit;");
            jobject toolkit = env->CallStaticObjectMethod(toolkit_class, toolkit_methodId);
            if (toolkit)
            {
                //Get System Event Queue
                static jmethodID queue_methodId = env->GetMethodID(toolkit_class, "getSystemEventQueue",
                                                                   "()Ljava/awt/EventQueue;");
                jobject queue = env->CallObjectMethod(toolkit, queue_methodId);
                if (queue)
                {
                    //Post Event
                    jclass queue_class = env->FindClass("java/awt/EventQueue");
                    if (queue_class)
                    {
                        static jmethodID post_methodId = env->GetMethodID(queue_class, "postEvent", "(Ljava/awt/AWTEvent;)V");
                        env->CallVoidMethod(queue, post_methodId, RIEvent(env, event).get());

                        //Cleanup
                        env->DeleteLocalRef(queue_class);
                    }
                    //Cleanup
                    env->DeleteLocalRef(queue);
                }
                //Cleanup
                env->DeleteLocalRef(toolkit);
            }
            //Cleanup
            env->DeleteLocalRef(toolkit_class);
        }
    }
}