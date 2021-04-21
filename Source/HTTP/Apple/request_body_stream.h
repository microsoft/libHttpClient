// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#import <Foundation/Foundation.h>

@interface RequestBodyStream : NSInputStream<NSStreamDelegate>
+(RequestBodyStream*)requestBodyStreamWithHCCallHandle:(HCCallHandle)call;
@end
