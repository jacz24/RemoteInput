#ifndef JVM_HXX_INCLUDED
#define JVM_HXX_INCLUDED

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <dlfcn.h>
#endif // defined

#include <stdexcept>
#include <vector>
#include <string>

#include "JNI_Common.hxx"

class JVM
{
private:
    JavaVM* vm;
    bool createdVM;
    bool loadedJNI;
    #if defined(_WIN32) || defined(_WIN64)
    HMODULE module;
    #else
    void* module;
    #endif // defined

    bool Init(int argc, const char* argv[]) noexcept;
    jint JNI_GetDefaultJavaVMInitArgs(void *args) const noexcept;
    jint JNI_CreateJavaVM(JavaVM **pvm, void **penv, void *args) const noexcept;
    jint JNI_GetCreatedJavaVMs(JavaVM **, jsize, jsize *) const noexcept;


public:
    JVM() noexcept;
    JVM(JNIEnv* env) noexcept;
    JVM(int argc, const char* argv[]) noexcept;
    ~JVM() noexcept;

    operator bool() const noexcept;
    JavaVM* getVM() const noexcept;


    jint AttachCurrentThread(JNIEnv** env) noexcept;
    jint AttachCurrentThreadAsDaemon(JNIEnv** env) noexcept;
    jint DetachCurrentThread() noexcept;

    //Daemon attach with a name, for a thread that has no JVM env yet (the raw JavaVM of a plugin object).
    static jint AttachDaemon(JavaVM* vm, JNIEnv** env) noexcept;

    //AttachDaemon once for the calling thread: it detaches when the thread ends, so a worker that calls this for each task
    //makes one java.lang.Thread, not one for each task (#197).
    static jint AttachWorker(JavaVM* vm, JNIEnv** env) noexcept;
};

#endif // JVM_HXX_INCLUDED
