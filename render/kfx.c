#include <stddef.h>
#include <wlr/config.h>
#include <wlr/kfx/render.h>
#include <wlr/render/wlr_renderer.h>

#if WLR_HAS_VULKAN_RENDERER
#include <wlr/render/vulkan.h>
#include "render/kfx.h"
#endif

bool wlr_kfx_is_supported(struct wlr_renderer *renderer) {
#if WLR_HAS_VULKAN_RENDERER
	return renderer != NULL && wlr_renderer_is_vk(renderer);
#else
	(void)renderer;
	return false;
#endif
}

struct wlr_kfx_shader *wlr_kfx_shader_create(struct wlr_renderer *renderer,
		const uint32_t *spirv, size_t len,
		const struct wlr_kfx_shader_layout *layout) {
#if WLR_HAS_VULKAN_RENDERER
	if (!wlr_kfx_is_supported(renderer)) {
		return NULL;
	}
	return kfx_vk_shader_create(renderer, spirv, len, layout);
#else
	(void)renderer;
	(void)spirv;
	(void)len;
	(void)layout;
	return NULL;
#endif
}

void wlr_kfx_shader_destroy(struct wlr_kfx_shader *shader) {
#if WLR_HAS_VULKAN_RENDERER
	if (shader != NULL) {
		kfx_vk_shader_destroy(shader);
	}
#else
	(void)shader;
#endif
}

void wlr_kfx_render_pass_add_draw(struct wlr_render_pass *pass,
		const struct wlr_kfx_draw_options *options) {
#if WLR_HAS_VULKAN_RENDERER
	if (pass != NULL && options != NULL) {
		kfx_vk_render_pass_add_draw(pass, options);
	}
#else
	(void)pass;
	(void)options;
#endif
}

struct wlr_render_pass *wlr_kfx_renderer_begin_buffer_pass(
		struct wlr_renderer *renderer, struct wlr_buffer *buffer,
		const struct wlr_buffer_pass_options *options, bool require_two_pass) {
#if WLR_HAS_VULKAN_RENDERER
	if (require_two_pass && renderer != NULL && wlr_renderer_is_vk(renderer)) {
		return vulkan_begin_buffer_pass_with_flags(renderer, buffer, options, true);
	}
#else
	(void)require_two_pass;
#endif
	return wlr_renderer_begin_buffer_pass(renderer, buffer, options);
}

void wlr_kfx_render_pass_set_dither(struct wlr_render_pass *pass, bool enable_dither) {
#if WLR_HAS_VULKAN_RENDERER
	if (pass != NULL) {
		kfx_vk_render_pass_set_dither(pass, enable_dither);
	}
#else
	(void)pass;
	(void)enable_dither;
#endif
}

void wlr_kfx_render_pass_set_tonemap(struct wlr_render_pass *pass,
		bool enable_tonemap, float max_pq) {
#if WLR_HAS_VULKAN_RENDERER
	if (pass != NULL) {
		kfx_vk_render_pass_set_tonemap(pass, enable_tonemap, max_pq);
	}
#else
	(void)pass;
	(void)enable_tonemap;
	(void)max_pq;
#endif
}

struct wlr_kfx_target *wlr_kfx_target_acquire(struct wlr_renderer *renderer,
		int width, int height) {
#if WLR_HAS_VULKAN_RENDERER
	if (!wlr_kfx_is_supported(renderer)) {
		return NULL;
	}
	return kfx_vk_target_acquire(renderer, width, height);
#else
	(void)renderer;
	(void)width;
	(void)height;
	return NULL;
#endif
}

void wlr_kfx_target_release(struct wlr_kfx_target *target) {
#if WLR_HAS_VULKAN_RENDERER
	if (target != NULL) {
		kfx_vk_target_release(target);
	}
#else
	(void)target;
#endif
}

void wlr_kfx_target_get_size(struct wlr_kfx_target *target,
		int *width, int *height) {
	if (width != NULL) {
		*width = 0;
	}
	if (height != NULL) {
		*height = 0;
	}
#if WLR_HAS_VULKAN_RENDERER
	if (target != NULL) {
		kfx_vk_target_get_size(target, width, height);
	}
#else
	(void)target;
#endif
}

struct wlr_render_pass *wlr_kfx_target_begin_pass(struct wlr_kfx_target *target) {
#if WLR_HAS_VULKAN_RENDERER
	if (target != NULL) {
		return kfx_vk_target_begin_pass(target);
	}
#else
	(void)target;
#endif
	return NULL;
}

bool wlr_kfx_renderer_run_target_chain(struct wlr_renderer *renderer,
		const struct wlr_kfx_target_draw_step *steps, size_t n_steps) {
#if WLR_HAS_VULKAN_RENDERER
	if (!wlr_kfx_is_supported(renderer)) {
		return false;
	}
	return kfx_vk_renderer_run_target_chain(renderer, steps, n_steps);
#else
	(void)renderer;
	(void)steps;
	(void)n_steps;
	return false;
#endif
}
