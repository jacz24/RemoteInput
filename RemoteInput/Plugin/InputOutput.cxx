//
//  InputOutput.cxx
//  RemoteInput
//
//  Created by Brandon on 2019-12-28.
//  Copyright © 2019 XIO. All rights reserved.
//

#include "InputOutput.hxx"
#include "ControlCenter.hxx"
#include "Platform.hxx"
#include "Random.hxx"
#include "Applet.hxx"
#include "InputEvent.hxx"
#include "FocusEvent.hxx"
#include "KeyEvent.hxx"
#include "MouseEvent.hxx"
#include "MouseWheelEvent.hxx"
#include "PointerInfo.hxx"
#include "SunToolkit.hxx"
#include "Toolkit.hxx"
#include "Window.hxx"
#include "WindowEvent.hxx"
#include <cmath>

#if !defined(_MSC_VER)
#warning "WINDOWS BS"
#endif

#ifdef VK_SHIFT
#undef VK_SHIFT
#undef VK_LSHIFT
#undef VK_RSHIFT
#undef VK_CONTROL
#undef VK_APPS
#undef VK_TAB
#undef VK_BACK
#undef VK_RETURN
#endif // VK_SHIFT

#if (defined(_WIN32) || defined(_WIN64)) && !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002 //Windows 10 1803's: older MinGW headers lack it.
#endif

enum ControlKeys : std::uint32_t
{
    VK_FUNCTION = 0x0,
    VK_NUM_LOCK = 0x90,
    VK_SHIFT = 0x10,
    VK_LSHIFT = 0xA0,
    VK_RSHIFT = 0xA1,
    VK_CONTROL = 0x11,
    VK_LEFT_CONTROL = 0xA2,
    VK_RIGHT_CONTROL = 0xA3,
    VK_ALT = 0x12, //VK_MENU
    VK_LEFT_ALT = 0xA4,
    VK_RIGHT_ALT = 0xA5,

    VK_COMMAND = 0x9D, //VK_META
    VK_LEFT_WINDOWS = 0x5B,
    VK_RIGHT_WINDOWS = 0x5C,
    VK_APPS = 0x5D,
    VK_TAB = 0x09,
    VK_BACK = 0x08,
    VK_RETURN = 0x0D
};

//A held control key posts one press, no KEY_TYPED and no repeat, as a Mac modifier does; VK_SHIFT, VK_CONTROL and VK_ALT are the codes Simba sends.
static ControlKeys control_keys[] = {
    ControlKeys::VK_FUNCTION,
    ControlKeys::VK_NUM_LOCK,
    ControlKeys::VK_SHIFT,
    ControlKeys::VK_LSHIFT,
    ControlKeys::VK_RSHIFT,
    ControlKeys::VK_CONTROL,
    ControlKeys::VK_LEFT_CONTROL,
    ControlKeys::VK_RIGHT_CONTROL,
    ControlKeys::VK_ALT,
    ControlKeys::VK_LEFT_ALT,
    ControlKeys::VK_RIGHT_ALT,
    ControlKeys::VK_COMMAND,
    ControlKeys::VK_LEFT_WINDOWS,
    ControlKeys::VK_RIGHT_WINDOWS,
    ControlKeys::VK_APPS
};

//The location of each key in control_keys, in the same order. A key with no side (VK_SHIFT) is the left one, as real AWT gives for java.awt.Robot's.
static java::KeyEvent::KeyCodes control_keys_locations[] = {
    java::KeyEvent::KeyCodes::KEY_LOCATION_UNKNOWN,  //VK_FUNCTION
    java::KeyEvent::KeyCodes::KEY_LOCATION_NUMPAD,   //VK_NUM_LOCK
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_SHIFT
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_LSHIFT
    java::KeyEvent::KeyCodes::KEY_LOCATION_RIGHT,    //VK_RSHIFT
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_CONTROL
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_LEFT_CONTROL
    java::KeyEvent::KeyCodes::KEY_LOCATION_RIGHT,    //VK_RIGHT_CONTROL
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_ALT
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_LEFT_ALT
    java::KeyEvent::KeyCodes::KEY_LOCATION_RIGHT,    //VK_RIGHT_ALT
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_COMMAND
    java::KeyEvent::KeyCodes::KEY_LOCATION_LEFT,     //VK_LEFT_WINDOWS
    java::KeyEvent::KeyCodes::KEY_LOCATION_RIGHT,    //VK_RIGHT_WINDOWS
    java::KeyEvent::KeyCodes::KEY_LOCATION_STANDARD  //VK_APPS
};
static_assert(std::size(control_keys) == std::size(control_keys_locations), "one location for each control key");

//MouseEvent.isPopupTrigger as real AWT sets it on each platform.
static bool IsPopupTrigger(std::int32_t id, std::int32_t button, std::int32_t modifiers) noexcept
{
    #if defined(_WIN32) || defined(_WIN64)
    return id == java::MouseEvent::MouseEventCodes::MOUSE_RELEASED && button == 3;
    #elif defined(__APPLE__)
    //NSEvent.isPopupTrigger: any event with the right button down, or Ctrl with the left.
    return (modifiers & java::InputEvent::BUTTON3_DOWN_MASK) || ((modifiers & java::InputEvent::CTRL_DOWN_MASK) && (modifiers & java::InputEvent::BUTTON1_DOWN_MASK));
    #else
    return id == java::MouseEvent::MouseEventCodes::MOUSE_PRESSED && button == 3;
    #endif
}

//The keyboard's repeats come at the first point at or after each deadline of a grid, whatever finer tick another process
//asked for (#105): at 33 ms, 2 steps between most and 3 between one in ten. The grid is not the OS's default tick of
//15.625 ms (Simba-Reflection #142): the operator's 6 holds of 2 s or more at a global tick of 1 ms (2026-10-28, 2026-10-30)
//fold best at 15.69-15.84 ms by the steady clock (QPC), a mean of 15.75, with a phase concentration of 0.57-0.96 there and
//0.11-0.44 at 15.625, as the grid's phase moves. A step of GRID_MS and up to GRID_STEP_MS either way (a random walk of the
//phase, sd 0.3 ms a step) gives those concentrations, the short intervals' spread (sd 0.45-0.60 ms, the keyboard's
//0.49-0.64) and the least interval by arrival (29.7 ms with the timer's lateness, the keyboard's 29.71) in a model of the
//holds (the probe's --model). Each hold's grid starts at a random phase. The client's own timers take its tick (a plain
//timer in the JVM woke 0-1.6 ms after its deadline), so a repeat finds its grid point itself, then waits on a
//high-resolution timer (0.03-0.76 ms late on the PC, never early). The client's sleep_until woke early and spun to the
//deadline, or woke a tick late, which gave intervals of 18-30 ms.
//shortcut: one period for every hold, where the keyboard's holds' best periods spread more (sd 0.059 ms over 6, the model's
//0.023): draw a period for each hold if a detector reads the spread. And the grid at the default tick is not measured, as a
//process held 1 ms with the browser closed (2026-10-30) and each Simba script with a layer asks for 1 ms: log it if a script
//with no layer ever holds a key.
static constexpr double GRID_MS = 15.75;
static constexpr double GRID_STEP_MS = 0.52;

static void wait_for_tick(std::chrono::steady_clock::time_point deadline, std::chrono::steady_clock::time_point& grid) noexcept
{
    #if defined(_WIN32) || defined(_WIN64)
    using hundred_ns = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
    while (grid < deadline)
    {
        grid += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::milli>(
            GRID_MS + Random::instance()->generate_random_double(-GRID_STEP_MS, GRID_STEP_MS)));
    }
    deadline = grid;
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, SYNCHRONIZE | TIMER_MODIFY_STATE);
    if (!timer)
    {
        //Before Windows 10 1803: a plain timer, on the client's tick.
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    if (timer)
    {
        LARGE_INTEGER relative;
        relative.QuadPart = -std::chrono::ceil<hundred_ns>(deadline - std::chrono::steady_clock::now()).count();
        if (relative.QuadPart < 0 && SetWaitableTimer(timer, &relative, 0, nullptr, nullptr, FALSE))
        {
            WaitForSingleObject(timer, INFINITE);
        }
        CloseHandle(timer);
        return;
    }
    #endif

    //Windows's Sleep can end up to a timer tick early: the first repeat came 499 ms after a press at a delay of 500 (#17).
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_until(deadline);
    }
}

InputOutput::InputOutput(Reflection* reflection) noexcept : vm(nullptr), applet(reflection->getApplet()), mutex(), input_thread(new ThreadPool(2)), keyboard_input_enabled(true), mouse_input_enabled(true), currently_held_key(-1), repeat_task(0), held_keys(), x(-1), y(-1), w(-1), h(-1), click_count(0), click_button(0), click_when(0), click_x(0), click_y(0), double_click_time(500), clickable_buttons(0), keyboard_speed(0), keyboard_repeat_delay(0), mouse_buttons(), focus_mu(206.9), focus_sigma(11), focus_tau(27.4)
{
    reflection->getEnv()->GetJavaVM(&vm);

    x = std::numeric_limits<std::int32_t>::min();
    y = std::numeric_limits<std::int32_t>::min();

    auto scale_to_range = [](float value, float fromMin, float fromMax, float toMin, float toMax) -> float {
        return (((toMax - toMin) * (value - fromMin)) / (fromMax - fromMin)) + toMin;
    };

    #if defined(_WIN32) || defined(_WIN64)
        this->double_click_time = GetDoubleClickTime();

        //0 (approximately 250 ms delay) through 3 (approximately 1 second delay).
        std::int32_t keyboard_delay = 0;
        SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, reinterpret_cast<void*>(&keyboard_delay), 0);

        //Small variance in delay due to electrical signal, lag, hardware, etc..
        std::int32_t min_delay = Random::instance()->generate_random_int(248, 252);
        std::int32_t max_delay = Random::instance()->generate_random_int(998, 1002);
        this->keyboard_repeat_delay = scale_to_range(keyboard_delay, 0, 3, min_delay, max_delay);

        //0 (approximately 2.5 repetitions per second) through 31 (approximately 30 repetitions per second).
        //The actual repeat rates are hardware-dependent and may vary from a linear scale by as much as 20%
        std::int32_t keyboard_repeat_speed = 0;
        SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, reinterpret_cast<void*>(&keyboard_repeat_speed), 0);

        //The user's setting, with no jitter: the OS repeats a held key on its own timer (#69).
        float repeat_rate = scale_to_range(keyboard_repeat_speed, 0, 31, 2.5, 30);
        this->keyboard_speed = round(1000.0 / repeat_rate);
    #elif defined(__APPLE__)
        //The user's settings, with no jitter: the OS repeats a held key on its own timer.
        std::int32_t key_repeat_delay = 0;
        std::int32_t key_repeat_interval = 0;
        std::int32_t click_time = 0;
        GetInputTimings(key_repeat_delay, key_repeat_interval, click_time);
        this->keyboard_repeat_delay = key_repeat_delay;
        this->keyboard_speed = key_repeat_interval;
        this->double_click_time = click_time;
    #else
        //Linux default is 660ms delay and 25ms repeat rate
        this->keyboard_repeat_delay = scale_to_range(44, 15, 120, 225, 1800) + Random::instance()->generate_random_int(-2, 2);

        float repeat_rate = scale_to_range(2, 2, 120, 25, 1800);
        repeat_rate += repeat_rate * Random::instance()->generate_random_float(0.0, 0.20);
        this->keyboard_speed = round(repeat_rate);
    #endif
}

InputOutput::~InputOutput() noexcept
{
    // Purposely do NOT join the thread or reset! This will hang upon dll unload.
    // We `release` it instead to prevent this.
    //this->input_thread->terminate();
    this->input_thread.release();
    this->vm = nullptr;
    this->applet = nullptr;
}

void InputOutput::handle_resize(java::Component* component) noexcept
{
    component->getSize(this->w, this->h);

    /*bool isMouseInsideComponent = !(this->x < 0 || this->y < 0 || this->x > static_cast<std::int32_t>(this->w) || this->y > static_cast<std::int32_t>(this->h));
    if (isMouseInsideComponent)
    {
        component->getMousePosition(this->x, this->y);

        if (this->x == -1 || this->y == -1)
        {
            JNIEnv* env = component->getEnv();

            std::int32_t x = this->x;
            std::int32_t y = this->y;

            java::PointerInfo info = java::PointerInfo::getPointerInfo(env);
            info.getLocation(x, y);
            info.PointToScreen(env, x, y,component);

            isMouseInsideComponent = !(x < 0 || y < 0 || x > static_cast<std::int32_t>(this->w) || y > static_cast<std::int32_t>(this->h));
            if (isMouseInsideComponent)
            {
                this->x = x;
                this->y = y;
            }
        }
    }*/
}

void InputOutput::hold_key(std::int32_t code) noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    //Key is not already held..
    if (!this->is_key_held(code))
    {
        //Key is a control key
        if (std::find(std::begin(control_keys), std::end(control_keys), code) != std::end(control_keys))
        {
            //Control Keys only generate a single held event..
            held_keys.push_back(code);

            //PostEvent
            java::Component receiver = control_center->reflect_canvas();

            JNIEnv* env = receiver.getEnv();
            if (!this->has_focus(&receiver))
            {
                this->gain_focus(&receiver, true);
            }

            std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::int32_t modifiers = GetActiveKeyModifiers();
            std::int32_t keycode = GetJavaKeyCode(code);
            std::int32_t location = GetKeyLocation(code);

            java::KeyEvent::Post(env,
                                 &receiver,
                                 java::KeyEvent::KeyCodes::KEY_PRESSED,
                                 when,
                                 modifiers,
                                 keycode,
                                 static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                                 location);
        }
        else
        {
            //A key pressed while another is held (rollover) takes the same path: its repeat task ends the held key's, as a keyboard repeats only the newest key.
            {
                //Regain the focus before the key counts as held: a released key's repeat task that wakes in the reaction's
                //wait then sees no held key and ends, where it would post this key's repeats before its press (#61).
                java::Component receiver = control_center->reflect_canvas();
                if (!this->has_focus(&receiver))
                {
                    this->gain_focus(&receiver, true);
                }
                std::int32_t modifiers = GetActiveKeyModifiers();
                jchar key_char = NativeKeyCodeToChar(code, modifiers);

                //The new key's task is the newest before the key counts as held, both under the lock a repeat task holds from its
                //check through its posts, so no older task reads this key as held while its own task is the newest. With the key
                //set first and the task after this key's posts, one that checked in that window posted a repeat of this key
                //before its delay (#104). A repeat that passed its check posts before this press.
                std::uint32_t task;
                {
                    std::lock_guard<std::mutex> lock(this->mutex);
                    task = ++repeat_task;
                    currently_held_key = code;
                    held_chars[code] = key_char;
                }
                held_keys.push_back(code);

                //Post Event
                JNIEnv* env = receiver.getEnv();
                std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                auto pressed_at = std::chrono::steady_clock::now();
                std::int32_t keycode = GetJavaKeyCode(code);
                std::int32_t location = GetKeyLocation(code);

                java::KeyEvent::Post(env,
                                     &receiver,
                                     java::KeyEvent::KeyCodes::KEY_PRESSED,
                                     when,
                                     modifiers,
                                     keycode,
                                     key_char,
                                     location);

                //A key with no char (an arrow, an F-key) gets no WM_CHAR on Windows, so real AWT posts no KEY_TYPED (#82).
                if (key_char != static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED))
                {
                    java::KeyEvent::Post(env,
                                         &receiver,
                                         java::KeyEvent::KeyCodes::KEY_TYPED,
                                         when,
                                         modifiers,
                                         0,
                                         key_char,
                                         java::KeyEvent::KeyCodes::KEY_LOCATION_UNKNOWN);
                }

                if (this->keyboard_speed >= 0 && this->keyboard_repeat_delay >= 0)
                {
                    //Repeats keep the OS timer's schedule from the press: the first after the delay, then one each interval, however long a post takes.
                    auto next = pressed_at + std::chrono::milliseconds(this->keyboard_repeat_delay);
                    auto grid = pressed_at - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double, std::milli>(Random::instance()->generate_random_double(0, GRID_MS)));
                    input_thread->add_task([&, task, next, grid](std::atomic_bool &stopped) mutable {
                        wait_for_tick(next, grid);

                        //A key released within the repeat delay never repeats, as on a keyboard: end before the attach.
                        if (currently_held_key == -1 || repeat_task != task)
                        {
                            return;
                        }

                        JNIEnv* env = nullptr;
                        this->vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr);

                        //Declared before the wrappers, so it detaches after their destructors release their references.
                        struct Detach { JavaVM* vm; ~Detach() { vm->DetachCurrentThread(); } } detach{this->vm};

                        java::Applet applet{env, this->applet, false};
                        java::Component receiver = applet.getComponent(0);

                        while (!stopped)
                        {
                            if (currently_held_key == -1)
                            {
                                break;
                            }

                            wait_for_tick(next, grid);

                            //An OS repeat timer skips the ticks a stall missed (gain_focus can wait 2 s); it does not post them in a burst.
                            auto now = std::chrono::steady_clock::now();
                            auto interval = std::chrono::milliseconds(this->keyboard_speed);
                            next = (now - next >= interval ? now : next) + interval;

                            //Read the key after the sleep: a release or a newer task during the sleep ends this one, with no press after the release.
                            //release_key takes the same lock, so a release cannot come between this check and the last post below.
                            std::lock_guard<std::mutex> lock(this->mutex);
                            std::int32_t code = currently_held_key;
                            if (code == -1 || repeat_task != task)
                            {
                                break;
                            }

                            //Post Event. A repeat is no new key: it waits no reaction.
                            if (!this->has_focus(&receiver))
                            {
                                this->gain_focus(&receiver, false);
                            }

                            std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                            std::int32_t modifiers = GetActiveKeyModifiers();
                            std::int32_t keycode = GetJavaKeyCode(code);
                            std::int32_t location = GetKeyLocation(code);
                            jchar key_char = NativeKeyCodeToChar(code, modifiers);
                            held_chars[code] = key_char;

                            java::KeyEvent::Post(env,
                                                 &receiver,
                                                 java::KeyEvent::KeyCodes::KEY_PRESSED,
                                                 when,
                                                 modifiers,
                                                 keycode,
                                                 key_char,
                                                 location);

                            //Each repeat is a WM_KEYDOWN: no KEY_TYPED for a key with no char, as for its press (#82).
                            if (key_char != static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED))
                            {
                                java::KeyEvent::Post(env,
                                                     &receiver,
                                                     java::KeyEvent::KeyCodes::KEY_TYPED,
                                                     when,
                                                     modifiers,
                                                     0,
                                                     key_char,
                                                     java::KeyEvent::KeyCodes::KEY_LOCATION_UNKNOWN);
                            }
                        }
                    });
                }
            }
        }
    }
}

void InputOutput::release_key(std::int32_t code) noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    if (auto it = std::find(held_keys.begin(), held_keys.end(), code); it != held_keys.end())
    {
        if (std::find(std::begin(control_keys), std::end(control_keys), code) != std::end(control_keys))
        {
            held_keys.erase(it); //held_keys.erase(std::remove(held_keys.begin(), held_keys.end(), code), held_keys.end());

            //Post Event
            java::Component receiver = control_center->reflect_canvas();
            if (!this->has_focus(&receiver))
            {
                this->gain_focus(&receiver, true);
            }

            JNIEnv* env = receiver.getEnv();
            std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::int32_t modifiers = GetActiveKeyModifiers();
            std::int32_t keycode = GetJavaKeyCode(code);
            std::int32_t location = GetKeyLocation(code);

            java::KeyEvent::Post(env,
                                 &receiver,
                                 java::KeyEvent::KeyCodes::KEY_RELEASED,
                                 when,
                                 modifiers,
                                 keycode,
                                 static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                                 location);
        }
        else
        {
            //A key-repeat task posts under the same lock, so none of its events can come after this release.
            std::lock_guard<std::mutex> lock(this->mutex);

            //Remove the held key from the list..
            held_keys.erase(it);

            //The repeating key's release ends the repeat, though an older key is still down: a keyboard repeats no key it
            //has passed over (#17, Windows 11). An older key's release leaves the repeat alone.
            if (currently_held_key == code)
            {
                currently_held_key = -1;
            }

            //Post Event
            java::Component receiver = control_center->reflect_canvas();
            if (!this->has_focus(&receiver))
            {
                this->gain_focus(&receiver, true);
            }

            JNIEnv* env = receiver.getEnv();
            std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::int32_t modifiers = GetActiveKeyModifiers();
            std::int32_t keycode = GetJavaKeyCode(code);
            std::int32_t location = GetKeyLocation(code);

            //AWT's Windows peer saves the char of each WM_KEYDOWN and gives it to the WM_KEYUP (WindowsKeyToJavaChar's SAVE and
            //LOAD), whatever Shift did in between (Simba-Reflection #141). X11's and Cocoa's take the modifiers at the release.
            #if defined(_WIN32) || defined(_WIN64)
            jchar key_char = held_chars[code];
            #else
            jchar key_char = NativeKeyCodeToChar(code, modifiers);
            #endif
            held_chars.erase(code);

            java::KeyEvent::Post(env,
                                 &receiver,
                                 java::KeyEvent::KeyCodes::KEY_RELEASED,
                                 when,
                                 modifiers,
                                 keycode,
                                 key_char,
                                 location);
        }
    }
}

bool InputOutput::is_key_held(std::int32_t code) const noexcept
{
    return std::find(std::begin(held_keys), std::end(held_keys), code) != std::end(held_keys);
}

bool InputOutput::any_key_held(std::array<std::int32_t, 4>&& keys) const noexcept
{
    return std::any_of(std::cbegin(held_keys), std::cend(held_keys), [&](std::int32_t key){
        return std::find(std::cbegin(keys), std::cend(keys), key) != std::cend(keys);
    });
}

void InputOutput::send_string(std::string string, std::int32_t keywait, std::int32_t keymodwait) const noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component receiver = control_center->reflect_canvas();
    JNIEnv* env = receiver.getEnv();

    if (!this->has_focus(&receiver))
    {
        this->gain_focus(&receiver, true);
    }

    bool isShiftDown = false;

    for (std::size_t i = 0; i < string.length(); ++i)
    {
        char c = string[i];
        char n = i == string.length() - 1 ? '\0' : string[i + 1];
        std::int32_t modifiers = this->ModifiersForChar(c);

        //Modifier Key
        if (modifiers & java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK)
        {
            if (!isShiftDown)
            {
                isShiftDown = true;

                std::int32_t code = VK_LSHIFT;
                std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::int32_t modifiers = java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
                std::int32_t keycode = GetJavaKeyCode(code);
                std::int32_t location = GetKeyLocation(code);

                java::KeyEvent::Post(env,
                                     &receiver,
                                     java::KeyEvent::KeyCodes::KEY_PRESSED,
                                     when,
                                     modifiers,
                                     keycode,
                                     static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                                     location);

                yield_thread(std::chrono::milliseconds(lround(Random::instance()->generate_random_float(1.0, 1.1) * keymodwait)));
            }
        }

        //Character Key
        std::int32_t code = static_cast<std::int32_t>(c);
        std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::int32_t keycode = CharToJavaKeyCode(code);
        //A character from text is never a control key: GetKeyLocation takes a key code, and '[' is VK_LEFT_WINDOWS's.
        std::int32_t location = java::KeyEvent::KeyCodes::KEY_LOCATION_STANDARD;

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_PRESSED,
                             when,
                             modifiers,
                             keycode,
                             static_cast<jchar>(c),
                             location);

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_TYPED,
                             when,
                             modifiers,
                             0,
                             static_cast<jchar>(c),
                             java::KeyEvent::KeyCodes::KEY_LOCATION_UNKNOWN);

        yield_thread(std::chrono::milliseconds(lround(Random::instance()->generate_random_float(1.0, 1.1) * keywait)));
        when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_RELEASED,
                             when,
                             modifiers,
                             keycode,
                             static_cast<jchar>(c),
                             location);

        yield_thread(std::chrono::milliseconds(lround(Random::instance()->generate_random_float(1.0, 1.1) * keywait)));

        //Modifier Key
        //Release Shift only when it is down: after the last character, or before one that needs no Shift.
        if (isShiftDown && (i == string.length() - 1 || !(this->ModifiersForChar(n) & java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK)))
        {
            isShiftDown = false;

            std::int32_t code = VK_LSHIFT;
            std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::int32_t modifiers = java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
            std::int32_t keycode = GetJavaKeyCode(code);
            std::int32_t location = GetKeyLocation(code);

            java::KeyEvent::Post(env,
                                 &receiver,
                                 java::KeyEvent::KeyCodes::KEY_RELEASED,
                                 when,
                                 modifiers,
                                 keycode,
                                 static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                                 location);

            yield_thread(std::chrono::milliseconds(lround(Random::instance()->generate_random_float(1.0, 1.1) * keymodwait)));
        }
    }
}

void InputOutput::send_key(char key, std::int32_t key_down_time, std::int32_t key_up_time, std::int32_t modifier_down_time, std::int32_t modifier_up_time) const noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component receiver = control_center->reflect_canvas();
    JNIEnv* env = receiver.getEnv();

    if (!this->has_focus(&receiver))
    {
        this->gain_focus(&receiver, true);
    }

    std::int32_t modifiers = this->ModifiersForChar(key);

    //Modifier Key is already pressed
    bool isShiftDown = GetActiveKeyModifiers() & java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;

    //Modifier Key
    if (!isShiftDown && (modifiers & java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK))
    {
        std::int32_t code = VK_LSHIFT;
        std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::int32_t modifiers = java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
        std::int32_t keycode = GetJavaKeyCode(code);
        std::int32_t location = GetKeyLocation(code);

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_PRESSED,
                             when,
                             modifiers,
                             keycode,
                             static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                             location);

        yield_thread(std::chrono::milliseconds(modifier_down_time));
    }

    //Character Key
    std::int32_t code = static_cast<std::int32_t>(key);
    std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::int32_t keycode = CharToJavaKeyCode(code);
    //A character is never a control key: GetKeyLocation takes a key code, and '[' is VK_LEFT_WINDOWS's.
    std::int32_t location = java::KeyEvent::KeyCodes::KEY_LOCATION_STANDARD;

    java::KeyEvent::Post(env,
                         &receiver,
                         java::KeyEvent::KeyCodes::KEY_PRESSED,
                         when,
                         modifiers,
                         keycode,
                         static_cast<jchar>(key),
                         location);

    java::KeyEvent::Post(env,
                         &receiver,
                         java::KeyEvent::KeyCodes::KEY_TYPED,
                         when,
                         modifiers,
                         0,
                         static_cast<jchar>(key),
                         java::KeyEvent::KeyCodes::KEY_LOCATION_UNKNOWN);

    yield_thread(std::chrono::milliseconds(key_down_time));
    when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    java::KeyEvent::Post(env,
                         &receiver,
                         java::KeyEvent::KeyCodes::KEY_RELEASED,
                         when,
                         modifiers,
                         keycode,
                         static_cast<jchar>(key),
                         location);

    yield_thread(std::chrono::milliseconds(key_up_time));

    //Modifier Key
    if (!isShiftDown && (modifiers & java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK))
    {
        std::int32_t code = VK_LSHIFT;
        std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::int32_t modifiers = java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
        std::int32_t keycode = GetJavaKeyCode(code);
        std::int32_t location = GetKeyLocation(code);

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_RELEASED,
                             when,
                             modifiers,
                             keycode,
                             static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                             location);

        yield_thread(std::chrono::milliseconds(modifier_up_time));
    }
}

void InputOutput::key_send(std::string string, std::vector<std::int32_t> sleeptimes) const noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component receiver = control_center->reflect_canvas();
    JNIEnv* env = receiver.getEnv();

    if (!this->has_focus(&receiver))
    {
        this->gain_focus(&receiver, true);
    }

    bool isShiftDown = false;
    std::int32_t sleepindex = 0;

    for (std::size_t i = 0; i < string.length(); ++i)
    {
        char c = string[i];
        char n = i == string.length() - 1 ? '\0' : string[i + 1];
        std::int32_t modifiers = this->ModifiersForChar(c);

        //Modifier Key
        if (modifiers & java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK)
        {
            if (!isShiftDown)
            {
                isShiftDown = true;

                std::int32_t code = VK_LSHIFT;
                std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::int32_t modifiers = java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
                std::int32_t keycode = GetJavaKeyCode(code);
                std::int32_t location = GetKeyLocation(code);

                java::KeyEvent::Post(env,
                                     &receiver,
                                     java::KeyEvent::KeyCodes::KEY_PRESSED,
                                     when,
                                     modifiers,
                                     keycode,
                                     static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                                     location);

                yield_thread(std::chrono::milliseconds(sleeptimes[sleepindex++]));
            }
        }

        //Character Key
        std::int32_t code = static_cast<std::int32_t>(c);
        std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::int32_t keycode = CharToJavaKeyCode(code);
        //A character from text is never a control key: GetKeyLocation takes a key code, and '[' is VK_LEFT_WINDOWS's.
        std::int32_t location = java::KeyEvent::KeyCodes::KEY_LOCATION_STANDARD;

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_PRESSED,
                             when,
                             modifiers,
                             keycode,
                             static_cast<jchar>(c),
                             location);

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_TYPED,
                             when,
                             modifiers,
                             0,
                             static_cast<jchar>(c),
                             java::KeyEvent::KeyCodes::KEY_LOCATION_UNKNOWN);

        yield_thread(std::chrono::milliseconds(sleeptimes[sleepindex++]));
        when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        java::KeyEvent::Post(env,
                             &receiver,
                             java::KeyEvent::KeyCodes::KEY_RELEASED,
                             when,
                             modifiers,
                             keycode,
                             static_cast<jchar>(c),
                             location);

        yield_thread(std::chrono::milliseconds(sleeptimes[sleepindex++]));

        //Modifier Key
        //Release Shift only when it is down: after the last character, or before one that needs no Shift.
        if (isShiftDown && (i == string.length() - 1 || !(this->ModifiersForChar(n) & java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK)))
        {
            isShiftDown = false;

            std::int32_t code = VK_LSHIFT;
            std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::int32_t modifiers = java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
            std::int32_t keycode = GetJavaKeyCode(code);
            std::int32_t location = GetKeyLocation(code);

            java::KeyEvent::Post(env,
                                 &receiver,
                                 java::KeyEvent::KeyCodes::KEY_RELEASED,
                                 when,
                                 modifiers,
                                 keycode,
                                 static_cast<jchar>(java::KeyEvent::KeyCodes::CHAR_UNDEFINED),
                                 location);

            yield_thread(std::chrono::milliseconds(sleeptimes[sleepindex++]));
        }
    }
}

bool InputOutput::has_focus() const noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return false;
    }

    java::Component component = control_center->reflect_canvas();
    return component.hasFocus();
}

bool InputOutput::has_focus(java::Component* component) const noexcept
{
    return component->hasFocus();
}

void InputOutput::gain_focus() const noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component component = control_center->reflect_canvas();
    this->gain_focus(&component, false);
}

//The OS's gaps inside a focus change (Simba-Reflection's gate 10, java.awt.Robot on the PC, 2026-10-23, Windows 11, JDK 25,
//20 of each): FOCUS_LOST to WINDOW_LOST_FOCUS a median 1.09 ms (0.92-2.55), WINDOW_ACTIVATED to WINDOW_GAINED_FOCUS 1.17 ms
//(0.94-1.43). Java's focus manager adds the rest of each change itself, as it does for the native peer's events.
//shortcut: the PC's medians, fixed, on every platform; measure gate 10 on a Mac or Linux before a script runs there.
static constexpr std::chrono::microseconds FOCUS_LOST_GAP{1090};
static constexpr std::chrono::microseconds ACTIVATION_GAP{1170};
static constexpr std::chrono::microseconds CLICK_PRESS_GAP{350}; //A click's press comes 0.36-0.57 ms after WINDOW_GAINED_FOCUS; the post adds about 0.1.

//Files a focus request for the canvas as Java's own does (KeyboardFocusManager.shouldNativelyFocusHeavyweight), less its
//native call. The focus manager's request on WINDOW_GAINED_FOCUS then joins it and asks the OS for nothing: on a window the
//OS has not activated, the case of every regain after a real loss, its own would ask Windows to bring the window to the
//front (SetForegroundWindow), then fail. The FOCUS_GAINED after it takes the request's cause, ACTIVATION, as a real regain's
//does; with no request the focus manager gives it UNEXPECTED (gate 10, 2026-10-23). It also makes the canvas the window's
//most recent focus owner, as Component.requestFocus does, for the focus manager to ask for it: a window that has had no
//focus owner since it opened would give the focus to its initial component, the frame, through the OS (the focus probe,
//2026-10-23). On a JDK without these (before 9), nothing is filed, and the focus manager asks the OS itself.
static void request_focus_quietly(JNIEnv* env, jobject component) noexcept
{
    jclass manager = env->FindClass("java/awt/KeyboardFocusManager");
    jclass request = manager ? env->FindClass("java/awt/KeyboardFocusManager$HeavyweightFocusRequest") : nullptr;
    jclass cause = request ? env->FindClass("java/awt/event/FocusEvent$Cause") : nullptr;
    jclass list = cause ? env->FindClass("java/util/LinkedList") : nullptr;
    jfieldID requests_id = list ? env->GetStaticFieldID(manager, "heavyweightRequests", "Ljava/util/LinkedList;") : nullptr;
    jfieldID activation_id = requests_id ? env->GetStaticFieldID(cause, "ACTIVATION", "Ljava/awt/event/FocusEvent$Cause;") : nullptr;
    jmethodID init = activation_id ? env->GetMethodID(request, "<init>", "(Ljava/awt/Component;Ljava/awt/Component;ZLjava/awt/event/FocusEvent$Cause;)V") : nullptr;
    jmethodID add = init ? env->GetMethodID(list, "add", "(Ljava/lang/Object;)Z") : nullptr;
    jmethodID most_recent = add ? env->GetStaticMethodID(manager, "setMostRecentFocusOwner", "(Ljava/awt/Component;)V") : nullptr;

    if (most_recent)
    {
        env->CallStaticVoidMethod(manager, most_recent, component);
        env->ExceptionClear();

        jobject requests = env->GetStaticObjectField(manager, requests_id);
        jobject activation = env->GetStaticObjectField(cause, activation_id);
        jobject focus_request = env->NewObject(request, init, component, component, JNI_FALSE, activation);

        //Java files and takes its requests under the list's own lock.
        if (requests && focus_request && env->MonitorEnter(requests) == JNI_OK)
        {
            env->CallBooleanMethod(requests, add, focus_request);
            env->MonitorExit(requests);
        }

        for (jobject ref : {focus_request, activation, requests})
        {
            if (ref)
            {
                env->DeleteLocalRef(ref);
            }
        }
    }

    //A class, field or method a JDK lacks leaves its error pending: clear it before the next JNI call.
    env->ExceptionClear();
    for (jclass cls : {manager, request, cause, list})
    {
        if (cls)
        {
            env->DeleteLocalRef(cls);
        }
    }
}

//A wait to the microsecond: a sleep on Windows ends on the timer's tick (15.6 ms by default), so the last stretch spins.
static void wait_until(std::chrono::steady_clock::time_point until) noexcept
{
    if (until - std::chrono::steady_clock::now() > std::chrono::milliseconds(20))
    {
        std::this_thread::sleep_until(until - std::chrono::milliseconds(20));
    }

    while (std::chrono::steady_clock::now() < until)
    {
        std::this_thread::yield();
    }
}

//Java's focus settles on the event thread, in 0.1-0.5 ms: spin the first 2 ms, as the input after it must not lag, then
//poll each 1 ms (a canvas that cannot take the focus holds no core for the 2 s).
static void wait_for_focus(java::Component* component, bool focused) noexcept
{
    auto start = std::chrono::steady_clock::now();
    while (component->hasFocus() != focused && std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
    {
        if (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2))
        {
            std::this_thread::yield();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

//The window's half of a regain, as the native peer posts it: WINDOW_ACTIVATED, then WINDOW_GAINED_FOCUS, on which Java's
//focus manager asks to focus the window's last focus owner, the canvas, through the request filed here. Returns the time
//of the first event.
std::chrono::steady_clock::time_point InputOutput::activate_window(java::Component* component) const noexcept
{
    auto start = std::chrono::steady_clock::now();
    JNIEnv* env = component->getEnv();
    java::Window window = java::SunToolkit::getContainingWindow(component);

    if (window.get())
    {
        java::WindowEvent::Post(env, &window, java::WindowEvent::WindowEventCodes::WINDOW_ACTIVATED, 0, 0);
        wait_until(std::chrono::steady_clock::now() + ACTIVATION_GAP);
        request_focus_quietly(env, component->get());
        java::WindowEvent::Post(env, &window, java::WindowEvent::WindowEventCodes::WINDOW_GAINED_FOCUS, 0, 0);
    }
    return start;
}

//The canvas's half: FOCUS_GAINED, which takes the filed request (cause ACTIVATION) and focuses the canvas.
void InputOutput::focus_canvas(java::Component* component) const noexcept
{
    java::FocusEvent::Post(component->getEnv(), component, java::FocusEvent::FocusCodes::FOCUS_GAINED, false, java::FocusEvent::Cause::ACTIVATION);
    wait_for_focus(component, true);
}

//react: a key waits a person's reaction from the regain's first event (focus_mu, focus_sigma, focus_tau). A person comes back
//to the window by a click on its title or Alt+Tab, then types. A press waits none: a click into the window is its own regain.
void InputOutput::gain_focus(java::Component* component, bool react) const noexcept
{
    auto start = this->activate_window(component);
    this->focus_canvas(component);

    if (react)
    {
        double ms = Random::instance()->generate_exgaussian(focus_mu, focus_sigma, focus_tau);
        wait_until(start + std::chrono::microseconds(std::llround(ms * 1000)));
    }
}

void InputOutput::lose_focus() const noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component component = control_center->reflect_canvas();
    this->lose_focus(&component);
}

//A loss to another application, as the native peer posts it: the canvas's FOCUS_LOST (temporary, for activation), then the
//window's WINDOW_LOST_FOCUS, after which Java's focus manager deactivates the window itself.
void InputOutput::lose_focus(java::Component* component) const noexcept
{
    JNIEnv* env = component->getEnv();
    java::Window window = java::SunToolkit::getContainingWindow(component);

    java::FocusEvent::Post(env, component, java::FocusEvent::FocusCodes::FOCUS_LOST, true, java::FocusEvent::Cause::ACTIVATION);

    if (window.get())
    {
        wait_until(std::chrono::steady_clock::now() + FOCUS_LOST_GAP);
        java::WindowEvent::Post(env, &window, java::WindowEvent::WindowEventCodes::WINDOW_LOST_FOCUS, 0, 0);
    }

    wait_for_focus(component, false);
}

void InputOutput::set_focus_reaction(double mu, double sigma, double tau) noexcept
{
    //The draw needs a finite mu, and a finite sigma and tau of 0 or more: keep the last good ones.
    if (std::isfinite(mu) && std::isfinite(sigma) && std::isfinite(tau) && sigma >= 0 && tau >= 0)
    {
        this->focus_mu = mu;
        this->focus_sigma = sigma;
        this->focus_tau = tau;
    }
}

bool InputOutput::is_keyboard_input_enabled() const noexcept
{
    return keyboard_input_enabled;
}

// No focus gain at a block (Simba-Reflection #13): real focus events pass the filter, so the game's focus follows the
// real window's, and RemoteInput regains it itself before a press that finds it without.
void InputOutput::set_keyboard_input_enabled(bool enabled) const noexcept
{
    keyboard_input_enabled = enabled;
}

bool InputOutput::is_mouse_input_enabled() const noexcept
{
    return mouse_input_enabled;
}

void InputOutput::set_mouse_input_enabled(bool enabled) const noexcept
{
    mouse_input_enabled = enabled;
}

std::int32_t InputOutput::get_keyboard_speed() const noexcept
{
    return this->keyboard_speed;
}

void InputOutput::set_keyboard_speed(std::int32_t speed) noexcept
{
    this->keyboard_speed = speed;
}

std::int32_t InputOutput::get_keyboard_repeat_delay() const noexcept
{
    return this->keyboard_repeat_delay;
}

void InputOutput::set_keyboard_repeat_delay(std::int32_t delay) noexcept
{
    this->keyboard_repeat_delay = delay;
}

void InputOutput::get_mouse_position(std::int32_t* x, std::int32_t* y) noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    if (this->x == std::numeric_limits<std::int32_t>::min() || this->y == std::numeric_limits<std::int32_t>::min())
    {
        java::Component receiver = control_center->reflect_canvas();
        receiver.getMousePosition(this->x, this->y);
        receiver.getSize(this->w, this->h);

        if (this->x == -1 || this->y == -1)
        {
            java::Component receiver = control_center->reflect_canvas();
            JNIEnv* env = receiver.getEnv();

            std::int32_t x = this->x;
            std::int32_t y = this->y;

            java::PointerInfo info = java::PointerInfo::getPointerInfo(env);
            info.getLocation(x, y);
            info.PointToScreen(env, x, y, &receiver);

            this->x = x;
            this->y = y;
        }
    }
    else if (!has_focus() && (!is_keyboard_input_enabled() || !is_mouse_input_enabled()))
    {
        java::Component receiver = control_center->reflect_canvas();
        this->handle_resize(&receiver);
    }

    *x = this->x;
    *y = this->y;
}

void InputOutput::get_real_mouse_position(std::int32_t* x, std::int32_t* y) const noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component receiver = control_center->reflect_canvas();
    JNIEnv* env = receiver.getEnv();

    java::PointerInfo info = java::PointerInfo::getPointerInfo(env);
    info.getLocation(*x, *y);
    info.PointToScreen(env, *x, *y, &receiver);
}

void InputOutput::move_mouse(std::int32_t x, std::int32_t y) noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component receiver = control_center->reflect_canvas();
    JNIEnv* env = receiver.getEnv();
    this->handle_resize(&receiver);

    std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    bool isRequestedPositionInsideComponent = !(x < 0 || y < 0 || x >= static_cast<std::int32_t>(this->w) || y >= static_cast<std::int32_t>(this->h));
    bool isMouseInsideComponent = !(this->x < 0 || this->y < 0 || this->x >= static_cast<std::int32_t>(this->w) || this->y >= static_cast<std::int32_t>(this->h));
    bool isDragging = mouse_buttons[0] || mouse_buttons[1] || mouse_buttons[2];

    //Button priority is 1 (left), 3 (right), 2 (middle)
    std::int32_t button = mouse_buttons[0] ? 1 : mouse_buttons[2] ? 3 : mouse_buttons[1] ? 2 : 0;
    std::int32_t buttonMask = (mouse_buttons[0] ? java::InputEvent::GetDownMaskForButton(1) : 0) |
                              (mouse_buttons[1] ? java::InputEvent::GetDownMaskForButton(2) : 0) |
                              (mouse_buttons[2] ? java::InputEvent::GetDownMaskForButton(3) : 0);

    //Key extended masks
    buttonMask |= GetActiveKeyModifiers();

    //A held button that stays put is no drag: the OS posts nothing.
    if (isDragging && x == this->x && y == this->y)
    {
        return;
    }

    //A move with a button held is a drag: no held button posts MOUSE_CLICKED. AppKit gives the drag and the release after it clickCount 0;
    //Windows gives the drag clickCount 0 and button 0, and the release keeps the press's count (#69).
    #if defined(_WIN32) || defined(_WIN64)
    std::int32_t drag_button = 0;
    #else
    std::int32_t drag_button = button;
    #endif
    if (isDragging)
    {
        #if !defined(_WIN32) && !defined(_WIN64)
        click_count = 0;
        #endif
        clickable_buttons = 0;
    }

    if (isRequestedPositionInsideComponent && !isMouseInsideComponent)
    {
        //MOUSE_ENTERED
        this->x = x; this->y = y;
        java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_ENTERED, when, buttonMask, x, y, 0, false, 0);
        java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_MOVED, when, buttonMask, x, y, 0, false, 0);
    }
    else if (!isRequestedPositionInsideComponent && isMouseInsideComponent)
    {
        //MOUSE_EXITED
        this->x = x; this->y = y;
        java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_EXITED, when, buttonMask, x, y, 0, false, 0);
    }
    else if (isRequestedPositionInsideComponent && isMouseInsideComponent)
    {
        //MOUSE_MOVED OR MOUSE_DRAGGED
        if (isDragging)
        {
            this->x = x; this->y = y;
            java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_DRAGGED, when, buttonMask, x, y, 0, IsPopupTrigger(java::MouseEvent::MouseEventCodes::MOUSE_DRAGGED, button, buttonMask), drag_button);
        }
        else
        {
            this->x = x; this->y = y;
            java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_MOVED, when, buttonMask, x, y, 0, false, 0);
        }
    }
    else if (!isRequestedPositionInsideComponent && !isMouseInsideComponent)
    {
        //MOUSE_DRAGGED OUTSIDE
        this->x = x; this->y = y;
        if (isDragging)
        {
            java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_DRAGGED, when, buttonMask, x, y, 0, IsPopupTrigger(java::MouseEvent::MouseEventCodes::MOUSE_DRAGGED, button, buttonMask), drag_button);
        }
    }
}

void InputOutput::hold_mouse(std::int32_t button) noexcept
{
    if (!this->is_mouse_held(button))
    {
        extern std::unique_ptr<ControlCenter> control_center;
        if (!control_center)
        {
            return;
        }

        java::Component receiver = control_center->reflect_canvas();
        JNIEnv* env = receiver.getEnv();
        this->handle_resize(&receiver);

        bool isRequestedPositionInsideComponent = !(this->x < 0 || this->y < 0 || this->x >= static_cast<std::int32_t>(this->w) || this->y >= static_cast<std::int32_t>(this->h));
        if (isRequestedPositionInsideComponent)
        {
            //A press into a canvas with no focus is a click into an inactive window: the OS activates the window, posts the
            //press, then focuses the canvas (gate 10). The press reads its time after the activation.
            bool regain = !this->has_focus(&receiver);
            if (regain)
            {
                this->activate_window(&receiver);
                wait_until(std::chrono::steady_clock::now() + CLICK_PRESS_GAP);
            }

            std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            //The press reports the pressed button; the mask carries every held one
            std::int32_t pressed = SimbaMouseButtonToJava(button);
            mouse_buttons[pressed - 1] = true;
            std::int32_t buttonMask = (mouse_buttons[0] ? java::InputEvent::GetDownMaskForButton(1) : 0) |
                                      (mouse_buttons[1] ? java::InputEvent::GetDownMaskForButton(2) : 0) |
                                      (mouse_buttons[2] ? java::InputEvent::GetDownMaskForButton(3) : 0);

            //A press of the same button within the double-click time and a few px of the last press counts up, as AWT counts.
            //shortcut: a fixed 4 px, Windows' default SM_CXDOUBLECLK; read the OS's own value if a platform needs it.
            bool isMultiClick = pressed == click_button && when - click_when <= double_click_time && std::abs(this->x - click_x) <= 4 && std::abs(this->y - click_y) <= 4;
            click_count = isMultiClick ? click_count + 1 : 1;
            click_button = pressed;
            click_when = when;
            click_x = this->x;
            click_y = this->y;
            clickable_buttons |= java::InputEvent::GetDownMaskForButton(pressed);

            //Key extended masks
            buttonMask |= GetActiveKeyModifiers();
            java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_PRESSED, when, buttonMask, this->x, this->y, click_count, IsPopupTrigger(java::MouseEvent::MouseEventCodes::MOUSE_PRESSED, pressed, buttonMask), pressed);

            if (regain)
            {
                this->focus_canvas(&receiver);
            }
        }
        else
        {
            //Lose Focus
            if (this->has_focus(&receiver))
            {
                this->lose_focus(&receiver);
            }
        }
    }
}

void InputOutput::release_mouse(std::int32_t button) noexcept
{
    if (this->is_mouse_held(button))
    {
        extern std::unique_ptr<ControlCenter> control_center;
        if (!control_center)
        {
            return;
        }

        java::Component receiver = control_center->reflect_canvas();
        JNIEnv* env = receiver.getEnv();
        this->handle_resize(&receiver);

        std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        bool isRequestedPositionInsideComponent = !(this->x < 0 || this->y < 0 || this->x >= static_cast<std::int32_t>(this->w) || this->y >= static_cast<std::int32_t>(this->h));
        bool isMouseInsideComponent = !(this->x < 0 || this->y < 0 || this->x >= static_cast<std::int32_t>(this->w) || this->y >= static_cast<std::int32_t>(this->h));

        //The release reports the released button; the mask carries the ones still held
        std::int32_t released = SimbaMouseButtonToJava(button);
        mouse_buttons[released - 1] = false;

        std::int32_t buttonMask = (mouse_buttons[0] ? java::InputEvent::GetDownMaskForButton(1) : 0) |
                                  (mouse_buttons[1] ? java::InputEvent::GetDownMaskForButton(2) : 0) |
                                  (mouse_buttons[2] ? java::InputEvent::GetDownMaskForButton(3) : 0);

        //No drag since the press of this button (move_mouse clears the bit)
        std::int32_t releasedMask = java::InputEvent::GetDownMaskForButton(released);
        bool isClick = clickable_buttons & releasedMask;
        clickable_buttons &= ~releasedMask;

        //Key extended masks
        buttonMask |= GetActiveKeyModifiers();

        //MOUSE_RELEASED
        java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_RELEASED, when, buttonMask, this->x, this->y, click_count, IsPopupTrigger(java::MouseEvent::MouseEventCodes::MOUSE_RELEASED, released, buttonMask), released);

        if (isClick && isRequestedPositionInsideComponent && isMouseInsideComponent)
        {
            //MOUSE_CLICKED
            java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_CLICKED, when, buttonMask, this->x, this->y, click_count, IsPopupTrigger(java::MouseEvent::MouseEventCodes::MOUSE_CLICKED, released, buttonMask), released);
        }
    }
}

void InputOutput::scroll_mouse(std::int32_t lines) noexcept
{
    extern std::unique_ptr<ControlCenter> control_center;
    if (!control_center)
    {
        return;
    }

    java::Component receiver = control_center->reflect_canvas();
    JNIEnv* env = receiver.getEnv();
    this->handle_resize(&receiver);

    bool isRequestedPositionInsideComponent = !(this->x < 0 || this->y < 0 || this->x >= static_cast<std::int32_t>(this->w) || this->y >= static_cast<std::int32_t>(this->h));
    bool isMouseInsideComponent = !(this->x < 0 || this->y < 0 || this->x >= static_cast<std::int32_t>(this->w) || this->y >= static_cast<std::int32_t>(this->h));

    if (isRequestedPositionInsideComponent && isMouseInsideComponent)
    {
        //Relative Mouse Position
        std::int32_t cx = 0;
        std::int32_t cy = 0;
        receiver.getLocationOnScreen(cx, cy);

        if (cx == -1 || cy == -1)
        {
            cx = 0;
            cy = 0;
        }

        std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::int32_t modifiers = GetActiveKeyModifiers();

        std::int32_t scroll_type = java::MouseWheelEvent::MouseWheelEventCodes::WHEEL_UNIT_SCROLL;
        std::int32_t scroll_amount = 1;
        #if defined(_WIN32) || defined(_WIN64)
        //Windows AWT's peer (WmMouseWheel) gives a notch the whole rotation, and scrollAmount the wheel-lines setting, 3 when
        //the call fails; the page setting is a block scroll of 1 (Simba-Reflection #75).
        double precise_rotation = lines;
        UINT wheel_lines = 0;
        if (!SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &wheel_lines, 0))
        {
            wheel_lines = 3;
        }

        if (wheel_lines == WHEEL_PAGESCROLL)
        {
            scroll_type = java::MouseWheelEvent::MouseWheelEventCodes::WHEEL_BLOCK_SCROLL;
        }
        else
        {
            scroll_amount = static_cast<std::int32_t>(wheel_lines);
        }
        #else
        //Technically, this should be gausian curve or bell-curve or parabola as the wheel speeds up and slows down..
        double precision = lines > 0 ? Random::instance()->generate_random_int(1, 9) : lines < 0 ? Random::instance()->generate_random_int(-9, -1) : Random::instance()->generate_random_double(-1, 1);
        double precise_rotation = lines + precision / 10.0;
        #endif

        java::MouseWheelEvent::Post(env,
                                    &receiver,
                                    java::MouseEvent::MouseEventCodes::MOUSE_WHEEL,
                                    when,
                                    modifiers,
                                    this->x,
                                    this->y,
                                    cx + this->x,
                                    cy + this->y,
                                    0,
                                    false,
                                    scroll_type,
                                    scroll_amount,
                                    lines,
                                    precise_rotation);
    }
    else if (isRequestedPositionInsideComponent && !isMouseInsideComponent)
    {
        std::int64_t when = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        bool isDragging = mouse_buttons[0] || mouse_buttons[1] || mouse_buttons[2];
        if (isDragging)
        {
            return; //Developer is an idiot. We shouldn't even have to be here in the first place..
        }

        //Button priority is 1 (left), 3 (right), 2 (middle)
        std::int32_t buttonMask = (mouse_buttons[0] ? java::InputEvent::GetDownMaskForButton(1) : 0) |
                                  (mouse_buttons[1] ? java::InputEvent::GetDownMaskForButton(2) : 0) |
                                  (mouse_buttons[2] ? java::InputEvent::GetDownMaskForButton(3) : 0);

        //Key extended masks
        buttonMask |= GetActiveKeyModifiers();


        //MOUSE_ENTERED
        java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_ENTERED, when, buttonMask, this->x, this->y, 0, false, 0);
        java::MouseEvent::Post(env, &receiver, java::MouseEvent::MouseEventCodes::MOUSE_MOVED, when, buttonMask, this->x, this->y, 0, false, 0);

        // Recursive call
        scroll_mouse(lines);
    }
}

bool InputOutput::is_mouse_held(std::int32_t button) const noexcept
{
    switch (button)
    {
        case 1: return mouse_buttons[0];
        case 2: return mouse_buttons[1];
        case 3: return mouse_buttons[2];
        default: return false;
    }
}

void InputOutput::stop_all_processing() noexcept
{
    std::int32_t x = 0;
    std::int32_t y = 0;

    this->get_mouse_position(&x, &y);
    this->release_mouse(0);
    this->release_mouse(1);
    this->release_mouse(2);

    for (std::int32_t key : std::vector<std::int32_t>(held_keys.begin(), held_keys.end()))
    {
        this->release_key(key);
    }

    this->set_keyboard_input_enabled(true);
    this->set_mouse_input_enabled(true);
}

jchar InputOutput::NativeKeyCodeToChar(std::int32_t keycode, std::int32_t modifiers) const noexcept
{
    static const std::int32_t mapping[256] = { //char16_t
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0008, 0x0009, 0x0000, 0x0000, 0x0000, 0x000A, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x001B, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0020, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x007F, 0x0000,
        0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
        0x0038, 0x0039, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
        0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
        0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
        0x0078, 0x0079, 0x007A, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
        0x0038, 0x0039, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x003B, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
        0x0060, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x005B, 0x005C, 0x005D, 0x0027, 0x0000,
        0x0000, 0x0000, 0x005C, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    };

    if (modifiers != 0)
    {
        static const std::string normal_chars = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./";
        static const std::string shift_chars = "~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
        if (auto index = normal_chars.find(static_cast<char>(mapping[keycode])); index != std::string::npos)
        {
            return static_cast<jchar>(shift_chars[index]);
        }
    }
    //0 is no char: real AWT gives such a key's press and release CHAR_UNDEFINED (#82). Escape is 27, and Delete 127, as
    //the JDK's Windows peer gives them.
    return static_cast<jchar>(mapping[keycode] ? mapping[keycode] : java::KeyEvent::KeyCodes::CHAR_UNDEFINED);
}

std::int32_t InputOutput::CharToJavaKeyCode(char c) const noexcept
{
    //Convert char to Java VK-Codes
    //0x00 and 0xFF are not type-able characters..
    static const std::uint32_t mapping[256] = {
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0008, 0x0009, 0x000A, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x001B, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0020, 0x0205, 0x0098, 0x0208, 0x0203, 0x0000, 0x0096, 0x00DE,
        0x0207, 0x020A, 0x0097, 0x0209, 0x002C, 0x002D, 0x002E, 0x002F,
        0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
        0x0038, 0x0039, 0x0201, 0x003B, 0x0099, 0x003D, 0x00A0, 0x0000,
        0x0200, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
        0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
        0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
        0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x0202, 0x020B,
        0x00C0, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
        0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
        0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
        0x0058, 0x0059, 0x005A, 0x00A1, 0xFFFF, 0x00A2, 0xFFFF, 0x007F,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0206, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0x0000, 0xFFFF,
        0xFFFF, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000
    };
    return mapping[static_cast<std::int32_t>(c)];
}

std::int32_t InputOutput::GetJavaKeyCode(std::int32_t native_key_code) const noexcept
{
    //Convert WinAPI VK-Codes to Java VK-Codes
    //0x00 and 0xFF are not type-able characters..
    static const std::uint32_t windows_mapping[256] = {
        0x0000, 0x0000, 0x0000, 0x0003, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0008, 0x0009, 0x0000, 0x0000, 0x000C, 0x000A, 0x0000, 0x0000,
        0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0000, 0x0000,
        0x0018, 0x0019, 0x0000, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
        0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
        0x0028, 0x0000, 0x0000, 0x0000, 0x009A, 0x009B, 0x007F, 0x009C,
        0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
        0x0038, 0x0039, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
        0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
        0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
        0x0058, 0x0059, 0x005A, 0x020C, 0x020C, 0x020D, 0x0000, 0x0000,
        0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
        0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
        0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
        0x0078, 0x0079, 0x007A, 0x007B, 0xF000, 0xF001, 0xF002, 0xF003,
        0xF004, 0xF005, 0xF006, 0xF007, 0xF008, 0xF009, 0xF00A, 0xF00B,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0090, 0x0091, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0010, 0x0010, 0x0011, 0x0011, 0x0012, 0x0012, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x003B, 0x0209, 0x002C, 0x002D, 0x002E, 0x002F,
        0x0083, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x005B, 0x005C, 0x005D, 0x00DE, 0x0205,
        0x0000, 0x0000, 0x0081, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x000C, 0x0000
    };
    return windows_mapping[native_key_code];
}

std::int32_t InputOutput::GetKeyLocation(std::int32_t keycode) const noexcept
{
    if (auto it = std::find(std::begin(control_keys), std::end(control_keys), keycode); it != std::end(control_keys))
    {
        return control_keys_locations[std::distance(std::begin(control_keys), it)];
    }
    return java::KeyEvent::KeyCodes::KEY_LOCATION_STANDARD;
}

std::int32_t InputOutput::GetActiveKeyModifiers() const noexcept
{
    std::int32_t modifiers = 0;

    //Key extended masks
    if (this->any_key_held({VK_SHIFT, VK_LSHIFT, VK_RSHIFT}))
    {
        modifiers |= java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
    }

    if (this->any_key_held({VK_CONTROL, VK_LEFT_CONTROL, VK_RIGHT_CONTROL}))
    {
        modifiers |= java::InputEvent::InputEventMasks::CTRL_DOWN_MASK;
    }

    if (this->is_key_held(VK_ALT) || this->is_key_held(VK_LEFT_ALT))
    {
        modifiers |= java::InputEvent::InputEventMasks::ALT_DOWN_MASK;
    }

    if (this->is_key_held(VK_RIGHT_ALT))
    {
        modifiers |= java::InputEvent::InputEventMasks::ALT_GRAPH_DOWN_MASK;
    }

    if (this->any_key_held({VK_LEFT_WINDOWS, VK_RIGHT_WINDOWS, VK_COMMAND}))
    {
        modifiers |= java::InputEvent::InputEventMasks::META_DOWN_MASK;
    }
    return modifiers;
}

std::int32_t InputOutput::ModifiersForChar(char c) const noexcept
{
    std::int32_t modifiers = 0;
    static const std::string shift_chars = "~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
    if (shift_chars.find(c) != std::string::npos)
    {
        modifiers |= java::InputEvent::InputEventMasks::SHIFT_DOWN_MASK;
    }
    return modifiers;
}

std::int32_t InputOutput::SimbaMouseButtonToJava(std::int32_t button) const noexcept
{
    //Java Button priority is 1 (left), 3 (right), 2 (middle)
    //Simba Button priority is 1 (left), 0 (right), 2 (middle)
    return button == 1 ? 1 : button == 0 ? 3 : button == 2 ? 2 : button;
}

void InputOutput::get_applet_dimensions(std::int32_t &x, std::int32_t &y, std::size_t &width, std::size_t &height) const noexcept
{
    JNIEnv* env = nullptr;
    if (this->vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr) == JNI_OK)
    {
        java::Applet receiver{env, this->applet, false};
        //Component receiver = applet.getComponent(0);

        receiver.getLocation(x, y);
        receiver.getSize(width, height);
    }
    else
    {
        x = -1;
        y = -1;
        width = static_cast<std::int32_t>(this->w);
        height = static_cast<std::int32_t>(this->h);
    }
}

void InputOutput::get_canvas_dimensions(std::int32_t &x, std::int32_t &y, std::size_t &width, std::size_t &height) const noexcept
{
    JNIEnv* env = nullptr;
    if (this->vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr) == JNI_OK)
    {
        java::Applet receiver{env, this->applet, false};
        java::Component canvas = receiver.getComponent(0);

        canvas.getLocation(x, y);
        canvas.getSize(width, height);
    }
    else
    {
        x = -1;
        y = -1;
        width = static_cast<std::int32_t>(this->w);
        height = static_cast<std::int32_t>(this->h);
    }
}

void InputOutput::get_applet_mouse_position(std::int32_t &x, std::int32_t &y) const noexcept
{
    x = this->x;
    y = this->y;
}
