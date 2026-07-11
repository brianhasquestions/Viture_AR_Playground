# 02 — AR Overlay (optical see-through)

World-locked graphics rendered on the glasses display, anchored in the
room by the **Carina 6DoF head pose**. Look around and the grid, cube and
axis gizmo stay put in space while you see the real world through them.

Verified on a **Luma Ultra** (`35CA:1104`).

## Run it

```sh
make            # build bin/ar_overlay
make run        # fullscreen on the glasses
make windowed   # desktop-window preview, no glasses needed
```

Options: `--windowed`, `--display <n>`, `--fov <deg>`, `--3dof`, `--help`.

Controls: **R** recentre · **Esc/Q** quit.

## The core idea

The scene never moves. Only the *view matrix* does.

Geometry is authored once in world coordinates and baked into a single
vertex buffer. Every frame we ask the device where the head is, invert
that pose into a view matrix, and redraw. The overlay appears nailed to
the room precisely *because* the world geometry is static and only the
camera moves:

```
world geometry (static)  →  view = inverse(head pose)  →  projection  →  screen
```

That inversion is the whole trick, and it is cheap: the pose rotation is
orthonormal, so its inverse is just the transpose (`xr_math.c`).

## Two things the optics dictate

**1. Black is transparent.** The combiner is *additive*: black pixels
emit no light, so they are fully see-through. There is no alpha channel
to the real world — dark means transparent, bright means solid. Hence the
scene is cleared to pure black, never to a background colour.

**2. Wireframe, not solid.** A filled polygon would wash out whatever is
behind it. Lines leave the real world visible, so the whole scene is
`GL_LINES`.

## How the glasses are driven

They enumerate as an **ordinary DisplayPort monitor** (1920x1080 @ 90Hz,
named `VITURE`). There is no special presentation API: rendering AR on
them means putting a fullscreen window on that display. The sample finds
it by name via SDL and opens there; `--display <n>` overrides.

## Carina tracking

The Luma Ultra is a Carina device: onboard VIO fuses the stereo cameras
with the IMU into a fused 6DoF pose. `common/carina_pose.c` wraps it.

The SDK lifecycle order is **load-bearing** and not obvious:

```
create → set_dof_type_carina → initialize → register_callbacks_carina → start
```

- The DoF type must be set **before** `initialize()`.
- The Carina callbacks must be registered **before** `start()` — the VIO
  engine captures the callback pointers at start time. We pass all-NULL
  (we don't want the stereo frames ourselves); the VIO still consumes
  them internally to produce the pose.

Get either wrong and you get a handle that starts but never tracks.

The pose is `[px, py, pz, qw, qx, qy, qz]` in OpenGL coordinates
(x→right, y→up, z→backward), with pitch and roll gravity-anchored so they
cannot drift. It is queried **per frame** rather than cached from a poll
thread, so rendering always uses the freshest sample, with ~11 ms of
forward prediction (one frame at 90Hz) to hide render+scanout latency.

> Do **not** use the raw IMU API (`xr_device_provider_open_imu`, in
> `viture_device.h`) on a Luma Ultra. The SDK documents it as *"no effect
> for carina device"* — it exists for the Gen1/Gen2 glasses.

### Wait for the VIO to converge — do not render before it does

Pose reports `unstable` for the first second or two while the VIO
converges, then `stable`. Stationary drift after that is in the low
millimetres.

**The unstable pose is not merely imprecise, it is meaningless.** Feed it
into the view matrix and the "world-locked" geometry flies around the
room — the cube visibly thrashes until tracking settles. The fix is to
respect `pose_status`:

```c
if ((0 == tracking_locked) && (CARINA_POSE_STABLE == pose_status))
{
    carina_pose_recenter(p_pose);   /* anchor where the wearer is looking */
    tracking_locked = 1;
}
ar_render_frame(p_render, view, projection, tracking_locked);
```

Until the first stable sample the sample presents a **black frame**,
which on an additive display is simply invisible — the wearer sees
nothing rather than seeing garbage. On that first lock it recentres, so
the scene anchors in front of the wearer instead of wherever the device
happened to initialise. Afterwards it keeps drawing through transient
unstable blips, so the overlay does not flicker.

Convergence needs **visual features and parallax**. Lying face-down on a
desk, the stereo cameras see a dark, featureless surface and the VIO may
never lock at all. Look around a lit area with some visual detail.

## Gotchas

**The drawable size is not final at startup.** Under Wayland the
compositor applies fullscreen asynchronously, so the size you get right
after creating the window is the *pre-fullscreen* one. Querying it once
leaves you rendering into a quarter of the panel with the wrong aspect:

```
[render] drawable 960x540      <- what you get at creation
[render] drawable 1920x1080    <- what it actually becomes
```

So the drawable is re-queried **every frame** and the projection matrix
rebuilt from it.

**The FOV is approximate.** `DEFAULT_FOV_Y_DEGREES` (27°) is derived from
the Luma Ultra's ~52° diagonal at 16:9 — it is *not* a calibrated value.
If the overlay swims or scales wrongly against the real world as you
move, this is the first knob to turn: `--fov <degrees>`. Proper
registration needs the panel's true optical parameters, and this sample
does not attempt per-eye stereo rendering or lens distortion correction.

## Files

| File | Role |
|---|---|
| `src/source/main.c` | Orchestration and the render loop |
| `src/source/ar_render.c` | SDL2 + OpenGL 3.3 renderer, scene geometry |
| `src/source/xr_math.c` | Matrix / quaternion math (pose → view matrix) |
| `common/src/carina_pose.c` | Carina 6DoF tracking lifecycle |
| `common/src/device_scan.c` | Find the glasses on USB |

OpenGL 3.3 core. On Mesa every core entry point is exported by `libGL`
directly, so `GL_GLEXT_PROTOTYPES` suffices — no GLEW/glad needed.
