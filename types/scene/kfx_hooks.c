#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/kfx/scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>

#include "render/color.h"
#include "types/wlr_scene.h"

struct kfx_output_state {
	struct wlr_addon addon;
	bool require_two_pass;
	bool dither;
	bool tonemap;
	bool color_dirty;
	float sdr_white_nits;
	float peak_luminance_nits;
};

static const int kfx_hook_addon_owner = 0;
static const int kfx_output_addon_owner = 0;

static void kfx_hook_addon_destroy(struct wlr_addon *addon) {
	struct wlr_kfx_node_hook *hook = wl_container_of(addon, hook, addon);
	wlr_addon_finish(&hook->addon);
	hook->active = false;
	hook->node = NULL;
}

static const struct wlr_addon_interface kfx_hook_addon_impl = {
	.name = "wlr_kfx_node_hook",
	.destroy = kfx_hook_addon_destroy,
};

static void kfx_output_addon_destroy(struct wlr_addon *addon) {
	struct kfx_output_state *state = wl_container_of(addon, state, addon);
	wlr_addon_finish(&state->addon);
	free(state);
}

static const struct wlr_addon_interface kfx_output_addon_impl = {
	.name = "wlr_kfx_output_two_pass",
	.destroy = kfx_output_addon_destroy,
};

bool wlr_kfx_node_hook_init(struct wlr_kfx_node_hook *hook,
		struct wlr_scene_node *leaf, enum wlr_kfx_hook_stage stage,
		const struct wlr_kfx_node_hook_impl *impl) {
	if (hook == NULL || leaf == NULL || impl == NULL || impl->render == NULL) {
		return false;
	}
	if (leaf->type != WLR_SCENE_NODE_RECT && leaf->type != WLR_SCENE_NODE_BUFFER) {
		return false;
	}
	if (stage != WLR_KFX_BEFORE && stage != WLR_KFX_INSTEAD && stage != WLR_KFX_AFTER) {
		return false;
	}
	if (wlr_addon_find(&leaf->addons, &kfx_hook_addon_owner, &kfx_hook_addon_impl) != NULL) {
		return false;
	}

	hook->stage = stage;
	hook->impl = impl;
	hook->node = leaf;
	hook->active = true;
	wlr_addon_init(&hook->addon, &leaf->addons, &kfx_hook_addon_owner, &kfx_hook_addon_impl);
	return true;
}

void wlr_kfx_node_hook_finish(struct wlr_kfx_node_hook *hook) {
	if (hook == NULL || !hook->active) {
		return;
	}
	hook->active = false;
	hook->node = NULL;
	wlr_addon_finish(&hook->addon);
}

struct wlr_kfx_node_hook *kfx_scene_node_get_hook(struct wlr_scene_node *node) {
	if (node == NULL || (node->type != WLR_SCENE_NODE_RECT &&
			node->type != WLR_SCENE_NODE_BUFFER)) {
		return NULL;
	}
	struct wlr_addon *addon = wlr_addon_find(&node->addons,
		&kfx_hook_addon_owner, &kfx_hook_addon_impl);
	if (addon == NULL) {
		return NULL;
	}
	struct wlr_kfx_node_hook *hook = wl_container_of(addon, hook, addon);
	return hook->active ? hook : NULL;
}

static struct kfx_output_state *kfx_output_get_or_create_state(
		struct wlr_output *output, bool create) {
	if (output == NULL || output->addons.addons.next == NULL) {
		return NULL;
	}
	struct wlr_addon *addon = wlr_addon_find(&output->addons,
		&kfx_output_addon_owner, &kfx_output_addon_impl);
	if (addon != NULL) {
		struct kfx_output_state *state = wl_container_of(addon, state, addon);
		return state;
	}
	if (!create) {
		return NULL;
	}
	struct kfx_output_state *state = calloc(1, sizeof(*state));
	if (state == NULL) {
		return NULL;
	}
	wlr_addon_init(&state->addon, &output->addons,
		&kfx_output_addon_owner, &kfx_output_addon_impl);
	return state;
}

static void kfx_output_cleanup_state_if_idle(struct kfx_output_state *state) {
	if (state != NULL && !state->require_two_pass && !state->dither &&
			!state->tonemap && state->sdr_white_nits <= 0.0f &&
			state->peak_luminance_nits <= 0.0f && !state->color_dirty) {
		kfx_output_addon_destroy(&state->addon);
	}
}

void wlr_kfx_output_set_two_pass(struct wlr_output *output, bool require_two_pass) {
	struct kfx_output_state *state =
		kfx_output_get_or_create_state(output, require_two_pass);
	if (state == NULL) {
		return;
	}
	state->require_two_pass = require_two_pass;
	kfx_output_cleanup_state_if_idle(state);
}

void wlr_kfx_output_set_dither(struct wlr_output *output, bool enable_dither) {
	struct kfx_output_state *state =
		kfx_output_get_or_create_state(output, enable_dither);
	if (state == NULL) {
		return;
	}
	state->dither = enable_dither;
	kfx_output_cleanup_state_if_idle(state);
}

void wlr_kfx_output_set_sdr_white_nits(struct wlr_output *output, float sdr_white_nits) {
	float norm = (isfinite(sdr_white_nits) && sdr_white_nits > 0.0f) ? sdr_white_nits : 0.0f;
	struct kfx_output_state *state =
		kfx_output_get_or_create_state(output, norm > 0.0f);
	if (state == NULL) {
		return;
	}
	if (fabsf(state->sdr_white_nits - norm) > 1e-4f) {
		state->sdr_white_nits = norm;
		state->color_dirty = true;
	}
	kfx_output_cleanup_state_if_idle(state);
}

float wlr_kfx_output_get_sdr_white_nits(struct wlr_output *output) {
	return kfx_output_get_sdr_white_nits(output);
}

void wlr_kfx_output_set_tonemap(struct wlr_output *output, bool enable_tonemap,
		float peak_luminance_nits) {
	float norm_peak = (isfinite(peak_luminance_nits) && peak_luminance_nits > 0.0f) ?
		peak_luminance_nits : 0.0f;
	struct kfx_output_state *state =
		kfx_output_get_or_create_state(output, enable_tonemap || norm_peak > 0.0f);
	if (state == NULL) {
		return;
	}
	if (state->tonemap != enable_tonemap ||
			fabsf(state->peak_luminance_nits - norm_peak) > 1e-4f) {
		state->tonemap = enable_tonemap;
		state->peak_luminance_nits = norm_peak;
		state->color_dirty = true;
	}
	kfx_output_cleanup_state_if_idle(state);
}

bool wlr_kfx_output_get_tonemap(struct wlr_output *output, float *out_peak_luminance_nits) {
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	if (out_peak_luminance_nits != NULL) {
		*out_peak_luminance_nits = state != NULL ? state->peak_luminance_nits : 0.0f;
	}
	return state != NULL && state->tonemap;
}

float wlr_kfx_output_get_hdr_headroom(struct wlr_output *output) {
	if (output == NULL) {
		return 1.0f;
	}
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	if (output->image_description != NULL) {
		struct wlr_color_luminances dst_lum;
		wlr_color_transfer_function_get_default_luminance(
			output->image_description->transfer_function, &dst_lum);
		float ref = (state != NULL && state->sdr_white_nits > 0.0f) ?
			state->sdr_white_nits : dst_lum.reference;
		float peak = (state != NULL && state->peak_luminance_nits > 0.0f) ?
			state->peak_luminance_nits : dst_lum.max;
		if (ref > 0.0f && peak > ref) {
			return peak / ref;
		}
		return 1.0f;
	}
	return 1.0f;
}

bool kfx_output_requires_two_pass(struct wlr_output *output) {
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	return state != NULL && (state->require_two_pass || state->dither ||
		state->tonemap || state->sdr_white_nits > 0.0f);
}

bool kfx_output_get_dither(struct wlr_output *output) {
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	return state != NULL && state->dither;
}

bool kfx_output_consume_color_dirty(struct wlr_output *output) {
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	if (state == NULL || !state->color_dirty) {
		return false;
	}
	state->color_dirty = false;
	kfx_output_cleanup_state_if_idle(state);
	return true;
}

float kfx_output_get_sdr_white_nits(struct wlr_output *output) {
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	return state != NULL ? state->sdr_white_nits : 0.0f;
}

static float kfx_linear_to_pq(float x) {
	const float c1 = 0.8359375f;
	const float c2 = 18.8515625f;
	const float c3 = 18.6875f;
	const float m = 78.84375f;
	const float n = 0.1593017578125f;
	if (x <= 0.0f) {
		return 0.0f;
	}
	if (x >= 1.0f) {
		return 1.0f;
	}
	float pow_n = powf(x, n);
	return powf((c1 + c2 * pow_n) / (1.0f + c3 * pow_n), m);
}

bool kfx_output_get_tonemap_max_pq(struct wlr_output *output, float *out_max_pq) {
	if (out_max_pq != NULL) {
		*out_max_pq = 0.0f;
	}
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	if (state == NULL || !state->tonemap ||
			state->peak_luminance_nits <= 0.0f ||
			state->peak_luminance_nits >= 10000.0f) {
		return false;
	}
	float max_pq = kfx_linear_to_pq(state->peak_luminance_nits / 10000.0f);
	if (max_pq <= 0.0f || max_pq >= 1.0f) {
		return false;
	}
	if (out_max_pq != NULL) {
		*out_max_pq = max_pq;
	}
	return true;
}
