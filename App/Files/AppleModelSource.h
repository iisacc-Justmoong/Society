#pragma once

#include "ModelImportSource.h"

#ifdef __OBJC__
@class NSItemProvider;
@class NSURL;
ModelImportSource appleModelSource(NSItemProvider *provider);
ModelImportSource appleModelSource(NSURL *url);
#endif
