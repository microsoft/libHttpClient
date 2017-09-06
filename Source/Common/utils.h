// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

std::string to_utf8string(std::string value);

std::string to_utf8string(const std::wstring &value);

std::wstring to_wstring(const std::string &value);

std::wstring to_wstring(std::wstring value);

NAMESPACE_XBOX_HTTP_CLIENT_END
