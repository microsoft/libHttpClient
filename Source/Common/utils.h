// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

std::string to_utf8string(std::string value);

std::string to_utf8string(const std::wstring &value);

std::wstring to_utf16string(const std::string &value);

std::wstring to_utf16string(std::wstring value);
