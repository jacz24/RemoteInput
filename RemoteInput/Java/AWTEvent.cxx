//
//  AWTEvent.cxx
//  RemoteInput
//
//  Created by Brandon on 2020-01-09.
//  Copyright © 2020 XIO. All rights reserved.
//

#include "AWTEvent.hxx"
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
            jobject result = ClearException(env, env->CallObjectMethod(this->self, methodID));
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

    //Real input's route (Simba-Reflection #15): the Windows peer posts each event with SunToolkit.postEvent(
    //targetToAppContext(source), event) (WComponentPeer.postEvent, JDK 11 and 25). It marks the event and puts it in AWT's
    //post queue, and the thread that flushes that queue posts it to the event queue: the dispatch thread, or a thread that
    //posts to the event queue itself. False when the event did not reach the post queue, and Post's own two steps post it:
    //- a JVM with no SunToolkit, targetToAppContext or postEvent;
    //- a source with no app context, where postEvent throws a NullPointerException;
    //- postEvent's "Event posted on wrong app context", the one RuntimeException of that exact class it throws, and before
    //  the queue.
    //Any other exception may come after the event is in the queue (the start of a dispatch thread): it is cleared, and the
    //event is not posted again, so no event reaches the event queue twice.
    bool AWTEvent::PostAsPeer(JNIEnv* env, jobject event)
    {
        jclass toolkit_class = env->FindClass("sun/awt/SunToolkit");
        if (!toolkit_class)
        {
            env->ExceptionClear();
            return false;
        }

        //A lookup that fails leaves a NoSuchMethodError, which the next JNI call must not find pending.
        auto find = [&](const char* name, const char* signature) {
            jmethodID methodId = env->GetStaticMethodID(toolkit_class, name, signature);
            env->ExceptionClear();
            return methodId;
        };
        static jmethodID context_methodId = find("targetToAppContext", "(Ljava/lang/Object;)Lsun/awt/AppContext;");
        static jmethodID post_methodId = find("postEvent", "(Lsun/awt/AppContext;Ljava/awt/AWTEvent;)V");

        jobject context = nullptr;
        if (context_methodId && post_methodId)
        {
            jclass object_class = env->FindClass("java/util/EventObject");
            static jmethodID source_methodId = env->GetMethodID(object_class, "getSource", "()Ljava/lang/Object;");
            jobject source = env->CallObjectMethod(event, source_methodId);
            context = ClearException(env, env->CallStaticObjectMethod(toolkit_class, context_methodId, source));
            env->DeleteLocalRef(source);
            env->DeleteLocalRef(object_class);
        }

        bool queued = false;
        if (context)
        {
            env->CallStaticVoidMethod(toolkit_class, post_methodId, context, event);
            jthrowable thrown = env->ExceptionOccurred();
            env->ExceptionClear();

            queued = !thrown;
            if (thrown)
            {
                jclass runtime_class = env->FindClass("java/lang/RuntimeException");
                jclass thrown_class = env->GetObjectClass(thrown);
                queued = !env->IsSameObject(thrown_class, runtime_class);
                env->DeleteLocalRef(thrown_class);
                env->DeleteLocalRef(runtime_class);
                env->DeleteLocalRef(thrown);
            }
            env->DeleteLocalRef(context);
        }

        env->DeleteLocalRef(toolkit_class);
        return queued;
    }

    void AWTEvent::Post(JNIEnv* env, jobject event, bool is_system_generated)
    {
        if (is_system_generated && PostAsPeer(env, event))
        {
            return;
        }

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
            jobject toolkit = ClearException(env, env->CallStaticObjectMethod(toolkit_class, toolkit_methodId));
            if (toolkit)
            {
                //Get System Event Queue
                static jmethodID queue_methodId = env->GetMethodID(toolkit_class, "getSystemEventQueue",
                                                                   "()Ljava/awt/EventQueue;");
                jobject queue = ClearException(env, env->CallObjectMethod(toolkit, queue_methodId));
                if (queue)
                {
                    //Post Event
                    jclass queue_class = env->FindClass("java/awt/EventQueue");
                    if (queue_class)
                    {
                        static jmethodID post_methodId = env->GetMethodID(queue_class, "postEvent", "(Ljava/awt/AWTEvent;)V");
                        env->CallVoidMethod(queue, post_methodId, event);
                        env->ExceptionClear();

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