# Kajo's wlroots fork

This repository is a fork of [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots), maintained for the [Kajo](https://github.com/mikolas/Kajo) compositor `kajo-comp`. Kajo builds wlroots from here as a static Meson subproject. The full specification lives in Kajo's `docs/spec_wlroots_fork.md`.

## Refs

| Ref | Based on | Purpose |
| :--- | :--- | :--- |
| `master`, upstream tags | upstream | Exact mirrors of upstream; never committed to |
| `kajo/<upstream-tag>` | the upstream release tag | What Kajo builds: a patch series on top of the release |
| `kajo-<base>-fx<N>` tags | a commit on `kajo/<base>` | The commit Kajo pins; never rewritten |
| `topic/<name>` | upstream `master` | A generic change prepared for an upstream merge request |

Patches are ordered in two layers: **upstreamable** patches (fixes, generic API) first, **kajo-only** patches (effects, policy) on top. A kajo-only patch may depend on an upstreamable one, never the other way round. Pinned commits are never rewritten, so patches added between base moves go on top in any order; the two-layer order is restored when the series moves to a new upstream release. Contributions go upstream as merge requests on gitlab.freedesktop.org, ported onto upstream `master` with upstream naming.

## Compatibility with upstream

This fork is upstream wlroots plus additive extensions. It can be swapped into any build that uses upstream wlroots of the same version, and everything keeps working.

- Every meson option keeps upstream's default; no upstream option, renderer, backend, allocator or protocol implementation is removed.
- Existing public struct layouts, function signatures, enum values and macro meanings are never changed (API and ABI compatible). New state lives in new structs, side tables or hooks.
- With no extension enabled, rendering, damage tracking and performance are identical to upstream.
- New public symbols use the `wlr_kfx_` prefix and new public headers live in `include/wlr/kfx/`. Private helpers do not start with `wlr_`.
- Extensions are detectable at build time through `WLR_HAS_KFX` in `<wlr/config.h>` and the `have_kfx` pkg-config variable (added with the first API patch).
- Project version, pkg-config name (`wlroots-0.20`) and library name stay upstream's.

## Compatibility exceptions

Any break of the rules above is listed here with what breaks, why, the alternatives that were rejected, and the feature macro that lets dependents detect it.

| Exception | Breaks | Why | Alternatives rejected | Feature macro |
| :--- | :--- | :--- | :--- | :--- |
| _(none)_ | | | | |

## Current base

- **Base:** `0.20.2` (`d783533489e1f75d6886c2ab5c5960090ef268f8`)
- **Branch:** `kajo/0.20.2`

## Patch series

| Patch | Layer | Status | Touches |
| :--- | :--- | :--- | :--- |
| `kfx: add build feature flag (WLR_HAS_KFX, have_kfx) and Vulkan custom shader draw (X1, X6)` | `kajo-only` | `local` | `meson.build`, `include/wlr/config.h.in`, `include/render/vulkan.h`, `render/meson.build`, `render/vulkan/meson.build`, `render/vulkan/pass.c`, `render/vulkan/renderer.c` |
| `kfx: add scene leaf-node render hooks (X4) and two-pass pathway tracking (X6)` | `kajo-only` | `local` | `include/types/wlr_scene.h`, `types/meson.build`, `types/scene/wlr_scene.c` |
| `scene: unlink color_manager_v1_destroy listener on scene destroy` | `upstreamable` | `local` | `types/scene/wlr_scene.c` |
| `kfx: add pooled offscreen targets (X3), below-node scene render (X2), damage helpers (X5), and output dither (X6)` | `kajo-only` | `local` | `include/render/vulkan.h`, `include/types/wlr_scene.h`, `render/vulkan/pass.c`, `render/vulkan/renderer.c`, `render/vulkan/shaders/output.frag`, `types/meson.build`, `types/scene/wlr_scene.c` |
| `kfx: render INSTEAD hooks in below-node pass, lazy 16x16 blue-noise dither, target pool eviction, and pass.c hook cleanup` | `kajo-only` | `local` | `include/render/vulkan.h`, `render/vulkan/pass.c`, `render/vulkan/renderer.c`, `render/vulkan/shaders/output.frag`, `types/scene/kfx_render_below.c`, `render/vulkan/kfx_draw.c` |
| `kfx: add single-CB target chain submission and exact sampled-target timeline tracking` | `kajo-only` | `local` | `include/wlr/kfx/render.h`, `include/render/kfx.h`, `include/render/vulkan.h`, `render/kfx.c`, `render/vulkan/pass.c`, `render/vulkan/kfx_draw.c`, `render/vulkan/shaders/output.frag` |
| `kfx: reject empty dst_box in target chain pre-flight and clarify blue-noise table provenance` | `kajo-only` | `local` | `render/vulkan/kfx_draw.c`, `render/vulkan/shaders/output.frag`, `FORK.md` |
| `kfx: add deterministic gen_blue_noise.py generator for output.frag` | `kajo-only` | `local` | `render/vulkan/shaders/output.frag`, `FORK.md` |
| `kfx: add X6 SDR reference white override, BT.2390 PQ tone mapping, HDR headroom query, and HDR output matrix folding` | `kajo-only` | `local` | `include/render/vulkan.h`, `include/types/wlr_scene.h`, `render/vulkan/pass.c`, `render/vulkan/renderer.c`, `render/vulkan/shaders/output.frag`, `types/scene/wlr_scene.c`, `FORK.md` |
| `kfx: quantize dithered output in FP32 in output.frag for cross-GPU 10-bit parity` | `kajo-only` | `local` | `render/vulkan/shaders/output.frag`, `FORK.md` |

Status values: `local`, `submitted !<MR>`, `merged in <version>`, `dropped (<reason>)`. "Touches" lists modified upstream files; new files are not listed.

## Upstream file touchpoint notes

- **`render/vulkan/pass.c`**: Keeps upstream indentation intact. Three small hooks connect `kfx_draw.c`:
  1. `if (pass->kfx_dither)` in the `two_pass` subpass delegates dither pipeline lookup/creation to `vulkan_setup_get_dither_output_pipeline()`.
  2. `if (render_buffer == NULL) { goto skip_render_buffer; }` around the swapchain/buffer barrier block in `render_pass_submit()` when submitting a pooled `wlr_kfx_target` pass, plus `kfx_vk_pass_submit(pass, render_timeline_point)` which records the exact submission timeline point on `pass->kfx_target` and all targets in `pass->kfx_sampled_targets`.
  3. `vulkan_render_pass_init()` exports the base `wlr_render_pass_init(&pass->base, &render_pass_impl)` call so `kfx_vk_target_begin_pass()` lives in `render/vulkan/kfx_draw.c`.

## Notes from the last base move

None yet.

## Licence

wlroots is MIT-licensed (see `LICENSE`). Patches in this fork are MIT as well.
The `blue_noise16[256]` permutation table in `render/vulkan/shaders/output.frag` is generated deterministically by `render/vulkan/shaders/gen_blue_noise.py` (`sigma=1.5, seed=0x517cc1b727220a95`, MIT-licensed).
