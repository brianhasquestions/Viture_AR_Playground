/**********************************************************************
 * @file    glyph.h
 * @brief   Glyph code layer: payload <-> 8x8 module grid.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * The marker is an 8x8 grid of black/white modules:
 *   - a 1-module solid black border ring (the finder frame);
 *   - a 6x6 interior carrying data.
 * Three of the four interior corners are white and one (top-left) is
 * black: an asymmetric L that fixes orientation. The remaining 32
 * interior modules hold 24 payload bits (3 bytes) followed by an 8-bit
 * CRC.
 *
 * This layer is pure logic and has no notion of pixels or cameras, so it
 * round-trips (encode -> decode) without any hardware.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef GLYPH_H
#define GLYPH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define GLYPH_MODULES        (8)   /* Grid is GLYPH_MODULES square.   */
#define GLYPH_PAYLOAD_BYTES  (3)   /* 24 data bits.                   */

/**********************************************************************
 * @brief  Encode a 3-byte payload into an 8x8 module grid.
 *
 * @param[in]  p_payload  GLYPH_PAYLOAD_BYTES bytes.
 * @param[out] grid       8x8, row-major; 1 = black module, 0 = white.
 **********************************************************************/
void glyph_encode(const uint8_t * p_payload,
                  uint8_t grid[GLYPH_MODULES][GLYPH_MODULES]);

/**********************************************************************
 * @brief  Decode an 8x8 module grid back to a payload.
 *
 * Tries all four rotations, checks the border ring and the orientation
 * corners, and verifies the CRC. Only a fully consistent grid decodes.
 *
 * @param[in]  grid       8x8, row-major; 1 = black, 0 = white.
 * @param[out] p_payload  Receives GLYPH_PAYLOAD_BYTES on success.
 *
 * @return  0 on success, -1 if the grid is not a valid glyph.
 **********************************************************************/
int glyph_decode(const uint8_t grid[GLYPH_MODULES][GLYPH_MODULES],
                 uint8_t * p_payload);

/**********************************************************************
 * @brief  CRC-8 (poly 0x07) of a buffer. Exposed for tests.
 **********************************************************************/
uint8_t glyph_crc8(const uint8_t * p_data, int len);

#ifdef __cplusplus
}
#endif

#endif /* GLYPH_H */
