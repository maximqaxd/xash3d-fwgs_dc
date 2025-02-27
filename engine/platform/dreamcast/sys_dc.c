/*
sys_dc.c - DC system component
Copyright (C) 2024 maximqad
sys_win.c - posix system utils
Copyright (C) 2019 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <unistd.h> // fork
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "platform/platform.h"
#include "menu_int.h"


#define MAX_LINE_LENGTH 40
#define Y_SPACING 24
#include <dc/video.h>
#include <arch/arch.h>
#include <dc/sound/sound.h>
#include <glkos.h>

/*
 * OpenBOR - http://www.LavaLit.com
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in OpenBOR root for details.
 *
 * Copyright (c) 2004 - 2009 OpenBOR Team
 */

/*
 * This library is used for calculating how much memory is available/used.
 * Certain platforms offer physical memory statistics, we obviously wrap
 * around those functions.  For platforms where we can't retrieve this
 * information we then calculate the estimated sizes based on a few key
 * variables and symbols.  These estimated values should tolerable.......
 */

/////////////////////////////////////////////////////////////////////////////
// Libraries

#include <malloc.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/////////////////////////////////////////////////////////////////////////////
// Globals

static unsigned long systemRam = 0x00000000;
static unsigned long elfOffset = 0x00000000;
static unsigned long stackSize = 0x00000000;

/////////////////////////////////////////////////////////////////////////////
// Symbols

#if defined(_arch_dreamcast)
extern unsigned long end;
extern unsigned long start;
#define _END end
#define _START start
#else
extern unsigned long end;
extern unsigned long _start;
#define _END end
#define _START _executable_start
#endif

/////////////////////////////////////////////////////////////////////////////
//  Functions

unsigned long getFreeRam(void)
{
#if defined(_WIN32) || XBOX
	MEMORYSTATUS stat;
	memset(&stat, 0, sizeof(MEMORYSTATUS));
	stat.dwLength = sizeof(MEMORYSTATUS);
	GlobalMemoryStatus(&stat);
	return stat.dwAvailPhys - stackSize;
#elif LINUX
	struct sysinfo info;
	sysinfo(&info);
	return info.freeram - stackSize;
#else
    struct mallinfo mi = mallinfo();
    return systemRam - (mi.usmblks + stackSize);
#endif
}

void setSystemRam(void)
{
#if defined(_arch_dreamcast)
	// 16 MBytes - ELF Memory Map:
	systemRam = 0x8d000000 - 0x8c000000;
	elfOffset = 0x8c000000;
#elif PSP
	// 24 MBytes - ELF Memory Map:
	systemRam = 0x01800000 - 0x00000000;
	elfOffset = 0x00000000;
	if (getHardwareModel() == 1) systemRam += 32 * 1024 * 1024;
#elif GP2X
	// 32 MBytes - ELF Memory Map:
	systemRam = 0x02000000 - 0x00000000;
	elfOffset = 0x00000000;
	if (gp2x_init() == 2) systemRam += 32 * 1024 * 1024;
#else
	systemRam = getFreeRam();
#endif
	stackSize = (int)&_END - (int)&_START + ((int)&_START - elfOffset);
}

unsigned long getSystemRam(void)
{
	return systemRam;
}

unsigned long getUsedRam(void)
{
	return (systemRam - getFreeRam());
}

void getRamStatus(void)
{

	GLint free_mem = 0;
    GLint used_mem = 0;
    GLint free_contiguous = 0;

    // Query memory values
    glGetIntegerv(GL_FREE_TEXTURE_MEMORY_KOS, &free_mem);
    glGetIntegerv(GL_USED_TEXTURE_MEMORY_KOS, &used_mem);
    glGetIntegerv(GL_FREE_CONTIGUOUS_TEXTURE_MEMORY_KOS, &free_contiguous);
	
	Con_Printf("stack: start:%x end:%x\n", (int)&_START, (int)&_END);
	Con_Printf("System RAM - Total: %.1f MB (%d KB), Free: %.1f MB (%d KB), Used: %.1f MB (%d KB)\n",
		(float)getSystemRam() / (1024*1024),    // MB
		getSystemRam() / 1024,                   // KB
		(float)getFreeRam() / (1024*1024),      // MB
		getFreeRam() / 1024,                     // KB
		(float)getUsedRam() / (1024*1024),      // MB
		getUsedRam() / 1024);                    // KB
	Con_Printf("GLDC Texture RAM: (KB) - Free: %d, Used: %d, Free Contiguous: %d\n",
              free_mem / 1024,   
              used_mem / 1024,
              free_contiguous / 1024);
	Con_Printf("SPU: Free: %d\n",snd_mem_available());
}

//-----------------------------------------------------------------------------
extern void bfont_draw_str(void *b, uint32_t width, bool opaque, const char *str);
static void drawtext(int x, int y, char *string) {
  printf("%s\n", string);
  fflush(stdout);
  int offset = ((y * 640) + x);
  bfont_draw_str(vram_s + offset, 640, 1, string);
}

static void assert_hnd(const char *file, int line, const char *expr, const char *msg, const char *func) {
  char strbuffer[1024];

  /* Reset video mode, clear screen */
  vid_set_mode(DM_640x480, PM_RGB565);
  vid_empty();

  /* Display the error message on screen */
  drawtext(32, 64, "Xash3D - Assertion failure");

  sprintf(strbuffer, " Location: %s, line %d (%s)", file, line, func);
  drawtext(32, 96, strbuffer);

  sprintf(strbuffer, "Assertion: %s", expr);
  drawtext(32, 128, strbuffer);

  sprintf(strbuffer, "  Message: %s", msg);
  drawtext(32, 160, strbuffer);
}
#if XASH_MESSAGEBOX == MSGBOX_KOS
void Platform_MessageBox(const char *title, const char *message, qboolean parentMainWindow)
{
    char line[MAX_LINE_LENGTH + 1];
    const char *msg = message;
    int y = 96;
    int len = 0;
    int i;

    drawtext(32, 64, title);

    while (*msg)
    {
        // Copy characters until we hit max length or end of string
        for (i = 0; i < MAX_LINE_LENGTH && msg[i] && msg[i] != '\n'; i++)
            line[i] = msg[i];
        
        line[i] = '\0';
        
        drawtext(32, y, line);
        y += Y_SPACING;
        
        msg += i;
        if (*msg == '\n') 
            msg++;
    }
}
#endif // XASH_MESSAGEBOX == MSGBOX_KOS
static qboolean Sys_FindExecutable( const char *baseName, char *buf, size_t size )
{
	char *envPath;
	char *part;
	size_t length;
	size_t baseNameLength;
	size_t needTrailingSlash;

	if( !baseName || !baseName[0] )
		return false;

	envPath = getenv( "PATH" );
	if( !COM_CheckString( envPath ) )
		return false;

	baseNameLength = Q_strlen( baseName );
	while( *envPath )
	{
		part = Q_strchr( envPath, ':' );
		if( part )
			length = part - envPath;
		else
			length = Q_strlen( envPath );

		if( length > 0 )
		{
			needTrailingSlash = ( envPath[length - 1] == '/' ) ? 0 : 1;
			if( length + baseNameLength + needTrailingSlash < size )
			{
				string temp;

				Q_strncpy( temp, envPath, length + 1 );
				Q_snprintf( buf, size, "%s%s%s",
					temp, needTrailingSlash ? "/" : "", baseName );

				if( access( buf, X_OK ) == 0 )
					return true;
			}
		}

		envPath += length;
		if( *envPath == ':' )
			envPath++;
	}
	return false;
}

void Posix_Daemonize( void )
{
	if( Sys_CheckParm( "-daemonize" ))
	{
#if XASH_POSIX && defined(_POSIX_VERSION) && !defined(XASH_MOBILE_PLATFORM)
		pid_t daemon;

		daemon = fork();

		if( daemon < 0 )
		{
			Host_Error( "fork() failed: %s\n", strerror( errno ) );
		}

		if( daemon > 0 )
		{
			// parent
			Con_Reportf( "Child pid: %i\n", daemon );
			exit( 0 );
		}
		else
		{
			// don't be closed by parent
			if( setsid() < 0 )
			{
				Host_Error( "setsid() failed: %s\n", strerror( errno ) );
			}

			// set permissions
			umask( 0 );

			// engine will still use stdin/stdout,
			// so just redirect them to /dev/null
			close( STDIN_FILENO );
			close( STDOUT_FILENO );
			close( STDERR_FILENO );
			open("/dev/null", O_RDONLY); // becomes stdin
			open("/dev/null", O_RDWR); // stdout
			open("/dev/null", O_RDWR); // stderr

			// fallthrough
		}
#elif defined(XASH_MOBILE_PLATFORM)
		Sys_Error( "Can't run in background on mobile platforms!" );
#else
		Sys_Error( "Daemonize not supported on this platform!" );
#endif
	}

}

#if XASH_TIMER == TIMER_KOS
double Platform_DoubleTime( void )
{
	struct timespec ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double) ts.tv_sec + (double) ts.tv_nsec/1000000000.0;
}

#endif // XASH_TIMER == TIMER_POSIX