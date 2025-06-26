// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#import <Foundation/Foundation.h>

@interface SessionDelegate : NSObject<NSURLSessionTaskDelegate, NSURLSessionDelegate, NSURLSessionDataDelegate>

@property (nonatomic, retain) NSMutableData *dataToDownload;
@property (nonatomic) float downloadSize;

+ (SessionDelegate*) sessionDelegateWithHCCallHandle:(HCCallHandle) call andCompletionHandler:(void(^)(NSURLResponse* response, NSError* error)) completion;
+ (void) reportProgress:(HCCallHandle)call progressReportFunction:(HCHttpCallProgressReportFunction)progressReportFunction minimumInterval:(size_t)minimumInterval current:(size_t)current total:(size_t)total progressReportCallbackContext:(void*)progressReportCallbackContext lastProgressReport:(std::chrono::steady_clock::time_point*)lastProgressReport;
@end
