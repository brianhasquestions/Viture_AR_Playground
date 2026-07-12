# Vendor SDK goes here

The **VITURE Glasses SDK is proprietary and is not included in this
repository.** Nothing builds without it. This directory is where you
unpack it.

## Where to get it

The SDK is distributed through **VITURE's developer program** — request
access and download it from VITURE directly, not from this repo. Check
VITURE's official developer channels for current access details.

These samples were developed and verified against **VITURE Glasses SDK
v2.3.2** on a **Luma Ultra** (a Carina device). Other versions may work
but are untested.

## Where to put it

Unpack the SDK for your architecture so the layout matches what
`common.mk` expects. `common.mk` picks the directory automatically
(`aarch64` → `viture_arm64`, otherwise `viture_x86_64`):

```
sdk/
├── README.md              # this file (the only tracked file in sdk/)
├── viture_x86_64/         # unpack the x86_64 SDK here
│   ├── include/           # public headers: viture_*.h
│   └── x86_64/            # libglasses.so, libcarina_vio.so, + bundled
│                          #   OpenCV 4.2 / libusb deps
└── viture_arm64/          # unpack the arm64 SDK here (aarch64 hosts)
    ├── include/
    └── aarch64/           # libglasses.so, libcarina_vio.so
```

You only need the directory matching your machine; the other can be
omitted. Each arch ships its own `include/` headers plus the
architecture-specific library directory.

Everything under `sdk/` except this README is git-ignored, so the vendor
binaries stay local and are never committed.
