#pragma once

#include <jni.h>
#include <string>
#include <vector>

bool CheckAndLogJavaException(JNIEnv* env, const wchar_t* stage);

bool CheckAndLogJavaMainException(JNIEnv* env, const wchar_t* stage);

bool LaunchInvokeJavaMain(JNIEnv* env, const std::wstring& className, const std::vector<std::string>& args,
    const std::wstring& isolatedClassPath = L"");
void LaunchSetJavaSystemProperty(JNIEnv* env, const std::wstring& key, const std::wstring& value);
