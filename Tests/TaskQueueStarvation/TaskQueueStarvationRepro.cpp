// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Standalone Linux repro / regression guard for the composite-queue delayed-
// callback starvation bug. Runs the shared scenario against the STL WaitTimer
// backend (the platform where the regression manifests; the Windows unit-test
// lanes only exercise the Win32 timer backend and cannot catch it).
//
// Exit codes:
//   0  - scenario healthy (every request completed in every iteration)
//   1  - starvation detected (a request never completed within the timeout)
//
// Built as the CTest target "taskqueue-starvation-linux" in
// Build/libHttpClient.Linux/CMakeLists.txt and run by the Linux CI lane.

#include "CompositeQueueStarvationScenario.h"

#include <cstdio>

int main()
{
    hc_test::CompositeStarvationConfig cfg{};

    int failedIteration = -1;
    int completedAtFailure = -1;

    std::printf(
        "[taskqueue-starvation] running %d iterations, %d submitters x %d requests, %d pumps...\n",
        cfg.outerIterations,
        cfg.submitterThreads,
        cfg.requestsPerSubmitter,
        cfg.pumpThreads);
    std::fflush(stdout);

    bool ok = hc_test::RunCompositeQueueStarvationScenario(cfg, &failedIteration, &completedAtFailure);

    if (!ok)
    {
        const int total = cfg.submitterThreads * cfg.requestsPerSubmitter;
        std::printf(
            "[taskqueue-starvation] FAILED: starvation at iteration %d (%d / %d requests completed)\n",
            failedIteration,
            completedAtFailure,
            total);
        std::fflush(stdout);
        return 1;
    }

    std::printf("[taskqueue-starvation] PASSED: no starvation detected\n");
    std::fflush(stdout);
    return 0;
}
