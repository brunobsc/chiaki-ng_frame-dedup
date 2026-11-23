// Native-resolution contrast adaptive sharpening for chiaki-ng.
//
// Written for chiaki-ng after the algorithm described in AMD FidelityFX
// Contrast Adaptive Sharpening (CAS, MIT licensed): sharpening strength is
// scaled per pixel by the local contrast headroom, so soft areas are sharpened
// more than edges that are already crisp and halos stay small. Runs on luma
// only, at the stream's own resolution (no scaling), so it also works when the
// stream and the display have the same resolution.

//!PARAM sharpness
//!DESC Sharpening amount (0 = off, 1 = strongest)
//!TYPE float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.5

//!HOOK LUMA
//!BIND HOOKED
//!DESC chiaki-ng native sharpening (CAS)
//!WHEN sharpness 0 >

vec4 hook()
{
	// 3x3 neighbourhood
	//  a b c
	//  d e f
	//  g h i
	float a = HOOKED_texOff(vec2(-1.0, -1.0)).x;
	float b = HOOKED_texOff(vec2( 0.0, -1.0)).x;
	float c = HOOKED_texOff(vec2( 1.0, -1.0)).x;
	float d = HOOKED_texOff(vec2(-1.0,  0.0)).x;
	vec4 center = HOOKED_texOff(vec2(0.0, 0.0));
	float e = center.x;
	float f = HOOKED_texOff(vec2( 1.0,  0.0)).x;
	float g = HOOKED_texOff(vec2(-1.0,  1.0)).x;
	float h = HOOKED_texOff(vec2( 0.0,  1.0)).x;
	float i = HOOKED_texOff(vec2( 1.0,  1.0)).x;

	// Soft min/max: cross plus full ring, as in CAS
	float mn_cross = min(min(min(d, e), min(f, b)), h);
	float mx_cross = max(max(max(d, e), max(f, b)), h);
	float mn = mn_cross + min(mn_cross, min(min(a, c), min(g, i)));
	float mx = mx_cross + max(mx_cross, max(max(a, c), max(g, i)));

	// Contrast headroom -> amount (0..1)
	float amp = clamp(min(mn, 2.0 - mx) / max(mx, 1e-5), 0.0, 1.0);
	amp = sqrt(amp);

	// Negative lobe weight: -1/8 (soft) .. -1/5 (strong)
	float w = amp * (-1.0 / mix(8.0, 5.0, sharpness));

	float result = ((b + d + f + h) * w + e) / (1.0 + 4.0 * w);
	center.x = clamp(result, 0.0, 1.0);
	return center;
}
