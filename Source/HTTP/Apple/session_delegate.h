// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#import <Foundation/Foundation.h>

@interface SessionDelegate : NSObject<NSURLSessionTaskDelegate, NSURLSessionDataDelegate>
+ (SessionDelegate*) sessionDelegateWithHCCallHandle:(HCCallHandle) call andCompletionHandler:(void(^)(NSURLResponse* response, NSError* error)) completion;
@end
