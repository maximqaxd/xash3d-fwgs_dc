/*
pvr_clip.c - near-plane clipping helpers for PVR direct rendering
*/

#include "pvr_local.h"
#include "pvr_clip.h"

typedef struct ClipVert_s
{
	shz_vec4_t pos;   // clip-space (x,y,z,w)
	float u, v;
	uint32_t argb;
} ClipVert_t;

// Returns: bit0 = v0 visible, bit1 = v1 visible, bit2 = v2 visible
static inline unsigned PVR_NearZ_VisMaskTri( const ClipVert_t *v )
{
	unsigned mask = 0;
	// sh4zam perspective: clip.z == near_z, clip.w == -z_eye -> inside when (w >= z)
	if( v[0].pos.w >= v[0].pos.z ) mask |= 1;
	if( v[1].pos.w >= v[1].pos.z ) mask |= 2;
	if( v[2].pos.w >= v[2].pos.z ) mask |= 4;
	return mask;
}

static inline uint32_t PVR_LerpARGB( uint32_t c1, uint32_t c2, uint8_t ti )
{
	uint32_t rb = ((((c2 & 0x00FF00FF) - (c1 & 0x00FF00FF)) * ti) >> 8) + (c1 & 0x00FF00FF);
	uint32_t g  = ((((c2 & 0x0000FF00) - (c1 & 0x0000FF00)) * ti) >> 8) + (c1 & 0x0000FF00);
	uint32_t a  = ((((c2 >> 24) - (c1 >> 24)) * ti) >> 8) + (c1 >> 24);
	return (a << 24) | (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

// Clip edge from v1 to v2 against near plane in clip space.
// For our projection, near plane is (w - z >= 0).
static inline void PVR_NearZ_ClipEdge( const ClipVert_t *v1, const ClipVert_t *v2, ClipVert_t *out )
{
	const float d0 = v1->pos.w - v1->pos.z;
	const float d1 = v2->pos.w - v2->pos.z;

	// Intersection parameter for plane (w-z)=0 along segment v1->v2:
	// t = d0 / (d0 - d1)
	// d0 >= 0 is "inside", d1 < 0 is "outside" (or vice versa). This yields t in [0,1].
	const float denom = ( d0 - d1 );
	float t;
	if( fabsf( denom ) < 1e-8f )
		t = 0.0f;
	else
	{
		// IMPORTANT: use precise division here. shz_invf_fsrra() is great for positive values,
		// but near-plane clipping frequently involves negative denominators; using FSRRA-based
		// reciprocal can introduce large errors and warp geometry.
		t = d0 / denom;
	}

	// Clamp for numerical safety (FSRRA is approximate).
	if( t < 0.0f ) t = 0.0f;
	if( t > 1.0f ) t = 1.0f;

	out->pos.x = shz_lerpf( v1->pos.x, v2->pos.x, t );
	out->pos.y = shz_lerpf( v1->pos.y, v2->pos.y, t );
	out->pos.z = shz_lerpf( v1->pos.z, v2->pos.z, t );
	out->pos.w = shz_lerpf( v1->pos.w, v2->pos.w, t );

	out->u = shz_lerpf( v1->u, v2->u, t );
	out->v = shz_lerpf( v1->v, v2->v, t );

	// Color lerp
	{
		const float tf = t * 255.0f;
		const uint8_t ti = (uint8_t)bound( 0, (int)tf, 255 );
		out->argb = PVR_LerpARGB( v1->argb, v2->argb, ti );
	}
}

static inline void PVR_SubmitClipVert( pvr_dr_state_t *dr_state, const ClipVert_t *cv, uint32_t flags )
{
	const float invw = shz_invf_fsrra( cv->pos.w );

	pvr_vertex_t *vert = pvr_dr_target( *dr_state );
	vert->flags = flags;
	vert->x = cv->pos.x * invw;
	vert->y = cv->pos.y * invw;
	vert->z = invw;
	vert->u = cv->u;
	vert->v = cv->v;
	vert->argb = cv->argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );
}

static inline void PVR_SubmitClipVertZBias( pvr_dr_state_t *dr_state, const ClipVert_t *cv, uint32_t flags, float z_bias )
{
	const float invw = shz_invf_fsrra( cv->pos.w );

	pvr_vertex_t *vert = pvr_dr_target( *dr_state );
	vert->flags = flags;
	vert->x = cv->pos.x * invw;
	vert->y = cv->pos.y * invw;
	vert->z = invw + z_bias;
	vert->u = cv->u;
	vert->v = cv->v;
	vert->argb = cv->argb;
	vert->oargb = 0;
	pvr_dr_commit( vert );
}

void PVR_ClipAndSubmitTriangle(
	pvr_dr_state_t *dr_state,
	shz_vec4_t p0, shz_vec4_t p1, shz_vec4_t p2,
	float u0, float v0, float u1, float v1, float u2, float v2,
	uint32_t c0, uint32_t c1, uint32_t c2 )
{
	ClipVert_t verts[5];
	unsigned n_verts = 3;

	verts[0].pos = p0; verts[0].u = u0; verts[0].v = v0; verts[0].argb = c0;
	verts[1].pos = p1; verts[1].u = u1; verts[1].v = v1; verts[1].argb = c1;
	verts[2].pos = p2; verts[2].u = u2; verts[2].v = v2; verts[2].argb = c2;

	const unsigned vismask = PVR_NearZ_VisMaskTri( verts );

	// all behind
	if( vismask == 0 )
		return;

	// all visible
	if( vismask == 7 )
	{
		PVR_SubmitClipVert( dr_state, &verts[0], PVR_CMD_VERTEX );
		PVR_SubmitClipVert( dr_state, &verts[1], PVR_CMD_VERTEX );
		PVR_SubmitClipVert( dr_state, &verts[2], PVR_CMD_VERTEX_EOL );
		return;
	}

	// Clip cases (produce either 1 triangle (3 verts) or a quad (4 verts) submitted as strip)
	switch( vismask )
	{
	// only v0 visible
	case 1:
		PVR_NearZ_ClipEdge( &verts[0], &verts[1], &verts[1] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[2], &verts[2] );
		break;
	// only v1 visible
	case 2:
		PVR_NearZ_ClipEdge( &verts[1], &verts[0], &verts[0] );
		PVR_NearZ_ClipEdge( &verts[1], &verts[2], &verts[2] );
		break;
	// v0+v1 visible => 4 verts
	case 3:
		n_verts = 4;
		PVR_NearZ_ClipEdge( &verts[1], &verts[2], &verts[3] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[2], &verts[2] );
		break;
	// only v2 visible
	case 4:
		PVR_NearZ_ClipEdge( &verts[2], &verts[0], &verts[0] );
		PVR_NearZ_ClipEdge( &verts[2], &verts[1], &verts[1] );
		break;
	// v0+v2 visible => 4 verts
	case 5:
		n_verts = 4;
		PVR_NearZ_ClipEdge( &verts[1], &verts[2], &verts[3] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[1], &verts[1] );
		break;
	// v1+v2 visible => 4 verts
	case 6:
		n_verts = 4;
		verts[3] = verts[2];
		PVR_NearZ_ClipEdge( &verts[0], &verts[2], &verts[2] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[1], &verts[0] );
		break;
	}

	if( n_verts == 3 )
	{
		PVR_SubmitClipVert( dr_state, &verts[0], PVR_CMD_VERTEX );
		PVR_SubmitClipVert( dr_state, &verts[1], PVR_CMD_VERTEX );
		PVR_SubmitClipVert( dr_state, &verts[2], PVR_CMD_VERTEX_EOL );
	}
	else
	{
		PVR_SubmitClipVert( dr_state, &verts[0], PVR_CMD_VERTEX );
		PVR_SubmitClipVert( dr_state, &verts[1], PVR_CMD_VERTEX );
		PVR_SubmitClipVert( dr_state, &verts[2], PVR_CMD_VERTEX );
		PVR_SubmitClipVert( dr_state, &verts[3], PVR_CMD_VERTEX_EOL );
	}
}

void PVR_ClipAndSubmitTriangleZBias(
	pvr_dr_state_t *dr_state,
	shz_vec4_t p0, shz_vec4_t p1, shz_vec4_t p2,
	float u0, float v0, float u1, float v1, float u2, float v2,
	uint32_t c0, uint32_t c1, uint32_t c2,
	float z_bias )
{
	ClipVert_t verts[5];
	unsigned n_verts = 3;

	// Clamp bias to something sane (safety belt).
	if( z_bias < 0.0f ) z_bias = 0.0f;
	if( z_bias > 0.01f ) z_bias = 0.01f;

	verts[0].pos = p0; verts[0].u = u0; verts[0].v = v0; verts[0].argb = c0;
	verts[1].pos = p1; verts[1].u = u1; verts[1].v = v1; verts[1].argb = c1;
	verts[2].pos = p2; verts[2].u = u2; verts[2].v = v2; verts[2].argb = c2;

	const unsigned vismask = PVR_NearZ_VisMaskTri( verts );

	if( vismask == 0 )
		return;

	if( vismask == 7 )
	{
		PVR_SubmitClipVertZBias( dr_state, &verts[0], PVR_CMD_VERTEX, z_bias );
		PVR_SubmitClipVertZBias( dr_state, &verts[1], PVR_CMD_VERTEX, z_bias );
		PVR_SubmitClipVertZBias( dr_state, &verts[2], PVR_CMD_VERTEX_EOL, z_bias );
		return;
	}

	switch( vismask )
	{
	case 1:
		PVR_NearZ_ClipEdge( &verts[0], &verts[1], &verts[1] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[2], &verts[2] );
		break;
	case 2:
		PVR_NearZ_ClipEdge( &verts[1], &verts[0], &verts[0] );
		PVR_NearZ_ClipEdge( &verts[1], &verts[2], &verts[2] );
		break;
	case 3:
		n_verts = 4;
		PVR_NearZ_ClipEdge( &verts[1], &verts[2], &verts[3] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[2], &verts[2] );
		break;
	case 4:
		PVR_NearZ_ClipEdge( &verts[2], &verts[0], &verts[0] );
		PVR_NearZ_ClipEdge( &verts[2], &verts[1], &verts[1] );
		break;
	case 5:
		n_verts = 4;
		PVR_NearZ_ClipEdge( &verts[1], &verts[2], &verts[3] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[1], &verts[1] );
		break;
	case 6:
		n_verts = 4;
		verts[3] = verts[2];
		PVR_NearZ_ClipEdge( &verts[0], &verts[2], &verts[2] );
		PVR_NearZ_ClipEdge( &verts[0], &verts[1], &verts[0] );
		break;
	}

	if( n_verts == 3 )
	{
		PVR_SubmitClipVertZBias( dr_state, &verts[0], PVR_CMD_VERTEX, z_bias );
		PVR_SubmitClipVertZBias( dr_state, &verts[1], PVR_CMD_VERTEX, z_bias );
		PVR_SubmitClipVertZBias( dr_state, &verts[2], PVR_CMD_VERTEX_EOL, z_bias );
	}
	else
	{
		PVR_SubmitClipVertZBias( dr_state, &verts[0], PVR_CMD_VERTEX, z_bias );
		PVR_SubmitClipVertZBias( dr_state, &verts[1], PVR_CMD_VERTEX, z_bias );
		PVR_SubmitClipVertZBias( dr_state, &verts[2], PVR_CMD_VERTEX, z_bias );
		PVR_SubmitClipVertZBias( dr_state, &verts[3], PVR_CMD_VERTEX_EOL, z_bias );
	}
}


