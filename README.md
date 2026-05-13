# 3DS Audio Player 🎵

A 3DS homebrew audio player with **real-time pitch and speed control**.  
Supports **MP3, OGG, FLAC, and WAV** with an SD card file browser.

---

## Controls

| Button | Action |
|---|---|
| **D-Pad Up/Down** | Navigate files |
| **A** | Play selected file / Enter folder |
| **B** | Go up a folder |
| **L** | Pitch down 1 semitone |
| **R** | Pitch up 1 semitone |
| **D-Pad Left** | Speed down |
| **D-Pad Right** | Speed up |
| **Select** | Pause / Resume |
| **Start** | Stop playback |
| **X** | Reset pitch + speed to default |

**Pitch range:** −12 to +12 semitones  
**Speed range:** 0.25× to 4.0×

---

## Full Setup Guide (Beginner)

### Step 1 — Homebrew your 3DS

If you haven't already:

1. Go to **https://3ds.hacks.guide** and follow the guide for your firmware version.
2. This installs **Luma3DS** (custom firmware) and **The Homebrew Launcher**.
3. Make sure you have a microSD card with enough space.

> ✅ You need a modded 3DS to run homebrew. This is permanent and free.

---

### Step 2 — Install devkitPro (Build Tools)

devkitPro provides the compiler for 3DS homebrew.

#### Windows
1. Download the installer from: https://github.com/devkitPro/installer/releases
2. Run `devkitProUpdater-*.exe`
3. Select **3DS Development** during install
4. Also check **portlibs for 3DS** to get extra libraries

#### macOS
```bash
brew tap devkitpro/devkitpro
brew install 3ds-dev
```

#### Linux (Ubuntu/Debian)
```bash
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -S 3ds-dev
```

After installing, set these environment variables (add to your `.bashrc` or `.zshrc`):
```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
```

---

### Step 3 — Get the single-header audio decoders

This project uses **single-header** C libraries — they're just `.h` files you drop in the `source/` folder.

Download these files and place them in the `source/` directory:

| Library | Download URL | Filename |
|---|---|---|
| dr_mp3 | https://github.com/mackron/dr_libs/raw/master/dr_mp3.h | `source/dr_mp3.h` |
| dr_flac | https://github.com/mackron/dr_libs/raw/master/dr_flac.h | `source/dr_flac.h` |
| dr_wav | https://github.com/mackron/dr_libs/raw/master/dr_wav.h | `source/dr_wav.h` |
| stb_vorbis | https://github.com/nothings/stb/raw/master/stb_vorbis.c | `source/stb_vorbis.c` |

You can download them all at once with curl:
```bash
cd source
curl -O https://raw.githubusercontent.com/mackron/dr_libs/master/dr_mp3.h
curl -O https://raw.githubusercontent.com/mackron/dr_libs/master/dr_flac.h
curl -O https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h
curl -O https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c
cd ..
```

---

### Step 4 — Create required folders and icon

```bash
mkdir -p romfs
mkdir -p build
```

You need a 48×48 PNG icon named `icon.png` in the project root. You can use any image editor, or grab a placeholder:
```bash
# On Linux with ImageMagick:
convert -size 48x48 xc:#7C3AFF icon.png
```

Or just create any 48×48 PNG and name it `icon.png`.

---

### Step 5 — Build the project

```bash
make
```

If successful, you'll see `audioplayer.3dsx` in the project folder.

**Common errors:**

| Error | Fix |
|---|---|
| `DEVKITPRO not set` | Run `export DEVKITPRO=/opt/devkitpro` |
| `cannot find -lctru` | Run `dkp-pacman -S 3ds-dev` to reinstall libs |
| `citro2d not found` | Run `dkp-pacman -S 3ds-citro2d` |
| `stb_vorbis.c: No such file` | Download the file per Step 3 |

---

### Step 6 — Put your music on the SD card

1. Insert your 3DS SD card into your computer (or use FTP via Luma's Rosalina menu)
2. Create a folder: `/music/` on the SD card
3. Copy your MP3, OGG, FLAC, or WAV files there

You can also use subdirectories — the file browser supports navigating folders.

---

### Step 7 — Copy the .3dsx to your SD card

Copy `audioplayer.3dsx` to:
```
SD:/3ds/audioplayer.3dsx
```

---

### Step 8 — Launch the app

1. Power on your 3DS
2. Hold **Select** while booting to open the Rosalina menu (Luma3DS)  
   — OR use the **Homebrew Launcher** shortcut you set up
3. Navigate to `audioplayer` and press **A**

---

## Troubleshooting

**No sound:** Make sure your 3DS volume is turned up. The slider is on the left side of the console.

**App crashes on launch:** Make sure `audioplayer.3dsx` is in `SD:/3ds/` and the SD card is properly inserted.

**No files showing:** The browser defaults to `sdmc:/music`. Create that folder and put files in it.

**Choppy audio at extreme pitch/speed:** The 3DS CPU is limited. Very high pitch + high speed simultaneously can cause buffer underruns. Try lowering one.

---

## Project Structure

```
3ds-audioplayer/
├── source/
│   ├── main.c          # Entry point, main loop, input handling
│   ├── audio.c/h       # Audio engine: decoding, pitch/speed, NDSP
│   ├── filebrowser.c/h # SD card directory browsing
│   ├── ui.c/h          # Top & bottom screen rendering (citro2d)
│   ├── dr_mp3.h        # (you download this)
│   ├── dr_flac.h       # (you download this)
│   ├── dr_wav.h        # (you download this)
│   └── stb_vorbis.c    # (you download this)
├── romfs/              # Read-only filesystem bundled into .3dsx
├── build/              # Compiled object files
├── Makefile
├── icon.png            # 48x48 app icon
└── README.md
```

---

## How Pitch & Speed Work

**Speed** is implemented by changing the NDSP (Nintendo DSP) playback sample rate. Playing at 2× speed means the DSP consumes samples twice as fast.

**Pitch** is implemented by resampling the decoded PCM audio before sending it to the DSP. Pitch-shifting up by 12 semitones (1 octave) means we resample at 2× the original rate and play it at normal speed — the audio plays at the same tempo but sounds an octave higher.

The two effects are applied independently, so you can have:
- High speed + same pitch (chipmunk tempo, normal pitch)
- Same speed + high pitch (normal tempo, high notes)
- Or any combination

---

Enjoy your music! 🎶
