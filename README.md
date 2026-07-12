# XR_Playground

Sample code for the **VITURE XR Glasses SDK** on Linux, in plain C.

Each directory under `examples/` is a self-contained, runnable sample
with its own `Makefile` and `README.md`. Shared plumbing lives in
`common/`.

Developed and verified against a **VITURE Luma Ultra**
(glasses `35CA:1104`, camera `0C45:636B`) on x86_64 Linux with GCC.

## Demo

`03_virtual_screen` in action: the desktop is pinned to a fixed spot in
the room, so it stays put on the wall as the head turns.

![Virtual screen world-locked to the wall as the head moves](docs/demo.gif)

▶ **[Watch the full video](https://github.com/brianhasquestions/Viture_AR_Playground/raw/main/docs/demo-social.mp4)** (with audio, full length).

## Examples

| Example | What it shows |
|---|---|
| [`01_camera_feed`](examples/01_camera_feed/) | Pull the live 1080p MJPEG camera feed; save stills or stream to a viewer |
| [`02_ar_overlay`](examples/02_ar_overlay/) | Optical see-through AR: world-locked graphics on the glasses display, anchored by the Carina 6DoF pose |
| [`03_virtual_screen`](examples/03_virtual_screen/) | Pin your desktop to a spot in the room: live screen capture on a world-locked quad |

## Layout

```
XR_Playground/
├── Makefile              # builds every example
├── common.mk             # shared build rules (SDK paths, flags, rpath)
├── common/               # shared modules used by several samples
│   ├── include/
│   │   ├── device_scan.h # find the glasses via POSIX sysfs
│   │   ├── usb_detach.h  # release the camera from uvcvideo
│   │   ├── carina_pose.h # 6DoF head tracking (Carina VIO)
│   │   └── xr_math.h     # matrix / quaternion math
│   └── src/
├── examples/             # each sample: src/headers, src/source, Makefile, README
│   ├── 01_camera_feed/
│   ├── 02_ar_overlay/
│   └── 03_virtual_screen/
├── sdk/
│   ├── viture_arm64/     # unpacked VITURE SDK (Linux arm64)
│   └── viture_x86_64/    # unpacked VITURE SDK (Linux x86_64)
└── scripts/              # udev rule + installer
```

`common.mk` picks the SDK matching your architecture automatically
(`aarch64` → arm64, otherwise x86_64).

## Setup

The **VITURE Glasses SDK is not included in this repository** (it is
proprietary). Obtain it from **VITURE's developer program** and unpack it
under `sdk/` so the layout matches `sdk/viture_x86_64/` (or
`sdk/viture_arm64/` on aarch64), each with its `include/` headers and
architecture library directory. Nothing builds without it. See
[`sdk/README.md`](sdk/README.md) for the exact layout and download
pointer. These samples were verified against **SDK v2.3.2**.

Prerequisites: `gcc`, `make`, and a VITURE device on USB. The vendor
library needs `libudev.so.1` (present on most distros). `02_ar_overlay`
and `03_virtual_screen` additionally need **SDL2** and **OpenGL**;
`03_virtual_screen` also needs **wayland-client** + **wayland-scanner**
and a **wlroots-based compositor** (Hyprland, Sway, river) for
screen capture. `ffplay` (from ffmpeg) is optional, for the live camera
viewer.

For the AR overlay the glasses must also be connected as a **display**
(they enumerate as an ordinary 1920x1080 @ 90Hz DisplayPort monitor), not
just over USB.

### Dependency tree

Only SDL2 is a dependency we chose and could drop; everything else is
either the vendor SDK, something the vendor SDK drags in, or a system
interface. Worth knowing before anyone tries to trim the list:

| Dependency | Where it comes from | Removable? |
|---|---|---|
| `libglasses`, `libcarina_vio` | Vendor SDK, vendored in `sdk/` | No, this *is* the project |
| OpenCV 4.2 (8 `.so`s), `libusb`, `libudev` | Hard-linked `NEEDED` entries in the SDK's own binaries | No, not ours to remove; they leave only if 6DoF tracking does |
| OpenGL, `wayland-client` | System / driver interface | No, not vendored deps |
| SDL2 | Our choice, used only in `screen_render.c` and `ar_render.c` | Yes, at the cost of hand-writing raw Wayland + EGL windowing and evdev input |

`objdump -p sdk/viture_x86_64/x86_64/libcarina_vio.so | grep NEEDED` shows
the OpenCV tail directly, if you want to confirm it rather than trust the
table.

**One-time USB permissions.** The camera's USB node is owned by `root`,
so a normal user cannot claim it and the SDK fails with `Access denied`
(-2). Install the bundled udev rule once, then replug the glasses:

```sh
sudo ./scripts/install_udev_rules.sh
```

## Glasses-only workspace (Hyprland)

`scripts/glasses-workspace.sh` gives you your **real desktop, world-locked
in the glasses**, with the laptop panel off.

The obvious approach does not work. With the laptop disabled, Hyprland
moves your windows onto the glasses output, which is exactly where the
overlay draws, so it covers them; and the overlay cannot capture the
glasses itself without creating a feedback loop. Either way you get a
black screen.

So the script gives the desktop somewhere else to live:

1. Creates a **headless output** (a real Hyprland monitor, off-screen).
2. Moves every workspace that has windows **onto it**.
3. Pins the overlay to the glasses output and captures the headless.

Net effect: laptop panel off, glasses show only the overlay, and the
overlay shows your actual windows, pinned in space. Quitting the overlay
(`Ctrl+Alt+Shift+Q`) moves the workspaces back and removes the headless
output.

On the development machine it is wired into the `display-external` shell
function (in `~/.bashrc`), so switching to glasses-only mode also starts
the overlay. On a fresh machine, either call the script directly or add
the same one-line hook to your own display-switch command. Requires
`hyprctl` and `jq`.

## Build & run

```sh
make                              # build every example
make list                         # list the examples

cd examples/01_camera_feed
make run                          # run that one
```

## Device notes

The **Luma Ultra is a Carina device** (`XR_DEVICE_TYPE_VITURE_CARINA`).
That matters for tracking: Carina hardware runs onboard VIO and exposes
a fused **6DoF pose** through `xr_device_provider_get_gl_pose_carina()`
(`viture_device_carina.h`), returning `[px, py, pz, qw, qx, qy, qz]` in
OpenGL coordinates (x→right, y→up, z→backward), with pitch and roll
gravity-anchored.

The raw-IMU functions in `viture_device.h` (`xr_device_provider_open_imu`
and friends) are for Gen1/Gen2 devices and are documented as **"no effect
for Carina device"**: do not reach for them on a Luma Ultra.

Note that USB enumeration via sysfs and `USBDEVFS_DISCONNECT` are
Linux-specific; there is no portable POSIX way to do either.
