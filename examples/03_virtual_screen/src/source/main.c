/**********************************************************************
 * @file    main.c
 * @brief   World-locked virtual multi-monitor for VITURE Luma Ultra.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Pins one or more desktops to spots in the room. Each is captured live
 * via wlr-screencopy, uploaded to a GL texture, and drawn on a quad that
 * is static in world space - the same world-locking trick as
 * 02_ar_overlay, with pixels on the quad instead of wireframe.
 *
 * Screens are arranged on a CURVED WALL: each panel sits on an arc at a
 * fixed distance and is toed-in to face the viewer. On a flat wall the
 * outer edges of side panels are physically farther from the eye and
 * look small and distant; curving keeps every panel square-on and its
 * edges equidistant. Neighbours are spaced so their bezels touch.
 *
 * Screens can be spawned and removed at runtime. Spawning conjures a
 * fresh headless Hyprland output (its own workspaces, not a mirror) and
 * captures it; removing tears it back down.
 *
 * Controls are read globally from evdev (see input_evdev.c), so they
 * work even though the overlay's window never holds keyboard focus.
 * They require a modifier combo (default Ctrl+Alt+Shift, chosen to not
 * clash with Hyprland or app shortcuts) held while pressing:
 *   R            re-anchor the whole wall upright in front of you
 *   - / =        shrink / grow all panels
 *   [ / ]        move the wall nearer / further
 *   I/K/J/L      spawn a screen above / below / left / right of selected
 *   X            remove the selected screen
 *   Tab          select the next screen (amber border = selected)
 *   , / .        angle the selected screen left / right (yaw)
 *   ; / '        tilt the selected screen down / up (pitch)
 *   Q            quit
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "hud.h"
#include "input_evdev.h"
#include "monitor_ctl.h"
#include "screen_capture.h"
#include "screen_render.h"

#include "carina_pose.h"
#include "device_scan.h"
#include "xr_args.h"
#include "xr_math.h"

#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* VITURE SDK public headers (C linkage). */
#include "viture_glasses_provider.h"

/* Approximate vertical FOV of the glasses. Not calibrated; see
 * 02_ar_overlay's README. Tune with --fov if the overlay swims. */
#define DEFAULT_FOV_Y_DEGREES   (27.0F)

#define NEAR_PLANE_METRES       (0.05F)
#define FAR_PLANE_METRES        (50.0F)

/* One frame of forward prediction at the panel's 90Hz. */
#define POSE_PREDICT_NS         (11000000.0)

#define DEGREES_TO_RADIANS      (3.14159265358979F / 180.0F)

/* Virtual screen placement, in metres. */
#define DEFAULT_WIDTH_METRES    (1.20F)
#define DEFAULT_DISTANCE_METRES (1.60F)

#define MIN_WIDTH_METRES        (0.30F)
#define MAX_WIDTH_METRES        (4.00F)
#define WIDTH_STEP_METRES       (0.10F)

#define MIN_DISTANCE_METRES     (0.40F)
#define MAX_DISTANCE_METRES     (6.00F)
#define DISTANCE_STEP_METRES    (0.10F)

/* Pose smoothing: exponential low-pass on position and orientation.
 * alpha = 1 is off (raw, jittery); smaller is steadier but lags head
 * motion. See 02_ar_overlay. Override with --smooth. */
#define DEFAULT_SMOOTHING       (0.08F)
#define MIN_SMOOTHING           (0.01F)
#define MAX_SMOOTHING           (1.00F)

/* Bezel spacing as a fraction of panel size along the arc. 1.0 = edges
 * exactly touch. */
#define PANEL_GAP_FRACTION      (1.0F)

/* Per-keypress manual angle step for the selected panel, in radians. */
#define ANGLE_STEP_RAD          (4.0F * DEGREES_TO_RADIANS)

#define STATUS_LOG_INTERVAL     (300U)

/**********************************************************************
 * @brief  One virtual screen.
 **********************************************************************/
typedef struct
{
    int    active;
    screen_capture_ctx_t * capture;
    int    render_slot;      /* Slot in the renderer.                */
    int    is_headless;      /* We created this output; remove on X. */
    char   output_name[64];  /* For monitor_ctl_remove.              */
    int    col;              /* Grid column (right positive).        */
    int    row;              /* Grid row (up positive).              */
    float  aspect;           /* Captured width / height.             */
    int    y_invert;         /* Compositor delivered bottom-up.      */
    float  yaw_adjust;       /* Manual per-panel yaw, radians.       */
    float  pitch_adjust;     /* Manual per-panel pitch, radians.     */
} panel_t;

static volatile sig_atomic_t g_stop_requested = 0;

/**********************************************************************
 * @brief  SIGINT handler: request a graceful shutdown.
 **********************************************************************/
static void handle_sigint(int signal_number)
{
    (void)signal_number;
    g_stop_requested = 1;
}

/**********************************************************************
 * @brief  Clamp a float to a range.
 **********************************************************************/
static float clampf(float value, float low, float high)
{
    float result = value;

    if (result < low)
    {
        result = low;
    }
    if (result > high)
    {
        result = high;
    }

    return result;
}

/**********************************************************************
 * @brief  Is a grid cell already taken by an active panel?
 **********************************************************************/
static int cell_taken(const panel_t * p_panels, int col, int row)
{
    int taken = 0;
    int i     = 0;

    for (i = 0; i < SCREEN_MAX_PANELS; i++)
    {
        if ((0 != p_panels[i].active) && (col == p_panels[i].col) &&
            (row == p_panels[i].row))
        {
            taken = 1;
            break;
        }
    }

    return taken;
}

/**********************************************************************
 * @brief  Count active panels.
 **********************************************************************/
static int active_count(const panel_t * p_panels)
{
    int count = 0;
    int i     = 0;

    for (i = 0; i < SCREEN_MAX_PANELS; i++)
    {
        if (0 != p_panels[i].active)
        {
            count++;
        }
    }

    return count;
}

/**********************************************************************
 * @brief  Add a panel: capture an output and claim a render slot.
 *
 * @param[in,out] p_panels   Panel table.
 * @param[in]     p_render   Renderer.
 * @param[in]     p_output   Output to capture, or NULL to auto-pick a
 *                           non-glasses output. When NULL and spawn is
 *                           set, a headless output is created first.
 * @param[in]     col,row    Grid cell.
 * @param[in]     spawn      Non-zero to conjure a headless output.
 *
 * @return  Panel index, or -1 on failure.
 **********************************************************************/
static int panel_add(panel_t * p_panels, screen_render_ctx_t * p_render,
                     const char * p_output, int col, int row, int spawn)
{
    int    idx         = -1;
    int    slot        = -1;
    int    i           = 0;
    int    is_headless = 0;
    char   name[64]    = { 0 };
    screen_capture_ctx_t * p_cap = NULL;

    /* Find a free panel record. */
    for (i = 0; i < SCREEN_MAX_PANELS; i++)
    {
        if (0 == p_panels[i].active)
        {
            idx = i;
            break;
        }
    }
    if (0 > idx)
    {
        (void)fprintf(stderr, "[vs] panel limit (%d) reached\n",
                      SCREEN_MAX_PANELS);
        goto fail;
    }

    /* Conjure a fresh headless output if this is a spawn. */
    if (0 != spawn)
    {
        if (0 != monitor_ctl_create_headless(name, sizeof(name)))
        {
            goto fail;
        }
        is_headless = 1;
        p_output    = name;
    }

    p_cap = screen_capture_create(p_output);
    if (NULL == p_cap)
    {
        if (0 != is_headless)
        {
            (void)monitor_ctl_remove(name);
        }
        goto fail;
    }

    slot = screen_render_acquire_panel(p_render);
    if (0 > slot)
    {
        screen_capture_destroy(p_cap);
        if (0 != is_headless)
        {
            (void)monitor_ctl_remove(name);
        }
        goto fail;
    }

    p_panels[idx].active       = 1;
    p_panels[idx].capture      = p_cap;
    p_panels[idx].render_slot  = slot;
    p_panels[idx].is_headless  = is_headless;
    p_panels[idx].col          = col;
    p_panels[idx].row          = row;
    p_panels[idx].aspect       = 16.0F / 9.0F;
    p_panels[idx].y_invert     = 0;
    p_panels[idx].yaw_adjust   = 0.0F;
    p_panels[idx].pitch_adjust = 0.0F;
    {
        /* Prefer the output the capture actually resolved, so even an
         * auto-picked panel has a real name to warp the cursor to. */
        const char * p_real = screen_capture_output_name(p_cap);

        if (NULL == p_real)
        {
            p_real = (NULL != p_output) ? p_output : "";
        }
        (void)snprintf(p_panels[idx].output_name,
                       sizeof(p_panels[idx].output_name), "%s", p_real);
    }

    return idx;

fail:
    return -1;
}

/**********************************************************************
 * @brief  Remove a panel: release its slot, output and capture.
 **********************************************************************/
static void panel_remove(panel_t * p_panels,
                         screen_render_ctx_t * p_render, int idx)
{
    if ((0 > idx) || (idx >= SCREEN_MAX_PANELS))
    {
        goto cleanup;
    }
    if (0 == p_panels[idx].active)
    {
        goto cleanup;
    }

    screen_render_release_panel(p_render, p_panels[idx].render_slot);

    if (NULL != p_panels[idx].capture)
    {
        screen_capture_destroy(p_panels[idx].capture);
        p_panels[idx].capture = NULL;
    }
    if (0 != p_panels[idx].is_headless)
    {
        (void)monitor_ctl_remove(p_panels[idx].output_name);
        (void)fprintf(stderr, "[vs] removed %s\n",
                      p_panels[idx].output_name);
    }

    p_panels[idx].active = 0;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Find the next active panel after idx (wrapping).
 *
 * @return  Index of the next active panel, or -1 if none active.
 **********************************************************************/
static int panel_next(const panel_t * p_panels, int idx)
{
    int result = -1;
    int step   = 0;
    int probe  = 0;

    for (step = 1; step <= SCREEN_MAX_PANELS; step++)
    {
        probe = (idx + step) % SCREEN_MAX_PANELS;
        if (0 != p_panels[probe].active)
        {
            result = probe;
            break;
        }
    }

    return result;
}

/**********************************************************************
 * @brief  Select a panel and move the pointer/focus onto its output.
 *
 * Warping the Hyprland cursor into the selected output is what makes the
 * mouse land "inside" the virtual screen the user just picked.
 **********************************************************************/
static void select_panel(const panel_t * p_panels, int idx)
{
    if ((0 > idx) || (idx >= SCREEN_MAX_PANELS))
    {
        goto cleanup;
    }
    if (0 == p_panels[idx].active)
    {
        goto cleanup;
    }
    if ('\0' != p_panels[idx].output_name[0])
    {
        (void)monitor_ctl_focus_output(p_panels[idx].output_name);
    }

cleanup:
    return;
}

/**********************************************************************
 * @brief  Choose a free grid cell one step from a base cell.
 *
 * Steps in the requested direction until an unoccupied cell is found.
 **********************************************************************/
static void free_cell(const panel_t * p_panels, int base_col,
                      int base_row, int spawn_dir, int * p_col,
                      int * p_row)
{
    int dcol = 0;
    int drow = 0;
    int col  = base_col;
    int row  = base_row;
    int step = 0;

    if (SCREEN_SPAWN_LEFT == spawn_dir)
    {
        dcol = -1;
    }
    else if (SCREEN_SPAWN_RIGHT == spawn_dir)
    {
        dcol = 1;
    }
    else if (SCREEN_SPAWN_UP == spawn_dir)
    {
        drow = 1;
    }
    else
    {
        drow = -1;   /* SCREEN_SPAWN_DOWN */
    }

    /* Advance until the cell is free (bounded by the panel limit). */
    for (step = 1; step <= (SCREEN_MAX_PANELS + 1); step++)
    {
        col = base_col + (dcol * step);
        row = base_row + (drow * step);
        if (0 == cell_taken(p_panels, col, row))
        {
            break;
        }
    }

    *p_col = col;
    *p_row = row;
}

/**********************************************************************
 * @brief  Build a panel's world model matrix on the curved wall.
 *
 * The panel sits on an arc of radius `distance`, at an angle set by its
 * grid cell (so bezels touch) plus its manual adjustment, and is rotated
 * to face the viewer. All of this is composed in the anchor's frame,
 * then lifted to world space by the anchor transform - so the panel is
 * fixed in the room and only the view matrix moves.
 *
 * @param[in]  p_panel       The panel.
 * @param[in]  width         Panel width in metres.
 * @param[in]  distance      Arc radius in metres.
 * @param[in]  p_anchor_world  T(origin) * upright-rotation, world frame.
 * @param[out] p_model       16-float result.
 **********************************************************************/
static void panel_model(const panel_t * p_panel, float width,
                        float distance, const float * p_anchor_world,
                        float * p_model)
{
    float height   = width / p_panel->aspect;
    float col_step = 0.0F;
    float row_step = 0.0F;
    float yaw      = 0.0F;
    float pitch    = 0.0F;
    float cx       = 0.0F;
    float cy       = 0.0F;
    float cz       = 0.0F;
    float ry[MAT4_ELEMENTS];
    float rx[MAT4_ELEMENTS];
    float rot[MAT4_ELEMENTS];
    float scale[MAT4_ELEMENTS];
    float trans[MAT4_ELEMENTS];
    float local[MAT4_ELEMENTS];
    float rot_scale[MAT4_ELEMENTS];

    /* Angular spacing that makes neighbouring bezels exactly touch.
     * Each panel is a FLAT quad tangent to the arc, so its edge sits at
     * angle atan((w/2)/d) off its centre's radial - use atan, not asin.
     * (asin spaces the centres too far apart and leaves a visible gap,
     * because a flat panel's edge is farther out than a point on the
     * arc.) At this spacing, panel i's right edge and panel i+1's left
     * edge land on the same point. */
    col_step = 2.0F * atanf((width * PANEL_GAP_FRACTION * 0.5F)
                            / distance);
    row_step = 2.0F * atanf((height * PANEL_GAP_FRACTION * 0.5F)
                            / distance);

    /* Column to the right = negative yaw in this frame. */
    yaw   = (-(float)p_panel->col * col_step) + p_panel->yaw_adjust;
    pitch = ((float)p_panel->row * row_step) + p_panel->pitch_adjust;

    /* Panel-centre direction in the anchor frame, then scaled to the
     * arc radius. Derived from Ry(yaw) * Rx(pitch) * (0,0,-1). */
    cx = -cosf(pitch) * sinf(yaw) * distance;
    cy = sinf(pitch) * distance;
    cz = -cosf(pitch) * cosf(yaw) * distance;

    mat4_rotate_y(ry, yaw);
    mat4_rotate_x(rx, pitch);
    mat4_multiply(rot, ry, rx);

    /* Negative Y scale flips a bottom-up captured image. */
    mat4_scale(scale, width,
               (0 != p_panel->y_invert) ? -height : height, 1.0F);

    mat4_multiply(rot_scale, rot, scale);
    mat4_translation(trans, cx, cy, cz);
    mat4_multiply(local, trans, rot_scale);

    mat4_multiply(p_model, p_anchor_world, local);
}

/**********************************************************************
 * @brief  Print usage.
 **********************************************************************/
static void print_usage(void)
{
    (void)fprintf(stderr,
        "Usage: virtual_screen [options]\n"
        "  --output <name>  Wayland output to capture, e.g. eDP-1.\n"
        "                   Repeat for multiple screens (max %d).\n"
        "                   Default: the first non-glasses output.\n"
        "  --stack          Lay initial screens vertically not across.\n"
        "  --windowed       Preview in a desktop window.\n"
        "  --no-tracking    Skip head tracking; fixed viewpoint.\n"
        "  --spawn-first    Start with one fresh headless screen instead\n"
        "                   of capturing the laptop panel. Use in\n"
        "                   glasses-only mode where the panel is off.\n"
        "  --display <n>    Force SDL display index n.\n"
        "  --fov <deg>      Vertical field of view (default %.1f).\n"
        "  --smooth <a>     Pose smoothing, 0.01-1.0 (1 = off, %.2f).\n"
        "  --mod <combo>    Control modifier (default ctrl+alt+shift,\n"
        "                   chosen to avoid Hyprland/app clashes).\n"
        "  --width <m>      Screen width in metres (%.2f).\n"
        "  --distance <m>   Distance in front of you (%.2f).\n"
        "  --help           Show this message.\n"
        "\nControls (hold the modifier combo):\n"
        "  R          re-anchor the wall upright in front of you\n"
        "  - / =      shrink / grow all screens\n"
        "  [ / ]      wall nearer / further\n"
        "  I/K/J/L    spawn screen above/below/left/right of selected\n"
        "  X          remove selected screen\n"
        "  Tab        select next screen (amber = selected)\n"
        "  , / .      angle selected screen left / right\n"
        "  ; / '      tilt selected screen down / up\n"
        "  Q          quit\n",
        SCREEN_MAX_PANELS, (double)DEFAULT_FOV_Y_DEGREES,
        (double)DEFAULT_SMOOTHING, (double)DEFAULT_WIDTH_METRES,
        (double)DEFAULT_DISTANCE_METRES);
}

/**********************************************************************
 * @brief  Parse a modifier spec like "ctrl+alt" or "super+shift".
 **********************************************************************/
static uint32_t parse_modifiers(const char * p_spec)
{
    uint32_t mask = 0U;

    if (NULL == p_spec)
    {
        goto cleanup;
    }
    if (NULL != strstr(p_spec, "ctrl"))
    {
        mask |= SCREEN_MOD_CTRL;
    }
    if (NULL != strstr(p_spec, "alt"))
    {
        mask |= SCREEN_MOD_ALT;
    }
    if (NULL != strstr(p_spec, "shift"))
    {
        mask |= SCREEN_MOD_SHIFT;
    }
    if (NULL != strstr(p_spec, "super"))
    {
        mask |= SCREEN_MOD_SUPER;
    }

cleanup:
    return mask;
}

/**********************************************************************
 * @brief  Program entry point.
 **********************************************************************/
int main(int argc, char * argv[])
{
    int    exit_code       = EXIT_FAILURE;
    int    glasses_pid     = DEVICE_SCAN_NO_PID;
    int    display_index   = SCREEN_DISPLAY_NOT_FOUND;
    int    windowed        = 0;
    int    quit            = 0;
    int    pose_status     = CARINA_POSE_UNSTABLE;
    int    tracking_locked = 0;
    int    no_tracking     = 0;
    int    spawn_first     = 0;
    int    arg_index       = 0;
    int    stack_layout    = 0;
    int    init_count      = 0;
    int    selected        = -1;
    int    help_visible    = 0;
    float  help_aspect     = 4.0F;
    int    i               = 0;
    uint32_t frame_count   = 0U;
    uint32_t uploads       = 0U;
    float  fov_degrees     = DEFAULT_FOV_Y_DEGREES;
    float  screen_width    = DEFAULT_WIDTH_METRES;
    float  screen_distance = DEFAULT_DISTANCE_METRES;
    float  smoothing       = DEFAULT_SMOOTHING;
    int    have_smoothed   = 0;
    int    have_anchor     = 0;
    float  position[3]     = { 0.0F };
    float  quaternion[4]   = { 1.0F, 0.0F, 0.0F, 0.0F };
    float  s_position[3]   = { 0.0F };
    float  s_quaternion[4] = { 1.0F, 0.0F, 0.0F, 0.0F };
    float  anchor_origin[3]      = { 0.0F };
    float  anchor_forward[3]     = { 0.0F, 0.0F, -1.0F };
    float  anchor_rot[MAT4_ELEMENTS]   = { 0.0F };
    float  anchor_world[MAT4_ELEMENTS] = { 0.0F };
    float  anchor_trans[MAT4_ELEMENTS] = { 0.0F };
    float  view[MAT4_ELEMENTS]         = { 0.0F };
    float  projection[MAT4_ELEMENTS]   = { 0.0F };
    float  help_model[MAT4_ELEMENTS]   = { 0.0F };
    float  models[SCREEN_MAX_PANELS * MAT4_ELEMENTS];
    /* Ctrl+Alt+Shift is the default because it is globally unique on a
     * typical Hyprland setup: the compositor binds nothing on it and
     * almost no application uses it either. That matters because the
     * global keyboard reader is PASSIVE - the keys still reach the
     * compositor and the focused window - so a combo that something else
     * also binds would fire two actions at once. Override with --mod. */
    uint32_t mod_mask       = SCREEN_MOD_CTRL | SCREEN_MOD_ALT |
                              SCREEN_MOD_SHIFT;
    const char * p_mod_spec = "ctrl+alt+shift";
    const char * init_outputs[SCREEN_MAX_PANELS] = { NULL };
    panel_t panels[SCREEN_MAX_PANELS];
    screen_input_t         input    = { 0 };
    carina_pose_ctx_t *    p_pose   = NULL;
    screen_render_ctx_t *  p_render = NULL;
    input_evdev_t *        p_evdev  = NULL;

    (void)memset(panels, 0, sizeof(panels));

    /* --- Arguments ------------------------------------------------ */
    for (arg_index = 1; arg_index < argc; arg_index++)
    {
        if (0 == strcmp(argv[arg_index], "--windowed"))
        {
            windowed = 1;
        }
        else if (0 == strcmp(argv[arg_index], "--no-tracking"))
        {
            no_tracking = 1;
        }
        else if (0 == strcmp(argv[arg_index], "--spawn-first"))
        {
            spawn_first = 1;
        }
        else if (0 == strcmp(argv[arg_index], "--stack"))
        {
            stack_layout = 1;
        }
        else if ((0 == strcmp(argv[arg_index], "--output")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (init_count < SCREEN_MAX_PANELS)
            {
                init_outputs[init_count] = argv[arg_index];
                init_count++;
            }
        }
        else if ((0 == strcmp(argv[arg_index], "--mod")) &&
                 ((arg_index + 1) < argc))
        {
            uint32_t parsed = 0U;

            arg_index++;
            parsed = parse_modifiers(argv[arg_index]);
            if (0U == parsed)
            {
                (void)fprintf(stderr, "Unrecognised --mod '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
            mod_mask   = parsed;
            p_mod_spec = argv[arg_index];
        }
        else if ((0 == strcmp(argv[arg_index], "--display")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (0 != xr_args_parse_int(argv[arg_index], &display_index))
            {
                (void)fprintf(stderr, "Invalid --display '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
        }
        else if ((0 == strcmp(argv[arg_index], "--fov")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (0 != xr_args_parse_float(argv[arg_index], &fov_degrees))
            {
                (void)fprintf(stderr, "Invalid --fov '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
        }
        else if ((0 == strcmp(argv[arg_index], "--smooth")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (0 != xr_args_parse_float(argv[arg_index], &smoothing))
            {
                (void)fprintf(stderr, "Invalid --smooth '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
        }
        else if ((0 == strcmp(argv[arg_index], "--width")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (0 != xr_args_parse_float(argv[arg_index], &screen_width))
            {
                (void)fprintf(stderr, "Invalid --width '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
        }
        else if ((0 == strcmp(argv[arg_index], "--distance")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (0 != xr_args_parse_float(argv[arg_index],
                                         &screen_distance))
            {
                (void)fprintf(stderr, "Invalid --distance '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
        }
        else if (0 == strcmp(argv[arg_index], "--help"))
        {
            print_usage();
            exit_code = EXIT_SUCCESS;
            goto cleanup;
        }
        else
        {
            (void)fprintf(stderr, "Unknown option: %s\n",
                          argv[arg_index]);
            print_usage();
            goto cleanup;
        }
    }

    smoothing       = clampf(smoothing, MIN_SMOOTHING, MAX_SMOOTHING);
    screen_width    = clampf(screen_width, MIN_WIDTH_METRES,
                             MAX_WIDTH_METRES);
    screen_distance = clampf(screen_distance, MIN_DISTANCE_METRES,
                             MAX_DISTANCE_METRES);

    (void)fprintf(stderr, "VITURE world-locked virtual screens\n");
    (void)fprintf(stderr, "-----------------------------------\n");

    (void)signal(SIGINT, handle_sigint);
    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);

    /* --- Find the glasses ----------------------------------------- */
    if (0 != device_scan_find_glasses(
                 &xr_device_provider_is_product_id_valid, &glasses_pid))
    {
        (void)fprintf(stderr,
                      "[device] no VITURE glasses found on USB\n");
        goto cleanup;
    }

    /* --- Head tracking -------------------------------------------- */
    if (0 == no_tracking)
    {
        p_pose = carina_pose_create();
        if (NULL == p_pose)
        {
            goto cleanup;
        }
        if (0 != carina_pose_start(p_pose, glasses_pid, 1))
        {
            goto cleanup;
        }
    }
    else
    {
        mat4_identity(view);
        tracking_locked = 1;
        (void)fprintf(stderr,
                      "[vs] head tracking disabled (--no-tracking)\n");
    }

    /* --- Window on the glasses ------------------------------------ */
    if (SCREEN_DISPLAY_NOT_FOUND == display_index)
    {
        display_index = screen_render_find_glasses_display();
    }
    if (SCREEN_DISPLAY_NOT_FOUND == display_index)
    {
        if (0 == windowed)
        {
            (void)fprintf(stderr,
                "[render] no VITURE display found. Use --display <n>"
                " or --windowed.\n");
            goto cleanup;
        }
        display_index = 0;
    }

    p_render = screen_render_create(display_index, windowed);
    if (NULL == p_render)
    {
        goto cleanup;
    }
    screen_render_set_modifiers(p_render, mod_mask);

    /* Build the help overlay texture once. White 8x8-font glyphs on a
     * black (transparent) background, drawn on a quad above the wall
     * when toggled with H. */
    {
        static const char * const help_lines[] =
        {
            "VITURE VIRTUAL SCREENS  --  hold Ctrl+Alt+Shift",
            "",
            "  L / J        spawn screen right / left",
            "  I / K        spawn screen up / down",
            "  X            remove selected screen",
            "  Tab          select next (amber border)",
            "  , / .        angle selected left / right",
            "  ; / '        tilt selected down / up",
            "  R            re-anchor wall in front of you",
            "  - / =        shrink / grow all",
            "  [ / ]        wall nearer / further",
            "  H            toggle this help",
            "  Q            quit"
        };
        uint32_t  help_w   = 0U;
        uint32_t  help_h   = 0U;
        uint8_t * p_help   = hud_render_lines(help_lines,
            (int)(sizeof(help_lines) / sizeof(help_lines[0])), 2,
            &help_w, &help_h);

        if (NULL != p_help)
        {
            screen_render_set_help(p_render, p_help, help_w, help_h, 0);
            help_aspect = (0U != help_h)
                        ? ((float)help_w / (float)help_h) : 4.0F;
            free(p_help);
        }
    }

    /* Global keyboard reader. The overlay's fullscreen window on the
     * glasses never holds Wayland focus while the wearer types in laptop
     * windows, so SDL would never see a control keypress. Reading evdev
     * directly makes the controls work regardless of focus. If no
     * keyboard is readable we fall back to SDL's focus-bound input. */
    p_evdev = input_evdev_create(mod_mask);

    /* --- Initial panels ------------------------------------------- */
    if (0 == init_count)
    {
        /* --spawn-first starts with a fresh headless output instead of
         * capturing the laptop panel. This is what makes the app useful
         * in glasses-only mode, where the laptop output is disabled and
         * there is nothing on it to capture. Otherwise capture the first
         * non-glasses output (the laptop desktop), centred. */
        int idx = panel_add(panels, p_render, NULL, 0, 0,
                            (0 != spawn_first) ? 1 : 0);
        if (0 <= idx)
        {
            selected = idx;
            select_panel(panels, selected);
        }
    }
    else
    {
        for (i = 0; i < init_count; i++)
        {
            /* Centre the initial row/column on the anchor. */
            int centre = init_count / 2;
            int col = (0 != stack_layout) ? 0 : (i - centre);
            int row = (0 != stack_layout) ? (centre - i) : 0;
            int idx = panel_add(panels, p_render, init_outputs[i], col,
                                row, 0);
            if ((0 <= idx) && (0 > selected))
            {
                selected = idx;
                select_panel(panels, selected);
            }
        }
    }

    if (0 > selected)
    {
        (void)fprintf(stderr, "[vs] no screens could be created\n");
        goto cleanup;
    }

    (void)fprintf(stderr, "[vs] %d screen(s)\n", active_count(panels));
    (void)fprintf(stderr,
        "[vs] hold %s then: R anchor | -/= size | [ ] dist | IKJL spawn"
        " | X remove | Tab select | ,./;' angle | Q quit\n", p_mod_spec);
    (void)fprintf(stderr,
        "[vs] waiting for head tracking to converge - look around a lit"
        " area with detail.\n");

    /* --- Main loop ------------------------------------------------ */
    while ((0 == quit) && (0 == g_stop_requested))
    {
        int recenter = 0;

        /* SDL pump: keeps the window responsive and catches a window
         * close. When the global reader is active it is the sole source
         * of control keys, so SDL's key fields are discarded (evdev's
         * poll zeroes the struct); only its quit survives. */
        screen_render_poll(p_render, &input);
        if (NULL != p_evdev)
        {
            int sdl_quit = input.quit;

            input_evdev_poll(p_evdev, &input);
            input.quit = (0 != input.quit) || (0 != sdl_quit);
        }
        quit = input.quit;

        if (0 != input.recenter)
        {
            recenter = 1;
        }
        if (0 != input.scale_step)
        {
            screen_width = clampf(
                screen_width
                    + ((float)input.scale_step * WIDTH_STEP_METRES),
                MIN_WIDTH_METRES, MAX_WIDTH_METRES);
        }
        if (0 != input.dist_step)
        {
            screen_distance = clampf(
                screen_distance
                    + ((float)input.dist_step * DISTANCE_STEP_METRES),
                MIN_DISTANCE_METRES, MAX_DISTANCE_METRES);
        }
        if (0 != input.select_next)
        {
            int next = panel_next(panels, selected);
            if (0 <= next)
            {
                selected = next;
                select_panel(panels, selected);
                (void)fprintf(stderr, "[vs] selected %s\n",
                              panels[selected].output_name);
            }
        }
        if ((SCREEN_SPAWN_NONE != input.spawn_dir) && (0 <= selected))
        {
            int col = 0;
            int row = 0;
            int idx = -1;

            free_cell(panels, panels[selected].col, panels[selected].row,
                      input.spawn_dir, &col, &row);
            idx = panel_add(panels, p_render, NULL, col, row, 1);
            if (0 <= idx)
            {
                selected = idx;
                select_panel(panels, selected);
                (void)fprintf(stderr,
                              "[vs] spawned %s at cell (%d,%d)\n",
                              panels[idx].output_name, col, row);
            }
        }
        if ((0 != input.remove_sel) && (0 <= selected))
        {
            int next = panel_next(panels, selected);

            panel_remove(panels, p_render, selected);
            selected = (next != selected) ? next : -1;
            if (0 > selected)
            {
                selected = panel_next(panels, 0);
            }
            if (0 <= selected)
            {
                select_panel(panels, selected);
            }
        }
        if ((0 != input.yaw_step) && (0 <= selected))
        {
            panels[selected].yaw_adjust +=
                (float)input.yaw_step * ANGLE_STEP_RAD;
        }
        if ((0 != input.pitch_step) && (0 <= selected))
        {
            panels[selected].pitch_adjust +=
                (float)input.pitch_step * ANGLE_STEP_RAD;
        }
        if (0 != input.toggle_help)
        {
            help_visible = (0 != help_visible) ? 0 : 1;
            screen_render_set_help(p_render, NULL, 0U, 0U, help_visible);
        }

        /* Pull newly captured frames into their textures. */
        for (i = 0; i < SCREEN_MAX_PANELS; i++)
        {
            const uint8_t *       p_pixels   = NULL;
            uint32_t              cap_w      = 0U;
            uint32_t              cap_h      = 0U;
            uint32_t              cap_stride = 0U;
            int                   y_invert   = 0;
            screen_pixel_format_t cap_format = SCREEN_PIXEL_UNKNOWN;

            if (0 == panels[i].active)
            {
                continue;
            }
            if (1 != screen_capture_poll(panels[i].capture))
            {
                continue;
            }

            p_pixels = screen_capture_pixels(panels[i].capture, &cap_w,
                                             &cap_h, &cap_stride,
                                             &cap_format, &y_invert);
            if (NULL == p_pixels)
            {
                continue;
            }

            screen_render_upload(p_render, panels[i].render_slot,
                                 p_pixels, cap_w, cap_h, cap_stride,
                                 cap_format, y_invert);
            uploads++;
            panels[i].y_invert = y_invert;
            if (0U < cap_h)
            {
                panels[i].aspect = (float)cap_w / (float)cap_h;
            }
        }

        /* Head pose -> smoothed view matrix. Hold the scene back until
         * the VIO converges (an unstable pose flings everything around
         * the room; see 02_ar_overlay). */
        if ((0 == no_tracking) &&
            (0 == carina_pose_get(p_pose, position, quaternion,
                                  &pose_status, POSE_PREDICT_NS)))
        {
            if (0 == have_smoothed)
            {
                (void)memcpy(s_position, position, sizeof(s_position));
                (void)memcpy(s_quaternion, quaternion,
                             sizeof(s_quaternion));
                have_smoothed = 1;
            }
            else
            {
                int axis = 0;

                for (axis = 0; axis < 3; axis++)
                {
                    s_position[axis] +=
                        smoothing * (position[axis] - s_position[axis]);
                }
                quat_nlerp(s_quaternion, s_quaternion, quaternion,
                           smoothing);
            }

            if ((0 == tracking_locked) &&
                (CARINA_POSE_STABLE == pose_status))
            {
                tracking_locked = 1;
                recenter        = 1;
                (void)fprintf(stderr,
                              "[vs] tracking locked; wall anchored\n");
            }

            mat4_view_from_pose(view, s_position, s_quaternion);
        }

        /* (Re-)anchor: pin the wall's origin along the current gaze,
         * facing the wearer and UPRIGHT. We build the basis ourselves
         * (not reset_origin_carina, which leaves pitch/roll gravity-
         * anchored and would skew the wall). */
        if ((1 == recenter) && (0 != tracking_locked))
        {
            float normal[3] = { 0.0F };

            (void)memcpy(anchor_origin, s_position,
                         sizeof(anchor_origin));
            quat_forward(anchor_forward, s_quaternion);
            normal[0] = -anchor_forward[0];
            normal[1] = -anchor_forward[1];
            normal[2] = -anchor_forward[2];
            mat4_upright_facing(anchor_rot, normal);
            have_anchor = 1;
            (void)fprintf(stderr, "[vs] anchored\n");
        }

        if (0 == have_anchor)
        {
            /* Not anchored yet (or --no-tracking): straight ahead. */
            mat4_identity(anchor_rot);
            anchor_origin[0] = 0.0F;
            anchor_origin[1] = 0.0F;
            anchor_origin[2] = 0.0F;
        }

        /* anchor_world = T(origin) * upright-rotation. Every panel model
         * is composed inside this frame. */
        mat4_translation(anchor_trans, anchor_origin[0],
                         anchor_origin[1], anchor_origin[2]);
        mat4_multiply(anchor_world, anchor_trans, anchor_rot);

        for (i = 0; i < SCREEN_MAX_PANELS; i++)
        {
            if (0 == panels[i].active)
            {
                mat4_identity(&models[i * MAT4_ELEMENTS]);
                continue;
            }
            panel_model(&panels[i], screen_width, screen_distance,
                        anchor_world, &models[i * MAT4_ELEMENTS]);
        }

        mat4_perspective(projection, fov_degrees * DEGREES_TO_RADIANS,
                         screen_render_aspect(p_render),
                         NEAR_PLANE_METRES, FAR_PLANE_METRES);

        /* Help overlay model: a quad centred on the anchor, floating
         * above the wall, upright and facing the wearer. Its up offset
         * clears the top of a single row of panels. */
        {
            float help_w      = screen_width * 1.10F;
            float help_h      = help_w / help_aspect;
            float panel_half  = (screen_width / (16.0F / 9.0F)) * 0.5F;
            float up_offset   = panel_half + (help_h * 0.5F) + 0.06F;
            float up[3]       = { anchor_rot[4], anchor_rot[5],
                                  anchor_rot[6] };
            float help_scale[MAT4_ELEMENTS];
            float help_trans[MAT4_ELEMENTS];
            float help_rs[MAT4_ELEMENTS];
            int   axis        = 0;
            float pos[3]      = { 0.0F };

            for (axis = 0; axis < 3; axis++)
            {
                pos[axis] = anchor_origin[axis]
                          + (anchor_forward[axis] * screen_distance)
                          + (up[axis] * up_offset);
            }

            mat4_scale(help_scale, help_w, help_h, 1.0F);
            mat4_multiply(help_rs, anchor_rot, help_scale);
            mat4_translation(help_trans, pos[0], pos[1], pos[2]);
            mat4_multiply(help_model, help_trans, help_rs);
        }

        screen_render_frame(p_render, view, projection, models,
                            (0 <= selected) ? panels[selected].render_slot
                                            : -1,
                            help_model, tracking_locked);

        frame_count++;
        if (0U == (frame_count % STATUS_LOG_INTERVAL))
        {
            (void)fprintf(stderr,
                "[vs] %s  %d screen(s)  %u uploads  %u frames\n",
                (CARINA_POSE_STABLE == pose_status) ? "stable  "
                                                    : "unstable",
                active_count(panels), (unsigned int)uploads,
                (unsigned int)frame_count);
        }
    }

    (void)fprintf(stderr, "[vs] rendered %u frames\n",
                  (unsigned int)frame_count);
    exit_code = EXIT_SUCCESS;

cleanup:
    /* Remove panels first so any headless outputs we created are torn
     * back down. */
    if (NULL != p_render)
    {
        for (i = 0; i < SCREEN_MAX_PANELS; i++)
        {
            if (0 != panels[i].active)
            {
                panel_remove(panels, p_render, i);
            }
        }
        screen_render_destroy(p_render);
    }
    if (NULL != p_evdev)
    {
        input_evdev_destroy(p_evdev);
    }
    if (NULL != p_pose)
    {
        carina_pose_destroy(p_pose);
    }
    return exit_code;
}
