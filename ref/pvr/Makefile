TARGET = libref_pvr.a
DEFINES = -D_KOS_=1 -D_SH4_=1 -DXASH_GAMEDIR="valve" -DXASH_LOW_MEMORY=2 -DALLOCA_H=<malloc.h> -DSTDINT_H=<stdint.h> -DHAVE_TGMATH_H=0 -DHAVE_STRCASECMP=1 -DXASH_STATIC_LIBS=1 -DREF_DLL=1
INCLUDES = -I../../engine/platform/dreamcast -I../../filesystem -I../../public -I../../common -I../../pm_shared -I../../engine -I../../ref/pvr -I../../engine/common -I../../engine/common/imagelib -I$(KOS_BASE)/../kos-ports/include

KOS_CFLAGS +=  $(INCLUDES) $(DEFINES) -O3 -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-strict-aliasing -mrelax -fno-asynchronous-unwind-tables -fno-stack-protector
KOS_CPPFLAGS += $(INCLUDES) $(DEFINES) -fno-exceptions -std=gnu++20 -fpermissive -MMD -MP  $(CFLAGS)
OBJS = pvr_alloc.o pvr_backend.o pvr_beams.o pvr_context.o pvr_cull.o pvr_decals.o link_helper.o pvr_draw.o gl_frustum.o pvr_image.o pvr_render.o pvr_rlight.o pvr_rmain.o pvr_rmisc.o pvr_rpart.o pvr_rsurf.o pvr_clip.o pvr_sprite.o pvr_studio.o pvr_triapi.o pvr_warp.o

include ../../Makefile.library