/**********************************************************************
 * @file    main.c
 * @brief   glyph_reader - recognise a glyph, reveal a device-bound msg.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Live mode:
 *   1. find the glasses, bring the device up, read its SN hash, tear it
 *      back down (we only need the hash once);
 *   2. stream the camera (MJPEG), decode each frame to grayscale;
 *   3. detect glyphs, and for each try to open the sealed message with
 *      (live SN hash, decoded glyph payload). The message appears only
 *      on the glasses it was sealed to, seeing the glyph it was sealed
 *      to.
 *
 * Offline mode (--image / --hash): run the exact same pipeline on a JPEG
 * file with a supplied hash, so the whole path is testable without the
 * headset. Create test material with the `glyphtool` binary.
 *
 * The revealed message currently prints to the terminal; drawing it on
 * the glasses HUD (as 03_virtual_screen does) is the remaining wire-up.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "binding.h"
#include "detect.h"
#include "glyph.h"
#include "jpeg_decode.h"

#include "carina_pose.h"
#include "device_scan.h"

#include "viture_camera_provider.h"
#include "viture_glasses_provider.h"
#include "viture_protocol_public.h"
#include "viture_result.h"

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SN_HASH_BYTES   (32)
#define MSG_CAP         (4096)
#define BLOB_CAP        (8192)

/**********************************************************************
 * @brief  Shared state between the SDK camera thread and main loop.
 **********************************************************************/
typedef struct
{
    pthread_mutex_t lock;
    uint8_t *       p_jpeg;      /* Latest MJPEG frame (heap). */
    size_t          jpeg_cap;
    size_t          jpeg_len;
    uint32_t        seq;         /* Bumped on every new frame. */
    int             have_frame;
} frame_slot_t;

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

/**********************************************************************
 * @brief  Camera callback (SDK thread). Copies the frame out; the SDK
 *         buffer is valid only for the duration of the call.
 **********************************************************************/
static void on_frame(const XRCameraFrame * p_frame, void * p_user)
{
    frame_slot_t * p_slot = (frame_slot_t *)p_user;

    if ((NULL == p_frame) || (NULL == p_slot) ||
        (NULL == p_frame->data) || (0U == p_frame->size))
    {
        return;
    }

    (void)pthread_mutex_lock(&p_slot->lock);
    if (p_slot->jpeg_cap < p_frame->size)
    {
        uint8_t * p_new = (uint8_t *)realloc(p_slot->p_jpeg,
                                             p_frame->size);
        if (NULL != p_new)
        {
            p_slot->p_jpeg   = p_new;
            p_slot->jpeg_cap = p_frame->size;
        }
    }
    if (p_slot->jpeg_cap >= p_frame->size)
    {
        memcpy(p_slot->p_jpeg, p_frame->data, p_frame->size);
        p_slot->jpeg_len   = p_frame->size;
        p_slot->seq++;
        p_slot->have_frame = 1;
    }
    (void)pthread_mutex_unlock(&p_slot->lock);
}

static void print_hex(const uint8_t * p_bytes, int n)
{
    int i = 0;
    for (i = 0; i < n; i++)
    {
        (void)printf("%02x", p_bytes[i]);
    }
}

/**********************************************************************
 * @brief  Parse a 64-char hex string into 32 bytes.
 *
 * @return  0 on success, -1 on malformed input.
 **********************************************************************/
static int parse_hash_hex(const char * p_hex, uint8_t * p_out)
{
    int i = 0;

    if (NULL == p_hex)
    {
        return -1;
    }
    for (i = 0; i < SN_HASH_BYTES; i++)
    {
        int hi = 0;
        int lo = 0;
        char ch = p_hex[i * 2];
        char cl = p_hex[(i * 2) + 1];

        if (('\0' == ch) || ('\0' == cl))
        {
            return -1;
        }
        hi = (ch >= '0' && ch <= '9') ? (ch - '0') :
             (ch >= 'a' && ch <= 'f') ? (ch - 'a' + 10) :
             (ch >= 'A' && ch <= 'F') ? (ch - 'A' + 10) : -1;
        lo = (cl >= '0' && cl <= '9') ? (cl - '0') :
             (cl >= 'a' && cl <= 'f') ? (cl - 'a' + 10) :
             (cl >= 'A' && cl <= 'F') ? (cl - 'A' + 10) : -1;
        if ((hi < 0) || (lo < 0))
        {
            return -1;
        }
        p_out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/**********************************************************************
 * @brief  Read an entire file into a heap buffer.
 *
 * @return  Buffer (caller frees) or NULL; *p_len set on success.
 **********************************************************************/
static uint8_t * load_file(const char * p_path, size_t * p_len)
{
    FILE *    p_f   = NULL;
    uint8_t * p_buf = NULL;
    long      sz    = 0;

    p_f = fopen(p_path, "rb");
    if (NULL == p_f)
    {
        return NULL;
    }
    if ((0 != fseek(p_f, 0, SEEK_END)) ||
        ((sz = ftell(p_f)) < 0) ||
        (0 != fseek(p_f, 0, SEEK_SET)))
    {
        (void)fclose(p_f);
        return NULL;
    }
    p_buf = (uint8_t *)malloc((size_t)sz);
    if (NULL != p_buf)
    {
        if (fread(p_buf, 1U, (size_t)sz, p_f) != (size_t)sz)
        {
            free(p_buf);
            p_buf = NULL;
        }
        else
        {
            *p_len = (size_t)sz;
        }
    }
    (void)fclose(p_f);
    return p_buf;
}

/**********************************************************************
 * @brief  Run the pipeline on one grayscale frame and report.
 *
 * @return  1 if the message was revealed, 0 otherwise.
 **********************************************************************/
static int process_gray(const uint8_t * p_gray, int w, int h,
                        const uint8_t * p_sn_hash,
                        const uint8_t * p_blob, size_t blob_len,
                        uint8_t * p_last_payload, int * p_have_last)
{
    detect_result_t results[DETECT_MAX_RESULTS];
    int             found = 0;
    int             i     = 0;

    found = detect_glyphs(p_gray, w, h, results, DETECT_MAX_RESULTS);
    if (found <= 0)
    {
        return 0;
    }

    for (i = 0; i < found; i++)
    {
        uint8_t msg[MSG_CAP];
        size_t  msg_len = 0;
        int     same    = 0;

        same = ((0 != *p_have_last) &&
                (0 == memcmp(p_last_payload, results[i].payload,
                             GLYPH_PAYLOAD_BYTES)));

        if (0 == binding_open(p_sn_hash, results[i].payload,
                              p_blob, blob_len, msg, MSG_CAP - 1U,
                              &msg_len))
        {
            if (0 == same)
            {
                msg[msg_len] = '\0';
                (void)printf("\n[unlocked] glyph ");
                print_hex(results[i].payload, GLYPH_PAYLOAD_BYTES);
                (void)printf(" @ (%.0f,%.0f)\n  \"%s\"\n",
                             results[i].cx, results[i].cy,
                             (const char *)msg);
                memcpy(p_last_payload, results[i].payload,
                       GLYPH_PAYLOAD_BYTES);
                *p_have_last = 1;
            }
            return 1;
        }
        else if (0 == same)
        {
            (void)printf("[locked] glyph ");
            print_hex(results[i].payload, GLYPH_PAYLOAD_BYTES);
            (void)printf(" seen, but not sealed to this device.\n");
            memcpy(p_last_payload, results[i].payload,
                   GLYPH_PAYLOAD_BYTES);
            *p_have_last = 1;
        }
    }
    return 0;
}

/**********************************************************************
 * @brief  Read the device SN hash: bring the device up, read, tear down.
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
static int read_sn_hash(int glasses_pid, uint8_t * p_sn_hash)
{
    carina_pose_ctx_t * p_pose = NULL;
    int                 rc     = -1;

    p_pose = carina_pose_create();
    if (NULL == p_pose)
    {
        return -1;
    }
    if (0 == carina_pose_start(p_pose, glasses_pid, 1))
    {
        if (VITURE_GLASSES_SUCCESS ==
            xr_device_provider_get_sn_hash(p_pose->p_handle, p_sn_hash))
        {
            rc = 0;
        }
        else
        {
            (void)fprintf(stderr, "[glyph] could not read sn_hash\n");
        }
    }
    carina_pose_stop(p_pose);
    carina_pose_destroy(p_pose);
    return rc;
}

static void usage(void)
{
    (void)printf(
        "glyph_reader - reveal a glyph-bound, device-bound message\n\n"
        "  --secret <file>   sealed blob to open (default secret.dat)\n"
        "  --image  <file>   offline: decode this JPEG instead of the "
        "camera\n"
        "  --hash   <hex>    offline: 64-hex-char SN hash to use\n"
        "  --help\n\n"
        "Create test material with: glyphtool\n");
}

int main(int argc, char ** argv)
{
    const char *  p_secret = "secret.dat";
    const char *  p_image  = NULL;
    const char *  p_hash   = NULL;
    int           arg      = 0;
    int           exit_code = EXIT_FAILURE;
    int           glasses_pid = 0;
    uint8_t       sn_hash[SN_HASH_BYTES] = { 0 };
    uint8_t *     p_blob   = NULL;
    size_t        blob_len = 0;
    uint8_t       last_payload[GLYPH_PAYLOAD_BYTES] = { 0 };
    int           have_last = 0;

    for (arg = 1; arg < argc; arg++)
    {
        if ((0 == strcmp(argv[arg], "--secret")) && ((arg + 1) < argc))
        {
            arg++;
            p_secret = argv[arg];
        }
        else if ((0 == strcmp(argv[arg], "--image")) &&
                 ((arg + 1) < argc))
        {
            arg++;
            p_image = argv[arg];
        }
        else if ((0 == strcmp(argv[arg], "--hash")) &&
                 ((arg + 1) < argc))
        {
            arg++;
            p_hash = argv[arg];
        }
        else if (0 == strcmp(argv[arg], "--help"))
        {
            usage();
            return EXIT_SUCCESS;
        }
        else
        {
            (void)fprintf(stderr, "Unknown option: %s\n", argv[arg]);
            usage();
            return EXIT_FAILURE;
        }
    }

    /* --- Load the sealed message --------------------------------- */
    p_blob = load_file(p_secret, &blob_len);
    if (NULL == p_blob)
    {
        (void)fprintf(stderr, "[glyph] cannot read secret '%s'\n",
                      p_secret);
        goto cleanup;
    }

    /* --- Offline mode: one JPEG, supplied hash ------------------- */
    if (NULL != p_image)
    {
        jpeg_image_t img;
        uint8_t *    p_jpeg = NULL;
        size_t       jlen   = 0;

        if ((NULL == p_hash) || (0 != parse_hash_hex(p_hash, sn_hash)))
        {
            (void)fprintf(stderr,
                          "[glyph] --image needs a valid --hash "
                          "(64 hex chars)\n");
            goto cleanup;
        }
        p_jpeg = load_file(p_image, &jlen);
        if (NULL == p_jpeg)
        {
            (void)fprintf(stderr, "[glyph] cannot read image '%s'\n",
                          p_image);
            goto cleanup;
        }
        if (0 != jpeg_decode_gray(p_jpeg, jlen, &img))
        {
            (void)fprintf(stderr, "[glyph] JPEG decode failed\n");
            free(p_jpeg);
            goto cleanup;
        }
        (void)printf("[glyph] decoded %dx%d; scanning...\n",
                     img.width, img.height);
        (void)process_gray(img.p_gray, img.width, img.height, sn_hash,
                           p_blob, blob_len, last_payload, &have_last);
        jpeg_image_free(&img);
        free(p_jpeg);
        exit_code = EXIT_SUCCESS;
        goto cleanup;
    }

    /* --- Live mode ----------------------------------------------- */
    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);

    if (0 != device_scan_find_glasses(
                 &xr_device_provider_is_product_id_valid, &glasses_pid))
    {
        (void)fprintf(stderr, "[glyph] no VITURE glasses on USB\n");
        goto cleanup;
    }

    if (0 != read_sn_hash(glasses_pid, sn_hash))
    {
        goto cleanup;
    }
    (void)printf("[glyph] device sn_hash ");
    print_hex(sn_hash, SN_HASH_BYTES);
    (void)printf("\n");

    /* --- Camera stream ------------------------------------------- */
    {
        frame_slot_t slot;
        int          cam_vid = 0;
        int          cam_pid = 0;
        XRCameraProviderHandle p_cam = NULL;
        uint32_t     last_seq = 0;

        memset(&slot, 0, sizeof(slot));
        (void)pthread_mutex_init(&slot.lock, NULL);

        cam_vid = xr_camera_provider_get_camera_vid(glasses_pid);
        cam_pid = xr_camera_provider_get_camera_pid(glasses_pid);
        if ((0 == cam_vid) || (0 == cam_pid))
        {
            (void)fprintf(stderr,
                          "[glyph] this model has no camera\n");
            (void)pthread_mutex_destroy(&slot.lock);
            goto cleanup;
        }
        p_cam = xr_camera_provider_create(cam_vid, cam_pid);
        if (NULL == p_cam)
        {
            (void)fprintf(stderr, "[glyph] camera create failed\n");
            (void)pthread_mutex_destroy(&slot.lock);
            goto cleanup;
        }
        if (VITURE_GLASSES_SUCCESS !=
            xr_camera_provider_start(p_cam, on_frame, &slot))
        {
            (void)fprintf(stderr, "[glyph] camera start failed\n");
            xr_camera_provider_destroy(p_cam);
            (void)pthread_mutex_destroy(&slot.lock);
            goto cleanup;
        }

        (void)signal(SIGINT, on_sigint);
        (void)printf("[glyph] scanning for glyphs. Ctrl+C to stop.\n");

        while (0 == g_stop)
        {
            uint8_t * p_local = NULL;
            size_t    local_len = 0;

            (void)pthread_mutex_lock(&slot.lock);
            if ((0 != slot.have_frame) && (slot.seq != last_seq))
            {
                p_local = (uint8_t *)malloc(slot.jpeg_len);
                if (NULL != p_local)
                {
                    memcpy(p_local, slot.p_jpeg, slot.jpeg_len);
                    local_len = slot.jpeg_len;
                    last_seq  = slot.seq;
                }
            }
            (void)pthread_mutex_unlock(&slot.lock);

            if (NULL != p_local)
            {
                jpeg_image_t img;
                if (0 == jpeg_decode_gray(p_local, local_len, &img))
                {
                    (void)process_gray(img.p_gray, img.width,
                                       img.height, sn_hash, p_blob,
                                       blob_len, last_payload,
                                       &have_last);
                    jpeg_image_free(&img);
                }
                free(p_local);
            }
            else
            {
                struct timespec ts = { 0, 5 * 1000 * 1000 };
                (void)nanosleep(&ts, NULL);
            }
        }

        (void)xr_camera_provider_stop(p_cam);
        xr_camera_provider_destroy(p_cam);
        (void)pthread_mutex_lock(&slot.lock);
        if (NULL != slot.p_jpeg)
        {
            free(slot.p_jpeg);
        }
        (void)pthread_mutex_unlock(&slot.lock);
        (void)pthread_mutex_destroy(&slot.lock);
        (void)printf("\n[glyph] stopped.\n");
        exit_code = EXIT_SUCCESS;
    }

cleanup:
    if (NULL != p_blob)
    {
        free(p_blob);
    }
    return exit_code;
}
