#include "actions.h"

#include "envelope.h"
#include "picture.h"
#include "scan.h"
#include "vault.h"

#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSG_CAP             (4096)
#define CAPTION_CAP         (MSG_CAP + 64)
#define MS_PER_SEC          (1000L)
#define NS_PER_MS           (1000000L)
#define SCAN_TICK_NS        (16L * NS_PER_MS)

typedef struct
{
    byte_span_t    jpeg;
    const char *   p_caption;
    const scan_t * p_scan;
} presentation_t;

static long now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);

    return (ts.tv_sec * MS_PER_SEC) + (ts.tv_nsec / NS_PER_MS);
}

static void present(session_t * p_s, const presentation_t * p_pres)
{
    picture_slot_t slot;
    rgba_image_t   img;
    char           path[PICTURE_PATH_CHARS];

    memset(&img, 0, sizeof(img));
    if (0 != picture_slot_begin(p_s->opts.p_captures, &slot))
    {
        goto cleanup;
    }
    if (0 == picture_save_jpeg(&slot, p_pres->jpeg, path))
    {
        (void)printf("[picture] frame   -> %s\n", path);
    }
    if (0 != picture_compose(p_pres->jpeg, p_pres->p_caption, &img))
    {
        (void)fprintf(stderr, "[picture] could not decode the frame\n");
        goto cleanup;
    }
    scan_mark_result(p_pres->p_scan, &img);
    if (0 == picture_save_bmp(&slot, &img, path))
    {
        (void)printf("[picture] overlay -> %s\n", path);
    }
    display_show_image(p_s->p_display, &img);

cleanup:
    if (NULL != img.p_rgba)
    {
        free(img.p_rgba);
    }
}

static int read_message(session_t * p_s, char * p_buf, size_t cap)
{
    int    result = ACTION_ERROR;
    size_t len    = 0;

    if (NULL != p_s->opts.p_message)
    {
        (void)snprintf(p_buf, cap, "%s", p_s->opts.p_message);
    }
    else
    {
        display_show_message(p_s->p_display, "New drawing",
                             "Type the message in the terminal.");
        (void)display_pump(p_s->p_display);
        (void)printf("Message to seal: ");
        (void)fflush(stdout);
        if (NULL == fgets(p_buf, (int)cap, stdin))
        {
            p_buf[0] = '\0';
        }
    }
    len        = strcspn(p_buf, "\r\n");
    p_buf[len] = '\0';
    if (0U != len)
    {
        result = ACTION_OK;
    }
    else
    {
        (void)fprintf(stderr, "[sealed] empty message\n");
    }

    return result;
}

static int seal_and_store(session_t * p_s, const phash_set_t * p_set,
                          const char * p_msg)
{
    int              result = ACTION_ERROR;
    uint8_t *        p_blob = NULL;
    envelope_input_t in;
    byte_buf_t       out;
    byte_span_t      blob;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    p_blob = (uint8_t *)malloc(MSG_CAP + ENVELOPE_OVERHEAD_BYTES);
    if (NULL == p_blob)
    {
        goto cleanup;
    }
    in.aad.p_data     = p_set->primary;
    in.aad.len        = PHASH_BYTES;
    in.payload.p_data = (const uint8_t *)p_msg;
    in.payload.len    = strnlen(p_msg, MSG_CAP);
    out.p_data        = p_blob;
    out.cap           = MSG_CAP + ENVELOPE_OVERHEAD_BYTES;
    if (ENVELOPE_OK != envelope_seal(p_s->recipient_pub, &in, &out))
    {
        (void)fprintf(stderr, "[sealed] envelope_seal failed\n");
        goto cleanup;
    }
    blob.p_data = out.p_data;
    blob.len    = out.len;
    if (0 != vault_store(p_s->opts.p_vault, p_set, blob))
    {
        (void)fprintf(stderr, "[sealed] could not write '%s'\n",
                      p_s->opts.p_vault);
        goto cleanup;
    }
    (void)printf("[sealed] message sealed to this picture and to the "
                 "recipient glasses (%zu-byte record in %s)\n",
                 out.len, p_s->opts.p_vault);
    p_s->sealed = 1;
    result      = ACTION_OK;

cleanup:
    if (NULL != p_blob)
    {
        free(p_blob);
    }

    return result;
}

int actions_seal(session_t * p_s, byte_span_t jpeg,
                 const phash_set_t * p_set)
{
    int            result    = ACTION_ERROR;
    char *         p_msg     = NULL;
    char *         p_caption = NULL;
    presentation_t pres;

    p_msg     = (char *)calloc(MSG_CAP, 1U);
    p_caption = (char *)calloc(CAPTION_CAP, 1U);
    if ((NULL == p_msg) || (NULL == p_caption) || (NULL == p_set))
    {
        goto cleanup;
    }
    if (ACTION_OK != read_message(p_s, p_msg, MSG_CAP))
    {
        goto cleanup;
    }
    result = seal_and_store(p_s, p_set, p_msg);
    if (ACTION_OK != result)
    {
        goto cleanup;
    }
    (void)snprintf(p_caption, CAPTION_CAP,
                   "SEALED - NEW MESSAGE STORED\n%s", p_msg);
    pres.jpeg      = jpeg;
    pres.p_caption = p_caption;
    pres.p_scan    = p_s->p_scan;
    present(p_s, &pres);

cleanup:
    if (NULL != p_msg)
    {
        memset(p_msg, 0, MSG_CAP);
        free(p_msg);
    }
    if (NULL != p_caption)
    {
        free(p_caption);
    }

    return result;
}

static int open_match(session_t * p_s, const vault_match_t * p_match,
                      char * p_caption)
{
    int              result  = ACTION_ERROR;
    int              rc      = ENVELOPE_ERROR;
    uint8_t *        p_plain = NULL;
    envelope_input_t in;
    byte_buf_t       out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    if (p_match->blob.len < ENVELOPE_OVERHEAD_BYTES)
    {
        goto cleanup;
    }
    p_plain = (uint8_t *)calloc(p_match->blob.len + 1U, 1U);
    if (NULL == p_plain)
    {
        goto cleanup;
    }
    in.aad.p_data     = p_match->phash;
    in.aad.len        = PHASH_BYTES;
    in.payload.p_data = p_match->blob.p_data;
    in.payload.len    = p_match->blob.len;
    out.p_data        = p_plain;
    out.cap           = p_match->blob.len - ENVELOPE_OVERHEAD_BYTES;
    rc                = envelope_open(p_s->identity.priv, &in, &out);
    if (ENVELOPE_OK == rc)
    {
        (void)printf("\n[unlocked] picture matched (distance %d)\n"
                     "  \"%s\"\n", p_match->dist, (const char *)p_plain);
        (void)snprintf(p_caption, CAPTION_CAP,
                       "DECODED - MESSAGE REVEALED\n%s",
                       (const char *)p_plain);
        result = ACTION_OK;
    }
    else if (ENVELOPE_LOCKED == rc)
    {
        (void)printf("[locked] picture matched (distance %d) but it was "
                     "not sealed to these glasses.\n", p_match->dist);
        (void)snprintf(p_caption, CAPTION_CAP,
                       "LOCKED - NOT YOUR GLASSES\nsealed to a different "
                       "pair");
        result = ACTION_LOCKED;
    }
    else
    {
        (void)fprintf(stderr, "[sealed] envelope_open error\n");
    }

cleanup:
    if (NULL != p_plain)
    {
        memset(p_plain, 0, p_match->blob.len + 1U);
        free(p_plain);
    }

    return result;
}

int actions_reveal(session_t * p_s, byte_span_t jpeg,
                   const phash_set_t * p_set)
{
    int            result    = ACTION_ERROR;
    int            rc        = VAULT_ERROR;
    char *         p_caption = NULL;
    vault_query_t  query;
    vault_match_t  match;
    presentation_t pres;

    memset(&match, 0, sizeof(match));
    query.p_live   = p_set;
    query.max_dist = p_s->opts.max_dist;
    rc             = vault_find(p_s->opts.p_vault, &query, &match);
    if (VAULT_ERROR == rc)
    {
        (void)fprintf(stderr, "[sealed] vault '%s' is unreadable\n",
                      p_s->opts.p_vault);
        goto cleanup;
    }
    if (VAULT_NO_MATCH == rc)
    {
        if ((0 != p_s->offline) || (NULL != p_s->p_scan))
        {
            (void)printf("[no match] this object is not in the vault.\n");
        }
        if (NULL != p_s->p_scan)
        {
            pres.jpeg      = jpeg;
            pres.p_caption = "NOT FOUND - NO MESSAGE FOR THIS OBJECT\n"
                             "hold volume DOWN to store one";
            pres.p_scan    = p_s->p_scan;
            present(p_s, &pres);
        }
        result = ACTION_NO_MATCH;
        goto cleanup;
    }
    if ((0 != p_s->have_shown) &&
        (0 == memcmp(p_s->last_shown, match.phash, PHASH_BYTES)))
    {
        result = ACTION_OK;
        goto cleanup;
    }
    memcpy(p_s->last_shown, match.phash, PHASH_BYTES);
    p_s->have_shown = 1;
    p_caption = (char *)calloc(CAPTION_CAP, 1U);
    if (NULL == p_caption)
    {
        goto cleanup;
    }
    result = open_match(p_s, &match, p_caption);
    if (ACTION_ERROR == result)
    {
        goto cleanup;
    }
    pres.jpeg      = jpeg;
    pres.p_caption = p_caption;
    pres.p_scan    = p_s->p_scan;
    present(p_s, &pres);

cleanup:
    vault_match_free(&match);
    if (NULL != p_caption)
    {
        memset(p_caption, 0, CAPTION_CAP);
        free(p_caption);
    }

    return result;
}

static void play_scan(session_t * p_s, scan_t * p_scan)
{
    long            start  = now_ms();
    long            spent  = 0;
    int             frames = 0;
    struct timespec tick;

    tick.tv_sec  = 0;
    tick.tv_nsec = SCAN_TICK_NS;
    while (spent <= SCAN_DURATION_MS)
    {
        double progress = (double)spent / (double)SCAN_DURATION_MS;

        display_show_image(p_s->p_display, scan_render(p_scan, progress));
        if (NULL != p_s->p_display)
        {
            (void)display_pump(p_s->p_display);
        }
        frames++;
        (void)nanosleep(&tick, NULL);
        spent = now_ms() - start;
    }
    (void)printf("[capture] scan animation: %d frames in %ld ms\n", frames,
                 spent);
}

int actions_capture(session_t * p_s, byte_span_t jpeg, int mode)
{
    int           result = ACTION_ERROR;
    phash_set_t * p_set  = NULL;
    scan_t        scan;

    memset(&scan, 0, sizeof(scan));
    p_set = (phash_set_t *)calloc(1U, sizeof(*p_set));
    if ((NULL == p_set) || (0 != phash_set_from_jpeg(jpeg, p_set)))
    {
        (void)fprintf(stderr, "[sealed] frame did not decode\n");
        goto cleanup;
    }
    if (0 == scan_begin(jpeg, &scan))
    {
        (void)printf("[capture] scanning outline...\n");
        play_scan(p_s, &scan);
        p_s->p_scan = &scan;
    }
    p_s->have_shown = 0;
    if (ACTION_MODE_ENCODE == mode)
    {
        (void)printf("[capture] encode: sealing a message to this "
                     "object.\n");
        result = actions_seal(p_s, jpeg, p_set);
    }
    else
    {
        (void)printf("[capture] decode: looking this object up.\n");
        result = actions_reveal(p_s, jpeg, p_set);
    }

cleanup:
    p_s->p_scan = NULL;
    scan_end(&scan);
    if (NULL != p_set)
    {
        free(p_set);
    }

    return result;
}

void actions_preview(session_t * p_s, byte_span_t jpeg, const char * p_hint)
{
    rgba_image_t img;

    memset(&img, 0, sizeof(img));
    if (NULL == p_s->p_display)
    {
        goto cleanup;
    }
    if (0 == picture_preview(jpeg, p_hint, &img))
    {
        display_show_image(p_s->p_display, &img);
    }

cleanup:
    if (NULL != img.p_rgba)
    {
        free(img.p_rgba);
    }
}
