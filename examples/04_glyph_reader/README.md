# 04_glyph_reader

Recognise a hand-drawn glyph through the glasses' camera and reveal a
short message that is **bound to a specific pair of glasses** — it only
unlocks on the device it was sealed to, seeing the glyph it was sealed
to.

Everything is pure C with no third-party libraries: the JPEG decoder,
the SHA-256, the glyph detector, and the binding are all in this example.

## How it works

```
camera (MJPEG) ─▶ jpeg_decode (Y plane) ─▶ detect ─▶ glyph_decode ─▶ binding_open ─▶ message
                                            │                          ▲
                          Otsu ▸ blobs ▸ quad corners ▸               │
                          homography ▸ 8x8 grid sample          live device sn_hash
```

- **jpeg_decode** — baseline MJPEG frame to grayscale (luma only).
- **detect** — Otsu threshold, connected components, find the black
  finder frame, extract its four corners, homography-unwarp the 8x8
  module grid.
- **glyph** — 8x8 marker: black border + 6x6 interior (24 payload bits +
  8-bit CRC, with an asymmetric corner for orientation).
- **binding** — key = `SHA256(sn_hash ‖ payload)` used as a keystream;
  a key-dependent tag means the wrong device or glyph fails cleanly.

The revealed message currently prints to the terminal. Drawing it on the
glasses HUD (as `03_virtual_screen` does) is the one remaining wire-up.

## Build

```sh
make            # bin/glyph_reader (SDK) and bin/glyphtool (no SDK)
```

## Use it

**1. Get your device's SN hash.** Plug in the glasses and run the reader
once against any secret (it prints the hash while bringing the device
up), or read it from the `[glyph] device sn_hash ...` line when it
starts. Call it `HASH`.

**2. Author a glyph and seal a message to your device:**

```sh
./bin/glyphtool gen  a1b2c3 16 glyph.pgm            # printable marker
./bin/glyphtool seal $HASH a1b2c3 "your secret" secret.dat
ffmpeg -i glyph.pgm glyph.png                       # print glyph.png
```

**3. Run the reader** and point the camera at the printed glyph:

```sh
make run                                            # uses secret.dat
```

It reveals the message only on the glasses the message was sealed to.

## Test it without the headset

The reader has an offline mode that runs the identical pipeline on a JPEG
file with a supplied hash:

```sh
./bin/glyphtool gen a1b2c3 16 glyph.pgm
ffmpeg -i glyph.pgm -vf pad=640:480:200:150:gray scene.jpg
./bin/glyphtool seal <64-hex-hash> a1b2c3 "hello" secret.dat
./bin/glyph_reader --image scene.jpg --hash <64-hex-hash> --secret secret.dat
```

## Scope / limits

- Baseline (SOF0) JPEG only — the format the camera emits.
- The detector expects the glyph roughly facing the camera on a plain
  background; extreme angles or clutter may not be found.
- The binding is a teaching construction (raw SHA-256 keystream, 32-bit
  tag), not a vetted protocol. Do not protect anything real with it.

See the top-level `README.md` for build prerequisites (the VITURE SDK is
not included and must be unpacked under `sdk/`).
