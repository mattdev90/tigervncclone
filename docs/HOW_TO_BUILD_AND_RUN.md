programm starten (windows): msys2 ucrt
cd /d/Coding/Tools/tigervnc/build

in build:
???
    cmake -G "MinGW Makefiles" ..
BUILD
    mingw32-make -j$(nproc)
RUN
./vncviewer/vncviewer.exe

# How to Build and Run TigerVNC on Windows

## One-time setup (do this once)

### 1. Install MSYS2
Download and install from https://www.msys2.org — keep the default path (`C:\msys64`).

### 2. Open the right terminal
Search **"MSYS2 UCRT64"** in the Start menu. Always use this one — not MSYS, not MinGW32.
The prompt will show `UCRT64` in the title bar.

### 3. Install dependencies
Run this single line in the UCRT64 terminal:

```bash
pacman -Sy && pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-fltk mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-pixman mingw-w64-ucrt-x86_64-gnutls mingw-w64-ucrt-x86_64-nettle mingw-w64-ucrt-x86_64-make
```

---

## Building

Open **MSYS2 UCRT64**, then:

```bash
cd "D:/Coding/Tools/tigervnc"
mkdir -p build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make -j$(nproc)
```

The viewer ends up at `build/vncviewer/vncviewer.exe`.

> **Note:** Always use `mingw32-make`, not `make` — the UCRT64 toolchain installs it under that name.

---

## Running

```bash
# From the build directory:
./vncviewer/vncviewer.exe hostname:port

# Examples:
./vncviewer/vncviewer.exe 192.168.1.10:5900
./vncviewer/vncviewer.exe myserver:1      # display :1 = port 5901
```

Or double-click `vncviewer.exe` in Explorer — it will show a connection dialog.

---

## Rebuilding after code changes

```bash
cd "D:/Coding/Tools/tigervnc/build"
mingw32-make -j$(nproc)
```

No need to re-run cmake unless you change CMakeLists.txt.

## Clean build (if something is broken)

```bash
cd "D:/Coding/Tools/tigervnc/build"
rm -rf CMakeCache.txt CMakeFiles
cmake -G "MinGW Makefiles" ..
mingw32-make -j$(nproc)
```

---

## Troubleshooting

| Error | Fix |
|---|---|
| `CMake was unable to find a build program` | You're in PowerShell. Open MSYS2 UCRT64 instead. |
| `make: command not found` | Use `mingw32-make` instead of `make` |
| `Could NOT find Pixman` | `pacman -S mingw-w64-ucrt-x86_64-pixman` |
| `Incompatible version of FLTK` | CMakeLists.txt version check patched to accept 1.4 — do a clean build |
| `Does not match the generator used previously` | Run the clean build steps above |
| Multi-line pacman commands break | Paste as a single line — the MSYS2 shell doesn't handle `\` continuation well when pasting |

---

## Finding the EdgeScroll setting

1. Connect to a server and go fullscreen (`F8` → Full Screen)
2. Open **Options** (`F8` → Options)
3. **Input** tab → **Mouse** group
4. Uncheck **"Scroll when mouse reaches screen edge (fullscreen)"**
