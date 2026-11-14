#version 450

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput in_color;

layout(set = 1, binding = 0) uniform sampler3D lut_3d;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

/* struct wlr_vk_frag_output_pcr_data */
layout(push_constant, row_major) uniform UBO {
	layout(offset = 80) mat4 matrix;
	float lut_3d_offset;
	float lut_3d_scale;
} data;

layout (constant_id = 0) const int OUTPUT_TRANSFORM = 0;
layout (constant_id = 1) const int OUTPUT_DITHER_QUANT = 0;

// Matches enum wlr_vk_output_transform
#define OUTPUT_TRANSFORM_IDENTITY 0
#define OUTPUT_TRANSFORM_INVERSE_SRGB 1
#define OUTPUT_TRANSFORM_INVERSE_ST2084_PQ 2
#define OUTPUT_TRANSFORM_LUT_3D 3
#define OUTPUT_TRANSFORM_INVERSE_GAMMA22 4
#define OUTPUT_TRANSFORM_INVERSE_BT1886 5

float linear_channel_to_srgb(float x) {
	return max(min(x * 12.92, 0.04045), 1.055 * pow(x, 1. / 2.4) - 0.055);
}

vec3 linear_color_to_srgb(vec3 color) {
	return vec3(
		linear_channel_to_srgb(color.r),
		linear_channel_to_srgb(color.g),
		linear_channel_to_srgb(color.b)
	);
}

vec3 linear_color_to_pq(vec3 color) {
	// H.273 TransferCharacteristics code point 16
	float c1 = 0.8359375;
	float c2 = 18.8515625;
	float c3 = 18.6875;
	float m = 78.84375;
	float n = 0.1593017578125;
	vec3 pow_n = pow(clamp(color, vec3(0), vec3(1)), vec3(n));
	return pow((vec3(c1) + c2 * pow_n) / (vec3(1) + c3 * pow_n), vec3(m));
}

vec3 linear_color_to_bt1886(vec3 color) {
	float Lmin = 0.01;
	float Lmax = 100.0;
	float lb = pow(Lmin, 1.0 / 2.4);
	float lw = pow(Lmax, 1.0 / 2.4);
	float a  = pow(lw - lb, 2.4);
	float b  = lb / (lw - lb);
	vec3 L = color * (Lmax - Lmin) + vec3(Lmin);
	return pow(L / a, vec3(1.0 / 2.4)) - vec3(b);
}

// Ulichney (1993) 16x16 toroidal void-and-cluster blue-noise rank table (0..255)
float output_dither_blue_noise(vec2 frag_coord) {
	const int blue_noise16[256] = int[256](
		233, 108, 142,  79,  13, 185,  70, 208, 130, 245, 158,  30,  56, 253,  81, 194,
		 10, 173,  37, 205, 231, 124,  33, 156,  16,  77, 195, 222, 143, 172,  23, 118,
		 58, 246,  93, 161,  54,  91, 225, 198, 104,  49, 119,   0,  73, 102, 223, 155,
		186, 122,  26, 133, 189,   3, 145,  60, 250, 171, 214, 152, 244,  39, 199,  87,
		 43, 226, 201,  67, 241, 112, 178,  38, 136,  95,  25,  83, 175, 126,  19, 140,
		 76,   8, 107, 169,  20, 215,  74, 232,   9, 191, 236,  47, 206,  66, 239, 217,
		177, 148, 255,  86,  46, 150,  97, 128, 162,  69, 117, 146, 106,   6, 160, 113,
		 55, 211,  27, 135, 234, 188,  17, 203,  44, 224, 174,  29, 252, 184,  84,  34,
		127,  98, 192,  65, 116, 167,  59, 249, 103,  21, 209,  92,  52, 134, 202, 228,
		 11, 238, 165,   4, 219,  36,  90, 137, 154,  75, 129, 159, 221,  24,  68, 157,
		182,  40,  80, 144, 101, 200, 235,  12, 196, 227,   1,  62, 190, 120, 247,  94,
		218, 114, 204, 248,  48, 132,  72, 170,  53, 111, 180, 242, 100,  14, 149,  51,
		139,  22,  61, 163,  15, 181, 115,  28, 254,  85,  35, 141,  45, 213, 176,  78,
		243, 197, 123,  89, 212, 229,  82, 193, 153, 125, 210, 164,  71, 105, 230,  32,
		 96,   2, 183, 151,  31,  57, 138,   5, 220,  63,  18, 237, 187,   7, 121, 168,
		 64, 216,  50, 251, 110, 166, 240,  99,  42, 179, 109,  88, 131, 207,  41, 147
	);
	ivec2 p = ivec2(frag_coord) & 15;
	int r = blue_noise16[p.y * 16 + p.x];
	return (float(r) + 0.5) / 256.0 - 0.5;
}

void main() {
	vec4 in_color = subpassLoad(in_color).rgba;

	// Convert from pre-multiplied alpha to straight alpha
	float alpha = in_color.a;
	vec3 rgb;
	if (alpha == 0) {
		rgb = vec3(0);
	} else {
		rgb = in_color.rgb / alpha;
	}

	rgb = mat3(data.matrix) * rgb;

	if (OUTPUT_TRANSFORM != OUTPUT_TRANSFORM_IDENTITY) {
		rgb = max(rgb, vec3(0));
	}
	if (OUTPUT_TRANSFORM == OUTPUT_TRANSFORM_LUT_3D) {
		// Apply 3D LUT
		vec3 pos = data.lut_3d_offset + rgb * data.lut_3d_scale;
		rgb = texture(lut_3d, pos).rgb;
	} else if (OUTPUT_TRANSFORM == OUTPUT_TRANSFORM_INVERSE_ST2084_PQ) {
		rgb = linear_color_to_pq(rgb);
	} else if (OUTPUT_TRANSFORM == OUTPUT_TRANSFORM_INVERSE_SRGB) {
		// Produce sRGB encoded values
		rgb = linear_color_to_srgb(rgb);
	} else if (OUTPUT_TRANSFORM == OUTPUT_TRANSFORM_INVERSE_GAMMA22) {
		rgb = pow(rgb, vec3(1. / 2.2));
	} else if (OUTPUT_TRANSFORM == OUTPUT_TRANSFORM_INVERSE_BT1886) {
		rgb = linear_color_to_bt1886(rgb);
	}

	if (OUTPUT_DITHER_QUANT > 0) {
		float d = output_dither_blue_noise(gl_FragCoord.xy) / float(OUTPUT_DITHER_QUANT);
		rgb = clamp(rgb + vec3(d), vec3(0.0), vec3(1.0));
	}

	// Back to pre-multiplied alpha
	out_color = vec4(rgb * alpha, alpha);
}
