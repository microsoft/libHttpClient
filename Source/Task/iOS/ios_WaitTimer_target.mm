// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include "../WaitTimer.h"
#include "ios_WaitTimerImpl.h"

#import "ios_WaitTimer_target.h"

@implementation ios_WaitTimer_target

- (void)timerFireMethod:(NSTimer*)timer
{
    auto value = (NSValue*)timer.userInfo;
    auto impl = static_cast<OS::WaitTimerImpl*>(value.pointerValue);
    impl->TimerFired();
}

@end
