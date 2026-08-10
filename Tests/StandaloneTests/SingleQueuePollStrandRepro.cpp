// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Standalone Linux repro / regression guard for the single-port delayed-callback
// strand bug. Runs the shared scenario against the STL WaitTimer backend (the
// platform where the regression manifests; the Windows unit-test lanes only
// exercise the Win32 timer backend and cannot catch it).
//
// Exit codes:
//   0  - scenario healthy (the poll loop kept advancing after every burst)
//   1  - strand detected (the poll loop stalled with no rescue traffic)
//
// Built as the CTest target "taskqueue-poll-strand-linux" in
// Build/libHttpClient.Linux/CMakeLists.txt and run by the Linux CI lane.

#include "SingleQueueDelayedPollStrandScenario.h"

#include <cstdio>

int main()
{
    hc_test::SingleQueuePollStrandConfig cfg{};

    int failedIteration = -1;
    int pollsAtStall = -1;

    std::printf(
        "[taskqueue-poll-strand] running %d iterations, poll delay %ums, burst %d threads x %d submits...\n",
        cfg.outerIterations,
        cfg.pollDelayMs,
        cfg.burstThreads,
        cfg.burstSubmitsPerThread);
    std::fflush(stdout);

    bool ok = hc_test::RunSingleQueuePollStrandScenario(cfg, &failedIteration, &pollsAtStall);

    if (!ok)
    {
        std::printf(
            "[taskqueue-poll-strand] FAILED: poll loop stalled at iteration %d (reached %d polls, then no progress)\n",
            failedIteration,
            pollsAtStall);
        std::fflush(stdout);
        return 1;
    }

    std::printf("[taskqueue-poll-strand] PASSED: no strand detected\n");
    std::fflush(stdout);
    return 0;
}
