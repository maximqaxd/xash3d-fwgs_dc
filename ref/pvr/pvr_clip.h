#ifndef PVR_CLIP_H
#define PVR_CLIP_H

#include <stdint.h>

#include <dc/pvr.h>
#include <sh4zam/shz_sh4zam.h>

// Vertices with w < z + PVR_NEAR_CLIP_EPSILON are treated as behind the near plane
// (avoids triangles straddling the plane that pop or project badly).
#define PVR_NEAR_CLIP_EPSILON 1e-4f

// Clip a single triangle against the near plane in clip space and submit it.
// NOTE: Our projection (sh4zam shz_xmtrx_apply_perspective) produces clip.z = near_z and clip.w = -z_eye,
// so the correct near-plane test is (w >= z), NOT the OpenGL-style (z >= -w).
// - Inputs are clip-space positions (after MVP), with U/V and packed ARGB per vertex.
// - Output is submitted as a triangle strip: either 3 verts (1 tri) or 4 verts (2 tris).
void PVR_ClipAndSubmitTriangle(
	pvr_dr_state_t *dr_state,
	shz_vec4_t p0, shz_vec4_t p1, shz_vec4_t p2,
	float u0, float v0, float u1, float v1, float u2, float v2,
	uint32_t c0, uint32_t c1, uint32_t c2
);

// Same as PVR_ClipAndSubmitTriangle, but adds a small positive z-bias to the submitted vertices.
// In this renderer we submit z = 1/w, so adding bias makes the polygon slightly "nearer"
// and reduces z-fighting for overlays like decals.
void PVR_ClipAndSubmitTriangleZBias(
	pvr_dr_state_t *dr_state,
	shz_vec4_t p0, shz_vec4_t p1, shz_vec4_t p2,
	float u0, float v0, float u1, float v1, float u2, float v2,
	uint32_t c0, uint32_t c1, uint32_t c2,
	float z_bias
);

#endif // PVR_CLIP_H


