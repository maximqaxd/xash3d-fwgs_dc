/*
pvr_image.c - texture uploading and processing
Copyright (C) 2025 maximqad

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdarg.h>
#include "pvr_local.h"
#include "crclib.h"

#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)
#define TEXTURE_SIZE_MIN	8

static gl_texture_t		gl_textures[MAX_TEXTURES];
static gl_texture_t*	gl_texturesHashTable[TEXTURES_HASH_SIZE];
static uint		gl_numTextures;
static uint		vq_codebook_sz = 2048;
static int		next_texture_id = 1;		// Next PVR texture ID (start at 1, 0 = unused)

static byte    dottexture[8][8] =
{
	  {0,1,1,0,0,0,0,0},
	  {1,1,1,1,0,0,0,0},
	  {1,1,1,1,0,0,0,0},
	  {0,1,1,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
};


#define IsLightMap( tex )	( FBitSet(( tex )->flags, TF_ATLAS_PAGE ))
/*
=================
R_GetTexture

acess to array elem
=================
*/
gl_texture_t *R_GetTexture( unsigned int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return &gl_textures[texnum];
}

/*
=================
GL_TargetToString
=================
*/
const char *GL_TargetToString()
{
	return "2D";
}

/*
=================
GL_Bind
=================
*/
void GL_Bind( int tmu, unsigned int texnum )
{
	gl_texture_t	*texture;

	// missed or invalid texture?
	if( texnum <= 0 || texnum >= MAX_TEXTURES )
	{
		if( texnum != 0 )
			gEngfuncs.Con_DPrintf( S_ERROR "%s: invalid texturenum %d\n", __func__, texnum );
		texnum = tr.defaultTexture;
	}

	texture = &gl_textures[texnum];

	// Check if already bound
	if( glState.currentTextures == texture->texnum )
		return;

	// Update state tracking
	glState.currentTextures = texture->texnum;
	glState.currentTexturesIndex = texnum;
}

qboolean GL_TextureFilteringEnabled( const gl_texture_t *tex )
{
	if( FBitSet( tex->flags, TF_NEAREST ))
		return false;

	if( FBitSet( tex->flags, TF_DEPTHMAP ))
		return true;

	if( FBitSet( tex->flags, TF_NOMIPMAP ) || tex->numMips <= 1 )
	{
		if( FBitSet( tex->flags, TF_ATLAS_PAGE ))
			return gl_lightmap_nearest.value == 0.0f;

		if( FBitSet( tex->flags, TF_ALLOW_NEAREST ))
			return gl_texture_nearest.value == 0.0f;

		return true;
	}

	return gl_texture_nearest.value == 0.0f;
}

/*
=================
GL_ApplyTextureParams
=================
*/
void GL_ApplyTextureParams( gl_texture_t *tex )
{
  // no-op params would be applied in txrheader
}

/*
=================
GL_UpdateTextureParams
=================
*/
static void GL_UpdateTextureParams( int iTexture )
{
	  // no-op params would be applied in txrheader

}

/*
=================
R_SetTextureParameters
=================
*/
void R_SetTextureParameters( void )
{
  // no-op params would be applied in txrheader
}

/*
================
GL_CalcTextureSamples
================
*/
static int GL_CalcTextureSamples( int flags )
{
	if( FBitSet( flags, IMAGE_HAS_COLOR ))
		return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 4 : 3;
	return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 2 : 1;
}

// VQ helpers:
// - Codebook is always 2048 bytes (RGB565/ARGB1555/ARGB4444, 256 entries * 4 texels * 2 bytes)
// - Index stream is 1 byte per 2x2 block => (w*h)/4 per level
// - For VQ mipmaps on PVR2, mip chain must stop at 8x8 minimum.
static size_t GL_CalcVQIndexBytes( int width, int height, qboolean mipmaps, int *out_levels )
{
	int mw = width;
	int mh = height;
	size_t total = (size_t)(mw * mh) / 4; // base level
	int levels = 1;

	if( mipmaps )
	{
		while( mw > TEXTURE_SIZE_MIN || mh > TEXTURE_SIZE_MIN )
		{
			mw = ( mw > TEXTURE_SIZE_MIN ) ? ( mw >> 1 ) : TEXTURE_SIZE_MIN;
			mh = ( mh > TEXTURE_SIZE_MIN ) ? ( mh >> 1 ) : TEXTURE_SIZE_MIN;
			total += (size_t)(mw * mh) / 4;
			levels++;
		}
	}

	if( out_levels ) *out_levels = levels;
	return total;
}

/*
==================
GL_CalcImageSize
==================
*/
static size_t GL_CalcImageSize( pixformat_t format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case PF_LUMINANCE:
		size = width * height * depth;
		break;
	case PF_RGB_24:
	case PF_BGR_24:
		size = width * height * depth * 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		size = width * height * depth * 4;
		break;
	case PF_DXT1:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
	case PF_BC6H_SIGNED:
	case PF_BC6H_UNSIGNED:
	case PF_BC7_UNORM:
	case PF_BC7_SRGB:
	case PF_ATI2:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	case PF_RGB_5650_TWID:
	case PF_RGB_5650:
		size = ((width + 3) & ~3) * ((height + 3) & ~3) * 2;
		break;
	case PF_VQ_ARGB_4444:
	case PF_VQ_RGB_5650:
		size = vq_codebook_sz + ((width * height) / 4);
		break;
	case PF_ARGB_1555:
		size = width * height * depth * 2;
		break;
	case PF_VQ_MIPMAP_ARGB_1555:
	case PF_VQ_MIPMAP_RGB_5650:
		// VQ_MIPMAP: codebook + base level + all mip levels down to 8x8
		size = vq_codebook_sz + GL_CalcVQIndexBytes( width, height, true, NULL );
		break;
	case PF_VQ_ARGB_1555:
		size = vq_codebook_sz + ((width * height) / 4);
		break; 
	case PF_ARGB_4444:
		size = width * height * depth * 2;
		break;
	case PF_SMALL_VQ_RGB_5650:
		size = 256 + ((width * height) / 4);
		break;
	}

	return size;
}

/*
==================
GL_CalcTextureSize
==================
*/
static size_t GL_CalcTextureSize( int format, int width, int height, int depth )
{
	size_t	size = 0;
	int	main_size, mip1_size, mip2_size, mip3_size;

	// check the depth error
	depth = Q_max( 1, depth );

	// Extract base format (bits 27-29 contain the format)
	uint32_t base_format = format & 0x38000000;

	switch( base_format )
	{
	case PVR_TXRFMT_ARGB1555:
		if( format & PVR_TXRFMT_VQ_ENABLE )
		{
			// VQ compressed ARGB1555
			size = vq_codebook_sz + ((width * height) / 4);
		}
		else
		{
			// Uncompressed ARGB1555: 16 bits = 2 bytes per pixel
			// For non-twiddled textures, use exact size
			size = width * height * depth * 2;
		}
		break;

	case PVR_TXRFMT_RGB565:
		if( format & PVR_TXRFMT_VQ_ENABLE )
		{
			// VQ compressed RGB565
			size = vq_codebook_sz + ((width * height) / 4);
		}
		else
		{
			// Uncompressed RGB565: 16 bits = 2 bytes per pixel
			// For non-twiddled textures, use exact size (no alignment needed)
			size = width * height * depth * 2;
		}
		break;

	case PVR_TXRFMT_ARGB4444:
		if( format & PVR_TXRFMT_VQ_ENABLE )
		{
			// VQ compressed ARGB4444
			size = vq_codebook_sz + ((width * height) / 4);
		}
		else
		{
			// Uncompressed ARGB4444: 16 bits = 2 bytes per pixel
			size = width * height * depth * 2;
		}
		break;

	default:
		gEngfuncs.Host_Error( "%s: bad texture internal format (0x%08x)\n", __func__, format );
		break;
	}

	return size;
}

static int GL_CalcMipmapCount( gl_texture_t *tex, qboolean haveBuffer )
{
	int	width, height;
	int	mipcount;

	Assert( tex != NULL );

	if( !haveBuffer )
		return 1;

	// generate mip-levels by user request
	if( FBitSet( tex->flags, TF_NOMIPMAP ))
		return 1;

	// mip-maps can't exceeds 4
	for( mipcount = 1; mipcount < 4; mipcount++ )
	{

		width = Q_max( TEXTURE_SIZE_MIN, ( tex->width >> mipcount ));
		height = Q_max( TEXTURE_SIZE_MIN, ( tex->height >> mipcount ));

		if( width == TEXTURE_SIZE_MIN && height == TEXTURE_SIZE_MIN )
			break;
	}
		
	return mipcount;
}

/*
================
GL_SetTextureDimensions
================
*/
static void GL_SetTextureDimensions( gl_texture_t *tex, int width, int height, int depth )
{
	int	maxTextureSize = glConfig.max_2d_texture_size;
	int	maxDepthSize = 1;

	Assert( tex != NULL );
	if( maxTextureSize <= 0 ) 
		maxTextureSize = 1024; // safe fallback for PVR

	// store original sizes
	tex->srcWidth = width;
	tex->srcHeight = height;

	int	step = (int)gl_round_down.value;
	int	scaled_width, scaled_height;

	for( scaled_width = 1; scaled_width < width; scaled_width <<= 1 );

	if( step > 0 && width < scaled_width && ( step == 1 || ( scaled_width - width ) > ( scaled_width >> step )))
		scaled_width >>= 1;

	for( scaled_height = 1; scaled_height < height; scaled_height <<= 1 );

	if( step > 0 && height < scaled_height && ( step == 1 || ( scaled_height - height ) > ( scaled_height >> step )))
		scaled_height >>= 1;

	width = scaled_width;
	height = scaled_height;
	
	if( width > maxTextureSize || height > maxTextureSize || depth > maxDepthSize )
	{
		while( width > maxTextureSize || height > maxTextureSize )
		{
			width >>= 1;
			height >>= 1;
		}
	}

	// set the texture dimensions
	tex->width = Q_max( TEXTURE_SIZE_MIN, width );
	tex->height = Q_max( TEXTURE_SIZE_MIN, height );
	tex->depth = Q_max( 1, depth );
}
/*
===============
GL_SetTextureTarget
===============
*/
static void GL_SetTextureTarget( gl_texture_t *tex, rgbdata_t *pic )
{
	Assert( pic != NULL );
	Assert( tex != NULL );

	// correct depth size
	pic->depth = Q_max( 1, pic->depth );
	tex->numMips = 0; // begin counting

	// correct mip count
	pic->numMips = Q_max( 1, pic->numMips );
}

/*
=================
GL_ConvertRGBA32ToARGB1555
Convert RGBA32 to ARGB1555 format
=================
*/
static void GL_ConvertRGBA32ToARGB1555( const byte *src, uint16_t *dst, int width, int height )
{
	int i, pixels = width * height;
	const byte *s = src;
	uint16_t *d = dst;

	for( i = 0; i < pixels; i++, s += 4, d++ )
	{
		// Source is RGBA bytes: s[0]=R, s[1]=G, s[2]=B, s[3]=A
		// Convert to ARGB1555: 1 bit alpha, 5 bits each for R, G, B
		uint16_t r = (uint16_t)(s[0] >> 3);  // 5 bits
		uint16_t g = (uint16_t)(s[1] >> 3);  // 5 bits
		uint16_t b = (uint16_t)(s[2] >> 3);  // 5 bits
		uint16_t a = (s[3] > 127) ? 1 : 0;   // 1 bit alpha
		// Pack as: A(15) R(14-10) G(9-5) B(4-0)
		*d = (a << 15) | (r << 10) | (g << 5) | b;
	}
}

/*
=================
GL_ConvertBGRA32ToARGB1555
Convert BGRA32 to ARGB1555 format (for BGRA source data)
=================
*/
static void GL_ConvertBGRA32ToARGB1555( const byte *src, uint16_t *dst, int width, int height )
{
	int i, pixels = width * height;
	const byte *s = src;
	uint16_t *d = dst;

	for( i = 0; i < pixels; i++, s += 4, d++ )
	{
		// Source is BGRA: s[0]=B, s[1]=G, s[2]=R, s[3]=A
		uint16_t r = s[2] >> 3;
		uint16_t g = s[1] >> 3;
		uint16_t b = s[0] >> 3;
		uint16_t a = (s[3] > 127) ? 1 : 0;
		*d = (a << 15) | (r << 10) | (g << 5) | b;
	}
}

/*
=================
GL_ConvertRGBA32ToRGB565
Convert RGBA32 to RGB565 format
=================
*/
static void GL_ConvertRGBA32ToRGB565( const byte *src, uint16_t *dst, int width, int height )
{
	int i, pixels = width * height;
	const byte *s = src;
	uint16_t *d = dst;

	for( i = 0; i < pixels; i++, s += 4, d++ )
	{
		// Source is RGBA bytes: s[0]=R, s[1]=G, s[2]=B, s[3]=A
		// Convert to RGB565: 5 bits R, 6 bits G, 5 bits B (no alpha)
		uint16_t r = (uint16_t)(s[0] >> 3);  // 5 bits
		uint16_t g = (uint16_t)(s[1] >> 2);  // 6 bits
		uint16_t b = (uint16_t)(s[2] >> 3);  // 5 bits
		// Pack as: R(15-11) G(10-5) B(4-0)
		*d = (r << 11) | (g << 5) | b;
	}
}

/*
=================
GL_ConvertBGRA32ToRGB565
Convert BGRA32 to RGB565 format (for BGRA source data)
=================
*/
static void GL_ConvertBGRA32ToRGB565( const byte *src, uint16_t *dst, int width, int height )
{
	int i, pixels = width * height;
	const byte *s = src;
	uint16_t *d = dst;

	for( i = 0; i < pixels; i++, s += 4, d++ )
	{
		// Source is BGRA: s[0]=B, s[1]=G, s[2]=R, s[3]=A
		uint16_t r = s[2] >> 3;
		uint16_t g = s[1] >> 2;
		uint16_t b = s[0] >> 3;
		*d = (r << 11) | (g << 5) | b;
	}
}

/*
=================
GL_ConvertRGBA32ToARGB4444
Convert RGBA32 to ARGB4444 format
=================
*/
static void GL_ConvertRGBA32ToARGB4444( const byte *src, uint16_t *dst, int width, int height )
{
	int i, pixels = width * height;
	const byte *s = src;
	uint16_t *d = dst;

	for( i = 0; i < pixels; i++, s += 4, d++ )
	{
		uint16_t a = s[3] >> 4;
		uint16_t r = s[0] >> 4;
		uint16_t g = s[1] >> 4;
		uint16_t b = s[2] >> 4;
		*d = (a << 12) | (r << 8) | (g << 4) | b;
	}
}

/*
===============
GL_SetTextureFormat
===============
*/
static void GL_SetTextureFormat( gl_texture_t *tex, pixformat_t format, int channelMask )
{
	qboolean	haveColor = ( channelMask & IMAGE_HAS_COLOR );
	qboolean	haveAlpha = ( channelMask & IMAGE_HAS_ALPHA );

	Assert( tex != NULL );

	if( ImageCompressed( format ))
	{
		// Handle compressed formats
		switch( format )
		{
		case PF_VQ_ARGB_4444: 
			// VQ textures should be twiddled for correct sampling on PVR
			tex->format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_TWIDDLED; 
			break;
		case PF_VQ_ARGB_1555: 
			tex->format = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_TWIDDLED; 
			break;
		case PF_VQ_RGB_5650: 
			tex->format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_TWIDDLED; 
			break;
		case PF_SMALL_VQ_RGB_5650:
			// Small VQ uses a smaller codebook and is stored non-twiddled in KOS file formats.
			// KOS examples treat "SMALL VQ" as VQ_ENABLE | NONTWIDDLED.
			tex->format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_NONTWIDDLED;
			break;
		case PF_VQ_MIPMAP_RGB_5650: 
			// Note: KOS does not expose a PVR_TXRFMT_MIPMAP flag; mipmapping is enabled via
			// polygon header setup (filter/mipmap_en). We currently upload only level 0,
			// so keep format as-is and handle mipmaps later in header compilation.
			tex->format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_TWIDDLED; 
			break;
		case PF_VQ_MIPMAP_ARGB_1555: 
			tex->format = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_TWIDDLED; 
			break;
		default:
			// Unsupported compressed format, fall back to uncompressed
			format = PF_RGBA_32;
			break;
		}
	}
	
	// Handle uncompressed formats or fallback
	if( !ImageCompressed( format ) || tex->format == 0 )
	{
		if( haveAlpha )
		{
			// Quake palette cutouts (SURF_TRANSPARENT / "{...") are effectively alpha-tested.
			// Use 1-bit alpha (ARGB1555) so punch-through polys work reliably.
			if( FBitSet( tex->flags, TF_QUAKEPAL ))
				tex->format = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED;
			else
				// Use ARGB4444 for smooth alpha (fonts, sprites, UI)
				tex->format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
		}
		else
		{
			// Use RGB565 for textures without alpha
			tex->format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
		}
	}
}

/*
=================
GL_ResampleTexture

Assume input buffer is RGBA
=================
*/
byte *GL_ResampleTexture( const byte *source, int inWidth, int inHeight, int outWidth, int outHeight, qboolean isNormalMap )
{
	uint		frac, fracStep;
	uint		*in = (uint *)source;
	uint		p1[0x1000], p2[0x1000];
	byte		*pix1, *pix2, *pix3, *pix4;
	uint		*out, *inRow1, *inRow2;
	static byte	*scaledImage = NULL;	// pointer to a scaled image
	vec3_t		normal;
	int		i, x, y;

	if( !source ) return NULL;

	scaledImage = Mem_Realloc( r_temppool, scaledImage, outWidth * outHeight * 4 );
	fracStep = inWidth * 0x10000 / outWidth;
	out = (uint *)scaledImage;

	frac = fracStep >> 2;
	for( i = 0; i < outWidth; i++ )
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	frac = (fracStep >> 2) * 3;
	for( i = 0; i < outWidth; i++ )
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	if( isNormalMap )
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				normal[0] = MAKE_SIGNED( pix1[0] ) + MAKE_SIGNED( pix2[0] ) + MAKE_SIGNED( pix3[0] ) + MAKE_SIGNED( pix4[0] );
				normal[1] = MAKE_SIGNED( pix1[1] ) + MAKE_SIGNED( pix2[1] ) + MAKE_SIGNED( pix3[1] ) + MAKE_SIGNED( pix4[1] );
				normal[2] = MAKE_SIGNED( pix1[2] ) + MAKE_SIGNED( pix2[2] ) + MAKE_SIGNED( pix3[2] ) + MAKE_SIGNED( pix4[2] );

				if( !VectorNormalizeLength( normal ))
					VectorSet( normal, 0.5f, 0.5f, 1.0f );

				((byte *)(out+x))[0] = 128 + (byte)(127.0f * normal[0]);
				((byte *)(out+x))[1] = 128 + (byte)(127.0f * normal[1]);
				((byte *)(out+x))[2] = 128 + (byte)(127.0f * normal[2]);
				((byte *)(out+x))[3] = 255;
			}
		}
	}
	else
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				((byte *)(out+x))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
				((byte *)(out+x))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
				((byte *)(out+x))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
				((byte *)(out+x))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
			}
		}
	}

	return scaledImage;
}

/*
=================
GL_BoxFilter3x3

box filter 3x3
=================
*/
static void GL_BoxFilter3x3( byte *out, const byte *in, int w, int h, int x, int y )
{
	int		r = 0, g = 0, b = 0, a = 0;
	int		count = 0, acount = 0;
	int		i, j, u, v;
	const byte	*pixel;

	for( i = 0; i < 3; i++ )
	{
		u = ( i - 1 ) + x;

		for( j = 0; j < 3; j++ )
		{
			v = ( j - 1 ) + y;

			if( u >= 0 && u < w && v >= 0 && v < h )
			{
				pixel = &in[( u + v * w ) * 4];

				if( pixel[3] != 0 )
				{
					r += pixel[0];
					g += pixel[1];
					b += pixel[2];
					a += pixel[3];
					acount++;
				}
			}
		}
	}

	if(  acount == 0 )
		acount = 1;

	out[0] = r / acount;
	out[1] = g / acount;
	out[2] = b / acount;
//	out[3] = (int)( SimpleSpline( ( a / 12.0f ) / 255.0f ) * 255 );
}

/*
=================
GL_ApplyFilter

Apply box-filter to 1-bit alpha
=================
*/
static byte *GL_ApplyFilter( const byte *source, int width, int height )
{
	byte	*in = (byte *)source;
	byte	*out = (byte *)source;
	int	i;

	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) || glConfig.max_multisamples > 1 )
		return in;

	for( i = 0; source && i < width * height; i++, in += 4 )
	{
		if( in[0] == 0 && in[1] == 0 && in[2] == 0 && in[3] == 0 )
			GL_BoxFilter3x3( in, source, width, height, i % width, i / width );
	}

	return out;
}

/*
=================
GL_BuildMipMap

Operates in place, quartering the size of the texture
=================
*/
static void GL_BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags )
{
	byte	*out = in;
	int	instride = ALIGN( srcWidth * 4, 1 );
	int	mipWidth, mipHeight, outpadding;
	int	row, x, y, z;
	vec3_t	normal;

	if( !in ) return;

	mipWidth = Q_max( 1, ( srcWidth >> 1 ));
	mipHeight = Q_max( 1, ( srcHeight >> 1 ));
	outpadding = ALIGN( mipWidth * 4, 1 ) - mipWidth * 4;
	row = srcWidth << 2;

	if( FBitSet( flags, TF_ALPHACONTRAST ))
	{
		memset( in, mipWidth, mipWidth * mipHeight * 4 );
		return;
	}

	// move through all layers
	for( z = 0; z < srcDepth; z++ )
	{
		if( FBitSet( flags, TF_NORMALMAP ))
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( in[row+4] )
						+ MAKE_SIGNED( next[row+0] ) + MAKE_SIGNED( next[row+4] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( in[row+5] )
						+ MAKE_SIGNED( next[row+1] ) + MAKE_SIGNED( next[row+5] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( in[row+6] )
						+ MAKE_SIGNED( next[row+2] ) + MAKE_SIGNED( next[row+6] );
					}
					else
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( next[row+0] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( next[row+1] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( next[row+2] );
					}

					if( !VectorNormalizeLength( normal ))
						VectorSet( normal, 0.5f, 0.5f, 1.0f );

					out[0] = 128 + (byte)(127.0f * normal[0]);
					out[1] = 128 + (byte)(127.0f * normal[1]);
					out[2] = 128 + (byte)(127.0f * normal[2]);
					out[3] = 255;
				}
			}
		}
		else
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						out[0] = (in[row+0] + in[row+4] + next[row+0] + next[row+4]) >> 2;
						out[1] = (in[row+1] + in[row+5] + next[row+1] + next[row+5]) >> 2;
						out[2] = (in[row+2] + in[row+6] + next[row+2] + next[row+6]) >> 2;
						out[3] = (in[row+3] + in[row+7] + next[row+3] + next[row+7]) >> 2;
					}
					else
					{
						out[0] = (in[row+0] + next[row+0]) >> 1;
						out[1] = (in[row+1] + next[row+1]) >> 1;
						out[2] = (in[row+2] + next[row+2]) >> 1;
						out[3] = (in[row+3] + next[row+3]) >> 1;
					}
				}
			}
		}
	}
}

static void GL_TextureImageRAW( gl_texture_t *tex, int side, int level, int width, int height, int depth, int type, const void *data )
{
	size_t		converted_size;
	size_t		padded_size;
	uint16_t	*converted_data;
	const byte	*src = (const byte *)data;
	const size_t	pixels = (size_t)width * (size_t)height * (size_t)Q_max( 1, depth );

	Assert( tex != NULL );
	Assert( data != NULL );

	// Calculate size needed for converted texture (all PVR formats are 16 bits = 2 bytes per pixel)
	converted_size = pixels * 2;
	padded_size = ( converted_size + 31 ) & ~31;

	// If the source data is already in the correct 16bpp format, upload directly.
	// This is critical for lightmap atlas pages which are built as PF_RGB_5650 on the engine side.
	// The conversion path below assumes 32bpp RGBA/BGRA input and will corrupt 16bpp sources.
	if( type == PF_RGB_5650 || type == PF_RGB_5650_TWID || type == PF_ARGB_4444 || type == PF_ARGB_1555 )
	{
		// For some 16bpp sources (e.g. PF_RGB_5650), the buffer can be padded/aligned.
		// Use the actual input size for this type to avoid under/over-copy.
		const size_t src_size = GL_CalcImageSize( type, width, height, depth );
		const size_t src_padded = ( src_size + 31 ) & ~31;

		// Allocate PVR memory if not already allocated
		if( !tex->loaded || tex->vram_ptr == NULL )
		{
			tex->vram_ptr = pvr_mem_malloc( src_padded );
			if( !tex->vram_ptr )
			{
				gEngfuncs.Con_Printf( S_ERROR "%s: failed to allocate PVR memory for %s\n", __func__, tex->name );
				return;
			}
		}

		// Upload raw 16bpp data as-is. pvr_txr_load operates in 32-byte chunks, so pad safely.
		if( src_padded != src_size )
		{
			byte *tmp = (byte *)Mem_Malloc( r_temppool, src_padded );
			if( !tmp )
			{
				gEngfuncs.Con_Printf( S_ERROR "%s: failed to allocate pad buffer for %s\n", __func__, tex->name );
				return;
			}
			memcpy( tmp, data, src_size );
			memset( tmp + src_size, 0, src_padded - src_size );
			pvr_txr_load( tmp, tex->vram_ptr, src_padded );
			Mem_Free( tmp );
		}
		else
		{
			pvr_txr_load( data, tex->vram_ptr, src_size );
		}
		tex->loaded = true;
		return;
	}

	// Allocate temporary buffer for converted data
	converted_data = (uint16_t *)Mem_Malloc( r_temppool, converted_size );
	if( !converted_data )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: failed to allocate conversion buffer for %s\n", __func__, tex->name );
		return;
	}

	// Extract base format (bits 27-29 contain the format, mask out VQ_ENABLE and other flags)
	uint32_t base_format = tex->format & 0x38000000; // Mask bits 27-29

	qboolean is_bgra = (type == PF_BGRA_32);
	qboolean is_rgba32 = (type == PF_RGBA_32 || type == PF_BGRA_32);
	qboolean is_rgb24 = (type == PF_RGB_24 || type == PF_BGR_24);

	// Convert to PVR format
	switch( base_format )
	{
	case PVR_TXRFMT_ARGB1555:
		if( is_rgba32 )
		{
			if( is_bgra ) GL_ConvertBGRA32ToARGB1555( src, converted_data, width, height );
			else GL_ConvertRGBA32ToARGB1555( src, converted_data, width, height );
		}
		else if( is_rgb24 )
		{
			// No alpha channel in 24bpp sources: treat as fully opaque.
			const byte *s = src;
			uint16_t *d = converted_data;
			size_t i;
			if( type == PF_BGR_24 )
			{
				for( i = 0; i < pixels; i++, s += 3, d++ )
				{
					uint16_t r = (uint16_t)(s[2] >> 3);
					uint16_t g = (uint16_t)(s[1] >> 3);
					uint16_t b = (uint16_t)(s[0] >> 3);
					*d = (1u << 15) | (r << 10) | (g << 5) | b;
				}
			}
			else
			{
				for( i = 0; i < pixels; i++, s += 3, d++ )
				{
					uint16_t r = (uint16_t)(s[0] >> 3);
					uint16_t g = (uint16_t)(s[1] >> 3);
					uint16_t b = (uint16_t)(s[2] >> 3);
					*d = (1u << 15) | (r << 10) | (g << 5) | b;
				}
			}
		}
		else
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: %s unexpected src type %d for ARGB1555\n", __func__, tex->name, type );
			Mem_Free( converted_data );
			return;
		}
		break;
	case PVR_TXRFMT_ARGB4444:
		if( is_rgba32 )
		{
			// ARGB4444 conversion (assume RGBA for now, can add BGRA variant if needed)
			// Note: BGRA32 is still 4 bytes per pixel; current helper expects RGBA ordering.
			if( is_bgra )
			{
				// Convert BGRA -> ARGB4444 locally (opaque alpha preserved from src)
				const byte *s = src;
				uint16_t *d = converted_data;
				size_t i;
				for( i = 0; i < pixels; i++, s += 4, d++ )
				{
					uint16_t a = (uint16_t)(s[3] >> 4);
					uint16_t r = (uint16_t)(s[2] >> 4);
					uint16_t g = (uint16_t)(s[1] >> 4);
					uint16_t b = (uint16_t)(s[0] >> 4);
					*d = (a << 12) | (r << 8) | (g << 4) | b;
				}
			}
			else
			{
				GL_ConvertRGBA32ToARGB4444( src, converted_data, width, height );
			}
		}
		else if( is_rgb24 )
		{
			// No alpha channel in 24bpp sources: treat as fully opaque.
			const byte *s = src;
			uint16_t *d = converted_data;
			size_t i;
			if( type == PF_BGR_24 )
			{
				for( i = 0; i < pixels; i++, s += 3, d++ )
				{
					uint16_t r = (uint16_t)(s[2] >> 4);
					uint16_t g = (uint16_t)(s[1] >> 4);
					uint16_t b = (uint16_t)(s[0] >> 4);
					*d = (0xFu << 12) | (r << 8) | (g << 4) | b;
				}
			}
			else
			{
				for( i = 0; i < pixels; i++, s += 3, d++ )
				{
					uint16_t r = (uint16_t)(s[0] >> 4);
					uint16_t g = (uint16_t)(s[1] >> 4);
					uint16_t b = (uint16_t)(s[2] >> 4);
					*d = (0xFu << 12) | (r << 8) | (g << 4) | b;
				}
			}
		}
		else
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: %s unexpected src type %d for ARGB4444\n", __func__, tex->name, type );
			Mem_Free( converted_data );
			return;
		}
		break;
	case PVR_TXRFMT_RGB565:
	default:
		// RGB565 or fallback
		if( is_rgba32 )
		{
			if( is_bgra ) GL_ConvertBGRA32ToRGB565( src, converted_data, width, height );
			else GL_ConvertRGBA32ToRGB565( src, converted_data, width, height );
		}
		else if( is_rgb24 )
		{
			const byte *s = src;
			uint16_t *d = converted_data;
			size_t i;
			if( type == PF_BGR_24 )
			{
				for( i = 0; i < pixels; i++, s += 3, d++ )
				{
					uint16_t r = (uint16_t)(s[2] >> 3);
					uint16_t g = (uint16_t)(s[1] >> 2);
					uint16_t b = (uint16_t)(s[0] >> 3);
					*d = (r << 11) | (g << 5) | b;
				}
			}
			else
			{
				for( i = 0; i < pixels; i++, s += 3, d++ )
				{
					uint16_t r = (uint16_t)(s[0] >> 3);
					uint16_t g = (uint16_t)(s[1] >> 2);
					uint16_t b = (uint16_t)(s[2] >> 3);
					*d = (r << 11) | (g << 5) | b;
				}
			}
		}
		else
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: %s unexpected src type %d for RGB565\n", __func__, tex->name, type );
			Mem_Free( converted_data );
			return;
		}
		break;
	}

	// Allocate PVR memory if not already allocated
	if( !tex->loaded || tex->vram_ptr == NULL )
	{
		tex->vram_ptr = pvr_mem_malloc( padded_size );
		if( !tex->vram_ptr )
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: failed to allocate PVR memory for %s\n", __func__, tex->name );
			Mem_Free( converted_data );
			return;
		}
	}

	// Upload to PVR memory
	if( padded_size != converted_size )
	{
		byte *tmp = (byte *)Mem_Malloc( r_temppool, padded_size );
		if( !tmp )
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: failed to allocate pad buffer for %s\n", __func__, tex->name );
			Mem_Free( converted_data );
			return;
		}
		memcpy( tmp, converted_data, converted_size );
		memset( tmp + converted_size, 0, padded_size - converted_size );
		pvr_txr_load( tmp, tex->vram_ptr, padded_size );
		Mem_Free( tmp );
	}
	else
	{
		pvr_txr_load( converted_data, tex->vram_ptr, converted_size );
	}

	// Mark as loaded
	tex->loaded = true;

	// Free conversion buffer
	Mem_Free( converted_data );
}

static void GL_TextureImageCompressed( gl_texture_t *tex, int side, int level, int width, int height, int depth, size_t size, const void *data )
{
	Assert( tex != NULL );
	Assert( data != NULL );

	// VQ compressed textures are already in PVR format, just upload directly
	// Allocate PVR memory if not already allocated
	const size_t padded_size = ( size + 31 ) & ~31;
	if( !tex->loaded || tex->vram_ptr == NULL )
	{
		tex->vram_ptr = pvr_mem_malloc( padded_size );
		if( !tex->vram_ptr )
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: failed to allocate PVR memory for compressed texture %s\n", __func__, tex->name );
			return;
		}
	}

	// Upload compressed data directly to PVR memory
	// VQ textures include codebook + compressed data, already in correct format
	if( padded_size != size )
	{
		byte *tmp = (byte *)Mem_Malloc( r_temppool, padded_size );
		if( !tmp )
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: failed to allocate pad buffer for compressed texture %s\n", __func__, tex->name );
			return;
		}
		memcpy( tmp, data, size );
		memset( tmp + size, 0, padded_size - size );
		pvr_txr_load( tmp, tex->vram_ptr, padded_size );
		Mem_Free( tmp );
	}
	else
	{
		pvr_txr_load( (const void *)data, tex->vram_ptr, size );
	}

	// Mark as loaded
	tex->loaded = true;
}

/*
===============
GL_UploadTexture

upload texture into video memory
===============
*/
static qboolean GL_UploadTexture( gl_texture_t *tex, rgbdata_t *pic )
{
	byte		*buf, *data;
	size_t		texsize, size;
	uint		width, height;
	uint		i, j, numSides;
	uint		offset = 0;
	qboolean		normalMap;
	const byte	*bufend;

	// dedicated server
	if( !glw_state.initialized )
		return true;

	Assert( pic != NULL );
	Assert( tex != NULL );

	GL_SetTextureTarget( tex, pic ); // must be first

	GL_SetTextureDimensions( tex, pic->width, pic->height, pic->depth );
	GL_SetTextureFormat( tex, pic->type, pic->flags );

	tex->fogParams[0] = pic->fogParams[0];
	tex->fogParams[1] = pic->fogParams[1];
	tex->fogParams[2] = pic->fogParams[2];
	tex->fogParams[3] = pic->fogParams[3];

	if(( pic->width * pic->height ) & 3 )
	{
		// will be resampled, just tell me for debug targets
		gEngfuncs.Con_Reportf( "%s: %s s&3 [%d x %d]\n", __func__, tex->name, pic->width, pic->height );
	}

	buf = pic->buffer;
	bufend = pic->buffer + pic->size; // total image size include all the layers, cube sides, mipmaps
	offset = GL_CalcImageSize( pic->type, pic->width, pic->height, pic->depth );
	texsize = GL_CalcTextureSize( tex->format, tex->width, tex->height, tex->depth );
	normalMap = FBitSet( tex->flags, TF_NORMALMAP ) ? true : false;
	numSides = FBitSet( pic->flags, IMAGE_CUBEMAP ) ? 6 : 1;

	// uploading texture into video memory, change the binding
	glState.currentTextures = tex->texnum;
	glState.currentTexturesIndex = tex - gl_textures;
	for( i = 0; i < numSides; i++ )
	{
		// track the buffer bounds
		if( buf != NULL && buf >= bufend )
			gEngfuncs.Host_Error( "%s: %s image buffer overflow\n", __func__, tex->name );

		if( ImageCompressed( pic->type ))
		{
			// VQ mipmapped textures arrive as a single contiguous blob: codebook + all mip levels.
			// Upload the entire payload in one shot to avoid buffer stepping corrupting the stream.
			if( pic->type == PF_VQ_MIPMAP_RGB_5650 || pic->type == PF_VQ_MIPMAP_ARGB_1555 )
			{
				int levels = 1;
				width = tex->width;
				height = tex->height;
				size = vq_codebook_sz + GL_CalcVQIndexBytes( width, height, true, &levels );
				GL_TextureImageCompressed( tex, i, 0, width, height, tex->depth, size, buf );
				tex->size += size;
				buf += size;
				tex->numMips += levels;
			}
			// Non-mip VQ (including Small VQ) is also a single blob: codebook + index stream.
			// Some loaders may set numMips oddly for compressed payloads; ignore it for VQ.
			else if( pic->type == PF_VQ_RGB_5650 || pic->type == PF_VQ_ARGB_4444 ||
			         pic->type == PF_VQ_ARGB_1555 || pic->type == PF_SMALL_VQ_RGB_5650 )
			{
				width = tex->width;
				height = tex->height;
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageCompressed( tex, i, 0, width, height, tex->depth, size, buf );
				tex->size += size;
				buf += size;
				tex->numMips += 1;
			}
			else for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( TEXTURE_SIZE_MIN, ( tex->width >> j ));
				height = Q_max( TEXTURE_SIZE_MIN, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageCompressed( tex, i, j, width, height, tex->depth, size, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;

			}
		}
		else if( Q_max( 1, pic->numMips ) > 1 )	// not-compressed DDS
		{
			for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( TEXTURE_SIZE_MIN, ( tex->width >> j ));
				height = Q_max( TEXTURE_SIZE_MIN, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;


			}
		}
		else // RGBA32
		{

   			int mipCount = GL_CalcMipmapCount(tex, (buf != NULL));

			// NOTE: only single uncompressed textures can be resamples, no mips, no layers, no sides
			if(( tex->depth == 1 ) && (( pic->width != tex->width ) || ( pic->height != tex->height )))
				data = GL_ResampleTexture( buf, pic->width, pic->height, tex->width, tex->height, normalMap );
			else data = buf;

			if( !ImageCompressed( pic->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ))
				data = GL_ApplyFilter( data, tex->width, tex->height );

			// mips will be auto-generated if desired
			for( j = 0; j < mipCount; j++ )
			{
				width = Q_max( TEXTURE_SIZE_MIN, ( tex->width >> j ));
				height = Q_max( TEXTURE_SIZE_MIN, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, data );
				if(mipCount > 1 && width == height)
					GL_BuildMipMap( data, width, height, tex->depth, tex->flags );
				tex->size += texsize;
				tex->numMips++;

			}

			// move to next side
			if( numSides > 1 && ( buf != NULL ))
				buf += GL_CalcImageSize( pic->type, pic->width, pic->height, 1 );
		}
	}

	SetBits( tex->flags, TF_IMG_UPLOADED ); // done
	tex->numMips /= numSides;

	return true;
}

/*
===============
GL_ProcessImage

do specified actions on pixels
===============
*/
static void GL_ProcessImage( gl_texture_t *tex, rgbdata_t *pic )
{
	uint	img_flags = 0;

	// force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	if( ImageCompressed( pic->type ))
	{
		if( !pic->numMips )
			tex->flags |= TF_NOMIPMAP; // disable mipmapping by user request

		// clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// copy flag about luma pixels
		if( pic->flags & IMAGE_HAS_LUMA )
			tex->flags |= TF_HAS_LUMA;

		if( pic->flags & IMAGE_QUAKEPAL )
			tex->flags |= TF_QUAKEPAL;

		// create luma texture from quake texture
		if( tex->flags & TF_MAKELUMA )
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
			tex->original = gEngfuncs.FS_CopyImage( pic ); // because current pic will be expanded to rgba

		// we need to expand image into RGBA buffer
		if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
			img_flags |= IMAGE_FORCE_RGBA;

		// processing image before uploading (force to rgba, make luma etc)
		if( pic->buffer ) gEngfuncs.Image_Process( &pic, 0, 0, img_flags, 0 );

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			ClearBits( pic->flags, IMAGE_HAS_COLOR );
	}
}

/*
================
GL_CheckTexName
================
*/
static qboolean GL_CheckTexName( const char *name )
{
	int len;

	if( !COM_CheckString( name ))
		return false;

	len = Q_strlen( name );

	// because multi-layered textures can exceed name string
	if( len >= sizeof( gl_textures->name ))
	{
		gEngfuncs.Con_Printf( S_ERROR "LoadTexture: too long name %s (%d)\n", name, len );
		return false;
	}

	return true;
}

/*
================
GL_TextureForName
================
*/
static gl_texture_t *GL_TextureForName( const char *name )
{
	gl_texture_t	*tex;
	uint		hash;

	// find the texture in array
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = gl_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
			return tex;
	}

	return NULL;
}

/*
================
GL_AllocTexture
================
*/
static gl_texture_t *GL_AllocTexture( const char *name, texFlags_t flags )
{
	const qboolean skyboxhack = FBitSet( flags, TF_SKYSIDE );
	gl_texture_t *tex = NULL;
	uint texnum;

	if( !skyboxhack )
	{
		// Use next available texture ID
		texnum = next_texture_id++;
		
		// Find a free texture_t slot
		uint i;
		// NOTE: slot 0 is reserved. Many engine paths treat 0 as "no texture"/failure.
		for( i = 1; i < MAX_TEXTURES; i++ )
		{
			if( gl_textures[i].texnum )
				continue;

			tex = &gl_textures[i];
			break;
		}
	}
	else
	{
		// Skybox hack: use sequential IDs starting from skyboxbasenum
		texnum = tr.skyboxbasenum++;
		
		// Try to use slot matching texnum
		if( texnum < MAX_TEXTURES && gl_textures[texnum].texnum == 0 )
		{
			// Never allocate slot 0
			if( texnum == 0 )
				tex = NULL;
			else
			tex = &gl_textures[texnum];
		}
		else
		{
			// Find a free slot
			uint i;
			// NOTE: slot 0 is reserved (see above)
			for( i = 1; i < MAX_TEXTURES; i++ )
			{
				if( gl_textures[i].texnum )
					continue;

				tex = &gl_textures[i];
				break;
			}
		}
	}

	if( tex == NULL )
	{
		gEngfuncs.Host_Error( "%s: MAX_TEXTURES limit exceeds\n", __func__ );
		return NULL;
	}

	// copy initial params
	Q_strncpy( tex->name, name, sizeof( tex->name ));
	tex->texnum = texnum;
	tex->flags = flags;
	
	// Initialize PVR fields
	tex->vram_ptr = NULL;
	tex->format = 0;
	tex->loaded = false;

	// increase counter
	gl_numTextures = Q_max(( tex - gl_textures ) + 1, gl_numTextures );

	// add to hash table
	tex->hashValue = COM_HashKey( name, TEXTURES_HASH_SIZE );
	tex->nextHash = gl_texturesHashTable[tex->hashValue];
	gl_texturesHashTable[tex->hashValue] = tex;

	return tex;
}

/*
================
GL_DeleteTexture
================
*/
static void GL_DeleteTexture( gl_texture_t *tex )
{
	gl_texture_t	**prev;
	gl_texture_t	*cur;

	Assert( tex != NULL );

	// already freed?
	if( !tex->texnum ) return;

	// debug
	if( !tex->name[0] )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: trying to free unnamed texture with texnum %i\n", __func__, tex->texnum );
		return;
	}

	// remove from hash table
	prev = &gl_texturesHashTable[tex->hashValue];

	while( 1 )
	{
		cur = *prev;
		if( !cur ) break;

		if( cur == tex )
		{
			*prev = cur->nextHash;
			break;
		}
		prev = &cur->nextHash;
	}

	// invalidate texture state cache
	if( glState.currentTextures == tex->texnum )
	{
		glState.currentTextures = -1;
		glState.currentTexturesIndex = 0;
	}

	// release source
	if( tex->original )
		gEngfuncs.FS_FreeImage( tex->original );

	// Free PVR memory
	if( tex->loaded && tex->vram_ptr != NULL )
	{
		pvr_mem_free( tex->vram_ptr );
		tex->vram_ptr = NULL;
		tex->loaded = false;
	}

	memset( tex, 0, sizeof( *tex ));
}

/*
================
GL_UpdateTexSize

recalc image room
================
*/
void GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
	int		i, j, texsize;
	int		numSides;
	gl_texture_t	*tex;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return;

	tex = &gl_textures[texnum];
	numSides = FBitSet( tex->flags, TF_CUBEMAP ) ? 6 : 1;
	GL_SetTextureDimensions( tex, width, height, depth );
	tex->size = 0; // recompute now

	for( i = 0; i < numSides; i++ )
	{
		for( j = 0; j < Q_max( 1, tex->numMips ); j++ )
		{
			width = Q_max( TEXTURE_SIZE_MIN, ( tex->width >> j ));	
			height = Q_max( TEXTURE_SIZE_MIN, ( tex->height >> j ));
			texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
			tex->size += texsize;
		}
	}
}

/*
================
GL_LoadTexture
================
*/
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	gl_texture_t	*tex;
	rgbdata_t		*pic;
	uint		picFlags = 0;

	if( !GL_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = GL_TextureForName( name )))
		return (tex - gl_textures);

	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );

	// set some image flags
	gEngfuncs.Image_SetForceFlags( picFlags );

	pic = gEngfuncs.FS_LoadImage( name, buf, size );
	if( !pic ) return 0; // couldn't loading image

	// allocate the new one
	tex = GL_AllocTexture( name, flags );
	GL_ProcessImage( tex, pic );

	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gl_texture_t ));
		gEngfuncs.FS_FreeImage( pic ); // release source texture
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc
	gEngfuncs.FS_FreeImage( pic ); // release source texture

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return tex - gl_textures;
}

/*
================
GL_LoadTextureArray
================
*/
int GL_LoadTextureArray( const char **names, int flags )
{
  // noop
}

/*
================
GL_LoadTextureFromBuffer
================
*/
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	gl_texture_t	*tex;

	if( !GL_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = GL_TextureForName( name )) && !update )
		return (tex - gl_textures);

	// couldn't loading image
	if( !pic ) return 0;

	if( update )
	{
		if( tex == NULL )
			gEngfuncs.Host_Error( "%s: couldn't find texture %s for update\n", __func__, name );
		SetBits( tex->flags, flags );
		// Reset accounting so size reflects this upload only
		tex->size = 0;
		tex->numMips = 0;
	}
	else
	{
		// allocate the new one
		tex = GL_AllocTexture( name, flags );
	}

	GL_ProcessImage( tex, pic );

	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gl_texture_t ));
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc
	
	// Store copy for lightmap textures (for Gouraud shading sampling)
	if( FBitSet( flags, TF_ATLAS_PAGE ) && pic && pic->buffer )
	{
		// Check if this is a lightmap texture (*lightmapN)
		if( name && Q_strncmp( name, "*lightmap", 9 ) == 0 )
		{
			int lightmap_index = Q_atoi( name + 9 );
			if( lightmap_index >= 0 && lightmap_index < MAX_LIGHTMAPS )
			{
				// Get the lightmap buffer size (RGB565 = 2 bytes per pixel)
				int bpp = ( pic->type == PF_RGB_5650 ) ? 2 : LIGHTMAP_BPP;
				R_StoreLightmapCPUCopy( lightmap_index, pic->buffer, pic->width, pic->height, bpp );
			}
		}
	}
	
	return (tex - gl_textures);
}

/*
================
GL_CreateTexture

creates texture from buffer
================
*/
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
    qboolean	update = FBitSet( flags, TF_UPDATE ) ? true : false;
	int	datasize = 1;
	rgbdata_t	r_empty;

	if( FBitSet( flags, TF_ARB_16BIT ))
		datasize = 2;
	else if( FBitSet( flags, TF_ARB_FLOAT ))
		datasize = 4;

    ClearBits( flags, TF_UPDATE );
	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = width;
	r_empty.height = height;
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * datasize * 4;
	r_empty.buffer = (byte *)buffer;

	// clear invalid combinations
	ClearBits( flags, TF_TEXTURE_3D );

	// if image not luminance and not alphacontrast it will have color
	if( !FBitSet( flags, TF_LUMINANCE ) && !FBitSet( flags, TF_ALPHACONTRAST ))
		SetBits( r_empty.flags, IMAGE_HAS_COLOR );

	if( FBitSet( flags, TF_HAS_ALPHA ))
		SetBits( r_empty.flags, IMAGE_HAS_ALPHA );

	if( FBitSet( flags, TF_CUBEMAP ))
	{
		return 0;
	}

    return GL_LoadTextureFromBuffer( name, &r_empty, flags, update );
}

/*
================
GL_CreateTextureArray

creates texture array from buffer
================
*/
int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	// no-op
}

/*
================
GL_FindTexture
================
*/
int GL_FindTexture( const char *name )
{
	gl_texture_t	*tex;

	if( !GL_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = GL_TextureForName( name )))
		return (tex - gl_textures);

	return 0;
}

/*
================
GL_FreeTexture
================
*/
void GL_FreeTexture( unsigned int texnum )
{
	// number 0 it's already freed
	if( texnum <= 0 ) return;

	GL_DeleteTexture( &gl_textures[texnum] );
}

/*
================
GL_ProcessTexture
================
*/
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	gl_texture_t	*image;
	rgbdata_t		*pic;
	int		flags = 0;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return; // missed image
	image = &gl_textures[texnum];

	// select mode
	if( gamma != -1.0f )
	{
		flags = IMAGE_LIGHTGAMMA;
	}
	else if( topColor != -1 && bottomColor != -1 )
	{
		flags = IMAGE_REMAP;
	}
	else
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: bad operation for %s\n", __func__, image->name );
		return;
	}

	if( !image->original )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: no input data for %s\n", __func__, image->name );
		return;
	}

	if( ImageCompressed( image->original->type ))
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: can't process compressed texture %s\n", __func__, image->name );
		return;
	}

	// all the operations makes over the image copy not an original
	pic = gEngfuncs.FS_CopyImage( image->original );

	// we need to expand image into RGBA buffer
	if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
		flags |= IMAGE_FORCE_RGBA;

	gEngfuncs.Image_Process( &pic, topColor, bottomColor, flags, 0.0f );

	GL_UploadTexture( image, pic );
	GL_ApplyTextureParams( image ); // update texture filter, wrap etc

	gEngfuncs.FS_FreeImage( pic );
}

/*
================
GL_TexMemory

return size of all uploaded textures
================
*/
int GL_TexMemory( void )
{
	int	i, total = 0;

	for( i = 0; i < gl_numTextures; i++ )
		total += gl_textures[i].size;

	return total;
}

/*
==============================================================================

INTERNAL TEXTURES

==============================================================================
*/
/*
==================
GL_FakeImage
==================
*/
static rgbdata_t *GL_FakeImage( int width, int height, int depth, int flags )
{
	static byte	data2D[1024]; // 16x16x4
	static rgbdata_t	r_image;

	// also use this for bad textures, but without alpha
	r_image.width = Q_max( TEXTURE_SIZE_MIN, width );
	r_image.height = Q_max( TEXTURE_SIZE_MIN, height );
	r_image.depth = Q_max( 1, depth );
	r_image.flags = flags;
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * r_image.depth * 4;
	r_image.buffer = (r_image.size > sizeof( data2D )) ? NULL : data2D;
	r_image.palette = NULL;
	r_image.numMips = 1;
	r_image.encode = 0;

	if( FBitSet( r_image.flags, IMAGE_CUBEMAP ))
		r_image.size *= 6;
	memset( data2D, 0xFF, sizeof( data2D ));

	return &r_image;
}

/*
==================
R_InitDlightTexture
==================
*/
void R_InitDlightTexture(void)
{
    rgbdata_t r_image;
    
    if (tr.dlightTexture != 0)
        return; // already initialized
    
    memset(&r_image, 0, sizeof(r_image));
    
    r_image.width = BLOCK_SIZE;
    r_image.height = BLOCK_SIZE;
    r_image.flags = IMAGE_HAS_COLOR;
    r_image.type = LIGHTMAP_FORMAT;
    r_image.size = r_image.width * r_image.height * LIGHTMAP_BPP;

    // Allocate memory for the image data
    r_image.buffer = (byte *)Mem_Malloc(r_temppool, r_image.size);
    if (!r_image.buffer) {
        fprintf(stderr, "Failed to allocate memory for dlight texture\n");
        return;
    }
    
    // Initialize the data to some default value (e.g., zero)
    memset(r_image.buffer, 0, r_image.size);

    tr.dlightTexture = GL_LoadTextureInternal("*dlight", &r_image, TF_NOMIPMAP | TF_CLAMP | TF_ATLAS_PAGE);

    if (tr.dlightTexture == 0) {
        fprintf(stderr, "Failed to create dlight texture\n");
    } else {
        printf("Successfully created dlight texture\n");
    }

    // Free the allocated memory after loading the texture
    Mem_Free(r_image.buffer);
}

/*
==================
GL_CreateInternalTextures
==================
*/
static void GL_CreateInternalTextures( void )
{
	int	dx2, dy, d;
	int	x, y;
	rgbdata_t	*pic;

	// emo-texture from quake1
	pic = GL_FakeImage( 16, 16, 1, IMAGE_HAS_COLOR );

	for( y = 0; y < 16; y++ )
	{
		for( x = 0; x < 16; x++ )
		{
			if(( y < 8 ) ^ ( x < 8 ))
				((uint *)pic->buffer)[y*16+x] = 0xFFFF00FF;
			else ((uint *)pic->buffer)[y*16+x] = 0xFF000000;
		}
	}

	tr.defaultTexture = GL_LoadTextureInternal( REF_DEFAULT_TEXTURE, pic, TF_COLORMAP );

	// particle texture from quake1
	pic = GL_FakeImage( 8, 8, 1, IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA );

	for( x = 0; x < 8; x++ )
	{
		for( y = 0; y < 8; y++ )
		{
			if( dottexture[x][y] )
				pic->buffer[( y * 8 + x ) * 4 + 3] = 255;
			else pic->buffer[( y * 8 + x ) * 4 + 3] = 0;
		}
	}

	tr.particleTexture = GL_LoadTextureInternal( REF_PARTICLE_TEXTURE, pic, TF_CLAMP );

	// white texture
	pic = GL_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFFFFFFFF;
	tr.whiteTexture = GL_LoadTextureInternal( REF_WHITE_TEXTURE, pic, TF_COLORMAP );

	// gray texture
	pic = GL_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF7F7F7F;
	tr.grayTexture = GL_LoadTextureInternal( REF_GRAY_TEXTURE, pic, TF_COLORMAP );

	// black texture
	pic = GL_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF000000;
	tr.blackTexture = GL_LoadTextureInternal( REF_BLACK_TEXTURE, pic, TF_COLORMAP );
#if !XASH_DREAMCAST
	// cinematic dummy
	pic = GL_FakeImage( 640, 100, 1, IMAGE_HAS_COLOR );
	tr.cinTexture = GL_LoadTextureInternal( "*cintexture", pic, TF_NOMIPMAP|TF_CLAMP );
#endif
}

/*
===============
R_TextureList_f
===============
*/
void R_TextureList_f( void )
{
	gl_texture_t	*image;
	int		i, texCount, bytes = 0;

	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( " -id-   -w-  -h-     -size- -fmt- -type- -data-  -encode- -wrap- -depth- -name--------\n" );
#if 0
	for( i = texCount = 0, image = gl_textures; i < gl_numTextures; i++, image++ )
	{
		if( !image->texnum ) continue;

		bytes += image->size;
		texCount++;

		gEngfuncs.Con_Printf( "%4i: ", i );
		gEngfuncs.Con_Printf( "%4i %4i ", image->width, image->height );
		gEngfuncs.Con_Printf( "%12s ", Q_memprint( image->size ));

		switch( image->format )
		{
		case GL_COMPRESSED_RGBA_ARB:
			gEngfuncs.Con_Printf( "CRGBA " );
			break;
		case GL_COMPRESSED_RGB_ARB:
			gEngfuncs.Con_Printf( "CRGB  " );
			break;
		case GL_COMPRESSED_LUMINANCE_ALPHA_ARB:
			gEngfuncs.Con_Printf( "CLA   " );
			break;
		case GL_COMPRESSED_LUMINANCE_ARB:
			gEngfuncs.Con_Printf( "CL    " );
			break;
		case GL_COMPRESSED_ALPHA_ARB:
			gEngfuncs.Con_Printf( "CA    " );
			break;
		case GL_COMPRESSED_INTENSITY_ARB:
			gEngfuncs.Con_Printf( "CI    " );
			break;
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			gEngfuncs.Con_Printf( "DXT1c " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			gEngfuncs.Con_Printf( "DXT1a " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			gEngfuncs.Con_Printf( "DXT3  " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			gEngfuncs.Con_Printf( "DXT5  " );
			break;
		case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
		case GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI:
			gEngfuncs.Con_Printf( "ATI2  " );
			break;
		case GL_RGBA:
			gEngfuncs.Con_Printf( "RGBA  " );
			break;
		case GL_RGBA8:
			gEngfuncs.Con_Printf( "RGBA8 " );
			break;
		case GL_RGBA4:
			gEngfuncs.Con_Printf( "RGBA4 " );
			break;
		case GL_RGB:
			gEngfuncs.Con_Printf( "RGB   " );
			break;
		case GL_RGB8:
			gEngfuncs.Con_Printf( "RGB8  " );
			break;
		case GL_RGB5:
			gEngfuncs.Con_Printf( "RGB5  " );
			break;
		case GL_LUMINANCE4_ALPHA4:
			gEngfuncs.Con_Printf( "L4A4  " );
			break;
		case GL_LUMINANCE_ALPHA:
		case GL_LUMINANCE8_ALPHA8:
			gEngfuncs.Con_Printf( "L8A8  " );
			break;
		case GL_LUMINANCE4:
			gEngfuncs.Con_Printf( "L4    " );
			break;
		case GL_LUMINANCE:
		case GL_LUMINANCE8:
			gEngfuncs.Con_Printf( "L8    " );
			break;
		case GL_ALPHA8:
			gEngfuncs.Con_Printf( "A8    " );
			break;
		case GL_INTENSITY8:
			gEngfuncs.Con_Printf( "I8    " );
			break;
		case GL_DEPTH_COMPONENT:
		case GL_DEPTH_COMPONENT24:
			gEngfuncs.Con_Printf( "DPTH24" );
			break;
		case GL_DEPTH_COMPONENT32F:
			gEngfuncs.Con_Printf( "DPTH32" );
			break;
		case GL_LUMINANCE16F_ARB:
			gEngfuncs.Con_Printf( "L16F  " );
			break;
		case GL_LUMINANCE32F_ARB:
			gEngfuncs.Con_Printf( "L32F  " );
			break;
		case GL_LUMINANCE_ALPHA16F_ARB:
			gEngfuncs.Con_Printf( "LA16F " );
			break;
		case GL_LUMINANCE_ALPHA32F_ARB:
			gEngfuncs.Con_Printf( "LA32F " );
			break;
		case GL_RG16F:
			gEngfuncs.Con_Printf( "RG16F " );
			break;
		case GL_RG32F:
			gEngfuncs.Con_Printf( "RG32F " );
			break;
		case GL_RGB16F_ARB:
			gEngfuncs.Con_Printf( "RGB16F" );
			break;
		case GL_RGB32F_ARB:
			gEngfuncs.Con_Printf( "RGB32F" );
			break;
		case GL_RGBA16F_ARB:
			gEngfuncs.Con_Printf( "RGBA16F" );
			break;
		case GL_RGBA32F_ARB:
			gEngfuncs.Con_Printf( "RGBA32F" );
			break;
		case GL_RGB565_KOS:
			gEngfuncs.Con_Printf( "RGB565" );
			break;
		case GL_ARGB1555_KOS:
			gEngfuncs.Con_Printf( "ARGB1555" );
			break;
		case GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_KOS:
			gEngfuncs.Con_Printf( "ARGB1555 VQ MIPMAP" );
			break;
		case GL_COMPRESSED_RGB_565_VQ_MIPMAP_KOS:
			gEngfuncs.Con_Printf( "RGB565 VQ MIPMAP" );
			break;
		case GL_COMPRESSED_RGB_565_VQ_KOS:
			gEngfuncs.Con_Printf( "RGB565 VQ" );
			break;
		case GL_COMPRESSED_ARGB_1555_VQ_KOS:
			gEngfuncs.Con_Printf( "ARGB 1555 VQ" );
			break;
		default:
			gEngfuncs.Con_Printf( " ^1ERROR^7 " );
			break;
		}

		switch( image->target )
		{
		case GL_TEXTURE_1D:
			gEngfuncs.Con_Printf( " 1D   " );
			break;
		case GL_TEXTURE_2D:
			gEngfuncs.Con_Printf( " 2D   " );
			break;
		case GL_TEXTURE_3D:
			gEngfuncs.Con_Printf( " 3D   " );
			break;
		case GL_TEXTURE_CUBE_MAP_ARB:
			gEngfuncs.Con_Printf( "CUBE  " );
			break;
		case GL_TEXTURE_RECTANGLE_EXT:
			gEngfuncs.Con_Printf( "RECT  " );
			break;
		case GL_TEXTURE_2D_ARRAY_EXT:
			gEngfuncs.Con_Printf( "ARRAY " );
			break;
		case GL_TEXTURE_2D_MULTISAMPLE:
			gEngfuncs.Con_Printf( "MSAA  ");
			break;
		default:
			gEngfuncs.Con_Printf( "????  " );
			break;
		}

		if( image->flags & TF_NORMALMAP )
			gEngfuncs.Con_Printf( "normal  " );
		else gEngfuncs.Con_Printf( "diffuse " );

		switch( image->encode )
		{
		case DXT_ENCODE_COLOR_YCoCg:
			gEngfuncs.Con_Printf( "YCoCg     " );
			break;
		case DXT_ENCODE_NORMAL_AG_ORTHO:
			gEngfuncs.Con_Printf( "ortho     " );
			break;
		case DXT_ENCODE_NORMAL_AG_STEREO:
			gEngfuncs.Con_Printf( "stereo    " );
			break;
		case DXT_ENCODE_NORMAL_AG_PARABOLOID:
			gEngfuncs.Con_Printf( "parabolic " );
			break;
		case DXT_ENCODE_NORMAL_AG_QUARTIC:
			gEngfuncs.Con_Printf( "quartic   " );
			break;
		case DXT_ENCODE_NORMAL_AG_AZIMUTHAL:
			gEngfuncs.Con_Printf( "azimuthal " );
			break;
		default:
			gEngfuncs.Con_Printf( "default   " );
			break;
		}

		if( image->flags & TF_CLAMP )
			gEngfuncs.Con_Printf( "clamp  " );
		else if( image->flags & TF_BORDER )
			gEngfuncs.Con_Printf( "border " );
		else gEngfuncs.Con_Printf( "repeat " );
		gEngfuncs.Con_Printf( "   %d  ", image->depth );
		gEngfuncs.Con_Printf( "  %s\n", image->name );
	}

	gEngfuncs.Con_Printf( "---------------------------------------------------------\n" );
	gEngfuncs.Con_Printf( "%i total textures\n", texCount );
	gEngfuncs.Con_Printf( "%s total memory used\n", Q_memprint( bytes ));
	gEngfuncs.Con_Printf( "\n" );
#endif // PVR image TODO texture list
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void )
{
	memset( gl_textures, 0, sizeof( gl_textures ));
	memset( gl_texturesHashTable, 0, sizeof( gl_texturesHashTable ));
	gl_numTextures = 0;

	// create unused 0-entry
	Q_strncpy( gl_textures->name, "*unused*", sizeof( gl_textures->name ));
	gl_textures->hashValue = COM_HashKey( gl_textures->name, TEXTURES_HASH_SIZE );
	gl_textures->nextHash = gl_texturesHashTable[gl_textures->hashValue];
	gl_texturesHashTable[gl_textures->hashValue] = gl_textures;
	gl_numTextures = 1;

	// validate cvars
	R_SetTextureParameters();
	GL_CreateInternalTextures();

	gEngfuncs.Cmd_AddCommand( "texturelist", R_TextureList_f, "display loaded textures list" );
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages( void )
{
	gl_texture_t	*tex;
	int		i;

	gEngfuncs.Cmd_RemoveCommand( "texturelist" );

	for( i = 0, tex = gl_textures; i < gl_numTextures; i++, tex++ )
		GL_DeleteTexture( tex );

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	memset( gl_texturesHashTable, 0, sizeof( gl_texturesHashTable ));
	memset( gl_textures, 0, sizeof( gl_textures ));
	gl_numTextures = 0;
}

void R_TextureReplacementReport( const char *modelname, int gl_texturenum, const char *foundpath )
{

}

qboolean R_SearchForTextureReplacement( char *out, size_t size, const char *modelname, const char *fmt, ... )
{
	va_list ap;
	int ret;

	va_start( ap, fmt );
	ret = Q_vsnprintf( out,	size, fmt, ap );
	va_end( ap );

	if( ret < 0 )
	{
		R_TextureReplacementReport( modelname, -1, "overflow" );
		return false;
	}

	if( gEngfuncs.fsapi->FileExists( out, false ))
		return true;

	R_TextureReplacementReport( modelname, -1, out );
	return false;
}
