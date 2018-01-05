The curl nuget package doesn't support UWP, but there's a libcurl.UWP which does.
However it doesn't install to this sample for some reason, so you have to manually extract it.

To build sample:
1. Download https://www.nuget.org/api/v2/package/libcurl.UWP/1.0.4
2. Rename libcurl.uwp.1.0.4.nupkg to libcurl.uwp.1.0.4.nupkg.zip
3. Extract contents to CustomHttpImplWithCurl\libcurl.uwp.1.0.4.nupkg\
4. Build and run sample