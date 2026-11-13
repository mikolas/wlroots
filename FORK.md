# Kajo's wlroots fork

This repository is a fork of [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots), maintained for the [Kajo](https://github.com/mikolas/Kajo) compositor `kajo-comp`. Kajo builds wlroots from here as a static Meson subproject. The full specification lives in Kajo's `docs/spec_wlroots_fork.md`.

## Refs

| Ref | Based on | Purpose |
| :--- | :--- | :--- |
| `master`, upstream tags | upstream | Exact mirrors of upstream; never committed to |
| `kajo/<upstream-tag>` | the upstream release tag | What Kajo builds: a patch series on top of the release |
| `kajo-<base>-fx<N>` tags | a commit on `kajo/<base>` | The commit Kajo pins; never rewritten |
| `topic/<name>` | upstream `master` | A generic change prepared for an upstream merge request |

Patches are ordered in two layers: **upstreamable** patches (fixes, generic API) first, **kajo-only** patches (effects, policy) on top. A kajo-only patch may depend on an upstreamable one, never the other way round. Contributions go upstream as merge requests on gitlab.freedesktop.org, ported onto upstream `master`.

## Current base

- **Base:** `0.20.2` (`d783533489e1f75d6886c2ab5c5960090ef268f8`)
- **Branch:** `kajo/0.20.2`

## Patch series

| Patch | Layer | Status | Touches |
| :--- | :--- | :--- | :--- |
| _(none yet; `kajo-0.20.2-fx1` is upstream 0.20.2 plus this file)_ | | | |

Status values: `local`, `submitted !<MR>`, `merged in <version>`, `dropped (<reason>)`. "Touches" lists modified upstream files; new files are not listed.

## Notes from the last base move

None yet.

## Licence

wlroots is MIT-licensed (see `LICENSE`). Patches in this fork are MIT as well.
