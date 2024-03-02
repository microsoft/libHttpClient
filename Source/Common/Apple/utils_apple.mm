#include "pch.h"
#include "utils_apple.h"

#import <CFNetwork/CFNetwork.h>
#import <Foundation/Foundation.h>

@interface ProxyConfiguration : NSObject
+ (instancetype) withHost:(NSString* _Nonnull) host
                     port:(NSNumber* _Nullable) port
                 username:(NSString* _Nullable) username
              andPassword:(NSString* _Nullable) password;

@property (readonly) NSString* host;
@property (readonly) NSNumber* port;
@property (readonly) NSString* username;
@property (readonly) NSString* password;
@end

@interface ProxyResolver : NSObject
+ (instancetype) withTargetUrl:(NSURL*)url;
- (BOOL) getProxy:(NSURL** _Nonnull)outUrl withUsername:(NSString** _Nullable)outUsername andPassword:(NSString** _Nullable)outPassword;
@end

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

bool getSystemProxyForUri(const Uri& uri, Uri* outUri, std::string* outUsername, std::string* outPassword)
{
    NSURL* url = [NSURL URLWithString:[NSString stringWithCString:uri.FullPath().c_str() encoding:NSUTF8StringEncoding]];
    ProxyResolver* resolver = [ProxyResolver withTargetUrl:url];

    NSURL* proxyUrl = nil;
    NSString* proxyUsername = nil;
    NSString* proxyPassword = nil;
    if ([resolver getProxy:&proxyUrl withUsername:&proxyUsername andPassword:&proxyPassword])
    {
        if (!outUri)
        {
            return false;
        }

        *outUri = Uri(String([proxyUrl.absoluteString UTF8String]));

        if (outUsername && proxyUsername)
        {
            *outUsername = [proxyUsername UTF8String];
        }

        if (outPassword && proxyPassword)
        {
            *outPassword = [proxyPassword UTF8String];
        }
        return true;
    }
    return false;
}

NAMESPACE_XBOX_HTTP_CLIENT_END

@implementation ProxyConfiguration
+ (instancetype) withHost:(NSString *)host port:(NSNumber *)port username:(NSString *)username andPassword:(NSString *)password
{
    return [[ProxyConfiguration alloc] initWithHost:host port:port username:username andPassword:password];
}

- (instancetype) initWithHost:(NSString *)host port:(NSNumber *)port username:(NSString *)username andPassword:(NSString *)password
{
    if (self = [super init])
    {
        _host = host;
        _port = port;
        _username = username;
        _password = password;
        return self;
    }
    return nil;
}
@end

@implementation ProxyResolver
{
    NSString* _originalScheme;
    NSString* _originalResourceSpecifier;
    NSURL* _url;
}

+ (instancetype) withTargetUrl:(NSURL*)url
{
    return [[ProxyResolver alloc] initWithTargetUrl:url];
}

- (BOOL) getProxy:(NSURL** _Nonnull)outUrl withUsername:(NSString**)outUsername andPassword:(NSString**)outPassword
{
    ProxyConfiguration* proxy = [self resolve];
    if (proxy)
    {
        NSString *url = nil;
        if (proxy.port)
        {
            url = [NSString stringWithFormat:@"%@://%@:%@", _originalScheme, proxy.host, proxy.port];
        }
        else
        {
            url = [NSString stringWithFormat:@"%@://%@", _originalScheme, proxy.host];
        }

        *outUrl = [NSURL URLWithString:url];

        if (outUsername)
        {
            *outUsername = proxy.username;
        }

        if (outPassword)
        {
            *outPassword = proxy.password;
        }
        return YES;
    }
    return NO;
}

- (instancetype) initWithTargetUrl:(NSURL*)url
{
    if (self = [super init])
    {
        _originalScheme = [url scheme];
        _originalResourceSpecifier = [url resourceSpecifier];
        _url = url;

        // CFNetworkCopyProxiesForURL doesn't know how to handle ws or wss
        if ([_originalScheme hasPrefix:@"ws"])
        {
            NSString* adjustedScheme = [_originalScheme stringByReplacingOccurrencesOfString:@"ws" withString:@"http"];
            _url = [NSURL URLWithString:[NSString stringWithFormat:@"%@:%@", adjustedScheme, _originalResourceSpecifier]];
        }
        return self;
    }
    return nil;
}

- (ProxyConfiguration* _Nullable) resolve
{
    return [self resolveFrom: [self getSystemProxies]];
}

- (ProxyConfiguration* _Nullable) resolveFrom:(NSArray<NSDictionary*>*)proxies
{
    if (proxies.count == 0)
    {
        return nil;
    }

    NSDictionary *proxy = [proxies objectAtIndex:0];
    if (!proxy)
    {
        return nil;
    }

    NSString *proxyType = proxy[(NSString *)kCFProxyTypeKey];
    if (!proxyType)
    {
        return nil;
    }

    if ([proxyType isEqualToString:(NSString*)kCFProxyTypeNone])
    {
        return nil;
    }
    else if ([proxyType isEqualToString:(NSString *)kCFProxyTypeAutoConfigurationURL])
    {
        NSURL *pacUrl = proxy[(NSString *)kCFProxyAutoConfigurationURLKey];
        if (!pacUrl)
        {
            return nil;
        }

        NSError* error = nil;
        NSString* pacScript = [NSString stringWithContentsOfURL:pacUrl encoding:NSUTF8StringEncoding error:&error];
        if (error || !pacScript)
        {
            return nil;
        }

        NSArray<NSDictionary*>* proxies = [self runPacScript:pacScript error:&error];
        if (error || !proxies)
        {
            return nil;
        }

        return [self resolveFrom:proxies];
    }
    else if ([proxyType isEqualToString:(NSString *)kCFProxyTypeAutoConfigurationJavaScript])
    {
        NSString *script = proxy[(NSString *)kCFProxyAutoConfigurationJavaScriptKey];
        if (!script)
        {
            return nil;
        }

        NSError *error = nil;
        NSArray<NSDictionary*>* proxies = [self runPacScript:script error:&error];
        if (error || !proxies)
        {
            return nil;
        }

        return [self resolveFrom:proxies];
    }
    else
    {
        NSString *host = proxy[(__bridge NSString*)kCFProxyHostNameKey];
        NSNumber *port = proxy[(__bridge NSString*)kCFProxyPortNumberKey];
        NSString *username = proxy[(__bridge NSString*)kCFProxyUsernameKey];
        NSString *password = proxy[(__bridge NSString*)kCFProxyPasswordKey];
        return [ProxyConfiguration withHost:host port:port username:username andPassword:password];
    }
}

- (NSArray<NSDictionary*>*) getSystemProxies
{
    NSDictionary* proxySettings = (__bridge_transfer NSDictionary*)CFNetworkCopySystemProxySettings();
    return (__bridge_transfer NSArray*)CFNetworkCopyProxiesForURL((__bridge CFURLRef)_url, (__bridge CFDictionaryRef)proxySettings);
}

- (NSArray<NSDictionary*>*) runPacScript:(NSString* _Nonnull)script error:(NSError** _Nullable)error
{
    return (__bridge_transfer NSArray*)CFNetworkCopyProxiesForAutoConfigurationScript((__bridge CFStringRef)script, (__bridge CFURLRef)_url, NULL);
}
@end
