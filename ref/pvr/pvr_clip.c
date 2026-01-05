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
	if( v[0].pos.z >= -v[0].pos.w ) mask |= 1;
	if( v[1].pos.z >= -v[1].pos.w ) mask |= 2;
	if( v[2].pos.z >= -v[2].pos.w ) mask |= 4;
	return mask;
}

static inline uint32_t PVR_LerpARGB( uint32_t c1, uint32_t c2, uint8_t ti )
{
	uint32_t rb = ((((c2 & 0x00FF00FF) - (c1 & 0x00FF00FF)) * ti) >> 8) + (c1 & 0x00FF00FF);
	uint32_t g  = ((((c2 & 0x0000FF00) - (c1 & 0x0000FF00)) * ti) >> 8) + (c1 & 0x0000FF00);
	uint32_t a  = ((((c2 >> 24) - (c1 >> 24)) * ti) >> 8) + (c1 >> 24);
	return (a << 24) | (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

// Clip edge from v1 to v2 against near plane in clip space (w+z >= 0).
static inline void PVR_NearZ_ClipEdge( const ClipVert_t *v1, const ClipVert_t *v2, ClipVert_t *out )
{
	const float d0 = v1->pos.w + v1->pos.z;
	const float d1 = v2->pos.w + v2->pos.z;

	// t = d0 / (d0 - d1) but using FSRRA for reciprocal
	const float t = fabsf( d0 ) * shz_invf_fsrra( d1 - d0 );

	out->pos.x = shz_lerpf( v1->pos.x, v2->pos.x, t );
	out->pos.y = shz_lerpf( v1->pos.y, v2->pos.y, t );
	out->pos.z = shz_lerpf( v1->pos.z, v2->pos.z, t );
	out->pos.w = shz_lerpf( v1->pos.w, v2->pos.w, t );

	out->u = shz_lerpf( v1->u, v2->u, t );
	out->v = shz_lerpf( v1->v, v2->v, t );

	// Color lerp
	{
		const uint8_t ti = (uint8_t)(t * 255.0f);
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


