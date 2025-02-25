TARGET = libref_gl.a
DEFINES = -D_KOS_=1 -D_SH4_=1 -DXASH_GAMEDIR="valve" -DXASH_GL_STATIC=1 -DXASH_LOW_MEMORY=2 -DALLOCA_H=<malloc.h> -DSTDINT_H=<stdint.h> -DHAVE_TGMATH_H=0 -DHAVE_STRNICMP=1 -DHAVE_STRICMP=1 -DXASH_STATIC_LIBS=1 -DREF_DLL=1
INCLUDES = -I$(KOS_PORTS)/include/GL -I../../engine/platform/dreamcast -I../../filesystem -I../../public -I../../common -I../../pm_shared -I../../engine -I../../ref/gl -I../../engine/common -I../../engine/common/imagelib

KOS_CFLAGS +=  $(INCLUDES) $(DEFINES) -Os -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-strict-aliasing -mrelax -fno-asynchronous-unwind-tables -fno-stack-protector
KOS_CPPFLAGS += $(INCLUDES) $(DEFINES) -fno-exceptions -std=gnu++11 -fpermissive -MMD -MP  $(CFLAGS)
OBJS = gl_backend.o gl_beams.o gl_context.o gl_cull.o gl_decals.o link_helper.o gl_draw.o gl_frustum.o gl_image.o gl_opengl.o gl_rlight.o gl_rmath.o gl_rmain.o gl_rmisc.o gl_rpart.o gl_rsurf.o gl_sprite.o gl_studio.o gl_triapi.o gl_warp.o
gl_rmath.o: FLAGS = -O3 -fomit-frame-pointer -fno-pie -fno-common -fno-strict-aliasing -fno-stack-protector -mrelax -ffunction-sections -fdata-sections -fno-exceptions -flto=auto

include ../../Makefile.library