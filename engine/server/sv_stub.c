#include "common.h"
#include "pm_local.h"

const char *const clc_strings[clc_lastmsg+1] =
{
	"clc_bad",
	"clc_nop",
	"clc_move",
	"clc_stringcmd",
	"clc_delta",
	"clc_resourcelist",
	"clc_legacy_userinfo",
	"clc_fileconsistency",
	"clc_voicedata",
	"clc_cvarvalue/clc_goldsrc_hltv",
	"clc_cvarvalue2/clc_goldsrc_requestcvarvalue",
	"clc_goldsrc_requestcvarvalue2",
};

qboolean SV_Active( void )
{
	
}
int SV_GetMaxClients( void )
{
	return;
}

/*
=============
pfnNumberOfEntities

returns actual entity count
=============
*/
int GAME_EXPORT pfnNumberOfEntities( void )
{
	return 0;
}

/*
==================
Log_Printf

Prints a frag log message to the server's frag log file, console, and possible a UDP port.
==================
*/
void Log_Printf( const char *fmt, ... )
{

}

/*
=============
SV_SysError

tell the game.dll about system error
=============
*/
void SV_SysError( const char *error_string )
{

}
/*
===============
SV_Init

Only called at startup, not for each game
===============
*/
void SV_Init( void )
{

}


qboolean CRC32_MapFile( uint32_t *crcvalue, const char *filename, qboolean multiplayer )
{
	char	headbuf[1024], buffer[1024];
	int	i, num_bytes, lumplen;
	int	version, hdr_size;
	dheader_t	*header;
	dc_file_t	*f;

	if( !crcvalue ) return false;

	// always calc same checksum for singleplayer
	if( multiplayer == false )
	{
		*crcvalue = (('H'<<24)+('S'<<16)+('A'<<8)+'X');
		return true;
	}

	f = FS_Open( filename, "rb", false );
	if( !f ) return false;

	// read version number
	FS_Read( f, &version, sizeof( int ));
	FS_Seek( f, 0, SEEK_SET );

	hdr_size = sizeof( int ) + sizeof( dlump_t ) * HEADER_LUMPS;
	num_bytes = FS_Read( f, headbuf, hdr_size );

	// corrupted map ?
	if( num_bytes != hdr_size )
	{
		FS_Close( f );
		return false;
	}

	header = (dheader_t *)headbuf;

	// invalid version ?
	switch( header->version )
	{
	case Q1BSP_VERSION:
	case HLBSP_VERSION:
	case QBSP2_VERSION:
		break;
	default:
		FS_Close( f );
		return false;
	}

	CRC32_Init( crcvalue );

	for( i = LUMP_PLANES; i < HEADER_LUMPS; i++ )
	{
		lumplen = header->lumps[i].filelen;
		FS_Seek( f, header->lumps[i].fileofs, SEEK_SET );

		while( lumplen > 0 )
		{
			if( lumplen >= sizeof( buffer ))
				num_bytes = FS_Read( f, buffer, sizeof( buffer ));
			else num_bytes = FS_Read( f, buffer, lumplen );

			if( num_bytes > 0 )
			{
				lumplen -= num_bytes;
				CRC32_ProcessBuffer( crcvalue, buffer, num_bytes );
			}

			// file unexpected end ?
			if( FS_Eof( f )) break;
		}
	}

	FS_Close( f );

	return 1;
}

/*
================
SV_DrawDebugTriangles

Called from renderer for debug purposes
================
*/
void SV_DrawDebugTriangles( void )
{

}

/*
================
SV_DrawOrthoTriangles

Called from renderer for debug purposes
================
*/
void SV_DrawOrthoTriangles( void )
{

}
/*
=============
SV_Serverinfo

get server infostring
=============
*/
char *SV_Serverinfo( void )
{
	return;
}

/*
================
SV_ExecLoadLevel

State machine exec new map
================
*/
void SV_ExecLoadLevel( void )
{

}

/*
================
SV_ExecLoadGame

State machine exec load saved game
================
*/
void SV_ExecLoadGame( void )
{

}
/*
================
SV_ExecChangeLevel

State machine exec changelevel path
================
*/
void SV_ExecChangeLevel( void )
{

}
/*
==================
Host_ServerFrame

==================
*/
void Host_ServerFrame( void )
{

}
/*
=================
SV_BroadcastCommand

Sends text to all active clients
=================
*/
void SV_BroadcastCommand( const char *fmt, ... )
{

}

void Rcon_Print( host_redirect_t *rd, const char *pMsg )
{

}

void SV_ClipPMoveToEntity( physent_t *pe, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, pmtrace_t *tr )
{

}
/*
================
SV_Shutdown

Called when each game quits,
before Sys_Quit or Sys_Error
================
*/
void SV_Shutdown( const char *finalmsg )
{

}

void SV_UnloadProgs( void )
{

}

void SV_ShutdownFilter( void )
{

}

/*
==============
SV_ShutdownGame

prepare to close server
==============
*/
void SV_ShutdownGame( void )
{

}
