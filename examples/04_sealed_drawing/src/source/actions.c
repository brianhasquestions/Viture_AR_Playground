#include "actions.h"

#include "envelope.h"
#include "fingerprint.h"
#include "picture.h"
#include "scan.h"
#include "vault.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MSG_CAP             (4096)
#define CAPTION_CAP         (MSG_CAP + 64)
#define MS_PER_SEC          (1000L)
#define NS_PER_MS           (1000000L)
#define SCAN_TICK_NS        (16L * NS_PER_MS)
#define VIEW_GAP_MS         (650L)
#define VIEW_TIMEOUT_MS     (2500L)
#define PICKER_TIMEOUT_MS   (60000L)
#define PICKER_TICK_NS      (20L * NS_PER_MS)
#define PRESET_MAX          (16)
#define PRESET_LINE_CAP     (256)
#define PICKER_TEXT_CAP     (PRESET_MAX * (PRESET_LINE_CAP + 4))

static const char * const DEFAULT_PRESETS[] =
{
    "Meet me at the usual place at 9pm",
    "The key is under the blue flowerpot",
    "Call me when you see this",
    "You found it. Now hide it again",
    "Happy birthday. Look inside the doll",
};

typedef struct
{
    byte_span_t    jpeg;
    const char *   p_caption;
    const scan_t * p_scan;
} presentation_t;

typedef struct
{
    byte_buf_t      frames[ACTION_MAX_VIEWS];
    fingerprint_t * p_prints;
    int             count;
} views_t;

static long now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);

    return (ts.tv_sec * MS_PER_SEC) + (ts.tv_nsec / NS_PER_MS);
}

static void pump(session_t * p_s, long tick_ns)
{
    struct timespec ts;

    if (NULL != p_s->p_display)
    {
        (void)display_pump(p_s->p_display);
    }
    ts.tv_sec  = 0;
    ts.tv_nsec = tick_ns;
    (void)nanosleep(&ts, NULL);
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

typedef struct
{
    char lines[PRESET_MAX][PRESET_LINE_CAP];
    int  count;
} presets_t;

static void load_presets(const char * p_path, presets_t * p_out)
{
    FILE * p_file = NULL;
    size_t i      = 0;
    size_t n      = sizeof(DEFAULT_PRESETS) / sizeof(DEFAULT_PRESETS[0]);

    p_out->count = 0;
    if (NULL != p_path)
    {
        p_file = fopen(p_path, "r");
    }
    while ((NULL != p_file) && (p_out->count < PRESET_MAX) &&
           (NULL != fgets(p_out->lines[p_out->count], PRESET_LINE_CAP,
                          p_file)))
    {
        char * p_line = p_out->lines[p_out->count];

        p_line[strcspn(p_line, "\r\n")] = '\0';
        if ('\0' != p_line[0])
        {
            p_out->count++;
        }
    }
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }
    for (i = 0; (0 == p_out->count) && (i < n); i++)
    {
        (void)snprintf(p_out->lines[i], PRESET_LINE_CAP, "%s",
                       DEFAULT_PRESETS[i]);
        p_out->count = (int)i + 1;
    }
}

static void show_picker(session_t * p_s, const presets_t * p_p, int cursor)
{
    char * p_text = NULL;
    size_t used   = 0;
    int    i      = 0;

    p_text = (char *)calloc(PICKER_TEXT_CAP, 1U);
    if (NULL == p_text)
    {
        goto cleanup;
    }
    for (i = 0; i < p_p->count; i++)
    {
        int n = snprintf(p_text + used, PICKER_TEXT_CAP - used, "%s %s\n",
                         (i == cursor) ? ">" : " ", p_p->lines[i]);

        if ((n < 0) || ((size_t)n >= (PICKER_TEXT_CAP - used)))
        {
            break;
        }
        used += (size_t)n;
    }
    display_show_message(p_s->p_display,
                         "CHOOSE A MESSAGE  (tap up/down, hold to confirm)",
                         p_text);
    (void)display_pump(p_s->p_display);

cleanup:
    if (NULL != p_text)
    {
        free(p_text);
    }
}

static int pick_on_glasses(session_t * p_s, const presets_t * p_p,
                           char * p_buf)
{
    int  result = ACTION_ERROR;
    int  cursor = 0;
    long start  = now_ms();

    (void)printf("[picker] tap volume up/down to choose, hold to confirm\n");
    show_picker(p_s, p_p, cursor);
    while ((now_ms() - start) < PICKER_TIMEOUT_MS)
    {
        int press = glasses_link_take_press(p_s->p_link);

        if (GLASSES_LINK_UP == press)
        {
            cursor = (cursor + p_p->count - 1) % p_p->count;
            show_picker(p_s, p_p, cursor);
        }
        else if (GLASSES_LINK_DOWN == press)
        {
            cursor = (cursor + 1) % p_p->count;
            show_picker(p_s, p_p, cursor);
        }
        if (GLASSES_LINK_NONE != glasses_link_take_trigger(p_s->p_link))
        {
            glasses_link_restore(p_s->p_link);
            (void)snprintf(p_buf, MSG_CAP, "%s", p_p->lines[cursor]);
            (void)printf("[picker] chose: %s\n", p_buf);
            result = ACTION_OK;
            goto cleanup;
        }
        pump(p_s, PICKER_TICK_NS);
    }
    (void)fprintf(stderr, "[picker] timed out\n");

cleanup:

    return result;
}

static int read_message(session_t * p_s, char * p_buf, size_t cap)
{
    int       result = ACTION_ERROR;
    size_t    len    = 0;
    presets_t presets;

    if (NULL != p_s->opts.p_message)
    {
        (void)snprintf(p_buf, cap, "%s", p_s->opts.p_message);
    }
    else if ((NULL != p_s->p_link) && (NULL != p_s->p_display))
    {
        load_presets(p_s->opts.p_messages, &presets);
        if (ACTION_OK != pick_on_glasses(p_s, &presets, p_buf))
        {
            goto cleanup;
        }
    }
    else
    {
        display_show_message(p_s->p_display, "New object",
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

cleanup:

    return result;
}

static int seal_and_store(session_t * p_s, const views_t * p_views,
                          const char * p_msg)
{
    int              result = ACTION_ERROR;
    uint8_t *        p_blob = NULL;
    envelope_input_t in;
    byte_buf_t       out;
    byte_span_t      blob;
    vault_views_t    vv;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    p_blob = (uint8_t *)malloc(MSG_CAP + ENVELOPE_OVERHEAD_BYTES);
    if (NULL == p_blob)
    {
        goto cleanup;
    }
    in.aad.p_data     = p_views->p_prints[0].hashes.primary;
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
    vv.p_views  = p_views->p_prints;
    vv.count    = p_views->count;
    if (0 != vault_store(p_s->opts.p_vault, &vv, blob))
    {
        (void)fprintf(stderr, "[sealed] could not write '%s'\n",
                      p_s->opts.p_vault);
        goto cleanup;
    }
    (void)printf("[sealed] message sealed to this object (%d view%s) and "
                 "to the recipient glasses (%zu-byte record in %s)\n",
                 p_views->count, (1 == p_views->count) ? "" : "s", out.len,
                 p_s->opts.p_vault);
    p_s->sealed = 1;
    result      = ACTION_OK;

cleanup:
    if (NULL != p_blob)
    {
        free(p_blob);
    }

    return result;
}

static void free_views(views_t * p_v)
{
    int i = 0;

    for (i = 1; i < ACTION_MAX_VIEWS; i++)
    {
        if (NULL != p_v->frames[i].p_data)
        {
            free(p_v->frames[i].p_data);
            p_v->frames[i].p_data = NULL;
        }
    }
    if (NULL != p_v->p_prints)
    {
        free(p_v->p_prints);
        p_v->p_prints = NULL;
    }
}

static int add_view(views_t * p_v, byte_span_t jpeg)
{
    int         result = -1;
    byte_buf_t  copy;
    byte_span_t src;

    memset(&copy, 0, sizeof(copy));
    if (p_v->count >= ACTION_MAX_VIEWS)
    {
        goto cleanup;
    }
    if (0 == p_v->count)
    {
        p_v->frames[0].p_data = (uint8_t *)jpeg.p_data;
        p_v->frames[0].len    = jpeg.len;
        src                   = jpeg;
    }
    else
    {
        copy.p_data = (uint8_t *)malloc(jpeg.len);
        if (NULL == copy.p_data)
        {
            goto cleanup;
        }
        memcpy(copy.p_data, jpeg.p_data, jpeg.len);
        copy.cap = jpeg.len;
        copy.len = jpeg.len;
        p_v->frames[p_v->count] = copy;
        src.p_data = copy.p_data;
        src.len    = copy.len;
    }
    if (0 != fingerprint_from_jpeg(src, &p_v->p_prints[p_v->count]))
    {
        goto cleanup;
    }
    p_v->count++;
    result = 0;

cleanup:

    return result;
}

static void collect_more_views(session_t * p_s, views_t * p_v)
{
    long       last  = now_ms();
    long       start = last;
    byte_buf_t frame;
    char       hint[64];

    memset(&frame, 0, sizeof(frame));
    while ((p_v->count < ACTION_MAX_VIEWS) &&
           ((now_ms() - start) < (VIEW_TIMEOUT_MS * ACTION_MAX_VIEWS)))
    {
        byte_span_t jpeg;

        if (1 != camera_take_frame(p_s->p_camera, &frame))
        {
            pump(p_s, SCAN_TICK_NS);
            continue;
        }
        jpeg.p_data = frame.p_data;
        jpeg.len    = frame.len;
        (void)snprintf(hint, sizeof(hint),
                       "TURN THE OBJECT SLOWLY  view %d of %d",
                       p_v->count + 1, ACTION_MAX_VIEWS);
        actions_preview(p_s, jpeg, hint);
        if ((now_ms() - last) >= VIEW_GAP_MS)
        {
            if (0 == add_view(p_v, jpeg))
            {
                (void)printf("[capture] view %d of %d taken\n",
                             p_v->count, ACTION_MAX_VIEWS);
            }
            last = now_ms();
        }
        pump(p_s, SCAN_TICK_NS);
    }
    if (NULL != frame.p_data)
    {
        free(frame.p_data);
    }
}

static int do_encode(session_t * p_s, byte_span_t jpeg, views_t * p_v)
{
    int            result    = ACTION_ERROR;
    char *         p_msg     = NULL;
    char *         p_caption = NULL;
    presentation_t pres;

    p_msg     = (char *)calloc(MSG_CAP, 1U);
    p_caption = (char *)calloc(CAPTION_CAP, 1U);
    if ((NULL == p_msg) || (NULL == p_caption))
    {
        goto cleanup;
    }
    if (NULL != p_s->p_camera)
    {
        collect_more_views(p_s, p_v);
    }
    if (ACTION_OK != read_message(p_s, p_msg, MSG_CAP))
    {
        goto cleanup;
    }
    result = seal_and_store(p_s, p_v, p_msg);
    if (ACTION_OK != result)
    {
        goto cleanup;
    }
    (void)snprintf(p_caption, CAPTION_CAP,
                   "SEALED - NEW MESSAGE STORED (%d views)\n%s", p_v->count,
                   p_msg);
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
        (void)printf("\n[unlocked] object matched (%d keypoint inliers, "
                     "hash score %d)\n  \"%s\"\n", p_match->inliers,
                     p_match->dist, (const char *)p_plain);
        (void)snprintf(p_caption, CAPTION_CAP,
                       "DECODED - MESSAGE REVEALED\n%s",
                       (const char *)p_plain);
        result = ACTION_OK;
    }
    else if (ENVELOPE_LOCKED == rc)
    {
        (void)printf("[locked] object matched (%d inliers) but it was not "
                     "sealed to these glasses.\n", p_match->inliers);
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

static int do_decode(session_t * p_s, byte_span_t jpeg,
                     const fingerprint_t * p_live)
{
    int            result    = ACTION_ERROR;
    int            rc        = VAULT_ERROR;
    char *         p_caption = NULL;
    vault_query_t  query;
    vault_match_t  match;
    presentation_t pres;

    memset(&match, 0, sizeof(match));
    query.p_live   = p_live;
    query.max_dist = p_s->opts.max_dist;
    rc             = vault_find(p_s->opts.p_vault, &query, &match);
    if (VAULT_ERROR == rc)
    {
        (void)fprintf(stderr, "[sealed] vault '%s' is unreadable\n",
                      p_s->opts.p_vault);
        goto cleanup;
    }
    pres.jpeg   = jpeg;
    pres.p_scan = p_s->p_scan;
    if (VAULT_NO_MATCH == rc)
    {
        if ((0 != p_s->offline) || (NULL != p_s->p_scan))
        {
            (void)printf("[no match] this object is not in the vault.\n");
        }
        if (NULL != p_s->p_scan)
        {
            pres.p_caption = "NOT FOUND - NO MESSAGE FOR THIS OBJECT\n"
                             "hold volume DOWN to store one";
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
    pres.p_caption = p_caption;
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
    long start  = now_ms();
    long spent  = 0;
    int  frames = 0;

    while (spent <= SCAN_DURATION_MS)
    {
        double progress = (double)spent / (double)SCAN_DURATION_MS;

        display_show_image(p_s->p_display, scan_render(p_scan, progress));
        frames++;
        pump(p_s, SCAN_TICK_NS);
        spent = now_ms() - start;
    }
    (void)printf("[capture] scan animation: %d frames in %ld ms\n", frames,
                 spent);
}

int actions_capture(session_t * p_s, byte_span_t jpeg, int mode)
{
    int     result = ACTION_ERROR;
    views_t views;
    scan_t  scan;

    memset(&views, 0, sizeof(views));
    memset(&scan, 0, sizeof(scan));
    views.p_prints = (fingerprint_t *)calloc(ACTION_MAX_VIEWS,
                                             sizeof(fingerprint_t));
    if ((NULL == views.p_prints) || (0 != add_view(&views, jpeg)))
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
        result = do_encode(p_s, jpeg, &views);
    }
    else
    {
        (void)printf("[capture] decode: looking this object up.\n");
        result = do_decode(p_s, jpeg, &views.p_prints[0]);
    }

cleanup:
    p_s->p_scan = NULL;
    scan_end(&scan);
    free_views(&views);

    return result;
}

int actions_seal_frame(session_t * p_s, byte_span_t jpeg)
{
    int     result = ACTION_ERROR;
    views_t views;

    memset(&views, 0, sizeof(views));
    views.p_prints = (fingerprint_t *)calloc(1U, sizeof(fingerprint_t));
    if ((NULL == views.p_prints) || (0 != add_view(&views, jpeg)))
    {
        goto cleanup;
    }
    result = do_encode(p_s, jpeg, &views);

cleanup:
    free_views(&views);

    return result;
}

int actions_reveal_frame(session_t * p_s, byte_span_t jpeg)
{
    int             result  = ACTION_ERROR;
    fingerprint_t * p_print = NULL;

    p_print = (fingerprint_t *)calloc(1U, sizeof(*p_print));
    if ((NULL == p_print) || (0 != fingerprint_from_jpeg(jpeg, p_print)))
    {
        goto cleanup;
    }
    result = do_decode(p_s, jpeg, p_print);

cleanup:
    if (NULL != p_print)
    {
        free(p_print);
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
