#ifndef RENDER_KFX_H
#define RENDER_KFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wlr_buffer;
struct wlr_buffer_pass_options;
struct wlr_box;
struct wlr_kfx_draw_options;
struct wlr_kfx_shader;
struct wlr_kfx_shader_layout;
struct wlr_kfx_target;
struct wlr_render_pass;
struct wlr_renderer;
struct wlr_vk_command_buffer;
struct wlr_vk_render_pass;
struct wlr_vk_renderer;

struct wlr_render_pass *vulkan_begin_buffer_pass_with_flags(
	struct wlr_renderer *wlr_renderer, struct wlr_buffer *buffer,
	const struct wlr_buffer_pass_options *options, bool force_two_pass);
void kfx_vk_render_pass_set_dither(struct wlr_render_pass *pass, bool enable_dither);

struct wlr_kfx_shader *kfx_vk_shader_create(struct wlr_renderer *renderer,
	const uint32_t *spirv, size_t len,
	const struct wlr_kfx_shader_layout *layout);
void kfx_vk_shader_destroy(struct wlr_kfx_shader *shader);
void kfx_vk_render_pass_add_draw(struct wlr_render_pass *pass,
	const struct wlr_kfx_draw_options *options);

struct wlr_kfx_target *kfx_vk_target_acquire(struct wlr_renderer *renderer,
	int width, int height);
void kfx_vk_target_release(struct wlr_kfx_target *target);
void kfx_vk_target_get_size(struct wlr_kfx_target *target,
	int *width, int *height);
struct wlr_render_pass *kfx_vk_target_begin_pass(struct wlr_kfx_target *target);

void kfx_vk_pass_submit(struct wlr_vk_render_pass *pass);
void kfx_vk_pass_cleanup(struct wlr_vk_render_pass *pass);
void kfx_vk_cb_release(struct wlr_vk_command_buffer *cb,
	struct wlr_vk_renderer *renderer);
void kfx_vk_renderer_finish(struct wlr_vk_renderer *renderer);

#endif
