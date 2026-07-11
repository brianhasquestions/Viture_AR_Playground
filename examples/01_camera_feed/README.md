# 01 — Camera Feed

Pulls the live camera feed from VITURE glasses and either saves MJPEG
frames to disk or streams them to a live preview window.

Verified on a **Luma Ultra** (glasses `35CA:1104`, camera `0C45:636B`).

## Run it

```sh
make          # build bin/camera_feed
make run      # capture 10 frames to bin/captures/frame_XXXXXX.jpg
make view     # live preview window (requires ffplay)
make test     # smoke test
```

`--stream` writes raw MJPEG to stdout, so the feed pipes into anything:

```sh
./bin/camera_feed --stream | ffplay -f mjpeg -i -
./bin/camera_feed --stream | mpv -
./bin/camera_feed --stream > feed.mjpeg
```

In `--stream` mode **all logging goes to stderr**, because stdout carries
binary JPEG data — a stray `printf` would corrupt the stream.

## What it does

1. Finds the glasses by walking `/sys/bus/usb/devices` (`common/device_scan.c`).
2. Detaches the kernel `uvcvideo` driver from the camera (`common/usb_detach.c`).
3. Resolves the camera VID/PID from the glasses product ID.
4. Streams 1920x1080 MJPEG @ 30fps via an SDK frame callback.
5. Saves frames, or pipes them to stdout.

Expected output:

```
[device] connected: Luma Ultra (PID 0x1104)
[usb] kernel driver detached from /dev/bus/usb/001/012
[camera] camera VID=0x0C45 PID=0x636B
[camera] saved frame seq=0  1920x1080  49376 bytes
...
[camera] total frames received: 11
```

## SDK API used

From `viture_camera_provider.h`:

| Function | Purpose |
|---|---|
| `xr_camera_provider_get_camera_vid/_pid` | Map glasses PID to camera USB IDs |
| `xr_camera_provider_create` | Create the provider |
| `xr_camera_provider_start` | Register the frame callback, begin streaming |
| `xr_camera_provider_stop` / `_destroy` | Tear down |

The camera is fixed at **1920x1080 @ 30fps, MJPEG** — the SDK does not
expose other modes. Frames arrive on a dedicated SDK thread, and the
frame buffer is **only valid during the callback**, so bytes must be
copied out there.

## Gotchas (both handled in code)

**`uvcvideo` must be detached.** The SDK speaks UVC directly over libusb,
not V4L2, but Linux auto-binds `uvcvideo` to the camera on plug-in. While
the kernel holds the interfaces, format negotiation fails with:

```
Failed to negotiate 1920x1080@30fps MJPEG: Invalid mode   (-4)
```

The tell-tale sign is the camera having **no `/dev/videoN` node** despite
`uvcvideo` being bound. `usb_detach.c` issues `USBDEVFS_DISCONNECT` on the
usbfs node to release every interface — no root needed (thanks to the udev
rule), and the kernel re-binds on replug.

**The camera is not instantly re-claimable.** After a process releases it,
the device needs a moment before it will negotiate again, so back-to-back
runs failed intermittently. `camera_capture_start()` retries up to 5 times
with a 500 ms backoff.

## Dark frames?

Auto-exposure takes a second or so to settle, and the camera faces
whatever the glasses face. If the first frames are black, the glasses are
probably lying face-down.
