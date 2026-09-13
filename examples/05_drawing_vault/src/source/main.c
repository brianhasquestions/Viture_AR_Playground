/**********************************************************************
 * @file    main.c
 * @brief   drawing_vault - draw something, seal a message to it and your
 *          glasses, then reveal it later by looking at the drawing.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 *   drawing_vault enroll --message "text"   [offline: --image f --hash h]
 *   drawing_vault recall  [--display]        [offline: --image f --hash h]
 *
 * enroll fingerprints the drawing in view and stores {fingerprint,
 * message-sealed-to-this-device}. recall fingerprints the current view,
 * finds the closest stored drawing, and decrypts its message with the
 * live glasses hash - showing it on the glasses HUD with --display.
 *
 * Offline mode runs the identical pipeline on a JPEG with a supplied
 * hash, so enroll/recall are testable without the headset.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "display.h"
#include "phash.h"
#include "vault.h"

#include "carina_pose.h"
#include "device_scan.h"
#include "jpeg_decode.h"

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
#include <time.h>

#define SN_HASH_BYTES       (32)
#define MSG_CAP             (4096)
#define DEFAULT_MAXDIST     (10)

/* ---- camera plumbing (mirrors 04_glyph_reader) --------------------- */
typedef struct
{
    pthread_mutex_t lock;
    uint8_t *       p_jpeg;
    size_t          jpeg_cap;
    size_t          jpeg_len;
    uint32_t        seq;
    int             have_frame;
} frame_slot_t;

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

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

/* ---- small helpers ------------------------------------------------- */
static int parse_hash_hex(const char * p_hex, uint8_t * p_out)
{
    int i = 0;

    if ((NULL == p_hex) || (strlen(p_hex) != (size_t)(SN_HASH_BYTES * 2)))
    {
        return -1;
    }
    for (i = 0; i < SN_HASH_BYTES; i++)
    {
        int hi = 0;
        int lo = 0;
        char ch = p_hex[i * 2];
        char cl = p_hex[(i * 2) + 1];

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

static uint8_t * load_file(const char * p_path, size_t * p_len)
{
    FILE *    p_f   = fopen(p_path, "rb");
    uint8_t * p_buf = NULL;
    long      sz    = 0;

    if (NULL == p_f)
    {
        return NULL;
    }
    if ((0 == fseek(p_f, 0, SEEK_END)) && ((sz = ftell(p_f)) > 0) &&
        (0 == fseek(p_f, 0, SEEK_SET)))
    {
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
    }
    (void)fclose(p_f);
    return p_buf;
}

/**********************************************************************
 * @brief  Fingerprint a JPEG frame in memory.
 *
 * @return  0 on success, -1 on decode failure.
 **********************************************************************/
static int fingerprint_jpeg(const uint8_t * p_jpeg, size_t len,
                            uint8_t p_phash[PHASH_BYTES])
{
    jpeg_image_t img;
    int          rc = -1;

    if (0 == jpeg_decode_gray(p_jpeg, len, &img))
    {
        rc = phash_compute(img.p_gray, img.width, img.height, p_phash);
        jpeg_image_free(&img);
    }
    return rc;
}

static int read_sn_hash(int glasses_pid, uint8_t * p_sn_hash)
{
    carina_pose_ctx_t * p_pose = carina_pose_create();
    int                 rc     = -1;

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
    }
    carina_pose_stop(p_pose);
    carina_pose_destroy(p_pose);
    return rc;
}

/**********************************************************************
 * @brief  Report a recall outcome to the terminal.
 **********************************************************************/
static void report_recall(int outcome, const uint8_t * p_msg,
                          size_t msg_len, int dist)
{
    if (VAULT_OPENED == outcome)
    {
        (void)printf("\n[unlocked] drawing matched (dist %d)\n  \"%.*s\"\n",
                     dist, (int)msg_len, (const char *)p_msg);
    }
    else if (VAULT_LOCKED == outcome)
    {
        (void)printf("[locked] drawing matched, but not sealed to "
                     "these glasses.\n");
    }
    else if (VAULT_NO_MATCH == outcome)
    {
        (void)printf("[no match] this drawing is not in the vault.\n");
    }
    else
    {
        (void)fprintf(stderr, "[vault] error.\n");
    }
}

static void usage(void)
{
    (void)printf(
        "drawing_vault - seal a message to a drawing + your glasses\n\n"
        "  enroll --message \"text\"   store a message for the drawing "
        "in view\n"
        "  recall  [--display]        reveal the message for the "
        "drawing in view\n\n"
        "Common options:\n"
        "  --vault  <file>   vault file (default vault.dat)\n"
        "  --maxdist <n>     match threshold, 0-64 (default 10)\n"
        "  --display         (recall) show the message on the glasses\n"
        "  --windowed        (recall) preview in a desktop window\n"
        "Offline (no headset):\n"
        "  --image <f.jpg>   use this frame instead of the camera\n"
        "  --hash  <hex64>   use this glasses hash\n");
}

/* ---- enroll / recall from a single fingerprint --------------------- */

static int do_enroll(const char * p_vault, const uint8_t * p_phash,
                     const uint8_t * p_sn_hash, const char * p_message)
{
    if (0 != vault_enroll(p_vault, p_phash, p_sn_hash,
                          (const uint8_t *)p_message, strlen(p_message)))
    {
        (void)fprintf(stderr, "[vault] enroll failed\n");
        return -1;
    }
    (void)printf("[vault] enrolled: message sealed to this drawing and "
                 "these glasses.\n");
    return 0;
}

int main(int argc, char ** argv)
{
    const char * p_cmd     = NULL;
    const char * p_vault   = "vault.dat";
    const char * p_image   = NULL;
    const char * p_hash    = NULL;
    const char * p_message = NULL;
    int          max_dist  = DEFAULT_MAXDIST;
    int          windowed  = 0;
    int          want_disp = 0;
    int          arg       = 0;
    int          exit_code = EXIT_FAILURE;
    int          glasses_pid = 0;
    uint8_t      sn_hash[SN_HASH_BYTES] = { 0 };

    if (argc < 2)
    {
        usage();
        return EXIT_FAILURE;
    }
    p_cmd = argv[1];

    for (arg = 2; arg < argc; arg++)
    {
        if ((0 == strcmp(argv[arg], "--message")) && ((arg + 1) < argc))
        { arg++; p_message = argv[arg]; }
        else if ((0 == strcmp(argv[arg], "--vault")) && ((arg + 1) < argc))
        { arg++; p_vault = argv[arg]; }
        else if ((0 == strcmp(argv[arg], "--image")) && ((arg + 1) < argc))
        { arg++; p_image = argv[arg]; }
        else if ((0 == strcmp(argv[arg], "--hash")) && ((arg + 1) < argc))
        { arg++; p_hash = argv[arg]; }
        else if ((0 == strcmp(argv[arg], "--maxdist")) && ((arg + 1) < argc))
        { arg++; max_dist = atoi(argv[arg]); }
        else if (0 == strcmp(argv[arg], "--display"))
        { want_disp = 1; }
        else if (0 == strcmp(argv[arg], "--windowed"))
        { windowed = 1; want_disp = 1; }
        else if (0 == strcmp(argv[arg], "--help"))
        { usage(); return EXIT_SUCCESS; }
        else
        {
            (void)fprintf(stderr, "Unknown option: %s\n", argv[arg]);
            usage();
            return EXIT_FAILURE;
        }
    }

    if ((0 != strcmp(p_cmd, "enroll")) && (0 != strcmp(p_cmd, "recall")))
    {
        (void)fprintf(stderr, "Unknown command '%s'\n", p_cmd);
        usage();
        return EXIT_FAILURE;
    }
    if ((0 == strcmp(p_cmd, "enroll")) && (NULL == p_message))
    {
        (void)fprintf(stderr, "enroll needs --message\n");
        return EXIT_FAILURE;
    }

    /* --- Offline mode: one JPEG + supplied hash ------------------- */
    if (NULL != p_image)
    {
        uint8_t   phash[PHASH_BYTES];
        uint8_t * p_jpeg = NULL;
        size_t    jlen   = 0;

        if ((NULL == p_hash) || (0 != parse_hash_hex(p_hash, sn_hash)))
        {
            (void)fprintf(stderr,
                          "[vault] --image needs --hash (64 hex)\n");
            return EXIT_FAILURE;
        }
        p_jpeg = load_file(p_image, &jlen);
        if (NULL == p_jpeg)
        {
            (void)fprintf(stderr, "[vault] cannot read '%s'\n", p_image);
            return EXIT_FAILURE;
        }
        if (0 != fingerprint_jpeg(p_jpeg, jlen, phash))
        {
            (void)fprintf(stderr, "[vault] decode/fingerprint failed\n");
            free(p_jpeg);
            return EXIT_FAILURE;
        }
        free(p_jpeg);

        if (0 == strcmp(p_cmd, "enroll"))
        {
            exit_code = (0 == do_enroll(p_vault, phash, sn_hash,
                                        p_message)) ? EXIT_SUCCESS
                                                    : EXIT_FAILURE;
        }
        else
        {
            uint8_t msg[MSG_CAP];
            size_t  msg_len = 0;
            int     dist    = 0;
            int     outcome = vault_recall(p_vault, phash, sn_hash,
                                           max_dist, msg, MSG_CAP,
                                           &msg_len, &dist);
            report_recall(outcome, msg, msg_len, dist);
            exit_code = EXIT_SUCCESS;
        }
        return exit_code;
    }

    /* --- Live mode ----------------------------------------------- */
    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);
    if (0 != device_scan_find_glasses(
                 &xr_device_provider_is_product_id_valid, &glasses_pid))
    {
        (void)fprintf(stderr, "[vault] no VITURE glasses on USB\n");
        return EXIT_FAILURE;
    }
    if (0 != read_sn_hash(glasses_pid, sn_hash))
    {
        (void)fprintf(stderr, "[vault] could not read glasses hash\n");
        return EXIT_FAILURE;
    }

    {
        frame_slot_t  slot;
        int           cam_vid = xr_camera_provider_get_camera_vid(glasses_pid);
        int           cam_pid = xr_camera_provider_get_camera_pid(glasses_pid);
        XRCameraProviderHandle p_cam = NULL;
        display_ctx_t * p_disp = NULL;
        uint32_t      last_seq = 0;
        int           enrolled = 0;

        memset(&slot, 0, sizeof(slot));
        (void)pthread_mutex_init(&slot.lock, NULL);

        if ((0 == cam_vid) || (0 == cam_pid))
        {
            (void)fprintf(stderr, "[vault] this model has no camera\n");
            (void)pthread_mutex_destroy(&slot.lock);
            return EXIT_FAILURE;
        }
        p_cam = xr_camera_provider_create(cam_vid, cam_pid);
        if ((NULL == p_cam) ||
            (VITURE_GLASSES_SUCCESS !=
             xr_camera_provider_start(p_cam, on_frame, &slot)))
        {
            (void)fprintf(stderr, "[vault] camera start failed\n");
            if (NULL != p_cam) { xr_camera_provider_destroy(p_cam); }
            (void)pthread_mutex_destroy(&slot.lock);
            return EXIT_FAILURE;
        }

        if ((0 == strcmp(p_cmd, "recall")) && (0 != want_disp))
        {
            p_disp = display_create(windowed);
            if (NULL != p_disp)
            {
                display_show_message(p_disp, "Drawing vault",
                                     "Look at an enrolled drawing.");
            }
        }

        (void)signal(SIGINT, on_sigint);
        (void)printf("[vault] %s: point the camera at the drawing. "
                     "Ctrl+C to stop.\n", p_cmd);

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
                uint8_t phash[PHASH_BYTES];
                if (0 == fingerprint_jpeg(p_local, local_len, phash))
                {
                    if (0 == strcmp(p_cmd, "enroll"))
                    {
                        (void)do_enroll(p_vault, phash, sn_hash,
                                        p_message);
                        enrolled = 1;
                        g_stop = 1;    /* One snapshot is enough. */
                    }
                    else
                    {
                        uint8_t msg[MSG_CAP];
                        size_t  msg_len = 0;
                        int     dist    = 0;
                        int     outcome = vault_recall(
                            p_vault, phash, sn_hash, max_dist, msg,
                            MSG_CAP, &msg_len, &dist);
                        if (VAULT_OPENED == outcome)
                        {
                            report_recall(outcome, msg, msg_len, dist);
                            if (NULL != p_disp)
                            {
                                msg[msg_len] = '\0';
                                display_show_message(p_disp,
                                    "Message revealed",
                                    (const char *)msg);
                            }
                        }
                    }
                }
                free(p_local);
            }

            if (NULL != p_disp)
            {
                if (0 != display_pump(p_disp))
                {
                    g_stop = 1;
                }
            }
            else
            {
                struct timespec ts = { 0, 10 * 1000 * 1000 };
                (void)nanosleep(&ts, NULL);
            }
        }

        (void)xr_camera_provider_stop(p_cam);
        xr_camera_provider_destroy(p_cam);
        if (NULL != p_disp) { display_destroy(p_disp); }
        (void)pthread_mutex_lock(&slot.lock);
        if (NULL != slot.p_jpeg) { free(slot.p_jpeg); }
        (void)pthread_mutex_unlock(&slot.lock);
        (void)pthread_mutex_destroy(&slot.lock);

        exit_code = ((0 == strcmp(p_cmd, "enroll")) && (0 == enrolled))
                        ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    return exit_code;
}
