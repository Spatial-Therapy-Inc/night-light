// The ring drawn into a buffer, plain C: test/run.sh tests it on a computer. The panel is RGB565, so the soft inner
// edge is dithered, or it would show bands.
#pragma once

#include <stdint.h>

#define NL_ART_SIZE 466

/** The inner radius of a ring taking up `area_percent` of the glass. */
int nl_art_ring_inner(int area_percent);
/** Fills NL_ART_SIZE x NL_ART_SIZE RGB565 pixels: black, a soft inner edge, then the ring out to the glass. */
void nl_art_ring(uint16_t *px, int area_percent, uint32_t rgb);
