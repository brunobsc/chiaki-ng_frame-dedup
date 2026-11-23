// Light compression artifact reduction for chiaki-ng.
//
// A small edge-preserving (bilateral) filter on luma. Low-amplitude noise left
// by video compression in flat and textured areas (mosquito noise, faint
// blocking, shimmer) is averaged away, while real edges, whose difference to
// the centre pixel is large, keep almost zero weight and stay sharp.
// Meant to run before sharpening so the sharpener does not amplify the noise.

//!PARAM strength
//!DESC Artifact reduction amount (0 = off, 1 = strongest)
//!TYPE float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.5

//!HOOK LUMA
//!BIND HOOKED
//!DESC chiaki-ng compression artifact reduction
//!WHEN strength 0 >

#define RADIUS 2
#define SIGMA_S 1.5

vec4 hook()
{
	vec4 center = HOOKED_texOff(vec2(0.0, 0.0));
	float c = center.x;

	// Range sigma in 0..1 luma units: ~0.01 (very light) .. ~0.05 (strong)
	float sigma_r = mix(0.01, 0.05, strength);
	float inv_r = -0.5 / (sigma_r * sigma_r);
	float inv_s = -0.5 / (SIGMA_S * SIGMA_S);

	float sum = 0.0;
	float wsum = 0.0;
	for (int y = -RADIUS; y <= RADIUS; y++) {
		for (int x = -RADIUS; x <= RADIUS; x++) {
			float v = HOOKED_texOff(vec2(float(x), float(y))).x;
			float dv = v - c;
			float w = exp(float(x * x + y * y) * inv_s + dv * dv * inv_r);
			sum += v * w;
			wsum += w;
		}
	}
	center.x = sum / wsum;
	return center;
}
