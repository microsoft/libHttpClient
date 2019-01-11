// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "UnitTestBase.h"
#include "UnitTestBase_winrt.h"
#include "DefineTestMacros.h"
#include "WexTestClass.h"

#define LOG_COMMENT(x, ...) WEX::Logging::Log::Comment(WEX::Common::String().Format(x, __VA_ARGS__))
