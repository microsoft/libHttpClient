// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

JNIEnv* get_jvm_env();

struct java_local_ref_deleter
{
    void operator()(jobject lref) const
    {
        xbox::httpclient::get_jvm_env()->DeleteLocalRef(lref);
    }
};

template<class T>
using java_local_ref = std::unique_ptr<typename std::remove_pointer<T>::type, java_local_ref_deleter>;


NAMESPACE_XBOX_HTTP_CLIENT_END
