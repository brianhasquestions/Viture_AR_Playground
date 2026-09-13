/**********************************************************************
 * @file    glyphtool.c
 * @brief   Author glyph markers and seal messages to them.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 *   glyphtool gen  <payload_hex6> <module_px> <out.pgm>
 *       Render a printable glyph (grayscale PGM) for a 3-byte payload.
 *
 *   glyphtool seal <sn_hash_hex64> <payload_hex6> <message> <out.dat>
 *       Seal a message so it opens only on the device whose SN hash is
 *       given, when it sees the given glyph.
 *
 * PGM converts to anything with ffmpeg/ImageMagick for printing, e.g.:
 *   ffmpeg -i glyph.pgm glyph.png
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "binding.h"
#include "detect.h"
#include "glyph.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SN_HASH_BYTES   (32)

static int hex_nibble(char ch)
{
    if ((ch >= '0') && (ch <= '9')) { return ch - '0'; }
    if ((ch >= 'a') && (ch <= 'f')) { return ch - 'a' + 10; }
    if ((ch >= 'A') && (ch <= 'F')) { return ch - 'A' + 10; }
    return -1;
}

/**********************************************************************
 * @brief  Parse exactly nbytes of hex from p_hex into p_out.
 *
 * @return  0 on success, -1 on malformed or wrong-length input.
 **********************************************************************/
static int parse_hex(const char * p_hex, uint8_t * p_out, int nbytes)
{
    int i = 0;

    if ((NULL == p_hex) || ((int)strlen(p_hex) != (nbytes * 2)))
    {
        return -1;
    }
    for (i = 0; i < nbytes; i++)
    {
        int hi = hex_nibble(p_hex[i * 2]);
        int lo = hex_nibble(p_hex[(i * 2) + 1]);
        if ((hi < 0) || (lo < 0))
        {
            return -1;
        }
        p_out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/**********************************************************************
 * @brief  Write a grayscale buffer as a binary PGM (P5).
 **********************************************************************/
static int write_pgm(const char * p_path, const uint8_t * p_gray,
                     int w, int h)
{
    FILE * p_f = fopen(p_path, "wb");

    if (NULL == p_f)
    {
        return -1;
    }
    (void)fprintf(p_f, "P5\n%d %d\n255\n", w, h);
    if (fwrite(p_gray, 1U, (size_t)w * (size_t)h, p_f) !=
        ((size_t)w * (size_t)h))
    {
        (void)fclose(p_f);
        return -1;
    }
    (void)fclose(p_f);
    return 0;
}

static int cmd_gen(int argc, char ** argv)
{
    uint8_t   payload[GLYPH_PAYLOAD_BYTES];
    uint8_t   grid[GLYPH_MODULES][GLYPH_MODULES];
    uint8_t * p_img = NULL;
    int       module_px = 0;
    int       w = 0;
    int       h = 0;

    if (argc != 5)
    {
        (void)fprintf(stderr,
            "usage: glyphtool gen <payload_hex6> <module_px> <out.pgm>\n");
        return EXIT_FAILURE;
    }
    if (0 != parse_hex(argv[2], payload, GLYPH_PAYLOAD_BYTES))
    {
        (void)fprintf(stderr, "bad payload (need 6 hex chars)\n");
        return EXIT_FAILURE;
    }
    module_px = atoi(argv[3]);
    if (module_px < 1)
    {
        (void)fprintf(stderr, "module_px must be >= 1\n");
        return EXIT_FAILURE;
    }

    glyph_encode(payload, grid);
    if (0 != glyph_render((const uint8_t (*)[GLYPH_MODULES])grid,
                          module_px, 2, &p_img, &w, &h))
    {
        (void)fprintf(stderr, "render failed\n");
        return EXIT_FAILURE;
    }
    if (0 != write_pgm(argv[4], p_img, w, h))
    {
        (void)fprintf(stderr, "cannot write '%s'\n", argv[4]);
        free(p_img);
        return EXIT_FAILURE;
    }
    (void)printf("wrote %s (%dx%d) for payload %s\n",
                 argv[4], w, h, argv[2]);
    free(p_img);
    return EXIT_SUCCESS;
}

static int cmd_seal(int argc, char ** argv)
{
    uint8_t sn_hash[SN_HASH_BYTES];
    uint8_t payload[GLYPH_PAYLOAD_BYTES];
    uint8_t blob[BINDING_HEADER_BYTES + 65536];
    size_t  blob_len = 0;
    FILE *  p_f = NULL;

    if (argc != 6)
    {
        (void)fprintf(stderr,
            "usage: glyphtool seal <sn_hash_hex64> <payload_hex6> "
            "<message> <out.dat>\n");
        return EXIT_FAILURE;
    }
    if (0 != parse_hex(argv[2], sn_hash, SN_HASH_BYTES))
    {
        (void)fprintf(stderr, "bad sn_hash (need 64 hex chars)\n");
        return EXIT_FAILURE;
    }
    if (0 != parse_hex(argv[3], payload, GLYPH_PAYLOAD_BYTES))
    {
        (void)fprintf(stderr, "bad payload (need 6 hex chars)\n");
        return EXIT_FAILURE;
    }

    if (0 != binding_seal(sn_hash, payload,
                          (const uint8_t *)argv[4], strlen(argv[4]),
                          blob, sizeof(blob), &blob_len))
    {
        (void)fprintf(stderr, "seal failed (message too long?)\n");
        return EXIT_FAILURE;
    }

    p_f = fopen(argv[5], "wb");
    if (NULL == p_f)
    {
        (void)fprintf(stderr, "cannot write '%s'\n", argv[5]);
        return EXIT_FAILURE;
    }
    if (fwrite(blob, 1U, blob_len, p_f) != blob_len)
    {
        (void)fclose(p_f);
        (void)fprintf(stderr, "write error\n");
        return EXIT_FAILURE;
    }
    (void)fclose(p_f);
    (void)printf("sealed %zu bytes to %s (glyph %s)\n",
                 blob_len, argv[5], argv[3]);
    return EXIT_SUCCESS;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        (void)fprintf(stderr,
            "usage: glyphtool <gen|seal> ...\n"
            "  gen  <payload_hex6> <module_px> <out.pgm>\n"
            "  seal <sn_hash_hex64> <payload_hex6> <message> <out.dat>\n");
        return EXIT_FAILURE;
    }
    if (0 == strcmp(argv[1], "gen"))
    {
        return cmd_gen(argc, argv);
    }
    if (0 == strcmp(argv[1], "seal"))
    {
        return cmd_seal(argc, argv);
    }
    (void)fprintf(stderr, "unknown command '%s'\n", argv[1]);
    return EXIT_FAILURE;
}
