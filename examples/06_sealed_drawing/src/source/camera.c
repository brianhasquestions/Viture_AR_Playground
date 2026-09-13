#include "camera.h"

#include "usb_detach.h"

#include "viture_camera_provider.h"
#include "viture_result.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define START_MAX_ATTEMPTS      (3)
#define START_RETRY_DELAY_NS    (500L * 1000L * 1000L)

struct camera_ctx
{
    pthread_mutex_t        lock;
    XRCameraProviderHandle p_provider;
    byte_buf_t             latest;
    uint32_t               seq;
    uint32_t               taken_seq;
    int                    lock_ready;
    int                    started;
};

static int buf_reserve(byte_buf_t * p_buf, size_t needed)
{
    int       result = 0;
    uint8_t * p_new  = NULL;

    if (p_buf->cap >= needed)
    {
        goto cleanup;
    }
    p_new = (uint8_t *)realloc(p_buf->p_data, needed);
    if (NULL == p_new)
    {
        result = -1;
        goto cleanup;
    }
    p_buf->p_data = p_new;
    p_buf->cap    = needed;

cleanup:

    return result;
}

static void on_frame(const XRCameraFrame * p_frame, void * p_user)
{
    camera_ctx_t * p_ctx = (camera_ctx_t *)p_user;

    if ((NULL == p_frame) || (NULL == p_ctx) || (NULL == p_frame->data) ||
        (0U == p_frame->size))
    {
        goto cleanup;
    }
    (void)pthread_mutex_lock(&p_ctx->lock);
    if (0 == buf_reserve(&p_ctx->latest, p_frame->size))
    {
        memcpy(p_ctx->latest.p_data, p_frame->data, p_frame->size);
        p_ctx->latest.len = p_frame->size;
        p_ctx->seq++;
    }
    (void)pthread_mutex_unlock(&p_ctx->lock);

cleanup:

    return;
}

static int start_with_retry(camera_ctx_t * p_ctx, int vid, int pid)
{
    int             result  = -1;
    int             attempt = 0;
    struct timespec backoff;

    backoff.tv_sec  = 0;
    backoff.tv_nsec = START_RETRY_DELAY_NS;
    for (attempt = 1; attempt <= START_MAX_ATTEMPTS; attempt++)
    {
        p_ctx->p_provider = xr_camera_provider_create(vid, pid);
        if (NULL == p_ctx->p_provider)
        {
            goto cleanup;
        }
        if (VITURE_GLASSES_SUCCESS ==
            xr_camera_provider_start(p_ctx->p_provider, on_frame, p_ctx))
        {
            p_ctx->started = 1;
            result         = 0;
            goto cleanup;
        }
        xr_camera_provider_destroy(p_ctx->p_provider);
        p_ctx->p_provider = NULL;
        (void)fprintf(stderr, "[camera] start attempt %d/%d failed\n",
                      attempt, START_MAX_ATTEMPTS);
        (void)nanosleep(&backoff, NULL);
    }

cleanup:

    return result;
}

camera_ctx_t * camera_open(int glasses_pid)
{
    camera_ctx_t * p_ctx = NULL;
    camera_ctx_t * p_ok  = NULL;
    int            vid   = xr_camera_provider_get_camera_vid(glasses_pid);
    int            pid   = xr_camera_provider_get_camera_pid(glasses_pid);

    if ((0 == vid) || (0 == pid))
    {
        (void)fprintf(stderr, "[camera] this model has no camera\n");
        goto cleanup;
    }
    p_ctx = (camera_ctx_t *)calloc(1U, sizeof(*p_ctx));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (0 != pthread_mutex_init(&p_ctx->lock, NULL))
    {
        goto cleanup;
    }
    p_ctx->lock_ready = 1;
    if (0 != usb_detach_kernel_driver(vid, pid))
    {
        (void)fprintf(stderr, "[camera] could not release the camera "
                              "from the kernel driver (udev rule "
                              "installed?)\n");
    }
    if (0 != start_with_retry(p_ctx, vid, pid))
    {
        (void)fprintf(stderr, "[camera] start failed\n");
        goto cleanup;
    }
    p_ok           = p_ctx;
    p_ctx          = NULL;

cleanup:
    camera_close(p_ctx);

    return p_ok;
}

int camera_take_frame(camera_ctx_t * p_ctx, byte_buf_t * p_frame)
{
    int result = -1;

    if ((NULL == p_ctx) || (NULL == p_frame))
    {
        goto cleanup;
    }
    (void)pthread_mutex_lock(&p_ctx->lock);
    if (p_ctx->seq == p_ctx->taken_seq)
    {
        result = 0;
    }
    else if (0 == buf_reserve(p_frame, p_ctx->latest.len))
    {
        memcpy(p_frame->p_data, p_ctx->latest.p_data, p_ctx->latest.len);
        p_frame->len     = p_ctx->latest.len;
        p_ctx->taken_seq = p_ctx->seq;
        result           = 1;
    }
    (void)pthread_mutex_unlock(&p_ctx->lock);

cleanup:

    return result;
}

void camera_close(camera_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (0 != p_ctx->started)
    {
        (void)xr_camera_provider_stop(p_ctx->p_provider);
    }
    if (NULL != p_ctx->p_provider)
    {
        xr_camera_provider_destroy(p_ctx->p_provider);
    }
    if (0 != p_ctx->lock_ready)
    {
        (void)pthread_mutex_destroy(&p_ctx->lock);
    }
    if (NULL != p_ctx->latest.p_data)
    {
        free(p_ctx->latest.p_data);
    }
    free(p_ctx);

cleanup:

    return;
}
