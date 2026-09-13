#include "glasses_link.h"

#include "viture_device_carina.h"
#include "viture_glasses_provider.h"
#include "viture_protocol_public.h"
#include "viture_result.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DOF_TYPE_3DOF       (0)
#define HOLD_GAP_MS         (700L)
#define HOLD_MIN_MS         (1200L)
#define HOLD_MIN_EVENTS     (4)
#define COOLDOWN_MS         (8000L)
#define VOLUME_MID          (4)
#define VOLUME_UNKNOWN      (-1)
#define SDK_LOG_ERRORS_ONLY (1)
#define MS_PER_SEC          (1000L)
#define NS_PER_MS           (1000000L)

struct glasses_link
{
    void *          p_handle;
    pthread_mutex_t lock;
    int             lock_ready;
    int             initialised;
    int             started;
    int             triggered;
    int             display_mode;
    int             mode_dirty;
    float           film_voltage;
    int             film_known;
    int             film_dirty;
    long            run_start_ms;
    long            run_last_ms;
    int             run_events;
    long            quiet_until_ms;
    int             volume_original;
    int             volume_prev;
    int             direction;
    int             fired_direction;
    uint8_t         sn_hash[GLASSES_LINK_ID_BYTES];
};

static glasses_link_t * g_p_listening = NULL;

static long now_ms(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);

    return (ts.tv_sec * MS_PER_SEC) + (ts.tv_nsec / NS_PER_MS);
}

static void record_press(glasses_link_t * p_link, long at_ms)
{
    if (at_ms < p_link->quiet_until_ms)
    {
        goto cleanup;
    }
    if ((0 == p_link->run_events) ||
        ((at_ms - p_link->run_last_ms) > HOLD_GAP_MS))
    {
        p_link->run_start_ms = at_ms;
        p_link->run_events   = 0;
    }
    p_link->run_last_ms = at_ms;
    p_link->run_events++;
    if ((p_link->run_events >= HOLD_MIN_EVENTS) &&
        ((at_ms - p_link->run_start_ms) >= HOLD_MIN_MS) &&
        (GLASSES_LINK_NONE != p_link->direction))
    {
        p_link->triggered       = 1;
        p_link->fired_direction = p_link->direction;
        p_link->quiet_until_ms  = at_ms + COOLDOWN_MS;
        p_link->run_events      = 0;
    }

cleanup:

    return;
}

static void note_direction(glasses_link_t * p_link, int level)
{
    if (VOLUME_UNKNOWN == p_link->volume_prev)
    {
        p_link->direction = GLASSES_LINK_NONE;
    }
    else if (level > p_link->volume_prev)
    {
        p_link->direction = GLASSES_LINK_UP;
    }
    else if (level < p_link->volume_prev)
    {
        p_link->direction = GLASSES_LINK_DOWN;
    }
    else if (0 == level)
    {
        p_link->direction = GLASSES_LINK_DOWN;
    }
    else if (level > VOLUME_MID)
    {
        p_link->direction = GLASSES_LINK_UP;
    }
    p_link->volume_prev = level;
}

static void count_press(glasses_link_t * p_link, int level)
{
    long         at     = now_ms();
    const char * p_what = NULL;

    (void)pthread_mutex_lock(&p_link->lock);
    if (0 == p_link->run_events)
    {
        p_link->direction = GLASSES_LINK_NONE;
    }
    note_direction(p_link, level);
    p_what = (GLASSES_LINK_UP == p_link->direction) ? "volume UP" :
             (GLASSES_LINK_DOWN == p_link->direction) ? "volume DOWN" :
             "volume (direction unknown)";
    record_press(p_link, at);
    if (0 != p_link->triggered)
    {
        (void)printf("[button] hold complete: %s (level %d)\n", p_what,
                     level);
    }
    else if (at >= p_link->quiet_until_ms)
    {
        (void)printf("[button] holding %s... level %d (%ld ms)\n", p_what,
                     level, at - p_link->run_start_ms);
    }
    (void)fflush(stdout);
    (void)pthread_mutex_unlock(&p_link->lock);
}

static void on_log(int level, const char * p_tag, const char * p_msg)
{
    glasses_link_t * p_link = g_p_listening;

    if ((NULL != p_msg) && (level <= SDK_LOG_ERRORS_ONLY))
    {
        (void)fprintf(stderr, "[%s] %s\n", (NULL != p_tag) ? p_tag : "sdk",
                      p_msg);
    }
    (void)p_link;
}

static int is_counted_event(int state_id)
{
    int counted = 0;

    if (VITURE_CALLBACK_ID_VOLUME == state_id)
    {
        counted = 1;
    }

    return counted;
}

static void on_toggle(glasses_link_t * p_link, int state_id, int value)
{
    (void)pthread_mutex_lock(&p_link->lock);
    if ((VITURE_CALLBACK_ID_DISPLAY_MODE == state_id) &&
        (p_link->display_mode > 0) && (value != p_link->display_mode))
    {
        p_link->mode_dirty = 1;
        (void)printf("[button] display mode toggled (%d -> %d); will "
                     "restore\n", p_link->display_mode, value);
    }
    if ((VITURE_CALLBACK_ID_ELECTROCHROMIC_FILM == state_id) &&
        (0 != p_link->film_known))
    {
        p_link->film_dirty = 1;
        (void)printf("[button] lens film toggled (%d); will restore\n",
                     value);
    }
    (void)fflush(stdout);
    (void)pthread_mutex_unlock(&p_link->lock);
}

static void on_state(int state_id, int value)
{
    glasses_link_t * p_link = g_p_listening;

    if (NULL == p_link)
    {
        goto cleanup;
    }
    if ((VITURE_CALLBACK_ID_DISPLAY_MODE == state_id) ||
        (VITURE_CALLBACK_ID_ELECTROCHROMIC_FILM == state_id))
    {
        on_toggle(p_link, state_id, value);
        goto cleanup;
    }
    if (0 == is_counted_event(state_id))
    {
        goto cleanup;
    }
    count_press(p_link, value);

cleanup:

    return;
}

static int bring_up(glasses_link_t * p_link, int glasses_pid)
{
    int result = -1;
    int rc     = VITURE_GLASSES_ERROR_UNKNOWN;

    p_link->p_handle = xr_device_provider_create(glasses_pid);
    if (NULL == p_link->p_handle)
    {
        (void)fprintf(stderr, "[link] device provider create failed\n");
        goto cleanup;
    }
    if (XR_DEVICE_TYPE_VITURE_CARINA ==
        xr_device_provider_get_device_type(p_link->p_handle))
    {
        rc = xr_device_provider_set_dof_type_carina(p_link->p_handle,
                                                    DOF_TYPE_3DOF);
        if (VITURE_GLASSES_SUCCESS != rc)
        {
            (void)fprintf(stderr, "[link] set_dof_type failed (%d)\n", rc);
            goto cleanup;
        }
    }
    rc = xr_device_provider_initialize(p_link->p_handle, NULL, NULL);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[link] initialize failed (%d)\n", rc);
        goto cleanup;
    }
    p_link->initialised = 1;
    rc = xr_device_provider_get_sn_hash(p_link->p_handle, p_link->sn_hash);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[link] get_sn_hash failed (%d)\n", rc);
        goto cleanup;
    }
    result = 0;

cleanup:

    return result;
}

static int start_listening(glasses_link_t * p_link)
{
    int result = -1;
    int rc     = VITURE_GLASSES_ERROR_UNKNOWN;

    if (NULL != g_p_listening)
    {
        (void)fprintf(stderr, "[link] another link is already listening\n");
        goto cleanup;
    }
    g_p_listening = p_link;
    xr_device_provider_set_log_hook(on_log);
    rc = xr_device_provider_register_state_callback(p_link->p_handle,
                                                    on_state);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[link] state callback failed (%d)\n", rc);
        goto cleanup;
    }
    rc = xr_device_provider_start(p_link->p_handle);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[link] start failed (%d)\n", rc);
        goto cleanup;
    }
    p_link->started         = 1;
    p_link->volume_original = xr_device_provider_get_volume_level(
        p_link->p_handle);
    p_link->volume_prev     = VOLUME_UNKNOWN;
    if (VITURE_GLASSES_SUCCESS ==
        xr_device_provider_set_volume_level(p_link->p_handle, VOLUME_MID))
    {
        p_link->volume_prev = VOLUME_MID;
    }
    p_link->display_mode = xr_device_provider_get_display_mode(
        p_link->p_handle);
    if (VITURE_GLASSES_SUCCESS ==
        xr_device_provider_get_film_mode(p_link->p_handle,
                                         &p_link->film_voltage))
    {
        p_link->film_known = 1;
    }
    (void)printf("[link] listening: hold volume UP to decode, DOWN to "
                 "encode (volume was %d, set to %d; display mode "
                 "0x%02x)\n", p_link->volume_original, VOLUME_MID,
                 (unsigned int)p_link->display_mode);
    result = 0;

cleanup:
    if ((0 != result) && (p_link == g_p_listening))
    {
        g_p_listening = NULL;
    }

    return result;
}

glasses_link_t * glasses_link_open(int glasses_pid, int listen_buttons)
{
    glasses_link_t * p_link = NULL;
    glasses_link_t * p_ok   = NULL;

    p_link = (glasses_link_t *)calloc(1U, sizeof(*p_link));
    if (NULL == p_link)
    {
        goto cleanup;
    }
    if (0 != pthread_mutex_init(&p_link->lock, NULL))
    {
        goto cleanup;
    }
    p_link->lock_ready = 1;
    if (0 != bring_up(p_link, glasses_pid))
    {
        goto cleanup;
    }
    if ((0 != listen_buttons) && (0 != start_listening(p_link)))
    {
        goto cleanup;
    }
    p_ok   = p_link;
    p_link = NULL;

cleanup:
    glasses_link_close(p_link);

    return p_ok;
}

const uint8_t * glasses_link_sn_hash(const glasses_link_t * p_link)
{
    const uint8_t * p_hash = NULL;

    if (NULL != p_link)
    {
        p_hash = p_link->sn_hash;
    }

    return p_hash;
}

int glasses_link_take_trigger(glasses_link_t * p_link)
{
    int fired = GLASSES_LINK_NONE;

    if ((NULL == p_link) || (0 == p_link->lock_ready))
    {
        goto cleanup;
    }
    (void)pthread_mutex_lock(&p_link->lock);
    if (0 != p_link->triggered)
    {
        fired = p_link->fired_direction;
    }
    p_link->triggered = 0;
    (void)pthread_mutex_unlock(&p_link->lock);

cleanup:

    return fired;
}

static void restore_film(glasses_link_t * p_link)
{
    int rc = xr_device_provider_set_film_mode(p_link->p_handle,
                                              p_link->film_voltage);

    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[link] could not restore film (%d)\n", rc);
    }
}

static void restore_mode(glasses_link_t * p_link)
{
    int rc = xr_device_provider_set_display_mode(p_link->p_handle,
                                                 p_link->display_mode);

    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[link] could not restore display mode "
                              "(%d)\n", rc);
    }
}

void glasses_link_restore(glasses_link_t * p_link)
{
    int mode_dirty = 0;
    int film_dirty = 0;

    if ((NULL == p_link) || (0 == p_link->lock_ready))
    {
        goto cleanup;
    }
    (void)pthread_mutex_lock(&p_link->lock);
    mode_dirty         = p_link->mode_dirty;
    film_dirty         = p_link->film_dirty;
    p_link->mode_dirty = 0;
    p_link->film_dirty = 0;
    (void)pthread_mutex_unlock(&p_link->lock);
    if (0 != mode_dirty)
    {
        restore_mode(p_link);
    }
    if (0 != film_dirty)
    {
        restore_film(p_link);
    }
    if (VITURE_GLASSES_SUCCESS ==
        xr_device_provider_set_volume_level(p_link->p_handle, VOLUME_MID))
    {
        (void)pthread_mutex_lock(&p_link->lock);
        p_link->volume_prev = VOLUME_MID;
        p_link->run_events  = 0;
        (void)pthread_mutex_unlock(&p_link->lock);
    }

cleanup:

    return;
}

void glasses_link_close(glasses_link_t * p_link)
{
    if (NULL == p_link)
    {
        goto cleanup;
    }
    if (p_link == g_p_listening)
    {
        g_p_listening = NULL;
    }
    if ((0 != p_link->started) && (p_link->volume_original >= 0))
    {
        (void)xr_device_provider_set_volume_level(p_link->p_handle,
                                                  p_link->volume_original);
    }
    if (0 != p_link->started)
    {
        (void)xr_device_provider_stop(p_link->p_handle);
    }
    if (0 != p_link->initialised)
    {
        (void)xr_device_provider_shutdown(p_link->p_handle);
    }
    if (NULL != p_link->p_handle)
    {
        xr_device_provider_destroy(p_link->p_handle);
    }
    if (0 != p_link->lock_ready)
    {
        (void)pthread_mutex_destroy(&p_link->lock);
    }
    free(p_link);

cleanup:

    return;
}
