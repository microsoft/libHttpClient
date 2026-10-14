// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Entry point for the portable unit-test executable (see UnitTestIncludes_Portable.h).

#include "pch.h"
#include "UnitTestIncludes.h"

int main(int argc, char** argv)
{
    return hcunittest::RunAllTests(argc, argv);
}
