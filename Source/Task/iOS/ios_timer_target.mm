// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include "timer.h"

#import "ios_timer_target.h"

@implementation ios_timer_target

- (void)timerFireMethod:(NSTimer*)timer
{
    auto value = (NSValue*)timer.userInfo;
    auto pt = static_cast<PlatformTimer*>(value.pointerValue);

    pt->m_callback(pt->m_context);
}

@end
