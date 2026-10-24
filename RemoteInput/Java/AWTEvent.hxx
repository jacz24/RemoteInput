//
//  AWTEvent.hxx
//  RemoteInput
//
//  Created by Brandon on 2020-01-09.
//  Copyright © 2020 XIO. All rights reserved.
//

#ifndef AWTEvent_HXX_INCLUDED
#define AWTEvent_HXX_INCLUDED

#include <atomic>
#include <cstdint>
#include "JNI_Common.hxx"

namespace java
{
    class AWTEvent
    {
    private:
        static inline std::atomic<std::uint32_t> lost_events{0};

    protected:
        jobject self;
        static void SetSystemGenerated(JNIEnv* env, jobject event, bool is_system_generated);
        static void Post(JNIEnv* env, jobject event, bool is_system_generated);

        //FindClass gives null for a class the JVM lacks, NewObject for a constructor that throws (a KEY_TYPED of
        //CHAR_UNDEFINED, a null source). The next post must not start with that exception pending, and the script must
        //learn of the lost event (Simba-Reflection #74). The second try is for a JVM that fails a call while an exception
        //is pending: HotSpot does not.
        template<typename Construct>
        static jobject Build(JNIEnv* env, const char* name, Construct&& construct) noexcept
        {
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                jclass cls = env->FindClass(name);
                jobject event = cls ? construct(cls) : nullptr;
                if (cls)
                {
                    env->DeleteLocalRef(cls);
                }

                if (event)
                {
                    return event;
                }
                env->ExceptionClear();
            }

            ++lost_events;
            return nullptr;
        }

    public:
        //The events no post could build since the last call, a key repeat's too: each input command's answer carries them.
        static std::uint32_t TakeLostEvents() noexcept { return lost_events.exchange(0); }

        AWTEvent() noexcept;
        AWTEvent(jobject self) noexcept;
        AWTEvent(AWTEvent&& other) noexcept;
        AWTEvent(const AWTEvent& other) = delete;
        virtual ~AWTEvent() noexcept;

        AWTEvent& operator = (AWTEvent&& other) noexcept;
        AWTEvent& operator = (const AWTEvent& other) = delete;

        virtual jobject get() const noexcept;
        virtual jobject getSource(JNIEnv* env) const noexcept;
    };
}

#endif /* AWTEvent_HXX_INCLUDED */
