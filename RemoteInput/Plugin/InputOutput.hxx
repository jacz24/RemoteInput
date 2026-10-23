//
//  InputOutput.hxx
//  RemoteInput
//
//  Created by Brandon on 2019-12-28.
//  Copyright © 2019 XIO. All rights reserved.
//

#ifndef INPUTOUTPUT_HXX_INCLUDED
#define INPUTOUTPUT_HXX_INCLUDED

#include <cstdint>
#include <list>
#include <atomic>
#include <mutex>
#include <array>
#include <chrono>
#include "DetachedThreadPool.hxx"
#include "ThreadPool.hxx"
#include "Reflection.hxx"
#include "Component.hxx"
#include "JVM.hxx"

class InputOutput final
{
private:
    JavaVM* vm;
    jobject applet;
    std::mutex mutex;
    std::unique_ptr<ThreadPool> input_thread;

    // Only flags: RemoteInput pushes no event queue, so it does not block real input.
    mutable bool keyboard_input_enabled;
    mutable bool mouse_input_enabled;

    // MARK: - Input Variables
    std::atomic<std::int32_t> currently_held_key;
    std::atomic<std::uint32_t> repeat_task; // The newest key-repeat task. An older task stops when it sees a newer one.
    std::list<std::int32_t> held_keys;
    std::int32_t x;
    std::int32_t y;
    std::size_t w;
    std::size_t h;
    std::int32_t click_count;
    std::int32_t click_button; // The button, time and place of the last press: the next press counts up from them.
    std::int64_t click_when;
    std::int32_t click_x;
    std::int32_t click_y;
    std::int64_t double_click_time; // In ms, as the OS counts clicks. 500 is X's default; the constructor reads Windows' and the Mac's.
    std::int32_t clickable_buttons; // Down masks of the held buttons with no drag since their press: their release posts MOUSE_CLICKED (AWT's mouseClickButtons).
    std::size_t keyboard_speed;
    std::size_t keyboard_repeat_delay;
    bool mouse_buttons[3];
    double focus_mu; // ms: the ex-Gaussian from a regain's first event to the first key, a person's simple reaction (set_focus_reaction); by default R1-R2's centre in Simba-Reflection's spec (docs/research/human-input-model.md), 206.9, 11 and 27.4.
    double focus_sigma;
    double focus_tau;

    // MARK: - Functions
    std::int32_t CharToJavaKeyCode(char c) const noexcept;
    jchar NativeKeyCodeToChar(std::int32_t keycode, std::int32_t modifiers) const noexcept;
    std::int32_t GetJavaKeyCode(std::int32_t native_key_code) const noexcept;
    std::int32_t GetKeyLocation(std::int32_t keycode) const noexcept;
    std::int32_t GetActiveKeyModifiers() const noexcept;
    std::int32_t ModifiersForChar(char c) const noexcept;
    std::int32_t SimbaMouseButtonToJava(std::int32_t button) const noexcept;

    bool has_focus(java::Component* component) const noexcept;
    //A regain as the OS posts one: the window's events, then the canvas's. A press goes between the two halves.
    std::chrono::steady_clock::time_point activate_window(java::Component* component) const noexcept;
    void focus_canvas(java::Component* component) const noexcept;
    void gain_focus(java::Component* component, bool react) const noexcept;
    void lose_focus(java::Component* component) const noexcept;

    bool any_key_held(std::array<std::int32_t, 4>&& keys) const noexcept;
    void handle_resize(java::Component* component) noexcept;

public:
    explicit InputOutput(Reflection* reflection) noexcept;
    ~InputOutput() noexcept;

    bool has_focus() const noexcept;
    void gain_focus() const noexcept;
    void lose_focus() const noexcept;
    bool is_keyboard_input_enabled() const noexcept;
    void set_keyboard_input_enabled(bool enabled) const noexcept;
    bool is_mouse_input_enabled() const noexcept;
    void set_mouse_input_enabled(bool enabled) const noexcept;

    std::int32_t get_keyboard_speed() const noexcept;
    void set_keyboard_speed(std::int32_t speed) noexcept;
    std::int32_t get_keyboard_repeat_delay() const noexcept;
    void set_keyboard_repeat_delay(std::int32_t delay) noexcept;
    void set_focus_reaction(double mu, double sigma, double tau) noexcept;

    void hold_key(std::int32_t code) noexcept;
    void release_key(std::int32_t code) noexcept;
    bool is_key_held(std::int32_t code) const noexcept;
    void send_string(std::string string, std::int32_t keywait, std::int32_t keymodwait) const noexcept;
    void send_key(char key, std::int32_t key_down_time, std::int32_t key_up_time, std::int32_t modifier_down_time, std::int32_t modifier_up_time) const noexcept;
    void key_send(std::string string, std::vector<std::int32_t> sleeptimes) const noexcept;
    
    void get_mouse_position(std::int32_t* x, std::int32_t* y) noexcept;
    void get_real_mouse_position(std::int32_t* x, std::int32_t* y) const noexcept;
    void move_mouse(std::int32_t x, std::int32_t y) noexcept;
    void hold_mouse(std::int32_t button) noexcept;
    void release_mouse(std::int32_t button) noexcept;
    void scroll_mouse(std::int32_t lines) noexcept;
    bool is_mouse_held(std::int32_t button) const noexcept;
    void stop_all_processing() noexcept;

private:
    friend class ControlCenter;
    void get_applet_dimensions(std::int32_t &x, std::int32_t &y, std::size_t &width, std::size_t &height) const noexcept;
    void get_canvas_dimensions(std::int32_t &x, std::int32_t &y, std::size_t &width, std::size_t &height) const noexcept;
    void get_applet_mouse_position(std::int32_t &x, std::int32_t &y) const noexcept;
};

#endif /* INPUTOUTPUT_HXX_INCLUDED */
