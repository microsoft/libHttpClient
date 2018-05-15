#pragma once

#include <httpClient/trace.h>

void HCTraceImplGlobalInit();
void HCTraceImplGlobalCleanup();

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message);




