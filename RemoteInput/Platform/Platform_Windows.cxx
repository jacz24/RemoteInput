#include "Platform.hxx"
#include "Plugin/ControlCenter.hxx"

#if defined(_WIN32) || defined(_WIN64)
#include <string>
#include <chrono>
#include <cerrno>
#include <algorithm>
#include <vector>
#include "Random.hxx"
#include "Thirdparty/Hook.hxx"
#if defined(CUSTOM_INJECTOR)
#include "Injection/Injector.hxx"
#else
#include "Thirdparty/Injector.hxx"
#endif

#include <windows.h>
#include <tlhelp32.h>
#include <shellscalingapi.h>
#endif // defined

#if defined(_WIN32) || defined(_WIN64)
int GetDpi(HWND hwnd)
{
    HDC hDC = GetDC(hwnd);
    int ydpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ReleaseDC(hwnd, hDC);
    return ydpi;
}

void ReverseScaleDPI(HWND hwnd, int &width, int &height)
{
    HDC hDC = GetDC(hwnd);
    int xdpi = GetDeviceCaps(hDC, LOGPIXELSX);
    int ydpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ReleaseDC(hwnd, hDC);

    // Reverse DPI Scaling
    width = static_cast<int>(96.0 * width / xdpi);
    height = static_cast<int>(96.0 * height / ydpi);
}

void GetDesktopResolution(int &width, int &height) noexcept
{
    const HWND hDesktop = GetDesktopWindow();
    RECT desktop = {0};
    GetWindowRect(hDesktop, &desktop);
    width = desktop.right;
    height = desktop.bottom;

    /*
    // DPI Scaling
    HMODULE user32 = GetModuleHandleA("User32.dll");
    HMODULE shcore = GetModuleHandleA("Shcore.dll");

    DPI_AWARENESS_CONTEXT (*GetWindowDpiAwarenessContext)(HWND) = reinterpret_cast<decltype(GetWindowDpiAwarenessContext)>(GetProcAddress(user32, "GetWindowDpiAwarenessContext"));
    DPI_AWARENESS (*GetAwarenessFromDpiAwarenessContext)(DPI_AWARENESS_CONTEXT) = reinterpret_cast<decltype(GetAwarenessFromDpiAwarenessContext)>(GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext"));
    HRESULT (*GetDpiForMonitor)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*) = reinterpret_cast<decltype(GetDpiForMonitor)>(GetProcAddress(shcore, "GetDpiForMonitor"));

    if (GetWindowDpiAwarenessContext && GetAwarenessFromDpiAwarenessContext && GetDpiForMonitor)
    {
        DPI_AWARENESS_CONTEXT context = GetWindowDpiAwarenessContext(hDesktop);
        if (context)
        {
            DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(context);
            if (awareness != DPI_AWARENESS_INVALID && awareness != DPI_AWARENESS_UNAWARE)
            {
                UINT xdpi = 0;
                UINT ydpi = 0;
                HMONITOR hMonitor = MonitorFromWindow(hDesktop, MONITOR_DEFAULTTONEAREST);
                if (GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &xdpi, &ydpi) == S_OK)
                {
                    width = static_cast<int>(width / (xdpi / 96.0));
                    height = static_cast<int>(height / (ydpi / 96.0));
                }
            }
        }
    }
     */
}

std::int32_t GetCurrentThreadID() noexcept
{
    return GetCurrentThreadId();
}

bool IsProcessAlive(std::int32_t pid) noexcept
{
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process)
    {
        DWORD ret = WaitForSingleObject(process, 0);
        CloseHandle(process);
        return ret == WAIT_TIMEOUT;
    }
    return false;
}

bool IsThreadAlive(std::int32_t tid) noexcept
{
    HANDLE thread = OpenThread(SYNCHRONIZE, FALSE, tid);
    if (thread)
    {
        DWORD ret = WaitForSingleObject(thread, 0);
        CloseHandle(thread);
        return ret == WAIT_TIMEOUT;
    }
    return false;
}

std::vector<std::int32_t> get_pids() noexcept
{
    std::vector<std::int32_t> result;
    PROCESSENTRY32 processInfo = {0};
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processesSnapshot != INVALID_HANDLE_VALUE)
    {
        if (Process32First(processesSnapshot, &processInfo))
        {
            result.push_back(processInfo.th32ProcessID);

            while (Process32Next(processesSnapshot, &processInfo))
            {
                result.push_back(processInfo.th32ProcessID);
            }
        }
        CloseHandle(processesSnapshot);
    }
    return result;
}

std::vector<std::int32_t> get_pids(const char* process_name) noexcept
{
    std::vector<std::int32_t> result;
    PROCESSENTRY32 processInfo = {0};
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processesSnapshot != INVALID_HANDLE_VALUE)
    {
        if (Process32First(processesSnapshot, &processInfo))
        {
            if (!_stricmp(process_name, processInfo.szExeFile))
            {
                result.push_back(processInfo.th32ProcessID);
            }

            while (Process32Next(processesSnapshot, &processInfo))
            {
                if (!_stricmp(process_name, processInfo.szExeFile))
                {
                    result.push_back(processInfo.th32ProcessID);
                }
            }
        }

        CloseHandle(processesSnapshot);
    }
    return result;
}

PROCESSENTRY32 GetProcessInfo(std::int32_t pid) noexcept
{
    HANDLE processesSnapshot = nullptr;
    if((processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) == INVALID_HANDLE_VALUE)
    {
        return {0};
    }

    PROCESSENTRY32 processInfo = {0};
    processInfo.dwSize = sizeof(PROCESSENTRY32);
    while(Process32Next(processesSnapshot, &processInfo))
    {
        if(processInfo.th32ProcessID == static_cast<DWORD>(pid))
        {
            CloseHandle(processesSnapshot);
            return processInfo;
        }
    }
    CloseHandle(processesSnapshot);
    return {0};
}

std::vector<std::string> GetLoadedModuleNames(const char* partial_module_name) noexcept
{
    auto find_substring = [](std::string_view haystack, std::string_view needle) -> bool {
        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
            return std::toupper(a) == std::toupper(b);
        });
        return it != haystack.end();
    };

    HANDLE modulesSnapshot = nullptr;
    if ((modulesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId())) == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    std::vector<std::string> result;
    MODULEENTRY32 moduleInfo = {0};
    moduleInfo.dwSize = sizeof(MODULEENTRY32);
    while (Module32Next(modulesSnapshot, &moduleInfo))
    {
        if (find_substring(moduleInfo.szModule, partial_module_name))
        {
            result.push_back(moduleInfo.szModule);
        }
    }

    CloseHandle(modulesSnapshot);
    return result;
}

MODULEENTRY32 GetModuleInfo(std::int32_t pid, const char* module_name) noexcept
{
    HANDLE modulesSnapshot = nullptr;
    if ((modulesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)) == INVALID_HANDLE_VALUE)
    {
        return {0};
    }

    MODULEENTRY32 moduleInfo = {0};
    moduleInfo.dwSize = sizeof(MODULEENTRY32);
    while (Module32Next(modulesSnapshot, &moduleInfo))
    {
        if (!_stricmp(module_name, moduleInfo.szModule))
        {
            CloseHandle(modulesSnapshot);
            return moduleInfo;
        }
    }

    CloseHandle(modulesSnapshot);
    return {0};
}

MODULEENTRY32 GetModuleInfo(std::int32_t pid, HMODULE module_handle) noexcept
{
    HANDLE modulesSnapshot = nullptr;
    if ((modulesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)) == INVALID_HANDLE_VALUE)
    {
        return {0};
    }

    MODULEENTRY32 moduleInfo = {0};
    moduleInfo.dwSize = sizeof(MODULEENTRY32);
    while (Module32Next(modulesSnapshot, &moduleInfo))
    {
        if (module_handle == moduleInfo.hModule)
        {
            CloseHandle(modulesSnapshot);
            return moduleInfo;
        }
    }

    CloseHandle(modulesSnapshot);
    return {0};
}

void PrintProcessInfo(std::int32_t pid) noexcept
{
    PROCESSENTRY32 Proc32 = GetProcessInfo(pid);
    if (Proc32.th32ProcessID != 0)
    {
        printf("  =======================================================\n");
        printf("  Process Name:            %s\n", Proc32.szExeFile);
        printf("  =======================================================\n\n");
        printf("  Process ID:              %lu\n", Proc32.th32ProcessID);
        printf("  Thread Count:            %lu\n", Proc32.cntThreads);
        printf("  Priority Base:           %lu\n", Proc32.pcPriClassBase);
        printf("  Parent Process ID:       %lu\n\n", Proc32.th32ParentProcessID);
        printf("  =======================================================\n");
    }
}

char* realpath(const char* path, char* resolved_path)
{
    auto MapWindowsErrorToErrno = [](DWORD error) -> std::int32_t {
        switch (error)
        {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return ENOENT;
            case ERROR_ACCESS_DENIED:
                return EACCES;
            case ERROR_INVALID_PARAMETER:
                return EINVAL;
            case ERROR_ALREADY_EXISTS:
                return EEXIST;
            default:
                return EIO;
        }
    };

    if (!path || !resolved_path)
    {
        errno = EINVAL;
        return nullptr;
    }

    HANDLE hFile = CreateFile(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        errno = MapWindowsErrorToErrno(GetLastError());
        return nullptr;
    }

    DWORD result = GetFinalPathNameByHandle(hFile, resolved_path, MAX_PATH, FILE_NAME_NORMALIZED);
    CloseHandle(hFile);

    if (result == 0)
    {
        errno = MapWindowsErrorToErrno(GetLastError());
        return nullptr;
    }

    return resolved_path;
}

// Deletes each earlier copy in the folder (a name of 11 digits) that no process holds: the copy of a
// client that exited, made by this or by any earlier Simba process (Simba-Reflection #194). Windows
// refuses to delete a copy that a running client loaded. A copy made in the last minute stays: its
// Simba process may not have injected it yet.
static void DeleteOldCopies(const std::string& folder) noexcept
{
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA((folder + "*.dll").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG minute = 60ULL * 10000000ULL;   // FILETIME counts 100 ns
    ULONGLONG cutoff = ((static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime) - minute;

    do
    {
        std::string name = data.cFileName;
        bool digits = name.size() == 15 && name.compare(11, 4, ".dll") == 0 &&
                      std::all_of(name.begin(), name.begin() + 11, [](char c) { return c >= '0' && c <= '9'; });
        ULONGLONG made = (static_cast<ULONGLONG>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime;
        if (digits && made < cutoff)
        {
            DeleteFileA((folder + name).c_str());
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

// A copy of this module's file under a new random name in the temp folder, so the module in the
// client has a different name each injection (a check by the name of the plugin's own file, a
// SHA1 for Simba, finds nothing). The name is 11 digits alone: it copies no other library's.
static std::string CopySelf(const std::string& file) noexcept
{
    char temp[MAX_PATH] = {0};
    DWORD length = GetTempPathA(MAX_PATH, temp);
    if (length == 0 || length >= MAX_PATH)
    {
        return {};
    }

    std::string folder(temp, length);
    DeleteOldCopies(folder);

    std::string copy = folder + std::to_string(Random::instance()->generate_random_int(10000000000LL, 99999999999LL)) + ".dll";
    if (!CopyFileA(file.c_str(), copy.c_str(), TRUE))
    {
        return {};
    }
    return copy;
}

bool InjectSelf(std::int32_t pid) noexcept
{
    if (IsProcessAlive(pid))
    {
        extern HMODULE module;

        #if !defined(CUSTOM_INJECTOR)
        extern std::vector<std::unique_ptr<Injector>> injectors;

        Injector* existing = nullptr;
        for (auto& injector : injectors)
        {
            if (injector && injector->get_pid() == pid)
            {
                existing = injector.get();
                break;
            }
        }

        if (existing && existing->is_injected())
        {
            return true;
        }
        #endif

        std::string file;
        file.resize(MAX_PATH);
        DWORD length = GetModuleFileName(module, &file[0], MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return false;
        }
        file.resize(length);

        std::string copy = CopySelf(file);
        if (copy.empty())
        {
            return false;
        }

        bool result = false;

        #if defined(CUSTOM_INJECTOR)
        result = Injector::Inject(copy, pid, nullptr);
        #else
        if (existing)
        {
            result = existing->Inject(copy);
        }
        else
        {
            std::unique_ptr<Injector> injector = std::make_unique<Injector>(pid);
            if (injector)
            {
                result = injector->Inject(copy);
                injectors.push_back(std::move(injector));
            }
        }
        #endif

        if (!result)
        {
            DeleteFileA(copy.c_str());
        }
        return result;
    }
    return false;
}

std::int32_t InjectProcess(std::int32_t pid) noexcept
{
    // The module in the client has a random name, so a client already injected is the one with a
    // controller (its shared memory); the module's base is kept as a second check.
    extern HMODULE module;
    MODULEENTRY32 info = GetModuleInfo(pid, module);
    if (info.dwSize == 0 && !ControlCenter::controller_exists(pid))
    {
        if (InjectSelf(pid))
        {
            return pid;
        }
    }
    return -1;
}

std::vector<std::int32_t> InjectProcesses(const char* process_name) noexcept
{
    std::vector<std::int32_t> result;
    std::vector<std::int32_t> pids = get_pids(process_name);
    for (std::int32_t pid : pids)
    {
        if (InjectProcess(pid) != -1)
        {
            result.push_back(pid);
        }
    }
    return result;
}

std::int32_t PIDFromWindow(void* window) noexcept
{
    DWORD pid = 0;
    GetWindowThreadProcessId(static_cast<HWND>(window), &pid);
    return static_cast<std::int32_t>(pid);
}
#endif // defined

#if defined(_WIN32) || defined(_WIN64)
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) noexcept
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && lParam)
    {
        char className[MAX_PATH] = {0};
        int result = GetClassName(hwnd, className, sizeof(className));
        if (result != 0 && std::string(className) == "SunAwtFrame")
        {
            *reinterpret_cast<HWND*>(lParam) = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

NativeKey GetNativeKey(std::int32_t virtual_key) noexcept
{
    //A message names Shift, Ctrl or Alt alone: its side is in the scancode, which the peer's table does not read.
    std::int32_t key = virtual_key >= VK_LSHIFT && virtual_key <= VK_RMENU ? VK_SHIFT + (virtual_key - VK_LSHIFT) / 2 : virtual_key;
    NativeKey native = {key, 0, 0, 0, false};
    if (key <= 0 || key > 0xFF)
    {
        return native;
    }

    //The peer maps on its toolkit thread's layout, the thread that owns the frame.
    HWND frame = nullptr;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&frame));
    HKL layout = GetKeyboardLayout(frame ? GetWindowThreadProcessId(frame, nullptr) : 0);

    native.scancode = static_cast<std::int32_t>(MapVirtualKeyExW(key, MAPVK_VK_TO_VSC, layout));
    if (native.scancode)
    {
        //The key alone held, as the peer's table: its char with no modifier. Flag 4 keeps this thread's dead-key state
        //(Windows 10 1607 and later), where the peer resets its own after.
        BYTE state[256] = {0};
        state[key] = 0x80;
        WCHAR chars[16] = {0};
        int count = ToUnicodeEx(key, native.scancode, state, chars, 16, 4, layout);
        native.unicode = count == 1 || count == -1 ? chars[0] : 0;

        //A numpad key's entry takes its key code at each event (UpdateDynPrimaryKeymap), as "/" and "NumPad /" share a char.
        bool numpad = key == VK_CLEAR || (key >= VK_NUMPAD0 && key <= VK_DIVIDE) || key == VK_NUMLOCK;
        native.extended_char = count == 1 && !numpad ? chars[0] : 0;
        //shortcut: a dead key's (-1) holds its VK_DEAD_* (the peer's charToDeadVKTable), which this does not map, so its
        //extendedKeyCode stays 0; the fork's key code of such a key is a US table's anyway. Map both when a client runs a
        //layout with dead keys.
        native.dead_key = count == -1;
    }
    return native;
}

std::unique_ptr<Reflection> GetNativeReflector() noexcept
{
    auto TimeOut = [&](std::uint32_t time, std::function<bool()> &&run) -> bool {
        auto start = std::chrono::high_resolution_clock::now();
        while(!run())
        {
            if (elapsed_time<std::chrono::seconds>(start) >= time)
            {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return true;
    };

    auto IsValidFrame = [](JNIEnv* env, jobject object) -> bool {
        jclass cls = env->FindClass("java/awt/Frame");
        bool result = env->IsInstanceOf(object, cls);
        env->DeleteLocalRef(cls);
        return result;
    };

    auto GetMainWindow = [&] {
        HWND windowFrame = nullptr;
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windowFrame));
        return windowFrame;
    };

    HMODULE awt = nullptr;
    if (!TimeOut(20, [&]{ return (awt = GetModuleHandle("awt.dll")); }))
    {
        return nullptr;
    }

    if (!TimeOut(5, [&]{ return JVM().getVM() != nullptr; }))
    {
        return nullptr;
    }

    JVM vm = JVM();
    if (!vm)
    {
        return nullptr;
    }

    JNIEnv* env = nullptr;
    if (vm.AttachCurrentThreadAsDaemon(&env) == JNI_OK)
    {
        std::unique_ptr<Reflection> reflection;
        bool hasReflection = TimeOut(20, [&]{
            jclass cls = env->FindClass("java/awt/Frame");
            if (!cls)
            {
                return false;
            }

            jmethodID method = env->GetStaticMethodID(cls, "getFrames", "()[Ljava/awt/Frame;");
            if (!method)
            {
                return false;
            }

            jobjectArray frames = static_cast<jobjectArray>(ClearException(env, env->CallStaticObjectMethod(cls, method)));
            env->DeleteLocalRef(cls);
            if (!frames)
            {
                return false;
            }

            jsize size = env->GetArrayLength(frames);
            for (jsize i = 0; i < size; ++i)
            {
                jobject frame = env->GetObjectArrayElement(frames, i);
                if (frame)
                {
                    if (IsValidFrame(env, frame))
                    {
                        reflection = Reflection::Create(frame);
                        if (reflection)
                        {
                            env->DeleteLocalRef(frames);
                            return true;
                        }
                    }

                    env->DeleteLocalRef(frame);
                }
            }

            env->DeleteLocalRef(frames);
            return false;
        });

        bool hasReflection2 = !hasReflection && TimeOut(20, [&]{
            auto DSGetComponent = reinterpret_cast<jobject (__stdcall *)(JNIEnv*, void*)>(GetProcAddress(awt, "_DSGetComponent@8"));
            if (!DSGetComponent)
            {
                DSGetComponent = reinterpret_cast<jobject (__stdcall* )(JNIEnv*, void*)>(GetProcAddress(awt, "DSGetComponent"));
            }

            if (!DSGetComponent)
            {
                int ordinal = 146;
                char* procName = reinterpret_cast<char*>(static_cast<std::uint64_t>(static_cast<std::uint16_t>(ordinal)));
                DSGetComponent = reinterpret_cast<jobject (__stdcall *)(JNIEnv*, void*)>(GetProcAddress(awt, procName));
            }

            if (!DSGetComponent)
            {
                return false;
            }

            return TimeOut(20, [&]{
                HWND windowFrame = GetMainWindow();
                if (windowFrame)
                {
                    jobject frame = DSGetComponent(reflection->getEnv(), windowFrame);  //java.awt.Frame
                    if (frame)
                    {
                        if (IsValidFrame(env, frame))
                        {
                            reflection = Reflection::Create(frame);
                            if (reflection)
                            {
                                return true;
                            }
                        }
                    }
                }
                return false;
            });
        });

        if (hasReflection || hasReflection2)
        {
            return reflection;
        }
    }

    vm.DetachCurrentThread();
    return nullptr;
}
#endif // defined
