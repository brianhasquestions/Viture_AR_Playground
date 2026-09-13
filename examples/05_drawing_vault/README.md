# 05_drawing_vault

Draw something in the real world, seal a secret message to it **and to
your glasses**, then reveal that message later just by looking at the
drawing again. The message only opens on the pair of glasses it was
enrolled on.

Frameless: the drawing needs no marker or border. Pure C except SDL2,
which is used only to put the revealed message on the glasses screen.

## How it works

```
enroll:  camera ─▶ jpeg_decode ─▶ phash(fingerprint) ─┐
                                                       ├─▶ vault record { fingerprint, sealed(message) }
                        glasses sn_hash ───────────────┘        key = SHA256(sn_hash ‖ fingerprint)

recall:  camera ─▶ jpeg_decode ─▶ phash ─▶ nearest stored fingerprint (Hamming ≤ threshold)
                                              └─▶ decrypt with live sn_hash ─▶ HUD on the glasses
```

- **phash** — central-crop, downscale, DCT low frequencies, median
  threshold → a 64-bit fingerprint. Tolerant of brightness, noise and
  scale; *not* rotation/perspective (re-view from a similar angle).
- **vault** — a file of `{fingerprint, sealed message}` records. The
  drawing *selects* the record (fuzzy match); your glasses hash *opens*
  it (exact). Wrong drawing → no match; wrong glasses → locked.
- **display** — the revealed message on the glasses via SDL2's 2D
  renderer and the embedded HUD font.

## Build

```sh
make            # bin/drawing_vault (SDK + SDL2)
```

## Use it

Draw something (ideally high-contrast, filling the view), then:

```sh
# store a message for the drawing currently in view
./bin/drawing_vault enroll --message "meet at the docks, 9pm"

# later, look at the same drawing to reveal it on the glasses
make run ARGS="recall --display"
```

`--maxdist <n>` (default 10) tunes how close the re-view must be; raise
it if a genuine drawing is not recognized, lower it if unrelated scenes
match.

## Test it without the headset

The same enroll/recall pipeline runs on a JPEG with a supplied hash:

```sh
H=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff
./bin/drawing_vault enroll --message "hello" --image drawing.jpg --hash $H
./bin/drawing_vault recall               --image drawing.jpg --hash $H
```

## Scope / limits

- Frameless perceptual hashing re-recognizes a drawing only from a
  roughly similar distance and angle. For pose-invariant recognition you
  would draw inside a square border and rectify it first (the machinery
  for that lives in `04_glyph_reader`).
- Teaching-grade crypto (raw SHA-256 keystream, 32-bit tag). Do not
  protect anything real with it.
- The vault file and any enrolled photos are local; nothing leaves the
  machine.

See the top-level `README.md` for build prerequisites (the VITURE SDK is
not included and must be unpacked under `sdk/`).
