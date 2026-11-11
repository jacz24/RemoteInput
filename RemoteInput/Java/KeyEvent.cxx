//
//  KeyEvent.cxx
//  RemoteInput
//
//  Created by Brandon on 2020-01-09.
//  Copyright © 2020 XIO. All rights reserved.
//

#include "KeyEvent.hxx"
#include "Platform.hxx"
#include <utility>

namespace java
{
	//The private fields the public constructor leaves 0, which a hand's key event carries: RuneLite's Keybind.matches
	//reads getExtendedKeyCode (Simba-Reflection #206). Set as the JDK's Windows peer sets them (awt_Component.cpp's
	//SendKeyEvent): the key's entry in its table of the layout, whatever the modifiers held; the key code where the
	//entry holds none (a key with no char, a numpad key, Enter's '\r').
	//shortcut: on macOS and Linux they stay 0, where those peers fill them from their own key codes: fill them when a
	//client runs there.
	static void SetNativeFields(JNIEnv* env, jclass cls, jobject event, std::int32_t keyCode, std::int32_t nativeKey) noexcept
	{
		#if defined(_WIN32) || defined(_WIN64)
		NativeKey key = GetNativeKey(nativeKey);
		std::int32_t extended = 0;
		if (key.extended_char)
		{
			static jmethodID forChar = env->GetStaticMethodID(cls, "getExtendedKeyCodeForChar", "(I)I");
			extended = env->CallStaticIntMethod(cls, forChar, key.extended_char);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
				extended = 0;
			}
		}

		static jfieldID raw = env->GetFieldID(cls, "rawCode", "J");
		static jfieldID scancode = env->GetFieldID(cls, "scancode", "J");
		static jfieldID unicode = env->GetFieldID(cls, "primaryLevelUnicode", "J");
		static jfieldID extendedKeyCode = env->GetFieldID(cls, "extendedKeyCode", "J");
		env->SetLongField(event, raw, key.raw_code);
		env->SetLongField(event, scancode, key.scancode);
		env->SetLongField(event, unicode, key.unicode);
		env->SetLongField(event, extendedKeyCode, extended ? extended : keyCode);
		#endif
	}

	KeyEvent::KeyEvent(JNIEnv* env, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t keyCode, jchar keyChar, std::int32_t keyLocation) noexcept : AWTEvent(), env(env), cls(nullptr)
	{
		this->cls = env->FindClass("java/awt/event/KeyEvent");
		env->DeleteLocalRef(std::exchange(this->cls, static_cast<jclass>(env->NewGlobalRef(this->cls))));

		static jmethodID methodId = env->GetMethodID(cls, "<init>", "(Ljava/awt/Component;IJIICI)V");
		self = env->NewObject(cls, methodId, source->get(), id, when, modifiers, keyCode, keyChar, keyLocation);
		env->DeleteLocalRef(std::exchange(self, static_cast<jclass>(env->NewGlobalRef(self))));
	}

	KeyEvent::~KeyEvent() noexcept
	{
		env->DeleteGlobalRef(cls);
		env->DeleteGlobalRef(self);
	}

	void KeyEvent::Dispatch(JNIEnv* env, Component* receiver, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t keyCode, jchar keyChar, std::int32_t keyLocation, bool is_system_generated) noexcept
	{
		jobject event = Build(env, "java/awt/event/KeyEvent", [&](jclass cls) {
			static jmethodID methodId = env->GetMethodID(cls, "<init>", "(Ljava/awt/Component;IJIICI)V");
			return env->NewObject(cls, methodId, source->get(), id, when, modifiers, keyCode, keyChar, keyLocation);
		});

		if (event)
		{
			AWTEvent::SetSystemGenerated(env, event, is_system_generated);
			receiver->dispatchEvent(event);
			env->DeleteLocalRef(event);
		}
	}

	void KeyEvent::Post(JNIEnv* env, Component* source, std::int32_t id, std::int64_t when, std::int32_t modifiers, std::int32_t keyCode, jchar keyChar, std::int32_t keyLocation, std::int32_t nativeKey, bool is_system_generated) noexcept
	{
		jobject event = Build(env, "java/awt/event/KeyEvent", [&](jclass cls) {
			static jmethodID methodId = env->GetMethodID(cls, "<init>", "(Ljava/awt/Component;IJIICI)V");
			jobject built = env->NewObject(cls, methodId, source->get(), id, when, modifiers, keyCode, keyChar, keyLocation);
			if (built && nativeKey)
			{
				SetNativeFields(env, cls, built, keyCode, nativeKey);
			}
			return built;
		});

		if (event)
		{
			AWTEvent::Post(env, event, is_system_generated);
			env->DeleteLocalRef(event);
		}
	}
}