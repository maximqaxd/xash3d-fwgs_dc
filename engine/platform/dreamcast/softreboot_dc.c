/*
softreboot_dc.c - Dreamcast soft reboot (heap defrag)
Copyright (C) 2026 maximqad

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "platform/platform.h"

#if XASH_DREAMCAST

#include "platform/dreamcast/softreboot_dc.h"

#include <arch/cache.h>
#include <arch/exec.h>
#include <arch/arch.h>
#include <arch/stack.h>
#include <kos/fs.h>
#include <string.h>

/* Reserved payload window size (tunable). */
#ifndef DC_SOFTREBOOT_RESERVE_BYTES
#define DC_SOFTREBOOT_RESERVE_BYTES (6u * 1024u * 1024u)
#endif

static uint32_t g_softreboot_payload_base = 0;
static uint32_t g_softreboot_payload_size = 0;
static uint32_t g_softreboot_payload_used = 0;
static uint32_t g_softreboot_old_mem_top = 0;
static qboolean g_softreboot_reserved = false;

static uint32_t DC_SoftReboot_DescAddr( void )
{
	uint32_t top = g_softreboot_reserved && g_softreboot_old_mem_top ? g_softreboot_old_mem_top : (uint32_t)_arch_mem_top;
	uint32_t safe_top = top - (uint32_t)THD_KERNEL_STACK_SIZE;

	/* Keep descriptor in a dedicated page right below kernel stack area. */
	return (safe_top - (uint32_t)DC_SOFTREBOOT_DESC_BYTES) & ~31u;
}

static uint32_t align_up_u32( uint32_t v, uint32_t a )
{
	return (v + (a - 1u)) & ~(a - 1u);
}

void DC_SoftReboot_MemReserveInit( void )
{
	uint32_t desc_addr = DC_SoftReboot_DescAddr();
	uint32_t base;

	if( g_softreboot_reserved )
		return;

	/* Keep it aligned for DMA/caches convenience and below descriptor page. */
	base = (desc_addr - (uint32_t)DC_SOFTREBOOT_RESERVE_BYTES) & ~31u;

	/* If the binary/heap ever grew too large, just disable the feature. */
	if( base < 0x8c100000u )
	{
		g_softreboot_payload_base = 0;
		g_softreboot_payload_size = 0;
		g_softreboot_payload_used = 0;
		g_softreboot_reserved = false;
		return;
	}

	g_softreboot_payload_base = base;
	g_softreboot_payload_size = (uint32_t)DC_SOFTREBOOT_RESERVE_BYTES;
	g_softreboot_payload_used = 0;

	/*
	 * Prevent future sbrk/malloc from ever reaching the reserved window.
	 * This is supported only on patched toolchains where _arch_mem_top is a variable.
	 */
#if defined(__KOS_GCC_32MB__) || (__KOS_GCC_PATCHLEVEL__ >= 2025062800)
	g_softreboot_old_mem_top = _arch_mem_top;
	_arch_mem_top = base;
#endif
	g_softreboot_reserved = true;
}

void DC_SoftReboot_MemReserveShutdown( void )
{
#if defined(__KOS_GCC_32MB__) || (__KOS_GCC_PATCHLEVEL__ >= 2025062800)
	if( g_softreboot_reserved && g_softreboot_old_mem_top )
		_arch_mem_top = g_softreboot_old_mem_top;
#endif
	g_softreboot_payload_base = 0;
	g_softreboot_payload_size = 0;
	g_softreboot_payload_used = 0;
	g_softreboot_old_mem_top = 0;
	g_softreboot_reserved = false;
}

dc_softreboot_desc_t *DC_SoftReboot_Desc( void )
{
	return (dc_softreboot_desc_t *)(uintptr_t)DC_SoftReboot_DescAddr();
}

void DC_SoftReboot_Clear( void )
{
	dc_softreboot_desc_t *d = DC_SoftReboot_Desc();
	memset( d, 0, sizeof( *d ));
}

void DC_SoftReboot_ReserveReset( void )
{
	g_softreboot_payload_used = 0;
}

void *DC_SoftReboot_ReserveAlloc( size_t size, size_t align )
{
	uint32_t a = (uint32_t)( align ? align : 4u );
	uint32_t cur, next, end;

	if( !g_softreboot_payload_base || !g_softreboot_payload_size || !size )
		return NULL;

	/* align allocation start */
	cur = g_softreboot_payload_base + g_softreboot_payload_used;
	cur = align_up_u32( cur, a );

	next = cur + (uint32_t)size;
	end = g_softreboot_payload_base + g_softreboot_payload_size;

	if( next > end )
		return NULL;

	g_softreboot_payload_used = next - g_softreboot_payload_base;
	return (void *)(uintptr_t)cur;
}

qboolean DC_SoftReboot_LoadFileToReserved( const char *path, uint32_t *out_addr, uint32_t *out_len )
{
	file_t f;
	size_t size;
	ssize_t rv;
	void *dst;
	const byte *hdr;

	if( !path || !out_addr || !out_len )
		return false;

	f = fs_open( path, O_RDONLY );
	if( f < 0 )
	{
		Host_Error( "%s: open failed: %s\n", __func__, path );
		return false;
	}

	size = fs_total( f );
	if( size <= 0 || size > (size_t)g_softreboot_payload_size )
	{
		Host_Error( "%s: bad size for %s: %u (reserve=%u)\n", __func__,
			path, (uint)size, g_softreboot_payload_size );
		fs_close( f );
		return false;
	}

	dst = DC_SoftReboot_ReserveAlloc( size, 32 );
	if( !dst )
	{
		Host_Error( "%s: reserve alloc failed for %s (%u bytes)\n", __func__, path, (uint)size );
		fs_close( f );
		return false;
	}

	rv = fs_read( f, dst, size );
	fs_close( f );

	if( rv != (ssize_t)size )
	{
		Host_Error( "%s: read failed for %s: got %d want %u\n", __func__, path, (int)rv, (uint)size );
		return false;
	}

	/* Make sure the source buffer is coherent for arch_exec. */
	dcache_flush_range( (uintptr_t)dst, size );
	hdr = (const byte *)dst;
	Con_DPrintf( "%s: loaded %s -> %08X (%u bytes), hdr=%02X %02X %02X %02X\n", __func__,
		path, (uint)(uintptr_t)dst, (uint)size, hdr[0], hdr[1], hdr[2], hdr[3] );

	*out_addr = (uint32_t)(uintptr_t)dst;
	*out_len = (uint32_t)size;
	return true;
}

void DC_SoftReboot_ExecImage( uint32_t image_addr, uint32_t image_len )
{
	const byte *hdr = (const byte *)(uintptr_t)image_addr;
	Con_DPrintf( "%s: arch_exec image=%08X len=%u hdr=%02X %02X %02X %02X\n", __func__,
		(uint)image_addr, (uint)image_len, hdr[0], hdr[1], hdr[2], hdr[3] );
	arch_exec( (const void *)(uintptr_t)image_addr, image_len );
}

#endif /* XASH_DREAMCAST */

