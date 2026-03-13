# Sinistar

Native C++/SDL2 reimplementation of **Sinistar** (Williams Electronics, 1983). This is not an emulator — it is a from-scratch rewrite that uses the original ROM assets for sprites, palette, sound effects, and speech.

## Requirements

- **g++** with C++17 support
- **SDL2** development libraries
- **Original Sinistar ROM files** (not included — you must supply your own)

### Installing SDL2

```bash
# Debian/Ubuntu
sudo apt install libsdl2-dev

# Fedora
sudo dnf install SDL2-devel

# Arch
sudo pacman -S sdl2
```

## ROM Setup

Place the following ROM files in the `source/` directory:

### Game ROMs (required)

| File | Description |
|------|-------------|
| `sinistar.01` - `sinistar.09` | Game code and graphics (4KB each) |
| `sinistar.10` - `sinistar.11` | PACK data (font/sprite data) |
| `sinistar.snd` | Sound ROM |

### Speech ROMs (required)

| File | Description |
|------|-------------|
| `speech.ic4` - `speech.ic7` | CVSD encoded speech data |

### Speech WAV files (optional, for higher quality speech)

Place these in `source/speech/`:

| File | Phrase |
|------|--------|
| `bewareil.wav` | "Beware, I live!" |
| `ihunger.wav` | "I hunger" |
| `iamsinis.wav` | "I am Sinistar" |
| `runcowar.wav` | "Run, coward!" |
| `bewareco.wav` | "Beware, coward!" |
| `ihungerc.wav` | "I hunger, coward!" |
| `runrunru.wav` | "Run! Run! Run!" |
| `aargh.wav` | Roar/growl |

Your `source/` directory should look like this:

```
source/
  sinistar.01
  sinistar.02
  ...
  sinistar.11
  sinistar.snd
  speech.ic4
  speech.ic5
  speech.ic6
  speech.ic7
  speech/
    bewareil.wav
    ihunger.wav
    iamsinis.wav
    runcowar.wav
    bewareco.wav
    ihungerc.wav
    runrunru.wav
    aargh.wav
```

## Building

```bash
make
```

## Running

```bash
./sinistar
```

To specify a different ROM directory:

```bash
./sinistar -roms /path/to/roms
```

## Controls

| Key | Action |
|-----|--------|
| Arrow keys / WASD | Move |
| Space | Fire |
| B | Launch Sinibomb |
| 5 | Insert coin |
| 1 | Start game |
| Escape | Quit |

Game controllers are also supported via SDL2 GameController API.

## About

Sinistar was designed by Sam Dicker, RJ Mical, Noah Falstein, Richard Witt, and Jack Haeger at Williams Electronics. It was one of the first arcade games to feature digitized speech and remains a classic of the golden age of arcade games.

This project is a fan reimplementation for educational and preservation purposes. The original ROM files are copyrighted by Williams Electronics and are not included in this repository.
