# cbzview

a fast, local, lightweight CBZ comic and manga viewer built for Linux.

## Features
- **Zero-Copy I/O**: `mmap` backing + `madvise` prefetching for masive scale CBZ archives(think GBs)
- **Parallel Thread Pool**: Asynchronous decoding using CPU thread scaling with persistent TurboJPEG instances(less destroy() )
- **Screentone Anti-Moiré Shader**: Hardware-accelerated Catmull-Rom bicubic reconstruction eliminates(or heavily reduces) moiré artifacts on black-and-white manga scans.
- **`ComicInfo.xml` Integration**: (if it exists) Automatic detection of reading direction (RTL/LTR), dual-page spreads, and front covers(depending on how the said xml file is structured)
- **Event-Driven**: No CPU usage during idle reading.

## Controls
| Key | Action |
| :--- | :--- |
| `Right` / `L` / `Space` | Next Page / Spread |
| `Left` / `H` / `Backspace` | Previous Page / Spread |
| `D` | Toggle Layout (Single $\rightarrow$ Dual Spread $\rightarrow$ Webtoon) |
| `M` | Toggle Reading Direction (LTR $\leftrightarrow$ RTL) |
| `W` / `2` | Toggle Fit Mode (Fit-Height $\leftrightarrow$ Fit-Width) |
| `C` | Toggle Contrast Boost |
| `R` | Reset Zoom / Pan |
| `F` / `F11` | Fullscreen Toggle |
| `Q` / `Esc` | Quit |

## Dependencies
- `glfw`
- `libzip`
- `libjpeg-turbo`
- `libwebp`
- `libpng`
- `mesa` (OpenGL)

## Building & Installing
```bash
make -j$(nproc)
sudo make install

