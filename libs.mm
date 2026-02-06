#include "platform.hpp"

#define SOKOL_IMPL

#if defined(PLATFORM_MAC)
#define SOKOL_METAL
#elif defined(PLATFORM_WINDOWS)
#define SOKOL_D3D11
#elif defined(PLATFORM_LINUX)
#define SOKOL_GLCORE
#define BACKEND_SOKOL_APP
#endif

#include "sokol_gfx.h"
#include "sokol_time.h"

#ifdef BACKEND_SOKOL_APP
#include "sokol_app.h"
#include "sokol_glue.h"  // sokol_glue.h must be included after sokol_gfx.h and sokol_app.h
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
