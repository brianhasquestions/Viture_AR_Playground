#include "actions.h"
#include "camera.h"
#include "vault.h"
#include "fileio.h"
#include "hexcodec.h"
#include "session.h"

#include "device_scan.h"
#include "xr_args.h"

#include "viture_glasses_provider.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_VAULT       "sealed.vault"
#define DEFAULT_CAPTURES    "captures"
#define DEFAULT_MAXDIST     (22)
#define HEX_CAP             ((2 * KEYRING_KEY_BYTES) + 1)
#define IDLE_SLEEP_NS       (10L * 1000L * 1000L)
#define SDK_LOG_ERRORS_ONLY (1)
#define FIRST_OPTION_ARG    (2)
#define SEAL_WARMUP_FRAMES  (20)
#define VAULT_LIST_CAP      (64)
#define RESULT_HOLD_MS      (5000L)
#define MS_PER_SEC          (1000L)
#define NS_PER_MS           (1000000L)
#define ARGS_OK             (0)
#define ARGS_HELP           (1)
#define ARGS_BAD            (-1)

#define WATCH_HINT  "Object in brackets. Hold volume UP: decode / DOWN: encode"

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

static long now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);

    return (ts.tv_sec * MS_PER_SEC) + (ts.tv_nsec / NS_PER_MS);
}

static void usage(void)
{
    (void)printf(
        "sealed_drawing - seal a message to a picture and one pair of "
        "glasses\n\n"
        "  identity                 print this pair's public key\n"
        "  list                     show the records in the vault\n"
        "  forget <n>               remove record n from the vault\n"
        "  watch                    live: hold volume UP to decode the "
        "object\n"
        "                           in view, volume DOWN to encode a "
        "message to it\n"
        "  seal [--message \"text\"]  seal a message to the view "
        "(one frame)\n"
        "  reveal                   reveal the message for the view\n\n"
        "Options:\n"
        "  --vault <file>       vault file (default sealed.vault)\n"
        "  --captures <dir>     where pictures go (default captures/)\n"
        "  --recipient <hex64>  seal for another pair's public key\n"
        "  --pin <text>         extra secret mixed into the identity\n"
        "  --maxdist <n>        hash-score threshold for texture-poor "
        "objects\n"
        "                       (default 22)\n"
        "  --messages <file>    preset messages for the on-glasses "
        "picker,\n"
        "                       one per line\n"
        "  --display            show pictures on the glasses (watch "
        "default)\n"
        "  --windowed           show them in a desktop window instead\n"
        "  --no-preview         (watch) do not stream the live view\n"
        "Offline (no headset):\n"
        "  --image <f.jpg>      use this frame instead of the camera\n"
        "  --hash <hex64>       use this glasses serial hash\n");
}

static int parse_one_arg(options_t * p_opts, char ** argv, int remaining)
{
    static const char * const NAMES[] =
    {
        "--message", "--vault", "--captures", "--image", "--hash",
        "--recipient", "--pin", "--maxdist", "--messages",
    };
    const char ** targets[] =
    {
        &p_opts->p_message, &p_opts->p_vault, &p_opts->p_captures,
        &p_opts->p_image, &p_opts->p_hash, &p_opts->p_recipient,
        &p_opts->p_pin, &p_opts->p_maxdist, &p_opts->p_messages,
    };
    size_t count    = sizeof(NAMES) / sizeof(NAMES[0]);
    size_t i        = 0;
    int    consumed = 0;

    for (i = 0; i < count; i++)
    {
        if ((0 == strcmp(argv[0], NAMES[i])) && (remaining >= 2))
        {
            *targets[i] = argv[1];
            consumed    = 2;
        }
    }
    if (0 == strcmp(argv[0], "--display"))
    {
        p_opts->want_display = 1;
        consumed             = 1;
    }
    if (0 == strcmp(argv[0], "--windowed"))
    {
        p_opts->windowed     = 1;
        p_opts->want_display = 1;
        consumed             = 1;
    }
    if (0 == strcmp(argv[0], "--no-preview"))
    {
        p_opts->no_preview = 1;
        consumed           = 1;
    }

    return consumed;
}

static int parse_args(int argc, char ** argv, options_t * p_opts)
{
    int result = ARGS_BAD;
    int arg    = FIRST_OPTION_ARG;

    memset(p_opts, 0, sizeof(*p_opts));
    p_opts->p_vault    = DEFAULT_VAULT;
    p_opts->p_captures = DEFAULT_CAPTURES;
    p_opts->max_dist   = DEFAULT_MAXDIST;
    if (argc < FIRST_OPTION_ARG)
    {
        goto cleanup;
    }
    p_opts->p_cmd = argv[1];
    if (0 == strcmp(p_opts->p_cmd, "--help"))
    {
        result = ARGS_HELP;
        goto cleanup;
    }
    if ((0 == strcmp(p_opts->p_cmd, "forget")) && (argc > FIRST_OPTION_ARG))
    {
        p_opts->p_forget = argv[FIRST_OPTION_ARG];
        arg++;
    }
    while (arg < argc)
    {
        int consumed = 0;

        if (0 == strcmp(argv[arg], "--help"))
        {
            result = ARGS_HELP;
            goto cleanup;
        }
        consumed = parse_one_arg(p_opts, &argv[arg], argc - arg);
        if (0 == consumed)
        {
            (void)fprintf(stderr, "Unknown option: %s\n", argv[arg]);
            goto cleanup;
        }
        arg += consumed;
    }
    if ((NULL != p_opts->p_maxdist) &&
        (0 != xr_args_parse_int(p_opts->p_maxdist, &p_opts->max_dist)))
    {
        (void)fprintf(stderr, "--maxdist needs a whole number\n");
        goto cleanup;
    }
    if ((p_opts->max_dist < 0) || (p_opts->max_dist > PHASH_MATCH_MAX))
    {
        (void)fprintf(stderr, "--maxdist must be 0-%d\n", PHASH_MATCH_MAX);
        goto cleanup;
    }
    if (0 == strcmp(p_opts->p_cmd, "watch"))
    {
        p_opts->want_display = 1;
    }
    result = ARGS_OK;

cleanup:

    return result;
}

static int validate_options(const options_t * p_opts)
{
    int result   = -1;
    int is_known = 0;

    is_known = (0 == strcmp(p_opts->p_cmd, "identity")) ||
               (0 == strcmp(p_opts->p_cmd, "seal")) ||
               (0 == strcmp(p_opts->p_cmd, "reveal")) ||
               (0 == strcmp(p_opts->p_cmd, "watch")) ||
               (0 == strcmp(p_opts->p_cmd, "list")) ||
               (0 == strcmp(p_opts->p_cmd, "forget"));
    if (0 == is_known)
    {
        (void)fprintf(stderr, "Unknown command '%s'\n", p_opts->p_cmd);
        goto cleanup;
    }
    if ((NULL != p_opts->p_image) && (NULL == p_opts->p_hash))
    {
        (void)fprintf(stderr, "--image needs --hash (64 hex chars)\n");
        goto cleanup;
    }
    result = 0;

cleanup:

    return result;
}

static int decode_key_hex(const char * p_hex, uint8_t * p_out)
{
    int        result = -1;
    byte_buf_t buf;

    buf.p_data = p_out;
    buf.cap    = KEYRING_KEY_BYTES;
    buf.len    = 0;
    if ((0 == hex_decode(p_hex, &buf)) && (KEYRING_KEY_BYTES == buf.len))
    {
        result = 0;
    }

    return result;
}

static int open_link(session_t * p_s, int * p_glasses_pid)
{
    int result = -1;
    int listen = (0 == strcmp(p_s->opts.p_cmd, "watch")) ? 1 : 0;

    xr_device_provider_set_log_level(SDK_LOG_ERRORS_ONLY);
    if (0 != device_scan_find_glasses(
                 &xr_device_provider_is_product_id_valid, p_glasses_pid))
    {
        (void)fprintf(stderr, "[sealed] no VITURE glasses on USB\n");
        goto cleanup;
    }
    p_s->p_link = glasses_link_open(*p_glasses_pid, listen);
    if (NULL == p_s->p_link)
    {
        (void)fprintf(stderr, "[sealed] could not open the glasses\n");
        goto cleanup;
    }
    memcpy(p_s->sn_hash, glasses_link_sn_hash(p_s->p_link),
           GLASSES_LINK_ID_BYTES);
    result = 0;

cleanup:

    return result;
}

static int acquire_identity(session_t * p_s, int * p_glasses_pid)
{
    int result = -1;

    if (NULL != p_s->opts.p_hash)
    {
        if (0 != decode_key_hex(p_s->opts.p_hash, p_s->sn_hash))
        {
            (void)fprintf(stderr, "[sealed] --hash must be 64 hex "
                                  "chars\n");
            goto cleanup;
        }
    }
    else if (0 != open_link(p_s, p_glasses_pid))
    {
        goto cleanup;
    }
    if (0 != keyring_derive(p_s->sn_hash, p_s->opts.p_pin, &p_s->identity))
    {
        (void)fprintf(stderr, "[sealed] identity derivation failed\n");
        goto cleanup;
    }
    memcpy(p_s->recipient_pub, p_s->identity.pub, KEYRING_KEY_BYTES);
    if ((NULL != p_s->opts.p_recipient) &&
        (0 != decode_key_hex(p_s->opts.p_recipient, p_s->recipient_pub)))
    {
        (void)fprintf(stderr, "[sealed] --recipient must be 64 hex "
                              "chars\n");
        goto cleanup;
    }
    result = 0;

cleanup:

    return result;
}

static void print_identity(const session_t * p_s)
{
    char        hex[HEX_CAP];
    byte_span_t span;

    span.p_data = p_s->sn_hash;
    span.len    = GLASSES_LINK_ID_BYTES;
    if (0 == hex_encode(span, hex, sizeof(hex)))
    {
        (void)printf("serial hash: %s\n", hex);
    }
    span.p_data = p_s->identity.pub;
    span.len    = KEYRING_KEY_BYTES;
    if (0 == hex_encode(span, hex, sizeof(hex)))
    {
        (void)printf("public key:  %s\n", hex);
    }
}

static int handle_frame(session_t * p_s, byte_span_t jpeg)
{
    int result = 0;

    p_s->frames_seen++;
    if (0 != strcmp(p_s->opts.p_cmd, "seal"))
    {
        result = (ACTION_ERROR == actions_reveal_frame(p_s, jpeg)) ? -1 : 0;
    }
    else if ((0 != p_s->offline) || (p_s->frames_seen > SEAL_WARMUP_FRAMES))
    {
        result = actions_seal_frame(p_s, jpeg);
        g_stop = 1;
    }

    return result;
}

static int run_vault_cmd(session_t * p_s)
{
    int           result = -1;
    int           total  = 0;
    int           i      = 0;
    int           index  = 0;
    vault_entry_t entries[VAULT_LIST_CAP];

    if (0 == strcmp(p_s->opts.p_cmd, "list"))
    {
        total = vault_list(p_s->opts.p_vault, entries, VAULT_LIST_CAP);
        if (total < 0)
        {
            (void)fprintf(stderr, "[sealed] vault '%s' is corrupt\n",
                          p_s->opts.p_vault);
            goto cleanup;
        }
        (void)printf("%s: %d record%s\n", p_s->opts.p_vault, total,
                     (1 == total) ? "" : "s");
        for (i = 0; (i < total) && (i < VAULT_LIST_CAP); i++)
        {
            (void)printf("  [%d] %d view%s, %zu-byte sealed message\n",
                         entries[i].index, entries[i].views,
                         (1 == entries[i].views) ? "" : "s",
                         entries[i].blob_bytes);
        }
        result = 0;
        goto cleanup;
    }
    if ((NULL == p_s->opts.p_forget) ||
        (0 != xr_args_parse_int(p_s->opts.p_forget, &index)))
    {
        (void)fprintf(stderr, "forget needs a record number from list\n");
        goto cleanup;
    }
    if (0 != vault_forget(p_s->opts.p_vault, index))
    {
        (void)fprintf(stderr, "[sealed] no record %d in '%s'\n", index,
                      p_s->opts.p_vault);
        goto cleanup;
    }
    (void)printf("[sealed] forgot record %d\n", index);
    result = 0;

cleanup:

    return result;
}

static int run_offline(session_t * p_s)
{
    int         result = -1;
    byte_buf_t  file;
    byte_span_t jpeg;

    memset(&file, 0, sizeof(file));
    p_s->offline = 1;
    if (0 != fileio_read_all(p_s->opts.p_image, &file))
    {
        (void)fprintf(stderr, "[sealed] cannot read '%s'\n",
                      p_s->opts.p_image);
        goto cleanup;
    }
    if (0 != p_s->opts.windowed)
    {
        p_s->p_display = display_create(1);
    }
    jpeg.p_data = file.p_data;
    jpeg.len    = file.len;
    if (0 == strcmp(p_s->opts.p_cmd, "watch"))
    {
        int mode = (NULL != p_s->opts.p_message) ? ACTION_MODE_ENCODE
                                                 : ACTION_MODE_DECODE;

        result = (ACTION_ERROR == actions_capture(p_s, jpeg, mode)) ? -1
                                                                    : 0;
    }
    else
    {
        result = handle_frame(p_s, jpeg);
    }
    if (0 != result)
    {
        (void)fprintf(stderr, "[sealed] frame did not decode or the "
                              "command failed\n");
    }
    while ((NULL != p_s->p_display) && (0 == g_stop))
    {
        if (0 != display_pump(p_s->p_display))
        {
            g_stop = 1;
        }
    }

cleanup:
    if (NULL != file.p_data)
    {
        free(file.p_data);
    }

    return result;
}

static void idle_tick(session_t * p_s)
{
    struct timespec ts;

    if (NULL != p_s->p_display)
    {
        if (0 != display_pump(p_s->p_display))
        {
            g_stop = 1;
        }
    }
    else
    {
        ts.tv_sec  = 0;
        ts.tv_nsec = IDLE_SLEEP_NS;
        (void)nanosleep(&ts, NULL);
    }
}

static void watch_frame(session_t * p_s, byte_span_t jpeg)
{
    int holding = (now_ms() < p_s->hold_until_ms) ? 1 : 0;

    int direction = glasses_link_take_trigger(p_s->p_link);

    if (GLASSES_LINK_NONE != direction)
    {
        (void)printf("[capture] trigger (%s), capturing.\n",
                     (GLASSES_LINK_UP == direction) ? "decode" : "encode");
        glasses_link_restore(p_s->p_link);
        (void)actions_capture(p_s, jpeg,
                              (GLASSES_LINK_UP == direction)
                                  ? ACTION_MODE_DECODE
                                  : ACTION_MODE_ENCODE);
        p_s->hold_until_ms = now_ms() + RESULT_HOLD_MS;
    }
    else if ((0 == holding) && (0 == p_s->opts.no_preview))
    {
        actions_preview(p_s, jpeg, WATCH_HINT);
    }
}

static void open_display(session_t * p_s)
{
    int is_seal = (0 == strcmp(p_s->opts.p_cmd, "seal")) ? 1 : 0;

    if ((0 == p_s->opts.want_display) || (0 != is_seal))
    {
        goto cleanup;
    }
    p_s->p_display = display_create(p_s->opts.windowed);
    display_show_message(p_s->p_display, "Sealed drawing",
                         (0 == strcmp(p_s->opts.p_cmd, "watch"))
                             ? "Waiting for the camera."
                             : "Look at a sealed picture.");

cleanup:

    return;
}

static int run_live(session_t * p_s, int glasses_pid)
{
    int            result   = -1;
    int            is_watch = (0 == strcmp(p_s->opts.p_cmd, "watch")) ? 1
                                                                       : 0;
    camera_ctx_t * p_cam    = NULL;
    byte_buf_t     frame;
    byte_span_t    jpeg;

    memset(&frame, 0, sizeof(frame));
    p_cam = camera_open(glasses_pid);
    if (NULL == p_cam)
    {
        goto cleanup;
    }
    p_s->p_camera = p_cam;
    open_display(p_s);
    (void)signal(SIGINT, on_sigint);
    (void)printf("[sealed] %s: point the camera at the picture. Ctrl+C "
                 "to stop.\n", p_s->opts.p_cmd);
    while (0 == g_stop)
    {
        if (1 == camera_take_frame(p_cam, &frame))
        {
            jpeg.p_data = frame.p_data;
            jpeg.len    = frame.len;
            if (0 != is_watch)
            {
                watch_frame(p_s, jpeg);
            }
            else
            {
                (void)handle_frame(p_s, jpeg);
            }
        }
        idle_tick(p_s);
    }
    if ((0 != strcmp(p_s->opts.p_cmd, "seal")) || (0 != p_s->sealed))
    {
        result = 0;
    }

cleanup:
    p_s->p_camera = NULL;
    camera_close(p_cam);
    if (NULL != frame.p_data)
    {
        free(frame.p_data);
    }

    return result;
}

int main(int argc, char ** argv)
{
    int         exit_code   = EXIT_FAILURE;
    int         rc          = ARGS_BAD;
    int         glasses_pid = DEVICE_SCAN_NO_PID;
    session_t * p_s         = NULL;

    p_s = (session_t *)calloc(1U, sizeof(*p_s));
    if (NULL == p_s)
    {
        goto cleanup;
    }
    rc = parse_args(argc, argv, &p_s->opts);
    if (ARGS_OK != rc)
    {
        usage();
        exit_code = (ARGS_HELP == rc) ? EXIT_SUCCESS : EXIT_FAILURE;
        goto cleanup;
    }
    if (0 != validate_options(&p_s->opts))
    {
        goto cleanup;
    }
    if ((0 == strcmp(p_s->opts.p_cmd, "list")) ||
        (0 == strcmp(p_s->opts.p_cmd, "forget")))
    {
        exit_code = (0 == run_vault_cmd(p_s)) ? EXIT_SUCCESS : EXIT_FAILURE;
        goto cleanup;
    }
    if (0 != acquire_identity(p_s, &glasses_pid))
    {
        goto cleanup;
    }
    if (0 == strcmp(p_s->opts.p_cmd, "identity"))
    {
        print_identity(p_s);
        exit_code = EXIT_SUCCESS;
        goto cleanup;
    }
    if (NULL != p_s->opts.p_image)
    {
        rc = run_offline(p_s);
    }
    else
    {
        rc = run_live(p_s, glasses_pid);
    }
    exit_code = (0 == rc) ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
    if (NULL != p_s)
    {
        display_destroy(p_s->p_display);
        glasses_link_close(p_s->p_link);
        keyring_wipe(&p_s->identity);
        free(p_s);
    }

    return exit_code;
}
