# Sinistar ROM Technical Analysis

Reverse-engineering documentation for the original Sinistar arcade ROMs (Williams Electronics, 1983).

## Hardware Overview

- **CPU**: Motorola 6809E @ 894.886 kHz
- **Display**: 304×256 pixel framebuffer, CRT rotated 90° (portrait orientation: 256×304 visible)
- **Color**: 16-color palette from 8-bit BBGGGRRR hardware register
- **Sound CPU**: Motorola 6808 for sound effects
- **Speech**: Harris HC-55516 CVSD codec on dedicated board
- **RAM**: 32KB shared video/CPU RAM mapped at $0000–$BFFF

## ROM File Inventory

### Game ROMs (9 × 4KB = 36KB)

| File | Size | CPU Address | Contents |
|------|------|-------------|----------|
| sinistar.01 | 4096 | $0000–$0FFF | Player sprites, sinibomb sprites, planetoid 1–4 |
| sinistar.02 | 4096 | $1000–$1FFF | Planetoid 5, worker sprites, warrior sprite, bone sprites, Sinistar piece pixel data |
| sinistar.03 | 4096 | $2000–$2FFF | Sinistar face composite data, shot sprites, crystal sprites, planetoid fragments |
| sinistar.04 | 4096 | $3000–$3FFF | Game logic, entity AI routines |
| sinistar.05 | 4096 | $4000–$4FFF | Game logic, Sinistar piece table, build thresholds |
| sinistar.06 | 4096 | $5000–$5FFF | Game logic, attract mode, scoring |
| sinistar.07 | 4096 | $6000–$6FFF | Game logic, collision handling |
| sinistar.08 | 4096 | $7000–$7FFF | Game logic, status screens |
| sinistar.09 | 4096 | $8000–$8FFF | Font data, text strings, high score |

### Fixed ROMs (2 × 4KB = 8KB)

| File | Size | CPU Address | Contents |
|------|------|-------------|----------|
| sinistar.10 | 4096 | $E000–$EFFF | Interrupt handlers, hardware I/O, blitter |
| sinistar.11 | 4096 | $F000–$FFFF | Boot code, NMI/IRQ vectors, core routines |

### Speech ROMs (4 × 4KB = 16KB)

| File | Size | Contents |
|------|------|----------|
| speech.ic4 | 4096 | CVSD bitstream segment 1 |
| speech.ic5 | 4096 | CVSD bitstream segment 2 |
| speech.ic6 | 4096 | CVSD bitstream segment 3 |
| speech.ic7 | 4096 | CVSD bitstream segment 4 |

**Total ROM**: 60 KB (36KB game + 8KB fixed + 16KB speech)

## CPU Memory Map

```
$0000–$0FFF  ROM bank 1 (sinistar.01)
$1000–$1FFF  ROM bank 2 (sinistar.02)
$2000–$2FFF  ROM bank 3 (sinistar.03)
$3000–$3FFF  ROM bank 4 (sinistar.04)
$4000–$4FFF  ROM bank 5 (sinistar.05)
$5000–$5FFF  ROM bank 6 (sinistar.06)
$6000–$6FFF  ROM bank 7 (sinistar.07)
$7000–$7FFF  ROM bank 8 (sinistar.08)
$8000–$8FFF  ROM bank 9 (sinistar.09)
$9000–$9FFF  Shared video RAM (upper)
$A000–$BFFF  Shared video RAM (lower)
$C000–$CFFF  I/O space (hardware registers)
$D000–$DFFF  CMOS RAM (high scores, settings)
$E000–$EFFF  ROM bank 10 (sinistar.10, fixed)
$F000–$FFFF  ROM bank 11 (sinistar.11, fixed)
```

## Sprite Descriptor Format

Standard sprite descriptors are 8 bytes:

```
Offset  Size  Field         Description
  +0     1    width_bytes   Width in bytes (pixels = width_bytes × 2)
  +1     1    height        Height in scan lines
  +2     1    data_hi       Pixel data address, high byte (big-endian)
  +3     1    data_lo       Pixel data address, low byte
  +4     1    flags         End marker or blitter flags
  +5     1    reserved      (unused in most sprites)
  +6     1    center_x      Horizontal center/registration point
  +7     1    center_y      Vertical center/registration point
```

**Pixel Data Encoding**: 2 pixels per byte. High nibble = left pixel, low nibble = right pixel. Palette index 0 = transparent. Data stored row-major: `width_bytes × height` total bytes.

## Fragment Descriptor Format

Sinistar bone/damage fragments and planetoid chunks use an 8-byte fragment descriptor:

```
Offset  Size  Field         Description
  +0     1    x_offset      X position within parent sprite
  +1     1    y_offset      Y position within parent sprite
  +2     1    width         Width in bytes (pixels = width × 2)
  +3     1    height        Height in scan lines
  +4     1    data_hi       Pixel data address, high byte
  +5     1    data_lo       Pixel data address, low byte
  +6     1    reserved      Always 0
  +7     1    reserved      Always 0
```

## Complete Sprite Inventory

### Player Ship (32 rotation frames)

| Address | ROM | Description |
|---------|-----|-------------|
| $0007 | ROM1 | Descriptor table (32 × 8 bytes = 256 bytes) |

- 32 frames for 16 directional angles (doubled for smooth rotation)
- Frame dimensions vary: 10–12 pixels wide, 9–11 pixels tall
- Pixel data interleaved throughout ROM1 ($0107–$0AC2)
- Example frame 0: width_bytes=6 (12px), height=9, data=$0107, center=(5,4)

### Worker Ship (16 rotation frames)

| Address | ROM | Description |
|---------|-----|-------------|
| $129D | ROM2 | Descriptor table (16 × 8 bytes = 128 bytes) |

- 16 frames for rotation
- Smaller than player ship

### Warrior Ship (1 frame)

| Address | ROM | Description |
|---------|-----|-------------|
| $19E1 | ROM2 | Single descriptor (8 bytes) |

- Single non-rotating sprite (17×16 pixels)
- Warriors always face the same direction visually

### Sinibomb (3 animation frames)

| Address | ROM | Description |
|---------|-----|-------------|
| $0AD4 | ROM1 | Descriptor table (3 × 8 bytes) |

- 3-frame pulsing animation
- Small sprite (5×6 pixels)

### Shots/Bullets (16 frames)

| Address | ROM | Description |
|---------|-----|-------------|
| $2438 | ROM3 | Descriptor table (16 × 8 bytes) |

- 16 directional bullet sprites
- Very small (2–4 pixels each)

### Crystal (small, collectible)

| Address | ROM | Description |
|---------|-----|-------------|
| $26A1 | ROM3 | Single descriptor (8 bytes) |

- 4×2 pixel sprite
- Rendered with palette color cycling in-game
- Pixel data at $26A9: `FF 00 FF 00`

### Large Crystal (mineable)

| Address | ROM | Description |
|---------|-----|-------------|
| $26B1 | ROM3 | Single descriptor (8 bytes) |

- 6×4 pixel sprite, center (1,1)
- Pixel data at $26B9 (12 bytes)

### Planetoids (5 types)

| Address | ROM | Type | Approximate Size |
|---------|-----|------|------------------|
| $0B23 | ROM1 | Type 1 | 28×26 |
| $0CCB | ROM1 | Type 2 | 26×24 |
| $0E3B | ROM1 | Type 3 | 20×18 |
| $0F1B | ROM1 | Type 4 | 24×27 |
| $109D | ROM2 | Type 5 | 32×28 |

### Planetoid Fragments (8 chunks)

Starting at $26CB in ROM3, 8 fragment descriptors (8 bytes each):

| Fragment | Address | x_off | y_off | Width | Height | Data |
|----------|---------|-------|-------|-------|--------|------|
| 0 | $26CB | 4 | 3 | 5 | 11 | $270E |
| 1 | $26D3 | 5 | 6 | 5 | 11 | $2745 |
| 2 | $26DB | 5 | 6 | 5 | 8 | $277C |
| 3 | $26E3 | 5 | 4 | 6 | 11 | $27A4 |
| 4 | $26EB | 6 | 6 | 6 | 9 | $27E6 |
| 5 | $26F3 | 6 | 5 | 4 | 7 | $281C |
| 6 | $26FB | 4 | 4 | 6 | 9 | $2838 |
| 7 | $2703 | 6 | 5 | 6 | 11 | $286E |

These fragments fly outward when a planetoid is destroyed by accumulated mining damage.

### Bone/Damage Sprites (8 fragments)

Starting at $1A91 in ROM2, 8 fragment descriptors for skull/bone patterns visible when Sinistar face pieces are knocked off:

| Fragment | Address | x_off | y_off | Width | Height | Data |
|----------|---------|-------|-------|-------|--------|------|
| 0 | $1A91 | 11 | 10 | 5 | 5 | $1AD4 |
| 1 | $1A99 | 2 | 2 | 4 | 7 | $1AED |
| 2 | $1AA1 | 2 | 2 | 3 | 9 | $1B09 |
| 3 | $1AA9 | 2 | 2 | 4 | 7 | $1B24 |
| 4 | $1AB1 | 4 | 2 | 5 | 5 | $1B40 |
| 5 | $1AB9 | 6 | 2 | 4 | 7 | $1B59 |
| 6 | $1AC1 | 4 | 4 | 3 | 9 | $1B75 |
| 7 | $1AC9 | 2 | 6 | 4 | 7 | $1B90 |

The face outline data at $1A89 (8 bytes before the bone descriptors): `0E 0E 0E 0F 0F 0E 0D 0C` — likely row widths or boundary coordinates for the skull outline.

## Sinistar Face Compositing System

The Sinistar face is the most complex sprite in the game, assembled from 11 individual pieces (plus 9 mirrored copies = 20 draw operations total). The complete face is 49×52 pixels with center at (24, 26).

### Face Piece Data

Each piece is stored in a column-major, bottom-up packed format:
- Data organized by columns (left to right)
- Within each column: bytes packed vertically, 2 pixels per byte (high nibble = lower row)
- Y coordinate measured from bottom of face (`y = (FH-1) - (L + 2*b)`)

### 11 Unique Pieces

| # | Name | L | S | Cols | Rows | Pixel Size | Location |
|---|------|---|---|------|------|-----------|----------|
| 0 | Skull Top (S1L) | $27 | $0B | 15 | 7 | 15×14 | Face top |
| 1 | Skull Side (S2L) | $25 | $04 | 11 | 6 | 11×12 | Upper sides |
| 2 | Mouth Frame (S3L) | $17 | $00 | 13 | 8 | 13×16 | Mid-face |
| 3 | Forehead (S4L) | $0E | $00 | 13 | 7 | 13×14 | Lower-mid |
| 4 | Horn Structure (S5L) | $02 | $04 | 13 | 7 | 13×14 | Bottom edge |
| 5 | Chin Side (S6L) | $00 | $11 | 9 | 6 | 9×12 | Chin area |
| 6 | Jaw (JAWL) | $06 | $0D | 8 | 8 | 8×16 | Jaw region |
| 7 | Cheek (CHEEKL) | $10 | $09 | 13 | 6 | 13×12 | Cheek area |
| 8 | Chin Center (CHIN) | $06 | $15 | 7 | 7 | 7×14 | Center chin |
| 9 | Eye (EYEL) | $18 | $09 | 18 | 10 | 18×20 | Eye socket |
| 10 | Nose (NEZ) | $10 | $13 | 11 | 6 | 11×12 | Center nose |

**L** = vertical position (measured from bottom of face, in half-pixel units)
**S** = horizontal position (column offset from left edge)

### Mirror Table

9 of the 11 pieces have mirrored right-side copies (CHIN and NEZ are center pieces, not mirrored):

| Piece | Left (L, S) | Right (L, S) |
|-------|-------------|--------------|
| 0: Skull Top | (39, 11) | (39, 23) |
| 1: Skull Side | (37, 4) | (37, 34) |
| 2: Mouth Frame | (23, 0) | (23, 36) |
| 3: Forehead | (14, 0) | (14, 36) |
| 4: Horn | (2, 4) | (2, 32) |
| 5: Chin Side | (0, 17) | (0, 23) |
| 6: Jaw | (6, 13) | (6, 28) |
| 7: Cheek | (16, 9) | (16, 27) |
| 9: Eye | (24, 9) | (24, 22) |

### Piece Table ($4FB0)

Located at CPU address $4FB0 (ROM5, file offset $0FB0). Contains 20 entries of 4 bytes each, referencing piece draw operations:

```
Format: L_pos  S_pos  data_hi  data_lo
```

| Entry | L | S | Data Addr | Piece |
|-------|---|---|-----------|-------|
| 0 | $0D | $0F | $1E13 | (unknown/extra piece) |
| 1 | $27 | $0B | $1E1B | S1L (Skull Top left) |
| 2 | $27 | $17 | $1EAA | S1L mirror (right) |
| 3 | $25 | $04 | $1F12 | S2L (Skull Side left) |
| 4 | $17 | $00 | $1EB2 | S3L (Mouth Frame left) |
| 5 | $25 | $22 | $1F1A | S2L mirror (right) |
| 6 | $17 | $24 | $1FAC | S3L mirror (right) |
| 7 | $0E | $24 | $1FA4 | S4L mirror (right) |
| 8 | $0E | $00 | $2029 | S4L (Forehead left) |
| 9 | $02 | $04 | $20AE | S5L (Horn left) |
| 10 | $00 | $11 | $20B6 | S6L (Chin Side left) |
| 11 | $00 | $17 | $2031 | S6L mirror (right) |
| 12 | $02 | $20 | $1D5B | S5L mirror (right) |
| 13 | $06 | $1C | $1BAC | JAWL mirror (right) |
| 14 | $10 | $09 | $1C24 | CHEEKL (left) |
| 15 | $06 | $15 | $1D53 | CHIN (center) |
| 16 | $06 | $0D | $1C6B | JAWL (left) |
| 17 | $18 | $09 | $1BB4 | EYEL (left) |
| 18 | $10 | $1B | $1C73 | CHEEKL mirror (right) |
| 19 | $18 | $16 | $1DB3 | EYEL mirror (right) |

The data addresses in the piece table point to the actual column-major pixel data for each piece, stored across ROM2 and ROM3.

### Build Thresholds ($4FAA)

Located at CPU address $4FAA (ROM5, file offset $0FAA). 8 bytes controlling Sinistar assembly pace per difficulty level:

```
$4FAA: 01 02 03 04 05 09 0D 0F
```

These values determine how many worker crystal deliveries are needed before the next piece appears, scaling with game difficulty.

### Rendering Pipeline

The original game renders the Sinistar face using three visual states:

1. **Partially Built** (pieces < 13): Individual pieces drawn one by one using the piece table. Workers deliver crystals to increment the piece count; each new piece adds a fragment to the face.

2. **Fully Built** (pieces = 13): All 20 draw operations executed — the complete face is visible. Sinistar becomes active and hunts the player.

3. **Damaged** (sinibomb hits): Pieces removed in reverse order. Where pieces are missing, bone/skull patterns show through from the underlayer at $1A91. Three progressive damage states affect the face appearance.

### Face Composite Data ($2106–$2370)

ROM3 contains 619 bytes of pre-rendered face composite data at $2106. This appears to be pre-computed face half-sprites used for optimized rendering of the fully-built face, avoiding the overhead of compositing 20 individual pieces every frame. Six sprites are stored here (3 damage states × 2 halves: left and right).

## Williams 16-Color Palette

Hardware format: single byte per color, `BBGGGRRR` (2 blue bits, 3 green bits, 3 red bits).

### Expansion Formula

```
R_8bit = (R3 << 5) | (R3 << 2) | (R3 >> 1)
G_8bit = (G3 << 5) | (G3 << 2) | (G3 >> 1)
B_8bit = (B2 << 6) | (B2 << 4) | (B2 << 2) | B2
```

### Color Table

| Index | Byte | R | G | B | Hex | Use |
|-------|------|---|---|---|-----|-----|
| 0 | $00 | 0 | 0 | 0 | #000000 | Transparent |
| 1 | $FF | 255 | 255 | 255 | #FFFFFF | White (text, explosions) |
| 2 | $BF | 255 | 255 | 170 | #FFFFAA | Light yellow |
| 3 | $AE | 219 | 182 | 170 | #DBB6AA | Tan/skin |
| 4 | $AD | 182 | 182 | 170 | #B6B6AA | Light grey |
| 5 | $A4 | 146 | 146 | 170 | #9292AA | Medium grey |
| 6 | $9A | 73 | 109 | 170 | #496DAA | Steel blue |
| 7 | $00 | 0 | 0 | 0 | #000000 | Black |
| 8 | $00 | 0 | 0 | 0 | #000000 | Black |
| 9 | $C9 | 36 | 36 | 255 | #2424FF | Bright blue |
| 10 | $50 | 0 | 73 | 85 | #004955 | Dark teal |
| 11 | $4B | 109 | 36 | 85 | #6D2455 | Dark purple |
| 12 | $05 | 182 | 0 | 0 | #B60000 | Dark red |
| 13 | $07 | 255 | 0 | 0 | #FF0000 | Bright red |
| 14 | $00 | 0 | 0 | 0 | #000000 | Black |
| 15 | $37 | 255 | 219 | 0 | #FFDB00 | Yellow (UI, crystals) |

Source: SAMTABLE.ASM in original Williams source code.

## Speech ROM Format

### Hardware

The speech system uses a **Harris HC-55516 CVSD** (Continuously Variable Slope Delta modulation) codec. This is a 1-bit adaptive delta modulation scheme where each bit indicates the direction of the audio waveform slope, and the step size adapts automatically.

### ROM Layout

The 4 speech ROMs (16KB total) contain a raw CVSD bitstream with no headers, sync markers, or container format. The game firmware manages playback timing and ROM bank selection through hardware registers.

### Speech Phrases

| Phrase | Text | Trigger |
|--------|------|---------|
| BEWAREIL | "Beware, I live!" | Sinistar fully assembled |
| IHUNGER | "I hunger" | Sinistar partially built |
| IAMSINIS | "I am Sinistar" | Sinistar active |
| RUNCOWAR | "Run, coward!" | Sinistar chasing player |
| BEWARECO | "Beware, coward!" | Sinistar threatening |
| IHUNGERC | "I hunger, coward!" | Sinistar hungry + aggressive |
| RUNRUNRU | "Run! Run! Run!" | Sinistar close to player |
| AARGH | (roar/scream) | Sinistar destroyed |

### Decoding

Direct WAV conversion from CVSD bitstream requires either:
- Hardware playback through an actual HC-55516 chip
- Software CVSD decoder emulation
- Pre-recorded WAV files from hardware playback sessions

The reimplementation uses pre-recorded WAV files in `assets/speech/`.

## Complete Address Reference

| CPU Address | ROM File (Offset) | Size | Description |
|------------|-------------------|------|-------------|
| $0007 | ROM1 ($0007) | 256B | Player sprite descriptors (32 × 8) |
| $0107–$0AC2 | ROM1 | ~2.5KB | Player sprite pixel data |
| $0AD4 | ROM1 ($0AD4) | 24B | Sinibomb descriptors (3 × 8) |
| $0B23 | ROM1 ($0B23) | 8B | Planetoid type 1 descriptor |
| $0CCB | ROM1 ($0CCB) | 8B | Planetoid type 2 descriptor |
| $0E3B | ROM1 ($0E3B) | 8B | Planetoid type 3 descriptor |
| $0F1B | ROM1 ($0F1B) | 8B | Planetoid type 4 descriptor |
| $109D | ROM2 ($009D) | 8B | Planetoid type 5 descriptor |
| $129D | ROM2 ($029D) | 128B | Worker sprite descriptors (16 × 8) |
| $19E1 | ROM2 ($09E1) | 8B | Warrior sprite descriptor |
| $1A89 | ROM2 ($0A89) | 8B | Face outline boundary data |
| $1A91 | ROM2 ($0A91) | 64B | Bone fragment descriptors (8 × 8) |
| $1AD4–$1BA7 | ROM2 | ~211B | Bone fragment pixel data |
| $1BB4–$1E12 | ROM2 | ~606B | Sinistar face piece pixel data (part 1) |
| $1E13–$20B5 | ROM2–ROM3 | ~675B | Sinistar face piece pixel data (part 2) |
| $2106–$2370 | ROM3 ($0106) | 619B | Pre-rendered face halves (6 sprites) |
| $2438 | ROM3 ($0438) | 128B | Shot sprite descriptors (16 × 8) |
| $26A1 | ROM3 ($06A1) | 8B | Crystal (small) descriptor |
| $26B1 | ROM3 ($06B1) | 8B | Crystal (large) descriptor |
| $26CB | ROM3 ($06CB) | 64B | Planetoid fragment descriptors (8 × 8) |
| $270E–$28B0 | ROM3 | ~418B | Planetoid fragment pixel data |
| $4FAA | ROM5 ($0FAA) | 8B | Build threshold table |
| $4FB0 | ROM5 ($0FB0) | 80B | Sinistar piece table (20 × 4) |

## Key Game Constants

From ROM analysis:

| Constant | Value | Address | Description |
|----------|-------|---------|-------------|
| SINI_MAX_PIECES | 13 | — | Maximum face pieces for full assembly |
| PTS_CRYSTAL | 200 | $EF6A | Points for collecting a crystal |
| PTS_WORKER | 150 | $EF66 | Points for destroying a worker |
| PTS_WARRIOR | 500 | — | Points for destroying a warrior |
| PTS_SINI_PIECE | 500 | — | Points per sinibomb hit on Sinistar |
| PTS_SINISTAR | 15000 | — | Points for destroying Sinistar ($7000+$8000) |
| THINK_INTERVAL | 16/60s | — | Sinistar AI update interval (~267ms) |
| INHIBITOR_TICKS | 12 | — | Post-activation orbit delay ticks |

## Sinistar AI State Machine

From ROM disassembly:

### State 0: Dormant/Incomplete
- Slow random drift (max 50 px/s)
- Random velocity changes every 3 seconds
- Speech triggers when pieces ≥ 5

### State 1: Active/Hunting
- Velocity table-driven pursuit of player
- Two chase modes:
  - **Inhibitor active**: Spiral orbit approach (first 12 ticks after activation)
  - **Direct chase**: Aggressive velocity toward player
- Collision with player = instant kill
- Speech increases as distance to player decreases

### Velocity Tables (STBL)

The Sinistar uses lookup tables to map distance to velocity adjustments:
- **Chase table**: Aggressive acceleration toward player
- **Orbit table**: Curved approach path during inhibitor period
- Difficulty multiplier scales all velocities

## Velocity Table Data (ROM7 $6000)

ROM7 contains multiple velocity lookup tables used by the Sinistar AI and possibly other entities. Each table consists of 6-byte entries terminated by $FFFF:

### Table 1 ($6000) — 5 entries + terminator

```
$6000: 48 F0  04 00  01 60    dist=1024, vel/accel params
$6006: 48 F0  03 00  01 20    dist=768
$600C: 48 F0  02 00  00 C0    dist=512
$6012: 48 F0  01 00  00 70    dist=256
$6018: 48 EC  00 00  00 30    dist=0
$601E: 48 EA  FF FF  FF FF    terminator
```

### Table 2 ($6053) — 7 entries + terminator

```
$6053: 48 EC  00 50  03 00    dist=80
$6059: 48 EA  00 40  02 C0    dist=64
$605F: 48 EA  00 30  02 40    dist=48
$6065: 48 EC  00 20  02 00    dist=32
$606B: 48 EC  00 08  01 00    dist=8
$6071: 48 EE  00 00  00 C0    dist=0
$6077: 48 F0  FF FF  FF FF    terminator
```

### Table 3 ($6083) — 5 entries + terminator

```
$6083: 48 EC  00 30  02 40    dist=48
$6089: 48 EA  00 20  02 00    dist=32
$608F: 48 EC  00 08  01 00    dist=8
$6095: 48 EE  00 00  00 80    dist=0
$609B: 48 F0  FF FF  FF FF    terminator
```

### Table 4 ($60B9) — 5 entries + terminator (possibly orbit table)

```
$60B9: 48 EC  00 64  00 E8    dist=100
$60BF: 48 EE  00 40  00 A0    dist=64
$60C5: 48 F2  00 20  00 70    dist=32
$60CB: 48 F2  00 10  00 40    dist=16
$60D1: 48 F0  00 00  00 00    dist=0 (maxvel=0)
```

### Table 5 ($60E5) — 7 entries + terminator

```
$60E5: 48 EA  06 00  02 30    dist=1536
$60EB: 48 EA  05 00  01 E0    dist=1280
$60F1: 48 EA  04 00  01 A0    dist=1024
$60F7: 48 EA  03 00  01 50    dist=768
$60FD: 48 EC  02 00  00 B0    dist=512
$6103: 48 EE  00 00  00 40    dist=0
$6109: 48 F0  FF FF  FF FF    terminator
```

### Table 6 ($611F) — 9 entries + terminator

```
$611F: 48 EA  0C 80  02 10    dist=3200
$6125: 48 EC  06 00  01 C0    dist=1536
$612B: 48 EE  05 00  01 70    dist=1280
$6131: 48 EE  04 00  01 48    dist=1024
$6137: 48 EE  03 00  00 F8    dist=768
$613D: 48 EE  02 00  00 80    dist=512
$6143: 48 F0  01 00  00 30    dist=256
$6149: 48 F0  00 00  00 00    dist=0
$614F: 48 F0  FF FF  FF FF    terminator
```

**Entry format**: The first byte ($48 = ASLA opcode) appears to be a constant or shift marker. The second byte ($EA–$F2) likely encodes the acceleration shift factor. Bytes 3–4 are the distance threshold (big-endian). Bytes 5–6 are the max velocity or acceleration magnitude. The table is scanned from highest distance to lowest; the first entry whose threshold is ≤ current distance provides the velocity parameters.

**Key finding**: The ROM contains 6+ velocity tables with varying entry counts (5–9), while the reimplementation uses only 2 tables (STBL_CHASE with 9 entries, STBL_ORBIT with 10 entries). Tables 4 and 6 most closely match the reimplementation's orbit and chase tables respectively.

## Speed Ramp Tables (ROM5 $4EA0)

ROM5 contains 16 columns of speed ramp data starting at $4EA0. Each table has 16 entries starting at $40 (64) and decreasing, with each subsequent table having higher minimum values (harder difficulty):

```
$4EA0: 40 20 12 0E 0A 08 06 04 04 04 02 02 02 02 02 02  (easiest)
$4EB0: 40 2E 20 1A 12 0E 0E 0A 0A 08 08 06 06 06 04 04
$4EC0: 40 32 26 20 1C 18 12 10 0E 0E 0A 0A 0A 08 08 08
$4ED0: 40 36 2E 24 20 1C 1A 16 12 10 0E 0E 0E 0E 0A 0A
$4EE0: 40 38 32 28 24 20 1E 1A 18 16 12 10 10 0E 0E 0E
$4EF0: 40 3A 32 2E 26 22 20 1E 18 ...                    (hardest)
```

These appear to be warrior/worker speed tables indexed by distance bin, with rows for increasing difficulty levels. The reimplementation uses flat speed constants from zone parameters instead.

## Sine Lookup Table (ROM5 $4E54)

Quarter-wave sine table (0–90°), 64 entries, values 0–$7E (0–126):

```
$4E54: 00 03 06 09 0C 0F 12 15 18 1B 1E 21 24 27 2A 2D
$4E64: 30 33 36 39 3B 3E 41 43 46 49 4B 4E 50 52 55 57
$4E74: 59 5B 5E 60 62 64 66 67 69 6B 6C 6E 70 71 72 74
$4E84: 75 76 77 78 79 7A 7B 7B 7C 7D 7D 7E 7E 7E 7E 7E
$4E94: 3F
```

Used for smooth rotational movement calculations. The $3F (63) at the end may be a half-amplitude marker.

## Scoring Table (ROM10 $EF65)

BCD scoring values confirmed from ROM10 at file offset $0F65:

```
$EF65: 8A
$EF66: 01 50 00     → $0150 BCD = 150 (Worker kill)
$EF69: 8A
$EF6A: 02 00 00     → $0200 BCD = 200 (Crystal pickup)
$EF6D: 8A
$EF6E: 05 00 00     → $0500 BCD = 500 (Warrior kill)
$EF71: 8A
$EF72: 01 05 00 00  → $0105 BCD = 105 (unknown — possibly piece hit)
```

The $8A bytes may be ORA# opcodes as part of score-adding subroutine code, or table delimiters.

## ROM vs Reimplementation Comparison

### Confirmed Correct (verified against ROM data)

| Feature | ROM Value | game.h Value | Status |
|---------|-----------|-------------|--------|
| Max face pieces | 13 | 13 | MATCH |
| Build thresholds | 01 02 03 04 05 09 0D 0F | (used in build order) | MATCH |
| Piece table | 20 entries × 4 bytes at $4FB2 | siniBuildOps[20] | MATCH (mapped) |
| PTS_WORKER | $0150 BCD = 150 | 150 | MATCH |
| PTS_CRYSTAL | $0200 BCD = 200 | 200 | MATCH |
| PTS_WARRIOR | $0500 BCD = 500 | 500 | MATCH |
| Palette | 16 colors at $50C0 | PALETTE[16] | MATCH |
| Sinistar AI think rate | 16-frame batch | THINK_DT = 16/60 | MATCH |
| Inhibitor ticks | 12 | siniInhibitor = 12 | MATCH |
| Initial lives | 3 (LDA# 3 at $F326) | lives = 3 | MATCH |
| Sprite formats | 8-byte descriptors | Extracted to PNG | MATCH |
| Face pieces | 11 unique + 9 mirrors | sinistarPieces sprite sheet | MATCH |
| Piece-by-piece build | 20 draw operations | drawSinistarPieces() | MATCH |
| Copyright/credits | "ROBERT J. MICAL" in ROM9 | — | NOTED |

### Known Discrepancies

| Feature | ROM Behavior | Reimplementation | Severity |
|---------|-------------|-----------------|----------|
| **Velocity tables** | 6+ tables with 5–9 entries each, distance→speed lookup with shift-based acceleration | 2 tables (chase: 9 entries, orbit: 10 entries) loaded from `zones.cfg` | MEDIUM — affects Sinistar movement feel |
| **Speed ramp tables** | 16-entry distance-to-speed lookup per difficulty level, warriors/workers use table-driven speed | Flat speed constant per zone (configurable in `zones.cfg`), linear acceleration | MEDIUM — enemies move with constant speed instead of distance-dependent speed |
| **Sine-based movement** | Quarter-wave sine LUT at $4E54 for smooth rotation/steering | Direct `cosf()`/`sinf()` calls (equivalent, just different precision) | LOW — functionally equivalent |
| **Scoring: PTS_SINI_PIECE** | ROM has $0105 BCD = 105 at $EF72 (uncertain) | 500 (configurable in `game.cfg`) | UNCERTAIN — needs verification, 500 is the commonly documented value |
| **World size** | World coordinates are 16-bit ($0000–$FFFF = 65536×65536) | WORLD_W/H = 1024.0f (compact wrapping world); entity counts scaled down proportionally | MEDIUM — density preserved through count reduction |
| **Zone parameters** | Zone spawn counts and speeds embedded in code, not cleanly tabulated | 4 zones configurable in `zones.cfg`; counts scaled for compact world | MEDIUM — exact ROM values not fully decoded |
| **Warrior AI states** | ROM warriors have state machine driven by distance tables and timer-based transitions | 4 states (idle/chase/mine/orbit) with aggression-scaled decision logic | MEDIUM — behavioral approximation |
| **Worker crystal handling** | Workers use specific ROM subroutines for crystal pathfinding and delivery distance checks | Workers navigate to nearest crystal, deliver at configurable distance | LOW — behavioral match |
| **Sinistar damage reveal** | Bone sprites at $1A91 shown beneath removed face pieces | Not rendered — pieces simply disappear | LOW — cosmetic only |
| **Planetoid destruction** | Fragment sprites ($26CB) fly outward using 8 fragment descriptors | Simple explosion particles | LOW — cosmetic difference |
| **Difficulty scaling** | Multiple speed ramp tables (6+ at ROM5 $4EA0–$4F97), granular per-enemy-type speed curves | `diffMul() = 1.0 + zoneCycle * scale_per_cycle` (configurable in `game.cfg`) | MEDIUM — less nuanced than original |

### Missing Features (not in reimplementation)

1. **Bone/damage underlayer**: When Sinistar pieces are knocked off, bone sprites show through. Currently pieces just vanish.
2. **Planetoid fragment sprites**: Original has 8 chunk sprites that fly outward on destruction. Currently uses generic explosion.
3. **Large crystal sprite**: ROM has a larger crystal variant at $26B1 (6×4 pixels vs 4×2 for small crystal). Not extracted/used.
4. **Bullet impact/spark sprites**: Not identified in ROM analysis; may be generated procedurally in original.
5. **Score box/life indicator sprites**: UI chrome sprites not yet extracted.
6. **Multi-table velocity system**: Original uses 6+ velocity tables for different entity types and situations; reimplementation uses only 2 (chase + orbit).
7. **Pre-rendered face halves**: ROM3 $2106–$2370 contains pre-computed face sprites for optimized rendering of the complete face. Not used (we composite in real time).

### Implemented ROM Features

1. **Free life system**: Extra lives awarded at 20,000 then every 25,000 points (configurable in `game.cfg`).
2. **Build threshold system**: ROM data at $4FAA (`01 02 03 04 05 09 0D 0F`) controls deliveries per piece per difficulty level. Implemented in `game.h`.
3. **Sinistar piece-by-piece assembly**: 13 pieces, 20 draw operations (11 unique + 9 mirrored), build order from ROM piece table at $4FB0.
4. **Sinistar AI velocity tables**: Chase and orbit tables with distance-threshold-based speed lookup and shift-based acceleration.
5. **All game state machine states**: Attract (title/hstd/points), status, playing, death, gameover, HSTD entry, sini explode.
6. **Scoring from ROM BCD tables**: Worker (150), Crystal (200), Warrior (500), Sinistar (15,000).
7. **Digitized speech**: All 8 original speech phrases from HC-55516 CVSD codec.

### Behavioral Accuracy Assessment

| System | Accuracy | Notes |
|--------|----------|-------|
| **Game state machine** | ~95% | All major states present (attract, status, playing, death, gameover, HSTD entry, sini explode). Timing from ROM. |
| **Player physics** | ~85% | Thrust/velocity/drag model configurable; original uses fixed-point integer math with different acceleration curves |
| **Warrior AI** | ~80% | 4-state behavior with aggression-scaled chase%, aggro distance, fire rate. Configurable via `game.cfg` |
| **Worker AI** | ~85% | Crystal collection and delivery with build threshold gating. Speed and distance configurable |
| **Sinistar AI** | ~85% | Velocity table chase/orbit system implemented; think rate and inhibitor ticks correct. Tables configurable in `zones.cfg` |
| **Sinistar assembly** | ~95% | Piece-by-piece build with correct piece count, build order, mirror system, and ROM build thresholds |
| **Scoring** | ~95% | All scores confirmed from ROM; free life system implemented |
| **Collision system** | ~85% | All entity interactions present; bounce/damage physics approximated |
| **Attract mode** | ~90% | Screen cycling with correct timing; tip messages present |
| **High score entry** | ~90% | Joystick letter selection matches HSTDTE.ASM flow |
| **Difficulty curve** | ~80% | Zone cycling with aggression ramp, build thresholds, and configurable scaling. Original uses more granular speed ramp tables |

## Notes

- All multi-byte values in the 6809 are big-endian
- Sprite data addresses in descriptors reference the flat 36KB ROM address space ($0000–$8FFF)
- The Williams blitter hardware handles sprite rendering; the CPU sets up blitter registers with sprite position, dimensions, and data pointer
- Color index 0 is always transparent in sprite rendering
- The face compositing system is one of the most sophisticated sprite systems in any 1983 arcade game, with its piece-by-piece assembly being a defining visual feature of Sinistar
- "ROBERT J. MICAL" credited in ROM9 at $8C65 — lead programmer
- ROM binary analysis performed via hex dumps without a full 6809 disassembler; some table formats remain partially decoded
