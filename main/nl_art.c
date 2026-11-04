#include "nl_art.h"

#define R_GLASS 233
#define SOFT_START 44      // the glow begins this far inside the ring
#define SOFT_KNEE 20       // and is this far inside it when it has reached KNEE_ALPHA
#define KNEE_ALPHA 31      // of 255: about 12 %

static uint32_t isqrt(uint32_t v) {
    uint32_t root = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

int nl_art_ring_inner(int area_percent) {
    return (int)isqrt((uint32_t)(R_GLASS * R_GLASS) * (uint32_t)(100 - area_percent) / 100);
}

/** One channel at `alpha` of 255, down to `bits` bits, with `threshold` (0..15, the Bayer cell) deciding the rounding. */
static uint32_t shade(uint32_t channel, uint32_t alpha, int bits, uint32_t threshold) {
    const uint32_t v = channel * alpha;  // 0..65025
    const int drop = 16 - bits;
    return (v + (threshold << (drop - 4))) >> drop > (1u << bits) - 1 ? (1u << bits) - 1 : (v + (threshold << (drop - 4))) >> drop;
}

void nl_art_ring(uint16_t *px, int area_percent, uint32_t rgb) {
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const int inner = nl_art_ring_inner(area_percent);
    const int start = inner - SOFT_START;
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    const uint16_t solid = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
    const int mid = NL_ART_SIZE / 2;
    for (int y = 0; y < NL_ART_SIZE; y++) {
        const int dy = y - mid;
        for (int x = 0; x < NL_ART_SIZE; x++) {
            const int dx = x - mid;
            const int d2 = dx * dx + dy * dy;
            if (d2 >= inner * inner) {
                *px++ = solid;
            } else if (d2 <= start * start) {
                *px++ = 0;
            } else {
                // The radius in sixteenths of a pixel, so the gradient is smooth.
                const int rad = (int)isqrt((uint32_t)d2 * 256);
                const int knee = (inner - SOFT_KNEE) * 16;
                uint32_t alpha;
                if (rad < knee) {
                    alpha = (uint32_t)(rad - start * 16) * KNEE_ALPHA / ((SOFT_START - SOFT_KNEE) * 16);
                } else {
                    alpha = KNEE_ALPHA + (uint32_t)(rad - knee) * (255 - KNEE_ALPHA) / (SOFT_KNEE * 16);
                }
                const uint32_t t = bayer[y & 3][x & 3];
                *px++ = (uint16_t)(shade(r, alpha, 5, t) << 11 | shade(g, alpha, 6, t) << 5 | shade(b, alpha, 5, t));
            }
        }
    }
}
