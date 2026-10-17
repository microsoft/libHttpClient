#pragma once

#include <jni.h>

// Java package (in JNI path form) that holds libHttpClient's Java classes.
// A private copy of libHttpClient embedded in another native library can
// relocate its Java classes to a different package (by building with
// HC_ANDROID_JAVA_PACKAGE_PATH) so they don't collide with a public copy in
// the same app. JNI's name-based lookup of native methods only matches the
// default package, so a relocated build binds them with RegisterNatives and
// hides the default-package Java_com_xbox_httpclient_* symbols.
#ifdef HC_ANDROID_JAVA_PACKAGE_PATH
#define HC_ANDROID_REGISTER_NATIVES 1
#define HC_JNIEXPORT __attribute__((visibility("hidden")))
#else
#define HC_ANDROID_JAVA_PACKAGE_PATH "com/xbox/httpclient"
#define HC_JNIEXPORT JNIEXPORT
#endif

#define HC_JAVA_CLASS(name) HC_ANDROID_JAVA_PACKAGE_PATH "/" name
