//
//  Plugin.cxx
//  RemoteInput
//
//  Created by Brandon on 2019-12-21.
//  Copyright © 2019 XIO. All rights reserved.
//

#include "Plugin.hxx"
#include <memory>

#include "Atomics.hxx"
#include "Platform.hxx"
#include "NativeHooks.hxx"
#include "ControlCenter.hxx"
#include "EIOS.hxx"
#include "DebugConsole.hxx"
#include "Thirdparty/Hook.hxx"
#include "Thirdparty/Injector.hxx"

#if defined(_WIN32) || defined(_WIN64)
HMODULE module = nullptr;
#elif defined(__APPLE__)
#include <algorithm>
#include <climits>
#include <cstring>
#include <string>
#include <crt_externs.h>
#include <dlfcn.h>
#endif

std::vector<std::unique_ptr<Injector>> injectors;
std::unique_ptr<ControlCenter> control_center;
std::unique_ptr<DebugConsole> console;

#if defined(_WIN32) || defined(_WIN64)
std::unique_ptr<Hook> exit_process;
#endif

// MARK: - MAIN

#if defined(_WIN32) || defined(_WIN64)
[[gnu::stdcall]] void __exit_process(unsigned int exit_code)
{
    #ifdef DEBUG
    console.reset();
    #endif
    control_center.reset();
    exit_process->call<void, decltype(ExitProcess)>(exit_code);
}

[[gnu::stdcall]] void __load()
{
    #if defined(DEBUG)
    printf("ATTACHED TO: %d\n", getpid());
    #endif

    //Increase our reference count by 1..
    //So that if someone calls `FreeLibrary` before the thread exists, we won't get a crash.
    //Later on we will call `FreeLibraryAndExitThread`
    HMODULE this_module = nullptr;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCTSTR>(module), &this_module);

    std::thread([](HMODULE module){
        auto main_reflector = GetNativeReflector();
        if (main_reflector)
        {
            #if defined(DEBUG)
            console = std::make_unique<DebugConsole>();
            #endif

            control_center = std::make_unique<ControlCenter>(getpid(), false, std::move(main_reflector));
            StartHook();

            //GetNativeReflector attached this thread. JNI requires a detach before the thread exits.
            JVM().DetachCurrentThread();
        }

        //exit_process = std::make_unique<Hook>((void *) GetProcAddress(GetModuleHandleA("kernel32.dll"), "ExitProcess"), (void *) __exit_process);
        //exit_process->apply();

        //Decrease our reference count by 1..
        //So if `FreeLibrary` was called previous, our count reaches 0 and we'll be freed.
        FreeLibraryAndExitThread(module, 0);
    }, this_module).detach();
}

[[gnu::stdcall]] void __unload()
{
    #if defined(DEBUG)
    printf("DETACHED FROM: %d\n", getpid());
    #endif
}
#elif defined(__APPLE__)
void __exit_process(int exit_code)
{
    #ifdef DEBUG
    console.reset();
    #endif
    control_center.reset();
}

//RuneLite blocks task_for_pid but allows DYLD_INSERT_LIBRARIES. Started that way, RuneLite's launcher and its
//client (the "-c" process) both load this library. Only the client runs the loader, and it removes the
//variable before Java can read it. The launcher keeps it, so the client inherits it.
static bool loaded_into_runelite_launcher() noexcept
{
    const char* inserted = getenv("DYLD_INSERT_LIBRARIES");
    Dl_info info = {0};
    char self[PATH_MAX] = {0};
    if (!inserted || !dladdr(reinterpret_cast<void*>(&loaded_into_runelite_launcher), &info) || !realpath(info.dli_fname, self))
    {
        return false;
    }

    bool listed = false;
    std::string paths = inserted;
    for (std::size_t start = 0, end = 0; start <= paths.size(); start = end + 1)
    {
        end = std::min(paths.find(':', start), paths.size());
        char path[PATH_MAX] = {0};
        listed = listed || (realpath(paths.substr(start, end - start).c_str(), path) && std::strcmp(path, self) == 0);
    }

    if (!listed)
    {
        return false;
    }

    char** argv = *_NSGetArgv();
    int argc = *_NSGetArgc();
    if (std::none_of(argv, argv + argc, [](const char* arg) { return std::strcmp(arg, "-c") == 0; }))
    {
        return true;
    }

    unsetenv("DYLD_INSERT_LIBRARIES");
    return false;
}

[[gnu::constructor]] void __load()
{
    #if defined(DEBUG)
    printf("ATTACHED TO: %d\n", getpid());
    #endif

    if (loaded_into_runelite_launcher())
    {
        return;
    }

    extern void disable_app_nap();

    std::thread([&] {
        #if defined(DEBUG)
        console = std::make_unique<DebugConsole>();
        #endif

        disable_app_nap();

        auto reflector = GetNativeReflector();
        if (reflector)
        {
            control_center = std::make_unique<ControlCenter>(getpid(), false, std::move(reflector));
            StartHook();

            //GetNativeReflector attached this thread. JNI requires a detach before the thread exits.
            JVM().DetachCurrentThread();
        }

        std::atexit([]{ __exit_process(0); });
    }).detach();
}

[[gnu::destructor]] void __unload()
{
    #if defined(DEBUG)
    printf("DETACHED FROM: %d\n", getpid());
    #endif
}
#else
void __exit_process(int exit_code)
{
    #ifdef DEBUG
    console.reset();
    #endif
    control_center.reset();
}

[[gnu::constructor]] void __load()
{
    #if defined(DEBUG)
    printf("ATTACHED TO: %d\n", getpid());
    #endif

    /*Dl_info this_info = {0};
    dladdr(reinterpret_cast<void*>(&__load), &this_info);
    void* this_module = dlopen(this_info.dli_fname, RTLD_LAZY);*/

    std::thread([&] {
        auto main_reflector = GetNativeReflector();
        if (main_reflector)
        {
            control_center = std::make_unique<ControlCenter>(getpid(), false, std::move(main_reflector));
            StartHook();

            //GetNativeReflector attached this thread. JNI requires a detach before the thread exits.
            JVM().DetachCurrentThread();
        }

        std::atexit([]{ __exit_process(0); });
    }).detach();
}

[[gnu::destructor]] void __unload()
{
    #if defined(DEBUG)
    printf("DETACHED FROM: %d\n", getpid());
    #endif
}
#endif // defined


#if defined(_WIN32) || defined(_WIN64)
extern "C" EXPORT BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            module = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);

            __load();
        }
            break;

        case DLL_PROCESS_DETACH:
        {
            __unload();
        }
            break;

        case DLL_THREAD_ATTACH:
            //printf("ATTACHED\n");
            break;

        case DLL_THREAD_DETACH:
            //printf("DETACHED\n");
            break;
    }
    return TRUE; // succesful
}
#endif // defined
