//
// Created by Brandon on 2021-10-17.
//

#include "RIEventQueue.hxx"
#include "WindowEvent.hxx"
#include <algorithm>
#include <string>
#include <utility>

namespace java
{
    namespace
    {
        // The wrapper class. A hidden class has no name, so FindClass cannot find it.
        jclass wrapper_class = nullptr;

        // Renames each "eos/EventInput/X" in the constant pool to "<prefix>X". Only CONSTANT_Utf8 entries change.
        std::vector<std::uint8_t> RenameClasses(const std::vector<std::uint8_t>& in, const std::string& prefix)
        {
            // Bytes after the tag, by tag. 1 (Utf8) has a length.
            static constexpr std::uint8_t sizes[] = {0, 0, 0, 4, 4, 8, 8, 2, 2, 4, 4, 4, 4, 0, 0, 3, 2, 4, 4, 2, 2};
            static const std::string old_prefix = "eos/EventInput/";
            auto u2 = [&](std::size_t i) { return static_cast<std::size_t>(in[i] << 8 | in[i + 1]); };

            std::vector<std::uint8_t> out(in.begin(), in.begin() + 10);
            std::size_t pos = 10;
            for (std::size_t i = 1, count = u2(8); i < count; ++i)
            {
                std::uint8_t tag = in[pos];
                if (tag == 1)
                {
                    std::string text(in.begin() + pos + 3, in.begin() + pos + 3 + u2(pos + 1));
                    pos += 3 + text.size();
                    for (std::size_t at = text.find(old_prefix); at != std::string::npos; at = text.find(old_prefix, at + prefix.size()))
                    {
                        text.replace(at, old_prefix.size(), prefix);
                    }
                    out.insert(out.end(), {1, static_cast<std::uint8_t>(text.size() >> 8), static_cast<std::uint8_t>(text.size())});
                    out.insert(out.end(), text.begin(), text.end());
                    continue;
                }

                out.insert(out.end(), in.begin() + pos, in.begin() + pos + 1 + sizes[tag]);
                pos += 1 + sizes[tag];
                i += (tag == 5 || tag == 6); // A long or a double takes two entries.
            }
            out.insert(out.end(), in.begin() + pos, in.end());
            return out;
        }

        // Defines `bytes` as a hidden class in the loader and package of `host`.
        // Returns nullptr before Java 15, which has no hidden classes.
        jclass DefineHiddenClass(JNIEnv* env, jclass host, const std::vector<std::uint8_t>& bytes)
        {
            // ClassOption came with hidden classes, in Java 15.
            jclass option_class = env->FindClass("java/lang/invoke/MethodHandles$Lookup$ClassOption");
            if (!option_class)
            {
                env->ExceptionClear();
                return nullptr;
            }

            jclass lookup_class = env->FindClass("java/lang/invoke/MethodHandles$Lookup");
            jfieldID trusted_id = env->GetStaticFieldID(lookup_class, "IMPL_LOOKUP", "Ljava/lang/invoke/MethodHandles$Lookup;");
            jmethodID in_id = env->GetMethodID(lookup_class, "in", "(Ljava/lang/Class;)Ljava/lang/invoke/MethodHandles$Lookup;");
            jmethodID define_id = env->GetMethodID(lookup_class, "defineHiddenClass", "([BZ[Ljava/lang/invoke/MethodHandles$Lookup$ClassOption;)Ljava/lang/invoke/MethodHandles$Lookup;");
            jmethodID lookup_class_id = env->GetMethodID(lookup_class, "lookupClass", "()Ljava/lang/Class;");

            jclass result = nullptr;
            if (!env->ExceptionCheck())
            {
                // IMPL_LOOKUP.in(host) has full privilege in host, which defineHiddenClass needs.
                jobject trusted = env->GetStaticObjectField(lookup_class, trusted_id);
                jobject lookup = env->CallObjectMethod(trusted, in_id, host);

                jbyteArray array = env->NewByteArray(static_cast<jsize>(bytes.size()));
                env->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
                jobjectArray options = env->NewObjectArray(0, option_class, nullptr);

                jobject hidden = lookup ? env->CallObjectMethod(lookup, define_id, array, JNI_TRUE, options) : nullptr;
                if (hidden)
                {
                    result = static_cast<jclass>(env->CallObjectMethod(hidden, lookup_class_id));
                    env->DeleteLocalRef(hidden);
                }

                env->DeleteLocalRef(options);
                env->DeleteLocalRef(array);
                env->DeleteLocalRef(lookup);
                env->DeleteLocalRef(trusted);
            }

            if (env->ExceptionCheck())
            {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }

            env->DeleteLocalRef(lookup_class);
            env->DeleteLocalRef(option_class);
            return result;
        }

        // The package of `host` as a class-name prefix: "a/b/" for a.b.Client, "" for a class with no package.
        std::string PackageOf(JNIEnv* env, jclass host)
        {
            jclass class_class = env->GetObjectClass(host);
            jmethodID methodID = env->GetMethodID(class_class, "getName", "()Ljava/lang/String;");
            jstring name = static_cast<jstring>(env->CallObjectMethod(host, methodID));

            const char* chars = env->GetStringUTFChars(name, nullptr);
            std::string result = chars;
            env->ReleaseStringUTFChars(name, chars);
            env->DeleteLocalRef(name);
            env->DeleteLocalRef(class_class);

            result.erase(result.rfind('.') + 1); // With no '.', rfind is npos and npos + 1 is 0.
            std::replace(result.begin(), result.end(), '.', '/');
            return result;
        }

        // True when the class loader of `host` is not the boot loader.
        // A hidden class in a boot-loader package (java.awt) makes Java 17 print a warning to the game's console.
        bool HasClassLoader(JNIEnv* env, jclass host)
        {
            jclass class_class = env->GetObjectClass(host);
            jmethodID methodID = env->GetMethodID(class_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
            jobject loader = env->CallObjectMethod(host, methodID);
            env->DeleteLocalRef(class_class);
            env->DeleteLocalRef(loader);
            return loader != nullptr;
        }
    }

    jobject RIEvent::Wrap(JNIEnv* env, jobject original) noexcept
    {
        if (wrapper_class)
        {
            jmethodID methodID = env->GetMethodID(wrapper_class, "<init>", "(Ljava/awt/AWTEvent;)V");
            jobject ref = env->NewObject(wrapper_class, methodID, original);

            env->DeleteLocalRef(std::exchange(ref, env->NewGlobalRef(ref)));
            return ref;
        }
        return nullptr;
    }

    RIEvent::RIEvent(JNIEnv* env, AWTEvent* original) noexcept : AWTEvent(RIEvent::Wrap(env, original->get())), env(env)
    {
    }

    RIEvent::RIEvent(JNIEnv* env, jobject original) noexcept : AWTEvent(RIEvent::Wrap(env, original)), env(env)
    {
    }

    RIEvent::RIEvent(RIEvent&& other) noexcept
    {
        this->env = other.env;
        this->self = other.self;
        other.self = nullptr;
    }

    RIEvent::~RIEvent() noexcept
    {
        if (self)
        {
            env->DeleteGlobalRef(self);
            self = nullptr;
            env = nullptr;
        }
    }

    RIEvent& RIEvent::operator = (RIEvent&& other) noexcept
    {
        if (self)
        {
            env->DeleteGlobalRef(self);
        }

        this->env = other.env;
        this->self = other.self;
        other.self = nullptr;
        return *this;
    }

    void RIEventQueue::DefineClass(jobject host)
    {
        // EventQueue
        std::vector<std::uint8_t> queue_bytes = {
            0xCA, 0xFE, 0xBA, 0xBE, 0x0, 0x0, 0x0, 0x32, 0x0, 0x16, 0x1, 0x0, 0x19, 0x65, 0x6F,
            0x73, 0x2F, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x49, 0x6E, 0x70, 0x75, 0x74, 0x2F, 0x45,
            0x76, 0x65, 0x6E, 0x74, 0x51, 0x75, 0x65, 0x75, 0x65, 0x7, 0x0, 0x1, 0x1, 0x0, 0x13,
            0x6A, 0x61, 0x76, 0x61, 0x2F, 0x61, 0x77, 0x74, 0x2F, 0x45, 0x76, 0x65, 0x6E, 0x74,
            0x51, 0x75, 0x65, 0x75, 0x65, 0x7, 0x0, 0x3, 0x1, 0x0, 0xF, 0x45, 0x76, 0x65, 0x6E,
            0x74, 0x51, 0x75, 0x65, 0x75, 0x65, 0x2E, 0x6A, 0x61, 0x76, 0x61, 0x1, 0x0, 0xB, 0x6E,
            0x61, 0x74, 0x69, 0x76, 0x65, 0x51, 0x75, 0x65, 0x75, 0x65, 0x1, 0x0, 0x1, 0x4A, 0x1,
            0x0, 0x6, 0x3C, 0x69, 0x6E, 0x69, 0x74, 0x3E, 0x1, 0x0, 0x3, 0x28, 0x29, 0x56, 0xC, 0x0,
            0x8, 0x0, 0x9, 0xA, 0x0, 0x4, 0x0, 0xA, 0xC, 0x0, 0x6, 0x0, 0x7, 0x9, 0x0, 0x2, 0x0, 0xC,
            0x1, 0x0, 0x4, 0x74, 0x68, 0x69, 0x73, 0x1, 0x0, 0x1B, 0x4C, 0x65, 0x6F, 0x73, 0x2F, 0x45,
            0x76, 0x65, 0x6E, 0x74, 0x49, 0x6E, 0x70, 0x75, 0x74, 0x2F, 0x45, 0x76, 0x65, 0x6E, 0x74,
            0x51, 0x75, 0x65, 0x75, 0x65, 0x3B, 0x1, 0x0, 0xD, 0x64, 0x69, 0x73, 0x70, 0x61, 0x74, 0x63,
            0x68, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x1, 0x0, 0x16, 0x28, 0x4C, 0x6A, 0x61, 0x76, 0x61, 0x2F,
            0x61, 0x77, 0x74, 0x2F, 0x41, 0x57, 0x54, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x3B, 0x29, 0x56,
            0x1, 0x0, 0x4, 0x43, 0x6F, 0x64, 0x65, 0x1, 0x0, 0x12, 0x4C, 0x6F, 0x63, 0x61, 0x6C, 0x56,
            0x61, 0x72, 0x69, 0x61, 0x62, 0x6C, 0x65, 0x54, 0x61, 0x62, 0x6C, 0x65, 0x1, 0x0, 0xF, 0x4C,
            0x69, 0x6E, 0x65, 0x4E, 0x75, 0x6D, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62, 0x6C, 0x65, 0x1, 0x0,
            0xA, 0x53, 0x6F, 0x75, 0x72, 0x63, 0x65, 0x46, 0x69, 0x6C, 0x65, 0x0, 0x21, 0x0, 0x2, 0x0,
            0x4, 0x0, 0x0, 0x0, 0x1, 0x0, 0x2, 0x0, 0x6, 0x0, 0x7, 0x0, 0x0, 0x0, 0x2, 0x0, 0x1, 0x0, 0x8,
            0x0, 0x9, 0x0, 0x1, 0x0, 0x12, 0x0, 0x0, 0x0, 0x38, 0x0, 0x3, 0x0, 0x1, 0x0, 0x0, 0x0, 0xA,
            0x2A, 0xB7, 0x0, 0xB, 0x2A, 0x9, 0xB5, 0x0, 0xD, 0xB1, 0x0, 0x0, 0x0, 0x2, 0x0, 0x13, 0x0, 0x0,
            0x0, 0xC, 0x0, 0x1, 0x0, 0x0, 0x0, 0xA, 0x0, 0xE, 0x0, 0xF, 0x0, 0x0, 0x0, 0x14, 0x0, 0x0, 0x0,
            0xA, 0x0, 0x2, 0x0, 0x0, 0x0, 0x3, 0x0, 0x4, 0x0, 0x4, 0x1, 0x4, 0x0, 0x10, 0x0, 0x11, 0x0,
            0x0, 0x0, 0x1, 0x0, 0x15, 0x0, 0x0, 0x0, 0x2, 0x0, 0x5
        };

        // AWTEvent
        std::vector<std::uint8_t> event_bytes = {
            0xCA, 0xFE, 0xBA, 0xBE, 0x0, 0x0, 0x0, 0x32, 0x0, 0x1E, 0x1, 0x0, 0x17, 0x65, 0x6F, 0x73, 0x2F,
            0x45, 0x76, 0x65, 0x6E, 0x74, 0x49, 0x6E, 0x70, 0x75, 0x74, 0x2F, 0x41, 0x57, 0x54, 0x45, 0x76,
            0x65, 0x6E, 0x74, 0x7, 0x0, 0x1, 0x1, 0x0, 0x11, 0x6A, 0x61, 0x76, 0x61, 0x2F, 0x61, 0x77, 0x74,
            0x2F, 0x41, 0x57, 0x54, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x7, 0x0, 0x3, 0x1, 0x0, 0xD, 0x41, 0x57,
            0x54, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x2E, 0x6A, 0x61, 0x76, 0x61, 0x1, 0x0, 0x8, 0x6F, 0x72, 0x69,
            0x67, 0x69, 0x6E, 0x61, 0x6C, 0x1, 0x0, 0x13, 0x4C, 0x6A, 0x61, 0x76, 0x61, 0x2F, 0x61, 0x77, 0x74,
            0x2F, 0x41, 0x57, 0x54, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x3B, 0x1, 0x0, 0x6, 0x3C, 0x69, 0x6E, 0x69,
            0x74, 0x3E, 0x1, 0x0, 0x16, 0x28, 0x4C, 0x6A, 0x61, 0x76, 0x61, 0x2F, 0x61, 0x77, 0x74, 0x2F, 0x41,
            0x57, 0x54, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x3B, 0x29, 0x56, 0x1, 0x0, 0x9, 0x67, 0x65, 0x74, 0x53,
            0x6F, 0x75, 0x72, 0x63, 0x65, 0x1, 0x0, 0x14, 0x28, 0x29, 0x4C, 0x6A, 0x61, 0x76, 0x61, 0x2F, 0x6C,
            0x61, 0x6E, 0x67, 0x2F, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x3B, 0xC, 0x0, 0xA, 0x0, 0xB, 0xA, 0x0,
            0x4, 0x0, 0xC, 0x1, 0x0, 0x5, 0x67, 0x65, 0x74, 0x49, 0x44, 0x1, 0x0, 0x3, 0x28, 0x29, 0x49, 0xC, 0x0,
            0xE, 0x0, 0xF, 0xA, 0x0, 0x4, 0x0, 0x10, 0x1, 0x0, 0x16, 0x28, 0x4C, 0x6A, 0x61, 0x76, 0x61, 0x2F, 0x6C,
            0x61, 0x6E, 0x67, 0x2F, 0x4F, 0x62, 0x6A, 0x65, 0x63, 0x74, 0x3B, 0x49, 0x29, 0x56, 0xC, 0x0, 0x8, 0x0,
            0x12, 0xA, 0x0, 0x4, 0x0, 0x13, 0xC, 0x0, 0x6, 0x0, 0x7, 0x9, 0x0, 0x2, 0x0, 0x15, 0x1, 0x0, 0x4, 0x74,
            0x68, 0x69, 0x73, 0x1, 0x0, 0x19, 0x4C, 0x65, 0x6F, 0x73, 0x2F, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x49, 0x6E,
            0x70, 0x75, 0x74, 0x2F, 0x41, 0x57, 0x54, 0x45, 0x76, 0x65, 0x6E, 0x74, 0x3B, 0x1, 0x0, 0x1, 0x65, 0x1,
            0x0, 0x4, 0x43, 0x6F, 0x64, 0x65, 0x1, 0x0, 0x12, 0x4C, 0x6F, 0x63, 0x61, 0x6C, 0x56, 0x61, 0x72, 0x69,
            0x61, 0x62, 0x6C, 0x65, 0x54, 0x61, 0x62, 0x6C, 0x65, 0x1, 0x0, 0xF, 0x4C, 0x69, 0x6E, 0x65, 0x4E, 0x75,
            0x6D, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62, 0x6C, 0x65, 0x1, 0x0, 0xA, 0x53, 0x6F, 0x75, 0x72, 0x63, 0x65,
            0x46, 0x69, 0x6C, 0x65, 0x0, 0x21, 0x0, 0x2, 0x0, 0x4, 0x0, 0x0, 0x0, 0x1, 0x0, 0x2, 0x0, 0x6, 0x0, 0x7,
            0x0, 0x0, 0x0, 0x1, 0x0, 0x1, 0x0, 0x8, 0x0, 0x9, 0x0, 0x1, 0x0, 0x1A, 0x0, 0x0, 0x0, 0x4E, 0x0, 0x3, 0x0,
            0x2, 0x0, 0x0, 0x0, 0x12, 0x2A, 0x2B, 0xB6, 0x0, 0xD, 0x2B, 0xB6, 0x0, 0x11, 0xB7, 0x0, 0x14, 0x2A, 0x2B,
            0xB5, 0x0, 0x16, 0xB1, 0x0, 0x0, 0x0, 0x2, 0x0, 0x1B, 0x0, 0x0, 0x0, 0x16, 0x0, 0x2, 0x0, 0x0, 0x0, 0x12,
            0x0, 0x17, 0x0, 0x18, 0x0, 0x0, 0x0, 0x0, 0x0, 0x12, 0x0, 0x19, 0x0, 0x7, 0x0, 0x1, 0x0, 0x1C, 0x0, 0x0,
            0x0, 0xE, 0x0, 0x3, 0x0, 0x0, 0x0, 0x5, 0x0, 0xC, 0x0, 0x6, 0x0, 0x11, 0x0, 0x7, 0x0, 0x1, 0x0, 0x1D, 0x0,
            0x0, 0x0, 0x2, 0x0, 0x5
        };

        // Java 15+: hidden classes in the game's loader and package, which no name can find.
        jclass cls = nullptr;
        jclass event_cls = nullptr;
        jclass host_class = host ? env->GetObjectClass(host) : nullptr;
        if (host_class && HasClassLoader(env, host_class))
        {
            std::string prefix = PackageOf(env, host_class) + "RI";
            event_cls = DefineHiddenClass(env, host_class, RenameClasses(event_bytes, prefix));
            cls = event_cls ? DefineHiddenClass(env, host_class, RenameClasses(queue_bytes, prefix)) : nullptr;
        }

        if (host_class)
        {
            env->DeleteLocalRef(host_class);
        }

        // Older JVMs: the named classes in the boot loader.
        if (!cls)
        {
            if (event_cls)
            {
                env->DeleteLocalRef(event_cls);
            }

            //ClassLoader systemClassLoader = sun.awt.AppContext.getAppContext().getContextClassLoader();
            cls = env->DefineClass("eos/EventInput/EventQueue",
                                   nullptr,
                                   reinterpret_cast<const jbyte *>(&queue_bytes[0]),
                                   static_cast<jsize>(queue_bytes.size()));

            if (!cls && env->ExceptionCheck())
            {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }

            event_cls = env->DefineClass("eos/EventInput/AWTEvent",
                                         nullptr,
                                         reinterpret_cast<const jbyte *>(&event_bytes[0]),
                                         static_cast<jsize>(event_bytes.size()));

            if (env->ExceptionCheck())
            {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }

        // Keep the first wrapper class: a queue from an earlier start may still unwrap with it.
        if (event_cls)
        {
            if (!wrapper_class)
            {
                wrapper_class = static_cast<jclass>(env->NewGlobalRef(event_cls));
            }
            env->DeleteLocalRef(event_cls);
        }

        if (cls)
        {
            // Apparently Order of Calls matters in Java-1.6.
            // Must call RegisterNatives AFTER creating an instance of the class
            // OR initialize the class first, by calling GetMethodID.
            // This will crash on Java-1.6 if not done in the correct order.
            jmethodID methodID = env->GetMethodID(cls, "<init>", "()V");
            this->queue = env->NewObject(cls, methodID);
            env->DeleteLocalRef(std::exchange(this->queue, env->NewGlobalRef(this->queue)));

            if (this->queue)
            {
                jfieldID fieldID = env->GetFieldID(cls, "nativeQueue", "J");
                env->SetLongField(this->queue, fieldID, reinterpret_cast<std::intptr_t>(this));
            }
            else
            {
                if (env->ExceptionCheck())
                {
                    env->ExceptionDescribe();
                    env->ExceptionClear();
                }
            }

            JNINativeMethod method = {
                const_cast<char*>("dispatchEvent"),
                const_cast<char*>("(Ljava/awt/AWTEvent;)V"),
                reinterpret_cast<void *>(&RIEventQueue::DispatchEvent)
            };

            env->RegisterNatives(cls, &method, 1);
            env->DeleteLocalRef(cls);
        }
    }

    void RIEventQueue::DispatchEvent(JNIEnv* env, jobject thiz, jobject event)
    {
        jclass cls = env->GetObjectClass(thiz);
        if (cls)
        {
            // super.dispatchEvent
            auto dispatch_event = [](JNIEnv* env, jclass cls, jobject thiz, jobject event) {
                jclass super_class = env->GetSuperclass(cls);

                // super.dispatchEvent
                if (super_class)
                {
                    jmethodID methodID = env->GetMethodID(super_class, "dispatchEvent", "(Ljava/awt/AWTEvent;)V");
                    env->CallNonvirtualVoidMethod(thiz, super_class, methodID, event);
                    env->DeleteLocalRef(super_class);
                }
            };

            // If the class is an instance of our event type, pass it on
            if (wrapper_class && env->IsInstanceOf(event, wrapper_class))
            {
                jfieldID fieldID = env->GetFieldID(wrapper_class, "original", "Ljava/awt/AWTEvent;");
                jobject original = env->GetObjectField(event, fieldID);
                if (original)
                {
                    // super.dispatchEvent(original)
                    dispatch_event(env, cls, thiz, original);
                    env->DeleteLocalRef(original);
                    env->DeleteLocalRef(cls);
                }
                return;
            }

            jfieldID fieldID = env->GetFieldID(cls, "nativeQueue", "J");
            RIEventQueue* this_ = reinterpret_cast<RIEventQueue*>(env->GetLongField(thiz, fieldID));

            if (this_ && (this_->is_blocking_keyboard_events || this_->is_blocking_mouse_events))
            {
                // Block events on the canvas
                jobject source = AWTEvent(event).getSource(env);
                if (source)
                {
                    jclass canvas_class = env->FindClass("java/awt/Canvas");
                    if (canvas_class)
                    {
                        if (env->IsInstanceOf(source, canvas_class))
                        {
                            // Check what kind of event it is..
                            jclass key_event_class = env->FindClass("java/awt/event/KeyEvent");
                            jclass mouse_event_class = env->FindClass("java/awt/event/MouseEvent");
                            jclass focus_event_class = env->FindClass("java/awt/event/FocusEvent");

                            if (key_event_class && mouse_event_class && focus_event_class)
                            {
                                // BLOCKED EVENT
                                if ((this_->is_blocking_keyboard_events && env->IsInstanceOf(event, key_event_class)) ||
                                    (this_->is_blocking_mouse_events && env->IsInstanceOf(event, mouse_event_class)) ||
                                    env->IsInstanceOf(event, focus_event_class))
                                {
                                    env->DeleteLocalRef(focus_event_class);
                                    env->DeleteLocalRef(mouse_event_class);
                                    env->DeleteLocalRef(key_event_class);

                                    env->DeleteLocalRef(canvas_class);
                                    env->DeleteLocalRef(source);
                                    env->DeleteLocalRef(cls);
                                    return;
                                }
                            }

                            if (focus_event_class)
                            {
                                env->DeleteLocalRef(focus_event_class);
                            }

                            if (mouse_event_class)
                            {
                                env->DeleteLocalRef(mouse_event_class);
                            }

                            if (key_event_class)
                            {
                                env->DeleteLocalRef(key_event_class);
                            }
                        }
                        env->DeleteLocalRef(canvas_class);
                    }
                    env->DeleteLocalRef(source);
                }

                // Block events on the window/frame
                jclass window_class = env->FindClass("java/awt/event/WindowEvent");
                if (window_class)
                {
                    if (env->IsInstanceOf(event, window_class))
                    {
                        jclass event_class = env->GetObjectClass(event);
                        if (event_class)
                        {
                            jmethodID getIDMethodID = env->GetMethodID(event_class, "getID", "()I");
                            jint event_id = env->CallIntMethod(event, getIDMethodID);

                            if (event_id == WindowEvent::WindowEventCodes::WINDOW_ICONIFIED ||
                                event_id == WindowEvent::WindowEventCodes::WINDOW_DEICONIFIED ||
                                event_id == WindowEvent::WindowEventCodes::WINDOW_ACTIVATED ||
                                event_id == WindowEvent::WindowEventCodes::WINDOW_DEACTIVATED ||
                                event_id == WindowEvent::WindowEventCodes::WINDOW_GAINED_FOCUS ||
                                event_id == WindowEvent::WindowEventCodes::WINDOW_LOST_FOCUS ||
                                event_id == WindowEvent::WindowEventCodes::WINDOW_STATE_CHANGED)
                            {
                                env->DeleteLocalRef(event_class);
                                env->DeleteLocalRef(window_class);
                                env->DeleteLocalRef(cls);
                                return;
                            }
                            env->DeleteLocalRef(event_class);
                        }
                    }
                    env->DeleteLocalRef(window_class);
                }
            }

            // super.dispatchEvent(event)
            dispatch_event(env, cls, thiz, event);
            env->DeleteLocalRef(cls);
        }
    }

    RIEventQueue::RIEventQueue(JNIEnv* env, jobject host) noexcept : EventQueue(env), is_blocking_keyboard_events(false), is_blocking_mouse_events(false)
    {
        this->DefineClass(host);
    }

    bool RIEventQueue::is_keyboard_input_enabled() const noexcept
    {
        return !is_blocking_keyboard_events;
    }

    void RIEventQueue::set_keyboard_input_enabled(bool enabled) noexcept
    {
        is_blocking_keyboard_events = !enabled;
    }

    bool RIEventQueue::is_mouse_input_enabled() const noexcept
    {
        return !is_blocking_mouse_events;
    }

    void RIEventQueue::set_mouse_input_enabled(bool enabled) noexcept
    {
        is_blocking_mouse_events = !enabled;
    }
}