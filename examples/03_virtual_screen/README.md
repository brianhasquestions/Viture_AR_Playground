# 03 — Virtual Screen (world-locked desktop)

Pins your desktop to a spot in the room. The laptop panel is captured
live, uploaded to a GL texture, and drawn on a quad that is **static in
world space** — the same world-locking trick as
[`02_ar_overlay`](../02_ar_overlay/), but with pixels on the quad instead
of wireframe.

Look away and the screen stays where you left it.

Verified on a **Luma Ultra** under Hyprland.

## Run it

```sh
make                     # build bin/virtual_screen
make run                 # fullscreen on the glasses, world-locked
make windowed            # desktop-window preview
./bin/virtual_screen --no-tracking   # fixed viewpoint, no VIO needed
```

Options: `--output <name>` (repeat for multiple screens, max 4),
`--stack` (vertical instead of side by side), `--mod <combo>`,
`--width <m>`, `--distance <m>`, `--fov <deg>`, `--smooth <a>`,
`--display <n>`, `--windowed`, `--no-tracking`, `--spawn-first`,
`--help`.

`--spawn-first` starts with one fresh headless output instead of
capturing the laptop panel. Note a freshly spawned output is **empty** —
an empty desktop is black, and black is invisible on the additive
combiner, so on its own it shows you nothing. To get your *real* desktop
world-locked in glasses-only mode, use `scripts/glasses-workspace.sh`,
which creates a headless output, **moves your populated workspaces onto
it**, and then captures it.

### Multiple screens

Pass `--output` more than once to place several panels around the anchor.
To get a *second desktop* (not a mirror) on a wlroots compositor, spawn a
headless output first:

```sh
hyprctl output create headless          # creates e.g. HEADLESS-2
./bin/virtual_screen --output eDP-1 --output HEADLESS-2
./bin/virtual_screen --output eDP-1 --output HEADLESS-2 --stack   # vertical
hyprctl output remove HEADLESS-2        # when done
```

Panels are offset along the anchor's own right/up axes, so they stay
coplanar and square to each other and re-anchor together.

Controls require a **modifier combo held** (default **Ctrl+Alt**), so
typing on a virtual screen is never stolen by the overlay. Change it with
`--mod` (e.g. `--mod alt+shift`) — Hyprland grabs most Super and
Ctrl+Super combos itself, so those never reach the app.

| Combo | Action |
|---|---|
| **mod + I / K / J / L** | Spawn a screen above / below / left / right of the selected one |
| **mod + X** | Remove the selected screen (tears down its output) |
| **mod + Tab** | Select the next screen — **amber border** marks it, and the mouse + focus warp into it |
| **mod + , / .** | Angle the selected screen left / right (yaw) |
| **mod + ; / '** | Tilt the selected screen down / up (pitch) |
| **mod + R** | Re-anchor the whole wall upright in front of you |
| **mod + - / =** | Shrink / grow all screens |
| **mod + [ / ]** | Wall nearer / further |
| **mod + Q** | Quit |

### Spawning is real: a curved wall of separate desktops

`mod+L` and friends don't mirror the desktop — each spawn runs
`hyprctl output create headless`, which gives you a **genuine new Hyprland
monitor** with its own workspaces, then captures it. `mod+X` removes it
again. Move a window onto the spawned output and it appears on that panel.

Screens sit on a **curved wall**: each panel is on an arc at the set
distance and toed-in to face you. On a flat wall the outer edges of side
panels are physically farther from the eye and look small and distant;
curving keeps every panel square-on. Neighbours are spaced so their
bezels touch. `mod+,` / `mod+.` and `mod+;` / `mod+'` add a manual angle
to the selected panel on top of the automatic curve.

The old flat side-by-side / `--stack` behaviour is gone; layout is now a
grid of cells relative to the anchor, filled as you spawn.

### Selecting warps the cursor into the screen

Selecting a panel (Tab, or spawning) runs `hyprctl dispatch movecursor`
to the centre of that panel's output and focuses it, so the pointer and
keyboard land on the desktop you just picked instead of floating outside
the virtual screens. The output geometry is read from `hyprctl monitors`
(logical position = physical resolution / scale).

## How it works

Three independent pieces, joined in the render loop:

```
wlr-screencopy  ->  GL texture  ─┐
                                 ├─>  quad drawn with  projection * view * model
Carina 6DoF pose -> view matrix ─┘
```

- The quad is a **unit square** in the XY plane. Where it sits and how
  big it is comes entirely from the **model matrix**, so resizing or
  moving the screen never touches the vertex buffer.
- The model matrix is expressed in **world space** and does not change as
  you move. Only the view matrix does. That is what pins the screen to
  the room.
- Head tracking is gated on `pose_status`, exactly as in 02: nothing is
  drawn until the VIO converges, then it auto-anchors in front of you.

## Screen capture

Uses **`zwlr_screencopy_manager_v1`** (wlr-screencopy-unstable-v1), the
same protocol `grim` uses. Works on wlroots-based compositors — Hyprland,
Sway, river. It will **not** work on GNOME or KDE, which do not implement
it (they expect the xdg-desktop-portal / PipeWire route instead).

The protocol XML is **vendored** under `protocols/` and compiled by
`wayland-scanner` at build time, so the sample does not depend on a
system `wlr-protocols` package.

Capture is asynchronous and pumped from the render loop: one frame is
requested at a time and the next is requested as soon as the last lands,
so the renderer never blocks on the compositor. In practice the
compositor delivers ~20-25 fps of captures while the glasses render at
90 fps — the texture simply persists between updates.

### Do not capture the glasses

Pointing the capture at the display you are rendering onto produces a
feedback loop (a hall of mirrors). The sample auto-picks the first output
whose description does *not* contain "VITURE", and logs what it skipped:

```
[capture] output: eDP-1
[capture] output: DP-1  (glasses - skipped)
[capture] capturing eDP-1
```

### Pixel formats and flipping

The shm buffer is uploaded straight to GL with `GL_BGRA` / `GL_RGBA` to
match the Wayland byte order, so there is **no per-pixel CPU swizzle**.
`GL_UNPACK_ROW_LENGTH` handles a stride wider than the image.

The compositor may deliver the image bottom-up (the `y_invert` flag). The
sample handles that by **negating the Y scale in the model matrix** —
cheaper and simpler than flipping pixel rows or rebuilding UVs.

The buffer is reallocated only when the output geometry changes, so the
steady state performs no allocation at all.

## Additive-display caveat

Black desktop pixels emit no light, so they are **invisible** through the
optics. A dark terminal will read as floating text with no window behind
it. That is why a cyan **border** is drawn around the quad — it keeps the
screen's edges visible regardless of content. Bright content (a browser,
a light-themed editor) reads best.

There is no way to render "darker than the real world" on an additive
combiner — you cannot dim what is behind the image.

## `--no-tracking`

Skips the VIO entirely and draws from a fixed viewpoint. Nothing is
world-locked; the screen just sits straight ahead. Useful for developing
the capture and render path without wearing the glasses or waiting for
tracking to converge.

## Files

| File | Role |
|---|---|
| `src/source/screen_capture.c` | wlr-screencopy client (Wayland, shm) |
| `src/source/screen_render.c` | SDL2 + OpenGL textured quad + border |
| `src/source/main.c` | Render loop; joins pose, capture and model matrix |
| `common/src/carina_pose.c` | Carina 6DoF tracking |
| `common/src/xr_math.c` | Matrix / quaternion math |
| `protocols/*.xml` | Vendored wlr-screencopy protocol (MIT) |
