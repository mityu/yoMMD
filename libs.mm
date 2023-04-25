#include "platform.hpp"

#define SOKOL_IMPL

#if defined(PLATFORM_MAC)
#  define SOKOL_METAL
#elif defined(PLATFORM_WINDOWS)
#  define SOKOL_D3D11
#endif

#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_time.h"


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
