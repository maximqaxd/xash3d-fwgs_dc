/*
pvr_local.h - renderer local declarations
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

#ifndef PVR_LOCAL_H
#define PVR_LOCAL_H
#include "port.h"
#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h"
#include "com_model.h"
#include "cl_entity.h"
#include "render_api.h"
#include "protocol.h"
#include "dlight.h"
#include "gl_frustum.h"
#include "ref_api.h"
#include "xash3d_mathlib.h"
#include "ref_params.h"
#include "enginefeatures.h"
#include "com_strings.h"
#include "pm_movevars.h"
#include "cvardef.h"
#include "wadfile.h"
#include "common/mod_local.h"
#include <dc/pvr.h>
#include <sh4zam/shz_sh4zam.h>

// PVR profiling support (uses SH4 performance counters)
#ifndef REF_PVR_PROFILE
#define REF_PVR_PROFILE 1 // Set to 1 to enable profiling
#endif

// Profiling helpers.
#if REF_PVR_PROFILE
#include <dc/perfctr.h>
static inline void PVR_Prof_Start( void )
{
	// Use PRFC1 to avoid interfering with KOS internal timing (PRFC0).
	perf_cntr_clear( PRFC1 );
	perf_cntr_start( PRFC1, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES );
}

static inline double PVR_Prof_End( void )
{
	perf_cntr_stop( PRFC1 );
	// Dreamcast SH4 runs at 200MHz -> 1 cycle ~= 5ns, so ms = cycles * 5e-6.
	const uint64_t cycles = perf_cntr_count( PRFC1 );
	return (double)cycles * 0.000005;
}
#else
static inline void PVR_Prof_Start( void ) {}
static inline double PVR_Prof_End( void ) { return 0.0; }
#endif

#ifndef offsetof
#ifdef __GNUC__
#define offsetof(s,m) __builtin_offsetof(s,m)
#else
#define offsetof(s,m) (size_t)&(((s *)0)->m)
#endif
#endif

#define ASSERT(x) if(!( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )
#define Assert(x) if(!( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )

#include <stdio.h>

// make mod_ref.h?
#define LM_SAMPLE_SIZE             16


extern poolhandle_t r_temppool;

#define BLOCK_SIZE		tr.block_size	// lightmap blocksize
#define BLOCK_SIZE_DEFAULT	128		// for keep backward compatibility
#define BLOCK_SIZE_MAX	128

#define MAX_TEXTURES            1536	// a1ba: increased by users request
#define MAX_DETAIL_TEXTURES	16
#define MAX_LIGHTMAPS	64
#define SUBDIVIDE_SIZE	64
#define MAX_DECAL_SURFS	32
#define MAX_DRAW_STACK	2		// normal view and menu view

#define SHADEDOT_QUANT 	16		// precalculated dot products for quantized angles
#define SHADE_LAMBERT	1.4953241
#define DEFAULT_ALPHATEST	0.0f

// refparams
#define RP_NONE		0
#define RP_ENVVIEW		BIT( 0 )	// used for cubemapshot
#define RP_OLDVIEWLEAF	BIT( 1 )
#define RP_CLIPPLANE	BIT( 2 )

#define RP_NONVIEWERREF	(RP_ENVVIEW)
#define R_ModelOpaque( rm )	( rm == kRenderNormal )
#define R_StaticEntity( ent )	( VectorIsNull( ent->origin ) && VectorIsNull( ent->angles ))
#define RP_LOCALCLIENT( e )	((e) != NULL && (e)->index == ( gp_cl->playernum + 1 ) && e->player )
#define RP_NORMALPASS()	( FBitSet( RI.params, RP_NONVIEWERREF ) == 0 )

#define CL_IsViewEntityLocalPlayer() ( gp_cl->viewentity == ( gp_cl->playernum + 1 ))

#define CULL_VISIBLE	0		// not culled
#define CULL_BACKSIDE	1		// backside of transparent wall
#define CULL_FRUSTUM	2		// culled by frustum
#define CULL_VISFRAME	3		// culled by PVS
#define CULL_OTHER		4		// culled by other reason

#define HACKS_RELATED_HLMODS		// some HL-mods works differently under Xash and can't be fixed without some hacks at least at current time

#define SKYBOX_BASE_NUM 5800 // set skybox base (to let some mods load hi-res skyboxes)


#define LIGHTMAP_BPP	2 //1 2 3 4 

#if LIGHTMAP_BPP == 1
#define LIGHTMAP_FORMAT	PF_RGB_332
#elif LIGHTMAP_BPP == 2
#define LIGHTMAP_FORMAT	PF_RGB_5650
#elif LIGHTMAP_BPP == 3
#define LIGHTMAP_FORMAT	PF_RGB_24
#elif LIGHTMAP_BPP == 4
#define LIGHTMAP_FORMAT	PF_RGBA_32
#else
#error (1 > LIGHTMAP_BPP > 4)
#endif



typedef struct gltexture_s
{
	char		name[64];	// game path, including extension (can be store image programs)
	word		srcWidth;		// keep unscaled sizes
	word		srcHeight;
	word		width;		// upload width\height
	word		height;
	word		depth;		// texture depth or count of layers for 2D_ARRAY
	byte		numMips;		// mipmap count

	uint		texnum;		// texture binding
	int			format;		// uploaded format
	texFlags_t	flags;

	rgba_t		fogParams;	// some water textures
					// contain info about underwater fog
	rgbdata_t		*original;	// keep original image

	// debug info
	size_t		size;		// upload size for debug targets
	uint		hashValue;
	struct gltexture_s	*nextHash;

	// PVR-specific fields
	pvr_ptr_t	vram_ptr;		// PVR memory pointer
	qboolean	loaded;		// Is texture loaded in PVR memory
} gl_texture_t;

typedef struct
{
	int		params;		// rendering parameters

	qboolean		drawWorld;	// ignore world for drawing PlayerModel
	qboolean		isSkyVisible;	// sky is visible
	qboolean		onlyClientDraw;	// disabled by client request
	qboolean		drawOrtho;	// draw world as orthogonal projection

	float		fov_x, fov_y;	// current view fov

	cl_entity_t	*currententity;
	model_t		*currentmodel;
	cl_entity_t	*currentbeam;	// same as above but for beams

	int		viewport[4];
	gl_frustum_t	frustum;

	mleaf_t		*viewleaf;
	mleaf_t		*oldviewleaf;
	vec3_t		pvsorigin;
	vec3_t		vieworg;		// locked vieworigin
	vec3_t		viewangles;
	vec3_t		vforward;
	vec3_t		vright;
	vec3_t		vup;

	vec3_t		cullorigin;
	vec3_t		cull_vforward;
	vec3_t		cull_vright;
	vec3_t		cull_vup;

	float		farClip;

	qboolean		fogCustom;
	qboolean		fogEnabled;
	qboolean		fogSkybox;
	vec4_t		fogColor;
	float		fogDensity;
	float		fogStart;
	float		fogEnd;
	int		cached_contents;	// in water
	int		cached_waterlevel;	// was in water

	float		skyMins[2][SKYBOX_MAX_SIDES];
	float		skyMaxs[2][SKYBOX_MAX_SIDES];

	shz_mat4x4_t		objectMatrix;		// currententity matrix
	shz_mat4x4_t		worldviewMatrix;		// modelview for world
	shz_mat4x4_t		modelviewMatrix;		// worldviewMatrix * objectMatrix

	shz_mat4x4_t		projectionMatrix;
	shz_mat4x4_t		worldviewProjectionMatrix;	// worldviewMatrix * projectionMatrix
	byte		visbytes[(MAX_MAP_LEAFS+7)/8];// actual PVS for current frame

	float		viewplanedist;
	mplane_t		clipPlane;
} ref_instance_t;

typedef struct
{
	cl_entity_t	*solid_entities[MAX_VISIBLE_PACKET];	// opaque moving or alpha brushes
	cl_entity_t	*trans_entities[MAX_VISIBLE_PACKET];	// translucent brushes
	cl_entity_t	*beam_entities[MAX_VISIBLE_PACKET];
	uint		num_solid_entities;
	uint		num_trans_entities;
	uint		num_beam_entities;
} draw_list_t;

typedef struct
{
	int		defaultTexture;   	// use for bad textures
	int		particleTexture;
	int		whiteTexture;
	int		grayTexture;
	int		blackTexture;
	int		solidskyTexture;	// quake1 solid-sky layer
	int		alphaskyTexture;	// quake1 alpha-sky layer
	int		lightmapTextures[MAX_LIGHTMAPS];
	int		dlightTexture;	// custom dlight texture
	int		skyboxTextures[SKYBOX_MAX_SIDES];	// skybox sides
	int		cinTexture;      	// cinematic texture

	int		skytexturenum;	// this not a gl_texturenum!
	int		skyboxbasenum;	// start with 5800

	// entity lists
	draw_list_t	draw_stack[MAX_DRAW_STACK];
	int		draw_stack_pos;
	draw_list_t	*draw_list;

	// Decals are queued during world/entity submission, but must be emitted in TR list
	// (alpha blended + depth test, no depth write). We snapshot the active
	// screen*proj*view*object matrix at queue time so we can render later even
	// after RI/currententity changes.
	struct
	{
		msurface_t	*surf;
		float		world_matrix[16]; // column-major, used by sh4zam (same convention as r_world_matrix)
	} draw_decals[MAX_DECAL_SURFS];
	int		num_draw_decals;

	// OpenGL matrix states
	qboolean		modelviewIdentity;

	int		visframecount;	// PVS frame
	int		dlightframecount;	// dynamic light frame
	int		realframecount;	// not including viewpasses
	int		framecount;

	qboolean		fCustomRendering;
	qboolean		fResetVis;
	qboolean		fFlipViewModel;

	byte		visbytes[(MAX_MAP_LEAFS+7)/8];	// member custom PVS
	int		lightstylevalue[MAX_LIGHTSTYLES];	// value 0 - 65536
	int		block_size;			// lightmap blocksize

	double		frametime;	// special frametime for multipass rendering (will set to 0 on a nextview)
	float		blend;		// global blend value

	// cull info
	vec3_t		modelorg;		// relative to viewpoint

	// get from engine
	world_static_t *world;
	cl_entity_t *entities;
	movevars_t *movevars;
	color24 *palette;
	cl_entity_t *viewent;
	dlight_t *dlights;
	dlight_t *elights;
	byte *texgammatable;
	uint *lightgammatable;
	uint *lineargammatable;
	uint *screengammatable;

	uint max_entities;
} gl_globals_t;

typedef struct
{
	uint		c_world_polys;
	uint		c_studio_polys;
	uint		c_sprite_polys;
	uint		c_alias_polys;
	uint		c_world_leafs;

	uint		c_view_beams_count;
	uint		c_active_tents_count;
	uint		c_alias_models_drawn;
	uint		c_studio_models_drawn;
	uint		c_sprite_models_drawn;
	uint		c_particle_count;

	uint		c_client_ents;	// entities that moved to client
	double		t_world_node;
	double		t_world_draw;
#if REF_PVR_PROFILE
	// Profiling data (in milliseconds)
	double		t_world_setup;		// World rendering setup time
	double		t_world_lighting;	// World lighting (SampleVertexLight) time
	double		t_world_transforms;	// World vertex transforms time
	double		t_world_geometry;	// World geometry submission time
	double		t_studio_setup;		// Studio model setup time
	double		t_studio_lighting;	// Studio lighting time
	double		t_studio_transforms;	// Studio transforms time
	double		t_studio_geometry;	// Studio geometry submission time
	double		t_studio_pervertex_lighting;	// Per-vertex lighting (R_LightLambert) time
	double		t_studio_quaternions;	// Quaternion calculations (R_StudioCalcRotations, R_StudioSlerpBones) time
	double		t_studio_bones;		// Bone transforms (Matrix3x4_ConcatTransforms) time
#endif
} ref_speeds_t;

extern ref_speeds_t		r_stats;
extern ref_instance_t	RI;
extern gl_globals_t	tr;

extern float		gldepthmin, gldepthmax;
extern float		r_world_matrix[16];  // sh4zam column-major screen*proj*worldview matrix

extern int		g_pvr_current_list;
#define r_numEntities	(tr.draw_list->num_solid_entities + tr.draw_list->num_trans_entities)
#define r_numStatics	(r_stats.c_client_ents)
#define Mod_AllowMaterials() (host_allow_materials->value && !FBitSet( gp_host->features, ENGINE_DISABLE_HDTEXTURES ))

//
// pvr_backend.c
//
void GL_BackendStartFrame( void );
void GL_BackendEndFrame( void );
void GL_CleanUpTextureUnits( int last );
void GL_Bind( int tmu, unsigned int texnum );
void GL_LoadTexMatrixExt( const float *glmatrix );
void GL_CleanupAllTextureUnits( void );
void GL_LoadIdentityTexMatrix( void );
void GL_DisableAllTexGens( void );
void GL_SetRenderMode( int mode );
void GL_EnableTextureUnit( int tmu, qboolean enable );
void GL_TextureTarget( uint target );
void GL_Cull( int cull );
void R_ShowTextures( void );
void SCR_TimeRefresh_f( void );

//
// pvr_beams.c
//
void CL_DrawBeams( int fTrans, BEAM *active_beams );
qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly );

//
// pvr_cull.c
//
int R_CullModel( cl_entity_t *e, const vec3_t absmin, const vec3_t absmax );
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs );
int R_CullSurface( msurface_t *surf, gl_frustum_t *frustum, uint clipflags );

//
// pvr_decals.c
//
void DrawSurfaceDecals( msurface_t *fa, qboolean single, qboolean reverse );
float *R_DecalSetupVerts( decal_t *pDecal, msurface_t *surf, int texture, int *outCount );
void DrawSingleDecal( decal_t *pDecal, msurface_t *fa );
void R_EntityRemoveDecals( model_t *mod );
void DrawDecalsBatch( void );
void R_ClearDecals( void );

//
// pvr_draw.c
//
void Draw_FlushBatch( void );
void R_Set2DMode( qboolean enable );
void R_UploadStretchRaw( int texture, int cols, int rows, int width, int height, const byte *data );

//
// pvr_drawhulls.c
//
void R_DrawWorldHull( void );
void R_DrawModelHull( void );

//
// pvr_image.c
//
void R_SetTextureParameters( void );
gl_texture_t *R_GetTexture( unsigned int texnum );
const char *GL_TargetToString();
#define GL_LoadTextureInternal( name, pic, flags ) GL_LoadTextureFromBuffer( name, pic, flags, false )
#define GL_UpdateTextureInternal( name, pic, flags ) GL_LoadTextureFromBuffer( name, pic, flags, true )
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int GL_LoadTextureArray( const char **names, int flags );
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
byte *GL_ResampleTexture( const byte *source, int in_w, int in_h, int out_w, int out_h, qboolean isNormalMap );
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor );
void GL_UpdateTexSize( int texnum, int width, int height, int depth );
qboolean GL_TextureFilteringEnabled( const gl_texture_t *tex );
void GL_ApplyTextureParams( gl_texture_t *tex );
int GL_FindTexture( const char *name );
void GL_FreeTexture( unsigned int texnum );
const char *GL_Target( int target );
void R_InitDlightTexture( void );
void R_TextureList_f( void );
void R_InitImages( void );
void R_ShutdownImages( void );
int GL_TexMemory( void );
qboolean R_SearchForTextureReplacement( char *out, size_t size, const char *modelname, const char *fmt, ... ) FORMAT_CHECK( 4 );
void R_TextureReplacementReport( const char *modelname, int gl_texturenum, const char *foundpath );
qboolean GL_UpdateTexture( int texnum, int xoff, int yoff, int width, int height, const void *buffer );

//
// pvr_rlight.c
//
void CL_RunLightStyles( lightstyle_t *ls );
void R_PushDlights( void );
void R_GetLightSpot( vec3_t lightspot );
void R_MarkLights( const dlight_t *light, int bit, const mnode_t *node );
colorVec R_LightVec( const vec3_t start, const vec3_t end, vec3_t lightspot, vec3_t lightvec );
colorVec R_LightPoint( const vec3_t p0 );

//
// pvr_rmain.c
//
void R_ClearScene( void );
void R_LoadIdentity( void );
void R_RenderScene( void );
void R_DrawCubemapView( const vec3_t origin, const vec3_t angles, int size );
void R_SetupRefParams( const struct ref_viewpass_s *rvp );
void R_TranslateForEntity( cl_entity_t *e );
void R_RotateForEntity( cl_entity_t *e );
void R_DrawBrushModelLightmapsOnly( cl_entity_t *e );
void R_SetupGL( qboolean set_gl_state );
void R_AllowFog( qboolean allowed );
qboolean R_OpaqueEntity( cl_entity_t *ent );
void R_SetupFrustum( void );
void R_FindViewLeaf( void );
void R_PushScene( void );
void R_PopScene( void );
void R_DrawFog( void );
int CL_FxBlend( cl_entity_t *e );

//
// pvr_rmath.c
//
void Matrix4x4_ToArrayFloatGL( const matrix4x4 in, float out[16] );
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateProjection(matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar);
void Matrix4x4_CreateOrtho(matrix4x4 m, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar);
void Matrix4x4_CreateModelview( matrix4x4 out );

//
// pvr_rmisc.c
//
void R_ClearStaticEntities( void );

//
// pvr_rsurf.c
//
void R_MarkLeaves( void );
void R_DrawWorld( void );
void R_DrawWaterSurfaces( void );
void R_DrawBrushModel( cl_entity_t *e );
void GL_SubdivideSurface( model_t *mod, msurface_t *fa );
static int GL_BuildPolygonFromSurface( model_t *mod, msurface_t *fa );
void GL_SetupFogColorForSurfaces( void );
void R_DrawAlphaTextureChains( void );
void GL_RebuildLightmaps( void );
void GL_InitRandomTable( void );
void GL_BuildLightmaps( void );
void GL_ResetFogColor( void );
void R_LightmapCoord( const vec3_t v, const msurface_t *surf, const float sample_size, vec2_t coords );

//
// pvr_rpart.c
//
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime );
void CL_DrawParticles( double frametime, particle_t *cl_active_particles, float partsize );
void CL_DrawTracers( double frametime, particle_t *cl_active_tracers );


//
// pvr_sprite.c
//
void R_SpriteInit( void );
void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags );
mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw );
void R_DrawSpriteModel( cl_entity_t *e );

//
// pvr_studio.c
//
void R_StudioInit( void );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
struct mstudiotex_s *R_StudioGetTexture( cl_entity_t *e );
int R_GetEntityRenderMode( cl_entity_t *ent );
void R_DrawStudioModel( cl_entity_t *e );
player_info_t *pfnPlayerInfo( int index );
void R_GatherPlayerLight( void );
float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
void R_StudioResetPlayerModels( void );
void CL_InitStudioAPI( void );
void Mod_StudioLoadTextures( model_t *mod, void *data );
void Mod_StudioUnloadTextures( void *data );

//
// pvr_warp.c
//
void R_AddSkyBoxSurface( msurface_t *fa );
void R_ClearSkyBox( void );
void R_DrawSkyBox( void );
void R_DrawClouds( void );
void R_UnloadSkybox( void );
void EmitWaterPolys( msurface_t *warp, qboolean reverse, qboolean ripples );
void R_ResetRipples( void );
void R_AnimateRipples( void );
qboolean R_UploadRipples( texture_t *image );

//#include "vid_common.h"

//
// renderer exports
//
qboolean Ref_Init( void );
void Ref_Shutdown( void );
void GL_SetupAttributes( int safegl );
void GL_OnContextCreated( void );
void GL_InitExtensions( void );
void GL_ClearExtensions( void );
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
void GL_FreeImage( const char *name );
qboolean VID_ScreenShot( const char *filename, int shot_type );
qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot );
void R_GammaChanged( qboolean do_reset_gamma );
void R_BeginFrame( qboolean clearScene );
void R_RenderFrame( const struct ref_viewpass_s *vp );
void R_EndFrame( void );
void R_ClearScene( void );
void R_GetTextureParms( int *w, int *h, int texnum );
void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int curFrame, const struct model_s *pSprite );
void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
qboolean R_SpeedsMessage( char *out, size_t size );
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs );
int R_WorldToScreen( const vec3_t point, vec3_t screen );
void R_ScreenToWorld( const vec3_t screen, vec3_t point );
qboolean R_AddEntity( struct cl_entity_s *pRefEntity, int entityType );
void Mod_SpriteUnloadTextures( void *data );
void Mod_UnloadAliasModel( struct model_s *mod );
void Mod_AliasUnloadTextures( void *data );
void GL_SetRenderMode( int mode );
void R_RunViewmodelEvents( void );
void R_DrawViewModel( void );
int R_GetSpriteTexture( const struct model_s *m_pSpriteModel, int frame );
void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
void R_DecalRemoveAll( int texture );
int R_CreateDecalList( decallist_t *pList );
void R_ClearAllDecals( void );
byte *Mod_GetCurrentVis( void );
void Mod_SetOrthoBounds( const float *mins, const float *maxs );
void R_NewMap( void );
void CL_AddCustomBeam( cl_entity_t *pEnvBeam );

//
// gl_opengl.c
//
#define GL_CheckForErrors() GL_CheckForErrors_( __FILE__, __LINE__ )
void GL_CheckForErrors_( const char *filename, const int fileline );
const char *GL_ErrorString( int err );
qboolean GL_Support( int r_ext );
int GL_MaxTextureUnits( void );

//
// pvr_triapi.c
//
void R_TriRenderMode( int mode );
void TriBegin( int mode );
void TriEnd( void );
void TriTexCoord2f( float u, float v );
void TriVertex3fv( const float *v );
void TriVertex3f( float x, float y, float z );
void _TriColor4f( float r, float g, float b, float a );
void _TriColor4ub( byte r, byte g, byte b, byte a );
void TriColor4f( float r, float g, float b, float a );
void TriColor4ub( byte r, byte g, byte b, byte a );
void TriBrightness( float brightness );
int TriWorldToScreen( const float *world, float *screen );
int TriSpriteTexture( model_t *pSpriteModel, int frame );
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn );
void TriGetMatrix( const int pname, float *matrix );
void TriFogParams( float flDensity, int iFogSkybox );
void TriCullFace( TRICULLSTYLE mode );

typedef struct
{
	const char	*renderer_string;
	const char	*vendor_string;
	const char	*version_string;

	int		max_texture_units;
	int		max_texture_coords;
	int		max_teximage_units;
	int		max_2d_texture_size;

	float		max_texture_anisotropy;
	float		max_texture_lod_bias;

	int		max_multisamples;

	int		color_bits;
	int		alpha_bits;
	int		depth_bits;
	int		stencil_bits;
	int		msaasamples;
	int		version_major;
	int		version_minor;

	qboolean		softwareGammaUpdate;
	qboolean		fCustomRenderer;
	int		prev_width;
	int		prev_height;
} glconfig_t;

typedef struct
{
	int		currentTextures;
	int		currentTexturesIndex;
	int		isFogEnabled;

	int		faceCull;

	qboolean		stencilEnabled;
	qboolean		in2DMode;
	
	uint32_t	currentColor;  // ARGB format: 0xAARRGGBB
	int		renderMode2D;  // GL_SetRenderMode for 2D: kRenderNormal, kRenderTransColor, etc.
} glstate_t;

typedef struct
{
	qboolean		initialized;	// OpenGL subsystem started
	qboolean		extended;		// extended context allows to GL_Debug
} glwstate_t;

extern glconfig_t		glConfig;
extern glstate_t		glState;

//
// -----------------------------------------------------------------------------
// sh4zam helpers for working with shz_mat4x4_t as an affine transform matrix
// -----------------------------------------------------------------------------
static inline void PVR_Mat4x4_TransformVec3( const shz_mat4x4_t *m, const vec3_t in, vec3_t out )
{
	// Manual column-major 4x4 affine transform (treating vec3 as (x,y,z,1.0))
	// Matrix is column-major: m[0-3] = col0, m[4-7] = col1, m[8-11] = col2, m[12-15] = col3
	const float x = in[0], y = in[1], z = in[2];
	out[0] = m->elem[0] * x + m->elem[4] * y + m->elem[8] * z + m->elem[12];
	out[1] = m->elem[1] * x + m->elem[5] * y + m->elem[9] * z + m->elem[13];
	out[2] = m->elem[2] * x + m->elem[6] * y + m->elem[10] * z + m->elem[14];
}

// Inverse-transform for affine matrix (handles general 3x3 + translation).
static inline void PVR_Mat4x4_VectorITransform( const shz_mat4x4_t *m, const vec3_t in, vec3_t out )
{
	// Extract A (3x3) and t (translation) from column-major matrix.
	const float a00 = m->elem[0],  a01 = m->elem[4],  a02 = m->elem[8];
	const float a10 = m->elem[1],  a11 = m->elem[5],  a12 = m->elem[9];
	const float a20 = m->elem[2],  a21 = m->elem[6],  a22 = m->elem[10];

	const float tx = m->elem[12], ty = m->elem[13], tz = m->elem[14];

	const float vx = in[0] - tx;
	const float vy = in[1] - ty;
	const float vz = in[2] - tz;

	// Invert 3x3 (adjugate/determinant)
	const float c00 =  (a11 * a22 - a12 * a21);
	const float c01 = -(a10 * a22 - a12 * a20);
	const float c02 =  (a10 * a21 - a11 * a20);

	const float det = a00 * c00 + a01 * c01 + a02 * c02;
	if( fabsf( det ) < 1e-8f )
	{
		// Singular: fall back to no transform.
		out[0] = vx; out[1] = vy; out[2] = vz;
		return;
	}

	const float invdet = 1.0f / det;

	// adjugate transpose gives inverse
	const float i00 = c00 * invdet;
	const float i01 = (-(a01 * a22 - a02 * a21)) * invdet;
	const float i02 = ( (a01 * a12 - a02 * a11)) * invdet;

	const float i10 = c01 * invdet;
	const float i11 = ( (a00 * a22 - a02 * a20)) * invdet;
	const float i12 = (-(a00 * a12 - a02 * a10)) * invdet;

	const float i20 = c02 * invdet;
	const float i21 = (-(a00 * a21 - a01 * a20)) * invdet;
	const float i22 = ( (a00 * a11 - a01 * a10)) * invdet;

	out[0] = i00 * vx + i01 * vy + i02 * vz;
	out[1] = i10 * vx + i11 * vy + i12 * vz;
	out[2] = i20 * vx + i21 * vy + i22 * vz;
}

static inline void PVR_Mat4x4_VectorRotate( const shz_mat4x4_t *m, const vec3_t in, vec3_t out )
{
	// Multiply by upper 3x3 only.
	const float a00 = m->elem[0],  a01 = m->elem[4],  a02 = m->elem[8];
	const float a10 = m->elem[1],  a11 = m->elem[5],  a12 = m->elem[9];
	const float a20 = m->elem[2],  a21 = m->elem[6],  a22 = m->elem[10];

	out[0] = a00 * in[0] + a01 * in[1] + a02 * in[2];
	out[1] = a10 * in[0] + a11 * in[1] + a12 * in[2];
	out[2] = a20 * in[0] + a21 * in[1] + a22 * in[2];
}


extern ref_api_t      gEngfuncs_gl;
extern ref_globals_t *gpGlobals_gl;
extern glwstate_t		glw_state_gl;
#define glw_state glw_state_gl
#define gEngfuncs gEngfuncs_gl
#define gpGlobals gpGlobals_gl
extern ref_client_t  *gp_cl;
extern ref_host_t    *gp_host;

#define ENGINE_GET_PARM_ (*gEngfuncs.EngineGetParm)
#define ENGINE_GET_PARM( parm ) ENGINE_GET_PARM_( ( parm ), 0 )

//
// helper funcs
//
static inline cl_entity_t *CL_GetEntityByIndex( int index )
{
	if( unlikely( index < 0 || index >= tr.max_entities || !tr.entities ))
		return NULL;

	return &tr.entities[index];
}

static inline model_t *CL_ModelHandle( int index )
{
	if( unlikely( index < 0 || index >= gp_cl->nummodels ))
		return NULL;

	return gp_cl->models[index];
}

static inline byte TextureToGamma( byte b )
{
	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.texgammatable[b] : b;
}

static inline uint LightToTexGamma( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.lightgammatable[b] : b;
}

static inline uint ScreenGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.screengammatable[b] : b;
}

static inline uint LinearGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.lineargammatable[b] : b;
}

#define WORLDMODEL (gp_cl->models[1])

//
// renderer cvars
//
extern convar_t	gl_texture_anisotropy;
extern convar_t	gl_extensions;
extern convar_t	gl_check_errors;
extern convar_t	gl_texture_lodbias;
extern convar_t	gl_texture_nearest;
extern convar_t	gl_lightmap_nearest;
extern convar_t	gl_keeptjunctions;
extern convar_t	gl_round_down;
extern convar_t	gl_wireframe;
extern convar_t	gl_polyoffset;
extern convar_t	gl_finish;
extern convar_t	gl_nosort;
extern convar_t	gl_test;		// cvar to testify new effects
extern convar_t	gl_msaa;
extern convar_t	gl_stencilbits;
extern convar_t	gl_overbright;
extern convar_t gl_fog;

extern convar_t	r_lighting_extended;
extern convar_t	r_lighting_ambient;
extern convar_t	r_studio_lambert;
extern convar_t	r_detailtextures;
extern convar_t	r_novis;
extern convar_t	r_nocull;
extern convar_t	r_lockpvs;
extern convar_t	r_lockfrustum;
extern convar_t	r_traceglow;
extern convar_t	r_vbo;
extern convar_t	r_vbo_dlightmode;
extern convar_t	r_vbo_detail;
extern convar_t	r_vbo_overbrightmode;
extern convar_t r_studio_sort_textures;
extern convar_t r_studio_drawelements;
extern convar_t r_shadows;
extern convar_t r_ripple;
extern convar_t r_ripple_updatetime;
extern convar_t r_ripple_spawntime;
extern convar_t r_large_lightmaps;
extern convar_t r_dlight_virtual_radius;

//
// engine shared convars
//
DECLARE_ENGINE_SHARED_CVAR_LIST()

//
// engine callbacks
//
#include "crtlib.h"

void GL_Mem_Free( void *data, const char *filename, int fileline );
void *GL_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
	ALLOC_CHECK( 2 ) MALLOC_LIKE( GL_Mem_Free, 1 ) WARN_UNUSED_RESULT;

#define Mem_Malloc( pool, size ) GL_Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) GL_Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngfuncs._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) GL_Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) gEngfuncs._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) gEngfuncs._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) gEngfuncs._Mem_EmptyPool( pool, __FILE__, __LINE__ )

#endif // PVR_LOCAL_H
