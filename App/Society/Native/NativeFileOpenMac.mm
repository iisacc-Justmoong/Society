#include "NativeFileOpen.h"
#import <AppKit/AppKit.h>

namespace society::native {
std::optional<std::string> openNativeFile(const std::string &absolutePath)
{
    @autoreleasepool {
        if (![NSThread isMainThread]) return "The file must be opened from the application thread";
        NSString *path=[NSString stringWithUTF8String:absolutePath.c_str()];
        if (!path || ![path isAbsolutePath]) return "The file path is invalid";
        if (![NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:path isDirectory:NO]])
            return "The operating system could not open this file";
        return {};
    }
}
}
