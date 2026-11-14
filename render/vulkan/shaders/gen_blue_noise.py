#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Deterministic 16x16 toroidal blue-noise rank table (0..255) generator
# implementing Robert Ulichney's void-and-cluster algorithm ("The void-and-cluster
# method for dither array generation", Proc. SPIE 1913, 1993) using only the
# Python standard library.
#
# Run: python3 render/vulkan/shaders/gen_blue_noise.py

import argparse
import math


def gen_blue_noise_16x16(sigma: float = 1.5, seed: int = 0x517CC1B727220A95) -> list[int]:
    n = 16
    m = n * n
    lut = [[0.0] * m for _ in range(m)]
    inv_2s2 = 1.0 / (2.0 * sigma * sigma)
    for y0 in range(n):
        for x0 in range(n):
            i = y0 * n + x0
            row = lut[i]
            for y1 in range(n):
                dy = abs(y0 - y1)
                if dy > n // 2:
                    dy = n - dy
                for x1 in range(n):
                    dx = abs(x0 - x1)
                    if dx > n // 2:
                        dx = n - dx
                    row[y1 * n + x1] = math.exp(-(dx * dx + dy * dy) * inv_2s2)

    state = seed & 0xFFFFFFFFFFFFFFFF

    def next_u64() -> int:
        nonlocal state
        state = (state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
        z = state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
        return z ^ (z >> 31)

    initial_ones = m // 10
    bits = [0] * m
    energy = [0.0] * m
    indices = list(range(m))
    for k in range(initial_ones):
        j = k + (next_u64() % (m - k))
        indices[k], indices[j] = indices[j], indices[k]
        p = indices[k]
        bits[p] = 1
        w = lut[p]
        for q in range(m):
            energy[q] += w[q]

    # Phase 0: relax initial binary pattern by swapping tightest cluster and largest void
    while True:
        c_idx = -1
        c_val = -1.0
        for i in range(m):
            if bits[i] and energy[i] > c_val:
                c_val = energy[i]
                c_idx = i
        bits[c_idx] = 0
        wc = lut[c_idx]
        for q in range(m):
            energy[q] -= wc[q]

        v_idx = -1
        v_val = 1e30
        for i in range(m):
            if not bits[i] and energy[i] < v_val:
                v_val = energy[i]
                v_idx = i
        bits[v_idx] = 1
        wv = lut[v_idx]
        for q in range(m):
            energy[q] += wv[q]

        if v_idx == c_idx:
            break

    ranks = [0] * m

    # Phase 1: rank initial ones down from initial_ones - 1 to 0
    b1 = list(bits)
    e1 = list(energy)
    for r in range(initial_ones - 1, -1, -1):
        c_idx = -1
        c_val = -1.0
        for i in range(m):
            if b1[i] and e1[i] > c_val:
                c_val = e1[i]
                c_idx = i
        b1[c_idx] = 0
        ranks[c_idx] = r
        wc = lut[c_idx]
        for q in range(m):
            e1[q] -= wc[q]

    # Phase 2: rank from initial_ones to m // 2 - 1 (largest void of 1s)
    b2 = list(bits)
    e2 = list(energy)
    for r in range(initial_ones, m // 2):
        v_idx = -1
        v_val = 1e30
        for i in range(m):
            if not b2[i] and e2[i] < v_val:
                v_val = e2[i]
                v_idx = i
        b2[v_idx] = 1
        ranks[v_idx] = r
        wv = lut[v_idx]
        for q in range(m):
            e2[q] += wv[q]

    # Phase 3: rank from m // 2 to m - 1 (tightest cluster of 0s)
    e3 = [0.0] * m
    for i in range(m):
        if not b2[i]:
            wi = lut[i]
            for q in range(m):
                e3[q] += wi[q]
    for r in range(m // 2, m):
        c_idx = -1
        c_val = -1.0
        for i in range(m):
            if not b2[i] and e3[i] > c_val:
                c_val = e3[i]
                c_idx = i
        b2[c_idx] = 1
        ranks[c_idx] = r
        wc = lut[c_idx]
        for q in range(m):
            e3[q] -= wc[q]

    return ranks


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate 16x16 blue-noise GLSL rank table.")
    parser.add_argument("--sigma", type=float, default=1.5)
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=0x517CC1B727220A95)
    args = parser.parse_args()

    ranks = gen_blue_noise_16x16(args.sigma, args.seed)
    assert sorted(ranks) == list(range(256))
    for y in range(16):
        row = ", ".join(f"{ranks[y * 16 + x]:3d}" for x in range(16))
        suffix = "," if y < 15 else ""
        print(f"\t\t{row}{suffix}")


if __name__ == "__main__":
    main()
