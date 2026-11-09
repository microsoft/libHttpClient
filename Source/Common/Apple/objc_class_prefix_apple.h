// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

// Objective-C class names are global to a process, even when C++ symbols are hidden. An app that
// links two copies of libHttpClient (for example, one inside another framework) gets duplicate
// classes. To avoid that, build one copy with HC_OBJC_CLASS_PREFIX defined, for example
// -DHC_OBJC_CLASS_PREFIX=Contoso_, and its classes become Contoso_SessionDelegate and so on.
// When HC_OBJC_CLASS_PREFIX is not defined (the default), the class names are unchanged.
//
// Include this after any system headers, and in every file that declares or uses these classes.

#ifdef HC_OBJC_CLASS_PREFIX

#define HC_OBJC_CLASS_NAME_CONCAT_(prefix, name) prefix##name
#define HC_OBJC_CLASS_NAME_CONCAT(prefix, name) HC_OBJC_CLASS_NAME_CONCAT_(prefix, name)

#define SessionDelegate HC_OBJC_CLASS_NAME_CONCAT(HC_OBJC_CLASS_PREFIX, SessionDelegate)
#define RequestBodyStream HC_OBJC_CLASS_NAME_CONCAT(HC_OBJC_CLASS_PREFIX, RequestBodyStream)
#define ProxyResolver HC_OBJC_CLASS_NAME_CONCAT(HC_OBJC_CLASS_PREFIX, ProxyResolver)
#define ProxyConfiguration HC_OBJC_CLASS_NAME_CONCAT(HC_OBJC_CLASS_PREFIX, ProxyConfiguration)

#endif
