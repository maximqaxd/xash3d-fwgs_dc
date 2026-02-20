/*
softreboot_dc.h - Dreamcast soft reboot handoff (heap defrag)
Copyright (C) 2026

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#pragma once

#if XASH_DREAMCAST

#include <stddef.h>
#include <stdint.h>

/* One guard page for descriptor metadata near top of RAM. */
#define DC_SOFTREBOOT_DESC_BYTES  0x1000u

/* 'SBRT' */
#define DC_SOFTREBOOT_MAGIC       0x54425253u
#define DC_SOFTREBOOT_VERSION     2u

/* Two-phase commit marker: set to this value only after payload is complete. */
#define DC_SOFTREBOOT_COMMIT      0xC001BEEFu

/* Payload layout flags */
#define DC_SOFTREBOOT_F_HAS_HL1   (1u << 0)
#define DC_SOFTREBOOT_F_HAS_HL2   (1u << 1)
#define DC_SOFTREBOOT_F_HAS_HL3   (1u << 2)
#define DC_SOFTREBOOT_F_HAS_IMAGE (1u << 3)
#define DC_SOFTREBOOT_F_HAS_GS    (1u << 4)

typedef struct dc_softreboot_desc_s
{
	uint32_t magic;
	uint16_t version;
	uint16_t header_size;

	uint32_t flags;
	uint32_t commit; /* 0 until ready, then DC_SOFTREBOOT_COMMIT */

	/* Next level info (for changelevel resume) */
	char prev_map[32];
	char next_map[32];
	char startspot[32];
	uint32_t background;

	/* Payload blobs in reserved RAM */
	uint32_t hl1_addr;
	uint32_t hl1_len;
	uint32_t hl1_crc32;

	uint32_t hl2_addr;
	uint32_t hl2_len;
	uint32_t hl2_crc32;

	uint32_t hl3_addr;
	uint32_t hl3_len;
	uint32_t hl3_crc32;

	uint32_t gs_addr;
	uint32_t gs_len;
	uint32_t gs_crc32;

	uint32_t image_addr;
	uint32_t image_len;
	uint32_t image_crc32;
} dc_softreboot_desc_t;

/* Initialize reserved RAM window for payloads (call early). */
void DC_SoftReboot_MemReserveInit( void );
/* Roll back reservation and restore heap top (for fallback path). */
void DC_SoftReboot_MemReserveShutdown( void );

/* Get pointer to persistent descriptor in low RAM. */
dc_softreboot_desc_t *DC_SoftReboot_Desc( void );

/* Clear/disable pending handoff. */
void DC_SoftReboot_Clear( void );

/* Allocate from the reserved payload window (returns NULL on overflow). */
void *DC_SoftReboot_ReserveAlloc( size_t size, size_t align );

/* Reset reserved window allocator (keeps the reservation). */
void DC_SoftReboot_ReserveReset( void );

/* Load a file into reserved window (returns false on failure). */
qboolean DC_SoftReboot_LoadFileToReserved( const char *path, uint32_t *out_addr, uint32_t *out_len );

/* Execute a previously loaded image (does not return). */
void DC_SoftReboot_ExecImage( uint32_t image_addr, uint32_t image_len ) __noreturn;

#endif /* XASH_DREAMCAST */

