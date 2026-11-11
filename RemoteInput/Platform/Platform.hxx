#ifndef PLATFORM_HXX_INCLUDED
#define PLATFORM_HXX_INCLUDED

#include <chrono>
#include <ratio>
#include <thread>
#include <cstdint>
#include <vector>
#include "Reflection.hxx"

void GetDesktopResolution(int &width, int &height) noexcept;
std::unique_ptr<Reflection> GetNativeReflector() noexcept;

std::int32_t GetCurrentThreadID() noexcept;

bool IsProcessAlive(std::int32_t pid) noexcept;
bool IsThreadAlive(std::int32_t tid) noexcept;
std::vector<std::int32_t> get_pids() noexcept;
std::vector<std::int32_t> get_pids(const char* process_name) noexcept;
std::int32_t InjectProcess(std::int32_t) noexcept;
std::vector<std::int32_t> InjectProcesses(const char* process_name) noexcept;
std::int32_t PIDFromWindow(void* window) noexcept;

std::vector<std::string> GetLoadedModuleNames(const char* partial_module_name) noexcept;

#if !defined(_WIN32) && !defined(_WIN64)
void* GetModuleHandle(const char* module_name) noexcept;
#endif

#if defined(_WIN32) || defined(_WIN64)
//A key's entry in the table the JDK's Windows peer keeps of the client's layout (awt_Component.cpp's dynPrimaryKeymap,
//BuildPrimaryDynamicTable; the same in JDK 11 and 25): the private fields of each key event it posts (Simba-Reflection #206).
struct NativeKey
{
    std::int32_t raw_code;      //the WM_KEYDOWN's key: VK_SHIFT, VK_CONTROL or VK_MENU for either side
    std::int32_t scancode;      //MapVirtualKey's: the left key's for either side, with no extended prefix
    std::int32_t unicode;       //the key's char with no modifier held, or 0
    std::int32_t extended_char; //the char whose extended key code the table holds, or 0: the table holds the key code
};
NativeKey GetNativeKey(std::int32_t virtual_key) noexcept;
#endif

#if defined(__APPLE__)
void GetInputTimings(std::int32_t &key_repeat_delay, std::int32_t &key_repeat_interval, std::int32_t &double_click_time) noexcept;
#endif

template<typename T>
void yield_thread(std::chrono::duration<std::int64_t, T> time) noexcept
{
    /*auto start = std::chrono::high_resolution_clock::now();
    auto end = start + time;
    do {
        //std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    } while (std::chrono::high_resolution_clock::now() < end);*/
    std::this_thread::sleep_for(time);
}

template<typename T>
std::int64_t elapsed_time(std::chrono::high_resolution_clock::time_point start) noexcept
{
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<T>(end - start).count();
}

#endif // PLATFORM_HXX_INCLUDED
