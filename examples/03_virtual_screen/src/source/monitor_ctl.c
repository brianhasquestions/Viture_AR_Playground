/**********************************************************************
 * @file    monitor_ctl.c
 * @brief   Spawn and remove Hyprland outputs via hyprctl.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Implementation notes:
 *  - hyprctl has no "tell me the name of the output you just made" call,
 *    so create_headless snapshots the monitor-name set before and after
 *    and returns whichever name is new.
 *  - hyprctl is spawned with fork + execvp and an argument vector. There
 *    is no shell anywhere in this file: nothing here builds a command
 *    string, so an output name is always one argument no matter what
 *    characters it holds.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "monitor_ctl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Upper bounds for the small fixed tables used here. */
#define MC_MAX_MONITORS     (32)
#define MC_NAME_MAX         (64)

/* sscanf needs its field width as a literal inside the format string, so
 * it cannot reference MC_NAME_MAX directly. Spelling the width out by
 * hand ("%63s") would leave two numbers that must agree but that no one
 * checks, and shrinking MC_NAME_MAX alone would then overflow the buffer.
 * Stringify the width instead and assert the relationship holds. */
#define MC_STRINGIFY_(x)    #x
#define MC_STRINGIFY(x)     MC_STRINGIFY_(x)
#define MC_NAME_WIDTH       63
#define MC_SCAN_NAME_FMT    "Monitor %" MC_STRINGIFY(MC_NAME_WIDTH) "s (ID"

_Static_assert((MC_NAME_WIDTH + 1) == MC_NAME_MAX,
               "sscanf width must leave exactly one byte for the NUL");

/**********************************************************************
 * @brief  Is p_name a plausible connector name?
 *
 * execvp already removes any shell-metacharacter concern, so this is
 * only about hyprctl itself: a name starting with '-' would be taken for
 * an option, and a name with whitespace never matches a real connector.
 * Connector names are things like "eDP-1" or "HEADLESS-2".
 *
 * @param[in]  p_name  Candidate name.
 *
 * @return  1 if usable, 0 otherwise.
 **********************************************************************/
static int is_valid_output_name(const char * p_name)
{
    int    valid = 0;
    size_t i     = 0;
    size_t len   = 0;

    if (NULL == p_name)
    {
        goto cleanup;
    }

    len = strnlen(p_name, MC_NAME_MAX);
    if ((0U == len) || (MC_NAME_MAX == len) || ('-' == p_name[0]))
    {
        goto cleanup;
    }

    for (i = 0U; i < len; i++)
    {
        const char c = p_name[i];
        const int  ok = (((c >= 'A') && (c <= 'Z')) ||
                         ((c >= 'a') && (c <= 'z')) ||
                         ((c >= '0') && (c <= '9')) ||
                         ('-' == c) || ('_' == c) || ('.' == c));

        if (0 == ok)
        {
            goto cleanup;
        }
    }

    valid = 1;

cleanup:
    return valid;
}

/**********************************************************************
 * @brief  Point the child's stdout and stderr at /dev/null.
 *
 * Runs in the forked child only. On failure the child simply keeps the
 * inherited descriptors; noisy output is not worth aborting over.
 **********************************************************************/
static void silence_child_output(void)
{
    int fd = open("/dev/null", O_WRONLY);

    if (0 <= fd)
    {
        (void)dup2(fd, STDOUT_FILENO);
        (void)dup2(fd, STDERR_FILENO);
        (void)close(fd);
    }
}

/**********************************************************************
 * @brief  Run hyprctl to completion, discarding its output.
 *
 * @param[in]  pp_args  NULL-terminated argv, pp_args[0] == "hyprctl".
 *
 * @return  0 if hyprctl exited 0, -1 otherwise.
 **********************************************************************/
static int run_hyprctl(char * const * pp_args)
{
    int   result = -1;
    int   status = 0;
    pid_t pid    = fork();

    if (0 == pid)
    {
        /* Child: no shell, so pp_args cross into hyprctl verbatim. */
        silence_child_output();
        (void)execvp("hyprctl", pp_args);
        _exit(127);   /* Only reached if exec failed. */
    }

    if (0 > pid)
    {
        goto cleanup;
    }

    while (-1 == waitpid(pid, &status, 0))
    {
        if (EINTR != errno)
        {
            goto cleanup;
        }
    }

    if ((0 != WIFEXITED(status)) && (0 == WEXITSTATUS(status)))
    {
        result = 0;
    }

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Run `hyprctl monitors` and hand back its stdout as a stream.
 *
 * The caller reads to EOF, then passes the stream and pid to
 * close_hyprctl(). stderr is discarded so a missing hyprctl stays quiet.
 *
 * @param[out] p_pid  Receives the child pid.
 *
 * @return  Readable stream on success, NULL on failure.
 **********************************************************************/
static FILE * open_hyprctl_monitors(pid_t * p_pid)
{
    FILE * p_stream = NULL;
    int    fds[2]   = { -1, -1 };
    pid_t  pid      = -1;
    char * args[3]  = { (char *)"hyprctl", (char *)"monitors", NULL };

    if (0 != pipe(fds))
    {
        goto cleanup;
    }

    pid = fork();
    if (0 == pid)
    {
        /* Child: wire the write end onto stdout, silence stderr. */
        int devnull = open("/dev/null", O_WRONLY);

        (void)close(fds[0]);
        (void)dup2(fds[1], STDOUT_FILENO);
        (void)close(fds[1]);

        if (0 <= devnull)
        {
            (void)dup2(devnull, STDERR_FILENO);
            (void)close(devnull);
        }

        (void)execvp("hyprctl", args);
        _exit(127);
    }

    if (0 > pid)
    {
        goto cleanup;
    }

    (void)close(fds[1]);
    fds[1] = -1;

    p_stream = fdopen(fds[0], "r");
    if (NULL == p_stream)
    {
        goto cleanup;
    }

    fds[0] = -1;   /* Owned by the stream now. */
    *p_pid = pid;

cleanup:
    if (0 <= fds[0])
    {
        (void)close(fds[0]);
    }
    if (0 <= fds[1])
    {
        (void)close(fds[1]);
    }
    return p_stream;
}

/**********************************************************************
 * @brief  Close a stream from open_hyprctl_monitors() and reap the child.
 **********************************************************************/
static void close_hyprctl(FILE * p_stream, pid_t pid)
{
    int status = 0;

    if (NULL != p_stream)
    {
        (void)fclose(p_stream);
    }

    if (0 < pid)
    {
        while (-1 == waitpid(pid, &status, 0))
        {
            if (EINTR != errno)
            {
                break;
            }
        }
    }
}

/**********************************************************************
 * @brief  Collect current Hyprland monitor names.
 *
 * Runs `hyprctl monitors` and scrapes the "Monitor <name> (ID n):"
 * header lines.
 *
 * @param[out] names  Table receiving up to MC_MAX_MONITORS names.
 * @param[in]  max    Row count of names.
 *
 * @return  Number of names found, or -1 on failure.
 **********************************************************************/
static int list_monitors(char names[][MC_NAME_MAX], int max)
{
    int    count   = -1;
    int    found   = 0;
    FILE * p_pipe  = NULL;
    char * p_line  = NULL;
    size_t cap     = 0;
    pid_t  pid     = -1;

    p_pipe = open_hyprctl_monitors(&pid);
    if (NULL == p_pipe)
    {
        goto cleanup;
    }

    while ((found < max) && (-1 != getline(&p_line, &cap, p_pipe)))
    {
        char name[MC_NAME_MAX] = { 0 };
        /* Lines look like: "Monitor HEADLESS-2 (ID 2):" */
        if (1 == sscanf(p_line, MC_SCAN_NAME_FMT, name))
        {
            (void)snprintf(names[found], MC_NAME_MAX, "%s", name);
            found++;
        }
    }

    count = found;

cleanup:
    if (NULL != p_line)
    {
        free(p_line);
    }
    close_hyprctl(p_pipe, pid);
    return count;
}

/**********************************************************************
 * @brief  Is name present in the first count rows of table?
 **********************************************************************/
static int name_in(char table[][MC_NAME_MAX], int count,
                   const char * p_name)
{
    int found = 0;
    int i     = 0;

    for (i = 0; i < count; i++)
    {
        if (0 == strncmp(table[i], p_name, MC_NAME_MAX))
        {
            found = 1;
            break;
        }
    }

    return found;
}

/**********************************************************************
 * @brief  Create a headless Hyprland output and return its name.
 **********************************************************************/
int monitor_ctl_create_headless(char * p_name, size_t size)
{
    int  result = -1;
    int  before = 0;
    int  after  = 0;
    int  i      = 0;
    char before_names[MC_MAX_MONITORS][MC_NAME_MAX];
    char after_names[MC_MAX_MONITORS][MC_NAME_MAX];
    char * args[5] = { (char *)"hyprctl", (char *)"output",
                       (char *)"create", (char *)"headless", NULL };

    if ((NULL == p_name) || (0U == size))
    {
        goto cleanup;
    }

    before = list_monitors(before_names, MC_MAX_MONITORS);
    if (0 > before)
    {
        (void)fprintf(stderr,
                      "[monitor] hyprctl unavailable (Hyprland only)\n");
        goto cleanup;
    }

    if (0 != run_hyprctl(args))
    {
        (void)fprintf(stderr, "[monitor] output create failed\n");
        goto cleanup;
    }

    after = list_monitors(after_names, MC_MAX_MONITORS);
    if (0 > after)
    {
        goto cleanup;
    }

    /* The name present after but not before is the one we just made. */
    for (i = 0; i < after; i++)
    {
        if (0 == name_in(before_names, before, after_names[i]))
        {
            (void)snprintf(p_name, size, "%s", after_names[i]);
            result = 0;
            break;
        }
    }

    if (0 != result)
    {
        (void)fprintf(stderr,
                      "[monitor] could not identify new output\n");
    }

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Warp the pointer to an output and focus it.
 *
 * Scrapes `hyprctl monitors` for the output's layout position, physical
 * resolution and scale, computes the logical centre, then dispatches
 * movecursor + focusmonitor.
 **********************************************************************/
int monitor_ctl_focus_output(const char * p_name)
{
    int    result    = -1;
    int    in_target = 0;
    int    have_geo  = 0;
    int    pos_x     = 0;
    int    pos_y     = 0;
    int    res_w     = 0;
    int    res_h     = 0;
    float  scale     = 1.0F;
    int    cx        = 0;
    int    cy        = 0;
    FILE * p_pipe    = NULL;
    char * p_line    = NULL;
    size_t cap       = 0;
    pid_t  pid       = -1;
    char   cx_text[16] = { 0 };
    char   cy_text[16] = { 0 };

    if (0 == is_valid_output_name(p_name))
    {
        goto cleanup;
    }

    p_pipe = open_hyprctl_monitors(&pid);
    if (NULL == p_pipe)
    {
        goto cleanup;
    }

    /* One monitor block spans several lines. Track when we are inside
     * the target block and pick up its geometry and scale. */
    while (-1 != getline(&p_line, &cap, p_pipe))
    {
        char name[MC_NAME_MAX] = { 0 };

        if (1 == sscanf(p_line, MC_SCAN_NAME_FMT, name))
        {
            if (0 != in_target)
            {
                break;   /* Reached the next monitor; target is done. */
            }
            in_target = (0 == strncmp(name, p_name, MC_NAME_MAX)) ? 1 : 0;
            continue;
        }

        if (0 == in_target)
        {
            continue;
        }

        /* Active mode line: "\t1920x1080@60.01 at 0x0". */
        if ((0 == have_geo) && (NULL != strstr(p_line, " at ")))
        {
            const char * p_at = strstr(p_line, " at ");

            if ((2 == sscanf(p_line, " %dx%d@", &res_w, &res_h)) &&
                (2 == sscanf(p_at + 4, "%dx%d", &pos_x, &pos_y)))
            {
                have_geo = 1;
            }
        }

        (void)sscanf(p_line, " scale: %f", &scale);
    }

    if ((0 == have_geo) || (0.01F > scale))
    {
        goto cleanup;
    }

    /* Layout coordinates are logical: divide the physical resolution by
     * the scale to get the on-desktop size. */
    cx = pos_x + (int)(((float)res_w / scale) * 0.5F);
    cy = pos_y + (int)(((float)res_h / scale) * 0.5F);

    (void)snprintf(cx_text, sizeof(cx_text), "%d", cx);
    (void)snprintf(cy_text, sizeof(cy_text), "%d", cy);

    /* Warp the pointer, then focus the monitor so keyboard input follows
     * too. Two execs rather than one `--batch` string: without a shell
     * there is nothing to batch through. */
    {
        /* movecursor takes x and y as two separate arguments. */
        char * move_args[6] = { (char *)"hyprctl", (char *)"dispatch",
                                (char *)"movecursor", cx_text, cy_text,
                                NULL };
        char * focus_args[5] = { (char *)"hyprctl", (char *)"dispatch",
                                 (char *)"focusmonitor",
                                 (char *)p_name, NULL };

        if (0 != run_hyprctl(move_args))
        {
            goto cleanup;
        }
        if (0 != run_hyprctl(focus_args))
        {
            goto cleanup;
        }
    }

    result = 0;

cleanup:
    if (NULL != p_line)
    {
        free(p_line);
    }
    close_hyprctl(p_pipe, pid);
    return result;
}

/**********************************************************************
 * @brief  Remove a previously created Hyprland output.
 **********************************************************************/
int monitor_ctl_remove(const char * p_name)
{
    int result = -1;

    if (0 == is_valid_output_name(p_name))
    {
        goto cleanup;
    }

    {
        char * args[5] = { (char *)"hyprctl", (char *)"output",
                           (char *)"remove", (char *)p_name, NULL };

        result = run_hyprctl(args);
    }

cleanup:
    return result;
}
