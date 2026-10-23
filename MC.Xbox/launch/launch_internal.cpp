#include "launch_internal.h"

#include "crash_fingerprint.h"
#include "launcher_common.h"
#include "telemetry.h"

#include <algorithm>
#include <string>
#include <sstream>
#include <vector>

namespace {

constexpr int kMaxCauseDepth = 16;
constexpr jsize kMaxCapturedFrames = 16;

// modified utf8 from GetStringUTFChars is invalid json for some characters
std::string JStringToUtf8(JNIEnv* env, jstring value) {
    if (!value) return std::string();

    const jsize length = env->GetStringLength(value);
    const jchar* chars = env->GetStringChars(value, nullptr);
    if (!chars) {
        env->ExceptionClear();
        return std::string();
    }

    std::wstring wide(reinterpret_cast<const wchar_t*>(chars), static_cast<size_t>(length));
    env->ReleaseStringChars(value, chars);
    return w2a(wide);
}

std::string JavaClassName(JNIEnv* env, jobject object, jmethodID getClass, jmethodID getName) {
    if (!object) return std::string();

    jobject cls = env->CallObjectMethod(object, getClass);
    if (!cls || env->ExceptionCheck()) {
        env->ExceptionClear();
        return std::string();
    }

    jstring name = static_cast<jstring>(env->CallObjectMethod(cls, getName));
    if (!name || env->ExceptionCheck()) {
        env->ExceptionClear();
        return std::string();
    }
    return JStringToUtf8(env, name);
}

int HeapMb(JNIEnv* env, jobject runtime, jmethodID method) {
    if (!runtime || !method) return 0;

    const jlong bytes = env->CallLongMethod(runtime, method);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    return static_cast<int>(bytes / (1024 * 1024));
}

void CaptureJavaCrashForTelemetry(JNIEnv* env, jthrowable throwable) {
    if (!env || !throwable) return;
    if (env->PushLocalFrame(64) != 0) {
        env->ExceptionClear();
        return;
    }

    jclass objectClass = env->FindClass("java/lang/Object");
    jclass classClass = env->FindClass("java/lang/Class");
    jclass throwableClass = env->FindClass("java/lang/Throwable");
    jclass elementClass = env->FindClass("java/lang/StackTraceElement");
    if (!objectClass || !classClass || !throwableClass || !elementClass) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }

    jmethodID getClass = env->GetMethodID(objectClass, "getClass", "()Ljava/lang/Class;");
    jmethodID getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    jmethodID getMessage = env->GetMethodID(throwableClass, "getMessage", "()Ljava/lang/String;");
    jmethodID getCause = env->GetMethodID(throwableClass, "getCause", "()Ljava/lang/Throwable;");
    jmethodID getStackTrace = env->GetMethodID(
        throwableClass, "getStackTrace", "()[Ljava/lang/StackTraceElement;");
    jmethodID elementToString = env->GetMethodID(elementClass, "toString", "()Ljava/lang/String;");
    if (!getClass || !getName || !getMessage || !getCause || !getStackTrace || !elementToString) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }

    const std::string outerClass = JavaClassName(env, throwable, getClass, getName);

    jobject root = throwable;
    for (int depth = 0; depth < kMaxCauseDepth; ++depth) {
        jobject cause = env->CallObjectMethod(root, getCause);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }
        // broken throwable chains can point back to themselves
        if (!cause || env->IsSameObject(cause, root)) break;
        root = cause;
    }

    const std::string rootClass = JavaClassName(env, root, getClass, getName);

    std::string message = JStringToUtf8(env, static_cast<jstring>(env->CallObjectMethod(root, getMessage)));
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (message.empty() && root != throwable) {
        message = JStringToUtf8(env, static_cast<jstring>(env->CallObjectMethod(throwable, getMessage)));
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    std::vector<std::string> frames;
    jobjectArray trace = static_cast<jobjectArray>(env->CallObjectMethod(root, getStackTrace));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (trace) {
        const jsize count = (std::min)(env->GetArrayLength(trace), kMaxCapturedFrames);
        for (jsize i = 0; i < count; ++i) {
            jobject element = env->GetObjectArrayElement(trace, i);
            if (!element || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }
            jstring text = static_cast<jstring>(env->CallObjectMethod(element, elementToString));
            if (!text || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }
            frames.push_back(JStringToUtf8(env, text));
        }
    }

    crashfp::JavaCrash crash = crashfp::Build(outerClass, rootClass, message, frames);

    jclass runtimeClass = env->FindClass("java/lang/Runtime");
    if (runtimeClass) {
        jmethodID getRuntime = env->GetStaticMethodID(runtimeClass, "getRuntime", "()Ljava/lang/Runtime;");
        jobject runtime = getRuntime ? env->CallStaticObjectMethod(runtimeClass, getRuntime) : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (runtime) {
            crash.heapMaxMb = HeapMb(env, runtime, env->GetMethodID(runtimeClass, "maxMemory", "()J"));
            const int total = HeapMb(env, runtime, env->GetMethodID(runtimeClass, "totalMemory", "()J"));
            const int free = HeapMb(env, runtime, env->GetMethodID(runtimeClass, "freeMemory", "()J"));
            crash.heapAtCrashMb = total - free;
        }
    }

    env->ExceptionClear();
    env->PopLocalFrame(nullptr);

    telemetry::RecordJavaCrash(crash);
}

}

static bool CheckAndLogJavaExceptionImpl(JNIEnv* env, const wchar_t* stage, bool captureForTelemetry) {
    if (!env->ExceptionCheck()) return false;
    WriteLogF(L"Java exception during %s", stage);

    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    if (!throwable) {
        WriteLog(L"Java exception object was null after ExceptionOccurred");
        return true;
    }

    if (captureForTelemetry) CaptureJavaCrashForTelemetry(env, throwable);

    jclass stringWriterClass = env->FindClass("java/io/StringWriter");
    jclass printWriterClass = env->FindClass("java/io/PrintWriter");
    jclass throwableClass = env->FindClass("java/lang/Throwable");
    if (!stringWriterClass || !printWriterClass || !throwableClass) {
        WriteLog(L"Unable to load Java exception formatting classes");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        return true;
    }

    jmethodID stringWriterCtor = env->GetMethodID(stringWriterClass, "<init>", "()V");
    jmethodID printWriterCtor = env->GetMethodID(printWriterClass, "<init>", "(Ljava/io/Writer;)V");
    jmethodID printStackTrace = env->GetMethodID(throwableClass, "printStackTrace", "(Ljava/io/PrintWriter;)V");
    jmethodID toString = env->GetMethodID(stringWriterClass, "toString", "()Ljava/lang/String;");
    if (!stringWriterCtor || !printWriterCtor || !printStackTrace || !toString || env->ExceptionCheck()) {
        WriteLog(L"Unable to resolve Java exception formatting methods");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        env->DeleteLocalRef(stringWriterClass);
        env->DeleteLocalRef(printWriterClass);
        env->DeleteLocalRef(throwableClass);
        return true;
    }

    jobject stringWriter = env->NewObject(stringWriterClass, stringWriterCtor);
    jobject printWriter = stringWriter ? env->NewObject(printWriterClass, printWriterCtor, stringWriter) : nullptr;
    if (!stringWriter || !printWriter || env->ExceptionCheck()) {
        WriteLog(L"Unable to create Java exception formatter");
        env->ExceptionClear();
        env->DeleteLocalRef(throwable);
        env->DeleteLocalRef(stringWriterClass);
        env->DeleteLocalRef(printWriterClass);
        env->DeleteLocalRef(throwableClass);
        return true;
    }

    env->CallVoidMethod(throwable, printStackTrace, printWriter);
    jstring trace = nullptr;
    if (!env->ExceptionCheck()) {
        trace = static_cast<jstring>(env->CallObjectMethod(stringWriter, toString));
    }
    if (trace && !env->ExceptionCheck()) {
        const char* utf8 = env->GetStringUTFChars(trace, nullptr);
        if (utf8) {
            WriteLogF(L"Java exception stack:\n%s", a2w(utf8).c_str());
            env->ReleaseStringUTFChars(trace, utf8);
        }
    } else {
        WriteLog(L"Unable to stringify Java exception stack");
        env->ExceptionClear();
    }

    if (trace) env->DeleteLocalRef(trace);
    env->DeleteLocalRef(printWriter);
    env->DeleteLocalRef(stringWriter);
    env->DeleteLocalRef(throwable);
    env->DeleteLocalRef(stringWriterClass);
    env->DeleteLocalRef(printWriterClass);
    env->DeleteLocalRef(throwableClass);
    return true;
}

bool CheckAndLogJavaException(JNIEnv* env, const wchar_t* stage) {
    return CheckAndLogJavaExceptionImpl(env, stage, false);
}

bool CheckAndLogJavaMainException(JNIEnv* env, const wchar_t* stage) {
    return CheckAndLogJavaExceptionImpl(env, stage, true);
}

bool LaunchInvokeJavaMain(JNIEnv* env, const std::wstring& className, const std::vector<std::string>& args,
    const std::wstring& isolatedClassPath) {
    if (env->PushLocalFrame(32) != JNI_OK) {
        CheckAndLogJavaException(env, L"Prep tool local frame");
        return false;
    }

    jobject loader = nullptr;
    jobject thread = nullptr;
    jobject previousLoader = nullptr;
    jmethodID setContextLoader = nullptr;
    jmethodID closeLoader = nullptr;
    bool contextChanged = false;
    auto failed = [&]() { return CheckAndLogJavaException(env, (L"Prep tool " + className).c_str()); };
    const bool invoked = [&]() {
        jclass mainClass = nullptr;
        if (isolatedClassPath.empty()) {
            std::wstring classPath = className;
            std::replace(classPath.begin(), classPath.end(), L'.', L'/');
            mainClass = env->FindClass(w2a(classPath).c_str());
            if (failed() || !mainClass) return false;
        } else {
            jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
            if (failed() || !classLoaderClass) return false;
            jmethodID getPlatform = env->GetStaticMethodID(classLoaderClass, "getPlatformClassLoader", "()Ljava/lang/ClassLoader;");
            if (failed() || !getPlatform) return false;
            jobject platform = env->CallStaticObjectMethod(classLoaderClass, getPlatform);
            if (failed() || !platform) return false;

            jclass fileClass = env->FindClass("java/io/File");
            if (failed() || !fileClass) return false;
            jmethodID fileCtor = env->GetMethodID(fileClass, "<init>", "(Ljava/lang/String;)V");
            if (failed() || !fileCtor) return false;
            jmethodID toUri = env->GetMethodID(fileClass, "toURI", "()Ljava/net/URI;");
            if (failed() || !toUri) return false;
            jclass uriClass = env->FindClass("java/net/URI");
            if (failed() || !uriClass) return false;
            jmethodID toUrl = env->GetMethodID(uriClass, "toURL", "()Ljava/net/URL;");
            if (failed() || !toUrl) return false;
            jclass urlClass = env->FindClass("java/net/URL");
            if (failed() || !urlClass) return false;
            std::vector<std::wstring> paths;
            std::wstringstream input(isolatedClassPath);
            std::wstring entry;
            while (std::getline(input, entry, L';')) {
                if (!entry.empty()) paths.push_back(entry);
            }
            if (paths.empty()) return false;
            jobjectArray urls = env->NewObjectArray(static_cast<jsize>(paths.size()), urlClass, nullptr);
            if (failed() || !urls) return false;
            for (jsize i = 0; i < static_cast<jsize>(paths.size()); ++i) {
                jstring path = env->NewString(reinterpret_cast<const jchar*>(paths[i].data()), static_cast<jsize>(paths[i].size()));
                if (failed() || !path) return false;
                jobject file = env->NewObject(fileClass, fileCtor, path);
                if (failed() || !file) return false;
                jobject uri = env->CallObjectMethod(file, toUri);
                if (failed() || !uri) return false;
                jobject url = env->CallObjectMethod(uri, toUrl);
                if (failed() || !url) return false;
                env->SetObjectArrayElement(urls, i, url);
                if (failed()) return false;
                env->DeleteLocalRef(url);
                env->DeleteLocalRef(uri);
                env->DeleteLocalRef(file);
                env->DeleteLocalRef(path);
            }
            jclass urlLoaderClass = env->FindClass("java/net/URLClassLoader");
            if (failed() || !urlLoaderClass) return false;
            jmethodID loaderCtor = env->GetMethodID(urlLoaderClass, "<init>", "([Ljava/net/URL;Ljava/lang/ClassLoader;)V");
            if (failed() || !loaderCtor) return false;
            closeLoader = env->GetMethodID(urlLoaderClass, "close", "()V");
            if (failed() || !closeLoader) return false;
            loader = env->NewObject(urlLoaderClass, loaderCtor, urls, platform);
            if (failed() || !loader) return false;

            jclass threadClass = env->FindClass("java/lang/Thread");
            if (failed() || !threadClass) return false;
            jmethodID currentThread = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
            if (failed() || !currentThread) return false;
            jmethodID getContextLoader = env->GetMethodID(threadClass, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
            if (failed() || !getContextLoader) return false;
            setContextLoader = env->GetMethodID(threadClass, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");
            if (failed() || !setContextLoader) return false;
            thread = env->CallStaticObjectMethod(threadClass, currentThread);
            if (failed() || !thread) return false;
            previousLoader = env->CallObjectMethod(thread, getContextLoader);
            if (failed()) return false;
            env->CallVoidMethod(thread, setContextLoader, loader);
            if (failed()) return false;
            contextChanged = true;
            jmethodID loadClass = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            if (failed() || !loadClass) return false;
            jstring name = env->NewString(reinterpret_cast<const jchar*>(className.data()), static_cast<jsize>(className.size()));
            if (failed() || !name) return false;
            mainClass = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, name));
            if (failed() || !mainClass) return false;
        }

        jmethodID mainMethod = env->GetStaticMethodID(mainClass, "main", "([Ljava/lang/String;)V");
        if (failed() || !mainMethod) return false;
        jclass stringClass = env->FindClass("java/lang/String");
        if (failed() || !stringClass) return false;
        jobjectArray argv = env->NewObjectArray(static_cast<jsize>(args.size()), stringClass, nullptr);
        if (failed() || !argv) return false;
        for (jsize i = 0; i < static_cast<jsize>(args.size()); ++i) {
            jstring value = env->NewStringUTF(args[i].c_str());
            if (failed() || !value) return false;
            env->SetObjectArrayElement(argv, i, value);
            env->DeleteLocalRef(value);
            if (failed()) return false;
        }

        WriteLogF(L"Invoking prep tool %s isolated=%d", className.c_str(), isolatedClassPath.empty() ? 0 : 1);
        env->CallStaticVoidMethod(mainClass, mainMethod, argv);
        return !failed();
    }();

    bool cleanupFailed = false;
    if (contextChanged) {
        env->CallVoidMethod(thread, setContextLoader, previousLoader);
        cleanupFailed = failed();
    }
    if (loader) {
        env->CallVoidMethod(loader, closeLoader);
        cleanupFailed = failed() || cleanupFailed;
    }
    env->PopLocalFrame(nullptr);
    return invoked && !cleanupFailed;
}

void LaunchSetJavaSystemProperty(JNIEnv* env, const std::wstring& key, const std::wstring& value) {
    jclass sys = env->FindClass("java/lang/System");
    if (!sys || CheckAndLogJavaException(env, L"FindClass(System)")) return;
    jmethodID setProp = env->GetStaticMethodID(sys, "setProperty",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!setProp || CheckAndLogJavaException(env, L"GetStaticMethodID(System.setProperty)")) {
        env->DeleteLocalRef(sys);
        return;
    }
    jstring k = env->NewStringUTF(w2a(key).c_str());
    jstring v = env->NewStringUTF(w2a(value).c_str());
    jobject prev = env->CallStaticObjectMethod(sys, setProp, k, v);
    CheckAndLogJavaException(env, L"System.setProperty");
    if (prev) env->DeleteLocalRef(prev);
    env->DeleteLocalRef(k);
    env->DeleteLocalRef(v);
    env->DeleteLocalRef(sys);
}
