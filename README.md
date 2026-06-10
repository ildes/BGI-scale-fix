> This repo was auto-created by **DeepSeek V4 Flash**.

# BGI-scale-fix — 2× upscale for BGI-engine visual novels on Linux

Makes hard-locked 1280×720 BGI/Ethornell engine games render at 2× native
resolution (2560×1440) without changing monitor resolution.

## How it works

A **D3D9 proxy DLL** placed next to the game's executable is auto-loaded by
Wine (same-directory native priority). It intercepts `CreateDevice`, forces
`BackBufferWidth=1280, BackBufferHeight=720`, and resizes the window to
2560×1440. DXVK's `forceSwapchainSize` handles the swapchain stretching.
The proxy also hooks `GetCursorPos`/`GetMessagePos` to scale mouse input
coordinates by 2×.

No DLL overrides required. No additional tools needed.

## Files

| File | Purpose |
|------|---------|
| `d3d9_resolution_proxy.c` | Source of the D3D9 proxy DLL (MinGW cross-compiled) |
| `d3d9.def` | Export definitions |
| `d3d9.dll` | Pre-compiled 32-bit proxy DLL |
| `dxvk.conf` | `d3d9.forceSwapchainSize = 2560x1440` |
| `launch.sh` | Example launcher |

## Prerequisites

- Linux with proprietary NVIDIA driver
- Wine
- DXVK installed in the game's Wine prefix

## Usage

1. Copy `d3d9.dll` and `dxvk.conf` into the game directory (next to the
   engine's executable).
2. Edit `launch.sh` to set the correct `WINEPREFIX`, then run:
   ```bash
   ./launch.sh
   ```

The proxy is loaded automatically. DXVK reads `dxvk.conf` automatically.
No environment variables needed.

## Building from source

```bash
i686-w64-mingw32-gcc -shared -o d3d9.dll d3d9_resolution_proxy.c \
  d3d9.def -static-libgcc -s -O2
```

## Customization

Adjust `FORCED_W`/`FORCED_H` and `NATIVE_W`/`NATIVE_H` in
`d3d9_resolution_proxy.c` for different target/native resolutions.