#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <wlr/kfx/render.h>
#include <wlr/kfx/scene.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include "render/color.h"
#include "types/wlr_scene.h"

struct kfx_below_leaf {
	struct wlr_scene_node *node;
	int x, y;
	struct wlr_box box;
	pixman_region32_t visible;
	pixman_region32_t opaque;
};

struct kfx_below_collect_ctx {
	struct wlr_scene_node *stop_node;
	struct wlr_box clamped_logical;
	struct kfx_below_leaf *leaves;
	size_t count;
	size_t cap;
	bool stopped;
	bool oom;
};

static bool kfx_scene_node_invisible(struct wlr_scene_node *node) {
	if (node->type == WLR_SCENE_NODE_TREE) {
		return true;
	} else if (node->type == WLR_SCENE_NODE_RECT) {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
		return rect->color[3] == 0.f;
	} else if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
		return buffer->buffer == NULL && buffer->texture == NULL;
	}
	return false;
}

static void kfx_scale_region(pixman_region32_t *region, float scale, bool round_up) {
	wlr_region_scale(region, region, scale);
	if (round_up && floor(scale) != scale) {
		wlr_region_expand(region, region, 1);
	}
}

static int kfx_scale_length(int length, int offset, float scale) {
	return (int)round((offset + length) * scale) - (int)round(offset * scale);
}

static void kfx_scale_box(struct wlr_box *box, float scale) {
	box->width = kfx_scale_length(box->width, box->x, scale);
	box->height = kfx_scale_length(box->height, box->y, scale);
	box->x = (int)round(box->x * scale);
	box->y = (int)round(box->y * scale);
}

static float kfx_get_luminance_multiplier(const struct wlr_color_luminances *src_lum,
		const struct wlr_color_luminances *dst_lum) {
	return (dst_lum->reference / src_lum->reference) * (src_lum->max / dst_lum->max);
}

static void kfx_collect_leaves_below(struct wlr_scene_node *node,
		int lx, int ly, struct kfx_below_collect_ctx *ctx) {
	if (ctx->stopped || ctx->oom) {
		return;
	}
	if (node == ctx->stop_node) {
		ctx->stopped = true;
		return;
	}
	if (!node->enabled) {
		return;
	}

	if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &tree->children, link) {
			kfx_collect_leaves_below(child, lx + child->x, ly + child->y, ctx);
			if (ctx->stopped || ctx->oom) {
				return;
			}
		}
		return;
	}

	struct wlr_kfx_node_hook *hook = kfx_scene_node_get_hook(node);
	if (hook != NULL && hook->stage == WLR_KFX_INSTEAD) {
		return;
	}
	if (kfx_scene_node_invisible(node)) {
		return;
	}

	struct wlr_box node_box = { .x = lx, .y = ly };
	scene_node_get_size(node, &node_box.width, &node_box.height);

	struct wlr_box inter;
	if (!wlr_box_intersection(&inter, &node_box, &ctx->clamped_logical)) {
		return;
	}

	if (ctx->count == ctx->cap) {
		size_t new_cap = ctx->cap == 0 ? 16 : ctx->cap * 2;
		struct kfx_below_leaf *grown =
			realloc(ctx->leaves, new_cap * sizeof(*grown));
		if (grown == NULL) {
			ctx->oom = true;
			return;
		}
		ctx->leaves = grown;
		ctx->cap = new_cap;
	}

	struct kfx_below_leaf *leaf = &ctx->leaves[ctx->count++];
	*leaf = (struct kfx_below_leaf){
		.node = node,
		.x = lx,
		.y = ly,
		.box = node_box,
	};
	pixman_region32_init(&leaf->visible);
	pixman_region32_init(&leaf->opaque);
}

bool wlr_kfx_scene_output_render_below(struct wlr_scene_output *scene_output,
		struct wlr_scene_node *node, const struct wlr_box *box,
		struct wlr_kfx_target *dst, int downscale, struct wlr_box *out_src_box) {
	if (out_src_box != NULL) {
		*out_src_box = (struct wlr_box){0};
	}
	if (scene_output == NULL || node == NULL || box == NULL || dst == NULL ||
			downscale <= 0 || wlr_box_empty(box)) {
		return false;
	}

	struct wlr_output *output = scene_output->output;
	if (output == NULL || output->renderer == NULL) {
		return false;
	}

	int trans_width = 0, trans_height = 0;
	wlr_output_transformed_resolution(output, &trans_width, &trans_height);
	float scale = output->scale;
	if (scale <= 0.0f || trans_width <= 0 || trans_height <= 0) {
		return false;
	}

	struct wlr_box output_logical = {
		.x = scene_output->x,
		.y = scene_output->y,
		.width = (int)(trans_width / scale),
		.height = (int)(trans_height / scale),
	};

	struct wlr_box clamped_logical;
	if (!wlr_box_intersection(&clamped_logical, box, &output_logical)) {
		return false;
	}

	enum wl_output_transform inv_transform =
		wlr_output_transform_invert(output->transform);

	struct wlr_box buf_box = {
		.x = clamped_logical.x - scene_output->x,
		.y = clamped_logical.y - scene_output->y,
		.width = clamped_logical.width,
		.height = clamped_logical.height,
	};
	kfx_scale_box(&buf_box, scale);
	wlr_box_transform(&buf_box, &buf_box, inv_transform, trans_width, trans_height);

	int x0 = (buf_box.x / downscale) * downscale;
	if (x0 < 0) {
		x0 = 0;
	}
	int y0 = (buf_box.y / downscale) * downscale;
	if (y0 < 0) {
		y0 = 0;
	}
	int x1 = ((buf_box.x + buf_box.width + downscale - 1) / downscale) * downscale;
	int y1 = ((buf_box.y + buf_box.height + downscale - 1) / downscale) * downscale;
	int max_x1 = ((output->width + downscale - 1) / downscale) * downscale;
	int max_y1 = ((output->height + downscale - 1) / downscale) * downscale;
	if (x1 > max_x1) {
		x1 = max_x1;
	}
	if (y1 > max_y1) {
		y1 = max_y1;
	}

	int t0_x = x0 / downscale;
	int t0_y = y0 / downscale;
	int target_w = (x1 - x0) / downscale;
	int target_h = (y1 - y0) / downscale;

	int dst_w = 0, dst_h = 0;
	wlr_kfx_target_get_size(dst, &dst_w, &dst_h);
	if (target_w <= 0 || target_h <= 0 || target_w > dst_w || target_h > dst_h) {
		return false;
	}

	struct kfx_below_collect_ctx ctx = {
		.stop_node = node,
		.clamped_logical = clamped_logical,
	};
	kfx_collect_leaves_below(&scene_output->scene->tree.node,
		scene_output->scene->tree.node.x,
		scene_output->scene->tree.node.y, &ctx);
	if (ctx.oom) {
		for (size_t i = 0; i < ctx.count; i++) {
			pixman_region32_fini(&ctx.leaves[i].visible);
			pixman_region32_fini(&ctx.leaves[i].opaque);
		}
		free(ctx.leaves);
		return false;
	}

	bool calc_vis = scene_output->scene->calculate_visibility;
	pixman_region32_t remaining;
	pixman_region32_init_rect(&remaining,
		clamped_logical.x, clamped_logical.y,
		clamped_logical.width, clamped_logical.height);

	for (ssize_t i = (ssize_t)ctx.count - 1; i >= 0; i--) {
		struct kfx_below_leaf *leaf = &ctx.leaves[i];
		pixman_region32_intersect_rect(&leaf->visible, &remaining,
			leaf->box.x, leaf->box.y, leaf->box.width, leaf->box.height);
		if (calc_vis) {
			scene_node_opaque_region(leaf->node, leaf->x, leaf->y, &leaf->opaque);
			pixman_region32_subtract(&remaining, &remaining, &leaf->opaque);
		}
	}
	pixman_region32_fini(&remaining);

	struct wlr_render_pass *pass = wlr_kfx_target_begin_pass(dst);
	if (pass == NULL) {
		for (size_t i = 0; i < ctx.count; i++) {
			pixman_region32_fini(&ctx.leaves[i].visible);
			pixman_region32_fini(&ctx.leaves[i].opaque);
		}
		free(ctx.leaves);
		return false;
	}

	pixman_region32_t target_clip;
	pixman_region32_init_rect(&target_clip, 0, 0, target_w, target_h);
	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
		.box = { .x = 0, .y = 0, .width = target_w, .height = target_h },
		.color = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
		.clip = &target_clip,
		.blend_mode = WLR_RENDER_BLEND_MODE_NONE,
	});

	float eff_scale = scale / (float)downscale;
	int trans_w_ds = (int)round((double)trans_width / (double)downscale);
	int trans_h_ds = (int)round((double)trans_height / (double)downscale);
	if (trans_w_ds < 1) {
		trans_w_ds = 1;
	}
	if (trans_h_ds < 1) {
		trans_h_ds = 1;
	}

	for (size_t i = 0; i < ctx.count; i++) {
		struct kfx_below_leaf *leaf = &ctx.leaves[i];
		if (pixman_region32_empty(&leaf->visible)) {
			continue;
		}

		pixman_region32_t render_region;
		pixman_region32_init(&render_region);
		pixman_region32_copy(&render_region, &leaf->visible);
		pixman_region32_translate(&render_region, -scene_output->x, -scene_output->y);
		kfx_scale_region(&render_region, eff_scale, true);
		wlr_region_transform(&render_region, &render_region,
			inv_transform, trans_w_ds, trans_h_ds);
		pixman_region32_translate(&render_region, -t0_x, -t0_y);
		pixman_region32_intersect(&render_region, &render_region, &target_clip);
		if (pixman_region32_empty(&render_region)) {
			pixman_region32_fini(&render_region);
			continue;
		}

		struct wlr_box dst_box = {
			.x = leaf->x - scene_output->x,
			.y = leaf->y - scene_output->y,
			.width = leaf->box.width,
			.height = leaf->box.height,
		};
		kfx_scale_box(&dst_box, eff_scale);
		wlr_box_transform(&dst_box, &dst_box, inv_transform, trans_w_ds, trans_h_ds);
		dst_box.x -= t0_x;
		dst_box.y -= t0_y;

		pixman_region32_t opaque;
		pixman_region32_init(&opaque);
		pixman_region32_copy(&opaque, &leaf->opaque);
		pixman_region32_translate(&opaque, -scene_output->x, -scene_output->y);
		kfx_scale_region(&opaque, eff_scale, false);
		wlr_region_transform(&opaque, &opaque,
			inv_transform, trans_w_ds, trans_h_ds);
		pixman_region32_translate(&opaque, -t0_x, -t0_y);
		pixman_region32_subtract(&opaque, &render_region, &opaque);

		switch (leaf->node->type) {
		case WLR_SCENE_NODE_TREE:
			assert(false);
			break;
		case WLR_SCENE_NODE_RECT:;
			struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(leaf->node);
			wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
				.box = dst_box,
				.color = {
					.r = scene_rect->color[0],
					.g = scene_rect->color[1],
					.b = scene_rect->color[2],
					.a = scene_rect->color[3],
				},
				.clip = &render_region,
			});
			break;
		case WLR_SCENE_NODE_BUFFER:;
			struct wlr_scene_buffer *scene_buffer =
				wlr_scene_buffer_from_node(leaf->node);
			if (scene_buffer->is_single_pixel_buffer) {
				wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
					.box = dst_box,
					.color = {
						.r = (float)scene_buffer->single_pixel_buffer_color[0] / (float)UINT32_MAX,
						.g = (float)scene_buffer->single_pixel_buffer_color[1] / (float)UINT32_MAX,
						.b = (float)scene_buffer->single_pixel_buffer_color[2] / (float)UINT32_MAX,
						.a = (float)scene_buffer->single_pixel_buffer_color[3] /
							(float)UINT32_MAX * scene_buffer->opacity,
					},
					.clip = &render_region,
				});
				break;
			}

			struct wlr_texture *texture =
				scene_buffer_get_texture(scene_buffer, output->renderer);
			if (texture == NULL) {
				break;
			}

			enum wl_output_transform transform =
				wlr_output_transform_invert(scene_buffer->transform);
			transform = wlr_output_transform_compose(transform, output->transform);

			struct wlr_color_primaries primaries = {0};
			if (scene_buffer->primaries != 0) {
				wlr_color_primaries_from_named(&primaries, scene_buffer->primaries);
			}

			struct wlr_color_luminances src_lum, srgb_lum;
			wlr_color_transfer_function_get_default_luminance(
				scene_buffer->transfer_function, &src_lum);
			wlr_color_transfer_function_get_default_luminance(
				WLR_COLOR_TRANSFER_FUNCTION_SRGB, &srgb_lum);
			float luminance_multiplier =
				kfx_get_luminance_multiplier(&src_lum, &srgb_lum);

			wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
				.texture = texture,
				.src_box = scene_buffer->src_box,
				.dst_box = dst_box,
				.transform = transform,
				.clip = &render_region,
				.alpha = &scene_buffer->opacity,
				.filter_mode = scene_buffer->filter_mode,
				.blend_mode = !calc_vis || !pixman_region32_empty(&opaque) ?
					WLR_RENDER_BLEND_MODE_PREMULTIPLIED : WLR_RENDER_BLEND_MODE_NONE,
				.transfer_function = scene_buffer->transfer_function,
				.primaries = scene_buffer->primaries != 0 ? &primaries : NULL,
				.color_encoding = scene_buffer->color_encoding,
				.color_range = scene_buffer->color_range,
				.luminance_multiplier = &luminance_multiplier,
				.wait_timeline = scene_buffer->wait_timeline,
				.wait_point = scene_buffer->wait_point,
			});
			break;
		}

		pixman_region32_fini(&opaque);
		pixman_region32_fini(&render_region);
	}

	pixman_region32_fini(&target_clip);
	for (size_t i = 0; i < ctx.count; i++) {
		pixman_region32_fini(&ctx.leaves[i].visible);
		pixman_region32_fini(&ctx.leaves[i].opaque);
	}
	free(ctx.leaves);

	if (!wlr_render_pass_submit(pass)) {
		return false;
	}

	if (out_src_box != NULL) {
		*out_src_box = (struct wlr_box){
			.x = t0_x,
			.y = t0_y,
			.width = target_w,
			.height = target_h,
		};
	}
	return true;
}
