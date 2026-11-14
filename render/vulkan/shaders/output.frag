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
layout (constant_id = 1) const int OUTPUT_DITHER = 0;

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

float output_dither_lsb(vec2 frag_coord) {
	const int rank_table[64] = int[64](
		 0, 48, 12, 60,  3, 51, 15, 63,
		32, 16, 44, 28, 35, 19, 47, 31,
		 8, 56,  4, 52, 11, 59,  7, 55,
		40, 24, 36, 20, 43, 27, 39, 23,
		 2, 50, 14, 62,  1, 49, 13, 61,
		34, 18, 46, 30, 33, 17, 45, 29,
		10, 58,  6, 54,  9, 57,  5, 53,
		42, 26, 38, 22, 41, 25, 37, 21
	);
	ivec2 p = ivec2(frag_coord) & 7;
	int r = rank_table[p.y * 8 + p.x];
	return ((float(r) + 0.5) / 64.0 - 0.5) / 255.0;
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

	if (OUTPUT_DITHER != 0) {
		float d = output_dither_lsb(gl_FragCoord.xy);
		rgb = clamp(rgb + vec3(d), vec3(0.0), vec3(1.0));
	}

	// Back to pre-multiplied alpha
	out_color = vec4(rgb * alpha, alpha);
}
