/**********************************************************************
 * @file    input_evdev.c
 * @brief   Global keyboard input via Linux evdev, focus-independent.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Implementation notes:
 *  - Every /dev/input/event* node that advertises the alphabetic keys is
 *    opened non-blocking. A machine may have several (laptop keyboard,
 *    external keyboard, a virtual "power button" device), so all are
 *    read and merged.
 *  - Reads are passive; the kernel still delivers the same events to the
 *    compositor, so nothing is stolen from normal typing.
 *  - Modifier state (Ctrl/Alt/Shift/Super, either side) is tracked
 *    across polls, since press and release arrive as separate events.
 *  - Only the initial key press (value 1) triggers an action;
 *    autorepeat (value 2) is ignored so a held key does not spam.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "input_evdev.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Most machines have only a handful of input devices. */
#define EVDEV_MAX_FDS       (16)

/* Bit-array helpers for the EVIOCGBIT capability query. */
#define BITS_PER_LONG       (8 * (int)sizeof(long))
#define NBITS(x)            ((((x) - 1) / BITS_PER_LONG) + 1)

/**********************************************************************
 * @brief  Reader state.
 **********************************************************************/
struct input_evdev
{
    int      fds[EVDEV_MAX_FDS];
    int      fd_count;
    uint32_t mod_mask;

    /* Modifier key state, tracked across polls. */
    int      l_ctrl;
    int      r_ctrl;
    int      l_alt;
    int      r_alt;
    int      l_shift;
    int      r_shift;
    int      l_super;
    int      r_super;
};

/**********************************************************************
 * @brief  Test bit n in a long-array capability bitmap.
 **********************************************************************/
static int test_bit(int n, const unsigned long * p_array)
{
    return (int)((p_array[n / BITS_PER_LONG]
                  >> (n % BITS_PER_LONG)) & 1UL);
}

/**********************************************************************
 * @brief  Does this fd look like a keyboard (has A..Z)?
 **********************************************************************/
static int is_keyboard(int fd)
{
    int           result = 0;
    unsigned long keybits[NBITS(KEY_MAX)] = { 0 };

    if (0 > ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits))
    {
        goto cleanup;
    }

    /* A device carrying the letter keys and a Ctrl key is a keyboard. */
    if ((0 != test_bit(KEY_A, keybits)) &&
        (0 != test_bit(KEY_Z, keybits)) &&
        (0 != test_bit(KEY_LEFTCTRL, keybits)))
    {
        result = 1;
    }

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Open all keyboard devices under /dev/input.
 **********************************************************************/
input_evdev_t * input_evdev_create(uint32_t mod_mask)
{
    input_evdev_t * p_ctx    = NULL;
    input_evdev_t * p_result = NULL;
    DIR *           p_dir     = NULL;
    struct dirent * p_entry   = NULL;
    char *          p_path    = NULL;

    p_ctx = (input_evdev_t *)calloc(1U, sizeof(input_evdev_t));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    p_ctx->mod_mask = mod_mask;

    p_path = (char *)calloc(512U, sizeof(char));
    if (NULL == p_path)
    {
        goto cleanup;
    }

    p_dir = opendir("/dev/input");
    if (NULL == p_dir)
    {
        goto cleanup;
    }

    for (p_entry = readdir(p_dir);
         (NULL != p_entry) && (p_ctx->fd_count < EVDEV_MAX_FDS);
         p_entry = readdir(p_dir))
    {
        int fd = -1;

        if (0 != strncmp(p_entry->d_name, "event", 5U))
        {
            continue;
        }

        (void)snprintf(p_path, 512U, "/dev/input/%s", p_entry->d_name);

        fd = open(p_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (0 > fd)
        {
            continue;   /* No permission for this one; skip it. */
        }

        if (0 == is_keyboard(fd))
        {
            (void)close(fd);
            continue;
        }

        p_ctx->fds[p_ctx->fd_count] = fd;
        p_ctx->fd_count++;
    }

    if (0 == p_ctx->fd_count)
    {
        (void)fprintf(stderr,
            "[input] no readable keyboard under /dev/input.\n"
            "        Add yourself to the 'input' group for global\n"
            "        controls:  sudo usermod -aG input $USER\n");
        goto cleanup;
    }

    (void)fprintf(stderr,
                  "[input] global keyboard: %d device(s)\n",
                  p_ctx->fd_count);

    p_result = p_ctx;
    p_ctx    = NULL;

cleanup:
    if (NULL != p_dir)
    {
        (void)closedir(p_dir);
    }
    if (NULL != p_path)
    {
        free(p_path);
    }
    if (NULL != p_ctx)
    {
        input_evdev_destroy(p_ctx);
    }
    return p_result;
}

/**********************************************************************
 * @brief  Update tracked modifier state from a modifier key event.
 *
 * @return  1 if the code was a modifier (and was consumed), else 0.
 **********************************************************************/
static int track_modifier(input_evdev_t * p_ctx, int code, int down)
{
    int handled = 1;

    switch (code)
    {
        case KEY_LEFTCTRL:   p_ctx->l_ctrl  = down; break;
        case KEY_RIGHTCTRL:  p_ctx->r_ctrl  = down; break;
        case KEY_LEFTALT:    p_ctx->l_alt   = down; break;
        case KEY_RIGHTALT:   p_ctx->r_alt   = down; break;
        case KEY_LEFTSHIFT:  p_ctx->l_shift = down; break;
        case KEY_RIGHTSHIFT: p_ctx->r_shift = down; break;
        case KEY_LEFTMETA:   p_ctx->l_super = down; break;
        case KEY_RIGHTMETA:  p_ctx->r_super = down; break;
        default:             handled = 0;           break;
    }

    return handled;
}

/**********************************************************************
 * @brief  Current held-modifier mask (OR of SCREEN_MOD_*).
 **********************************************************************/
static uint32_t held_mask(const input_evdev_t * p_ctx)
{
    uint32_t held = 0U;

    if ((0 != p_ctx->l_ctrl) || (0 != p_ctx->r_ctrl))
    {
        held |= SCREEN_MOD_CTRL;
    }
    if ((0 != p_ctx->l_alt) || (0 != p_ctx->r_alt))
    {
        held |= SCREEN_MOD_ALT;
    }
    if ((0 != p_ctx->l_shift) || (0 != p_ctx->r_shift))
    {
        held |= SCREEN_MOD_SHIFT;
    }
    if ((0 != p_ctx->l_super) || (0 != p_ctx->r_super))
    {
        held |= SCREEN_MOD_SUPER;
    }

    return held;
}

/**********************************************************************
 * @brief  Map a pressed key code to an action in the input tick.
 **********************************************************************/
static void apply_key(int code, screen_input_t * p_input)
{
    switch (code)
    {
        case KEY_Q:          p_input->quit        = 1; break;
        case KEY_R:          p_input->recenter    = 1; break;
        case KEY_EQUAL:      p_input->scale_step  = 1; break;
        case KEY_MINUS:      p_input->scale_step  = -1; break;
        case KEY_RIGHTBRACE: p_input->dist_step   = 1; break;
        case KEY_LEFTBRACE:  p_input->dist_step   = -1; break;
        case KEY_I:          p_input->spawn_dir   = SCREEN_SPAWN_UP; break;
        case KEY_K:          p_input->spawn_dir   = SCREEN_SPAWN_DOWN;
                             break;
        case KEY_J:          p_input->spawn_dir   = SCREEN_SPAWN_LEFT;
                             break;
        case KEY_L:          p_input->spawn_dir   = SCREEN_SPAWN_RIGHT;
                             break;
        case KEY_X:          p_input->remove_sel  = 1; break;
        case KEY_TAB:        p_input->select_next = 1; break;
        case KEY_COMMA:      p_input->yaw_step    = -1; break;
        case KEY_DOT:        p_input->yaw_step    = 1; break;
        case KEY_SEMICOLON:  p_input->pitch_step  = -1; break;
        case KEY_APOSTROPHE: p_input->pitch_step  = 1; break;
        case KEY_H:          p_input->toggle_help = 1; break;
        default:             /* Not bound. */                break;
    }
}

/**********************************************************************
 * @brief  Drain pending key events into an input tick.
 **********************************************************************/
void input_evdev_poll(input_evdev_t * p_ctx, screen_input_t * p_input)
{
    int i = 0;

    if (NULL == p_input)
    {
        goto cleanup;
    }

    (void)memset(p_input, 0, sizeof(*p_input));

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    for (i = 0; i < p_ctx->fd_count; i++)
    {
        struct input_event events[64];
        ssize_t            got = 0;

        for (;;)
        {
            ssize_t n   = 0;
            int     cnt = 0;
            int     e   = 0;

            n = read(p_ctx->fds[i], events, sizeof(events));
            if (0 >= n)
            {
                break;   /* EAGAIN / nothing more to read. */
            }
            got = n;
            cnt = (int)(got / (ssize_t)sizeof(struct input_event));

            for (e = 0; e < cnt; e++)
            {
                int code = 0;
                int val  = 0;

                if (EV_KEY != events[e].type)
                {
                    continue;
                }

                code = (int)events[e].code;
                val  = (int)events[e].value;

                /* value: 0 release, 1 press, 2 autorepeat. */
                if (0 != track_modifier(p_ctx, code, (0 != val) ? 1 : 0))
                {
                    continue;
                }

                if (1 != val)
                {
                    continue;   /* Act on the initial press only. */
                }

                if (p_ctx->mod_mask ==
                    (held_mask(p_ctx) & p_ctx->mod_mask))
                {
                    apply_key(code, p_input);
                }
            }
        }
    }

cleanup:
    return;
}

/**********************************************************************
 * @brief  Close all devices and free the reader.
 **********************************************************************/
void input_evdev_destroy(input_evdev_t * p_ctx)
{
    int i = 0;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    for (i = 0; i < p_ctx->fd_count; i++)
    {
        if (0 <= p_ctx->fds[i])
        {
            (void)close(p_ctx->fds[i]);
        }
    }

    free(p_ctx);

cleanup:
    return;
}
