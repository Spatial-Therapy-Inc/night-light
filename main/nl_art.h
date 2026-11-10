// The ring drawn into a buffer, plain C: test/run.sh tests it on a computer. The panel is RGB565, so the soft inner
// edge is dithered, or it would show bands.
#pragma once

#include <stdint.h>

#define NL_ART_SIZE 466

/** The inner radius of a ring taking up `area_percent` of the glass. */
int nl_art_ring_inner(int area_percent);
/** Fills NL_ART_SIZE x NL_ART_SIZE RGB565 pixels: black, a soft inner edge, then the ring out to the glass. */
void nl_art_ring(uint16_t *px, int area_percent, uint32_t rgb);

// The crescent: a disc of radius NL_MOON_R in the middle with a disc of NL_MOON_CUT_R cut out of it, up and to the
// right: a thick crescent on the left, about a third of the glass, and the dark of the cut for the time.
#define NL_MOON_R 205
#define NL_MOON_CUT_R 178
#define NL_MOON_CUT_DX 100
#define NL_MOON_CUT_DY (-36)
/** Fills NL_ART_SIZE x NL_ART_SIZE RGB565 pixels: the crescent in rgb, both its edges soft as the ring's inner one. */
void nl_art_moon(uint16_t *px, uint32_t rgb);
