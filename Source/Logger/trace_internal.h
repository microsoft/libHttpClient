#pragma once

#include <httpClient/trace.h>

void HCTraceImplInit();
void HCTraceImplCleanup();

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message);




