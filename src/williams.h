// Williams Arcade Hardware Emulation - Sinistar variant
//
// Based on MAME Williams driver (williams.cpp, williams_v.cpp).
//
// Memory map (main 6809E CPU @ ~894.886 kHz):
//   $0000-$8FFF  Video RAM (when $C900 bit0=0) / ROM overlay (when bit0=1)
//   $9000-$BFFF  RAM (always accessible; $9000-$97FF is also video)
//   $C000-$C00F  Palette RAM (16 entries, BBGGGRRR format)
//   $C804-$C807  PIA 0 (widget board: joystick, buttons)
//   $C80C-$C80F  PIA 1 (sound board latch, diagnostic)
//   $C900        Write: VRAM/ROM select (bit0: 0=VRAM, 1=ROM at $0000-$8FFF)
//   $CA00-$CA07  Blitter registers (write to $CA00 triggers blit)
//   $CB00        Read: video counter (scanline); Write: ROM bank select ($D000)
//   $CC00-$CFFF  NVRAM (battery-backed)
//   $D000-$DFFF  ROM bank window (selected via $CB00 write)
//   $E000-$EFFF  ROM (sinistar.10, fixed)
//   $F000-$FFFF  ROM (sinistar.11, fixed, has 6809 vectors)
//
// ROM bank overlay ($C900=1):
//   $0000-$0FFF  sinistar.01
//   $1000-$1FFF  sinistar.02
//   $2000-$2FFF  sinistar.03
//   $3000-$3FFF  sinistar.04
//   $4000-$4FFF  sinistar.05
//   $5000-$5FFF  sinistar.06
//   $6000-$6FFF  sinistar.07
//   $7000-$7FFF  sinistar.08
//   $8000-$8FFF  sinistar.09
//
// $D000 bank window (selected via $CB00):
//   Bank 0: sinistar.01   Bank 1: sinistar.02  ... Bank 8: sinistar.09
//
// Video:
//   304x256 pixels, 4bpp (16 simultaneous colors from 256)
//   Column-major layout: address = (x/2)*256 + y
//   Each byte: high nibble = left pixel, low nibble = right pixel
//
// Blitter (custom DMA chip):
//   Copies rectangular blocks for sprite rendering
//   Supports transparency (skip zero nibbles) and solid fill
//
// Sound:
//   MC6808 sound CPU at ~894.75 kHz (separate board)
//   PIA 1 port A: sound command latch to sound CPU
//   HC-55516 CVSD speech decoder for Sinistar's voice

#pragma once
#include "cpu6809.h"
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// Williams hardware constants
static constexpr int WMS_SCREEN_W = 304;
static constexpr int WMS_SCREEN_H = 256;
static constexpr int WMS_ROM_SIZE = 0x1000; // 4KB per ROM
static constexpr int WMS_NUM_BANK_ROMS = 9; // sinistar.01-09

struct WilliamsHardware {
    CPU6809 cpu;

    // Main RAM (64KB address space; lower portion is also video RAM)
    uint8_t ram[0x10000];

    // ROM banks (sinistar.01-09, accessible via $C900 overlay and $D000 window)
    uint8_t romBank[WMS_NUM_BANK_ROMS][WMS_ROM_SIZE];

    // Fixed ROMs
    uint8_t romE000[WMS_ROM_SIZE]; // sinistar.10 at $E000-$EFFF
    uint8_t romF000[WMS_ROM_SIZE]; // sinistar.11 at $F000-$FFFF (has vectors)

    // Sound ROM
    uint8_t soundROM[WMS_ROM_SIZE]; // sinistar.snd

    // Speech ROMs (16KB total: ic7, ic5, ic6, ic4)
    uint8_t speechData[4 * WMS_ROM_SIZE];

    // Bank state
    bool romBankEnabled;    // $C900 bit 0: when true, ROM overlays $0000-$8FFF for reads
    int d000BankSelect;     // Which ROM bank appears at $D000-$DFFF

    // Palette: BBGGGRRR format per MAME paletteram_BBGGGRRR_w
    uint8_t paletteRAM[16];
    uint32_t paletteRGBA[16]; // Pre-computed RGBA for fast rendering

    // Blitter registers
    uint8_t blitReg[8];

    // PIA 0 ($C804-$C807): Widget board (joystick, buttons, coin)
    // PIA 1 ($C80C-$C80F): Sound board interface
    // Simplified PIA emulation - we track DDR and data registers
    struct PIA {
        uint8_t portA_data;
        uint8_t portA_ddr;
        uint8_t portA_ctrl;
        uint8_t portB_data;
        uint8_t portB_ddr;
        uint8_t portB_ctrl;
        // Input callbacks
        uint8_t portA_input;
        uint8_t portB_input;
    };
    PIA pia[2];

    // NVRAM
    uint8_t nvram[0x400]; // $CC00-$CFFF

    // Input state
    int16_t joystickX;   // -7 to +7 (49-way: 7 positions per axis)
    int16_t joystickY;
    bool fireButton;
    bool sinibombButton;
    bool startButton;
    bool coinButton;

    // Video
    int scanline;
    int frameCount;
    uint32_t screenBuffer[WMS_SCREEN_W * WMS_SCREEN_H];

    // Audio
    SDL_AudioDeviceID audioDev;
    SDL_mutex* audioMutex;

    // Sound channels (synthesized to match 6800 sound CPU output)
    struct SoundChannel {
        float freq, phase, volume, decay;
        int samplesLeft;
        int waveform; // 0=square, 1=noise, 2=triangle
    };
    static constexpr int NUM_CHANNELS = 8;
    SoundChannel channels[NUM_CHANNELS];

    // CVSD speech
    float speechBuffer[65536];
    int speechBufLen, speechBufPos;
    bool speechActive;

    // Debug
    bool debugTrace;
    int debugLogCount;

    void init() {
        memset(ram, 0, sizeof(ram));
        memset(romBank, 0xFF, sizeof(romBank));
        memset(romE000, 0xFF, sizeof(romE000));
        memset(romF000, 0xFF, sizeof(romF000));
        memset(soundROM, 0xFF, sizeof(soundROM));
        memset(speechData, 0, sizeof(speechData));
        memset(paletteRAM, 0, sizeof(paletteRAM));
        memset(paletteRGBA, 0, sizeof(paletteRGBA));
        memset(blitReg, 0, sizeof(blitReg));
        memset(nvram, 0xFF, sizeof(nvram));
        memset(screenBuffer, 0, sizeof(screenBuffer));
        memset(channels, 0, sizeof(channels));
        memset(speechBuffer, 0, sizeof(speechBuffer));
        memset(&pia, 0, sizeof(pia));

        romBankEnabled = false;
        d000BankSelect = 0;
        joystickX = joystickY = 0;
        fireButton = sinibombButton = startButton = coinButton = false;
        scanline = 0;
        frameCount = 0;
        audioDev = 0;
        audioMutex = nullptr;
        speechBufLen = speechBufPos = 0;
        speechActive = false;
        debugTrace = false;
        debugLogCount = 0;

        // Default PIA input values (active-low for most signals)
        pia[0].portA_input = 0xFF;
        pia[0].portB_input = 0xFF;
        pia[1].portA_input = 0xFF;
        pia[1].portB_input = 0xFF;

        // Default NVRAM to sensible values (free play)
        memset(nvram, 0xFF, sizeof(nvram));

        cpu.memRead = &memReadStatic;
        cpu.memWrite = &memWriteStatic;
        cpu.memCtx = this;
        cpu.init();
    }

    bool loadROMs(const char* romDir) {
        char path[512];
        FILE* f;

        // Load banked ROMs: sinistar.01 through sinistar.09
        for (int i = 0; i < WMS_NUM_BANK_ROMS; i++) {
            snprintf(path, sizeof(path), "%s/sinistar.%02d", romDir, i + 1);
            f = fopen(path, "rb");
            if (!f) { fprintf(stderr, "Missing ROM: %s\n", path); return false; }
            if (fread(romBank[i], 1, WMS_ROM_SIZE, f) != WMS_ROM_SIZE) {
                fprintf(stderr, "Short read: %s\n", path);
                fclose(f); return false;
            }
            fclose(f);
            printf("  Loaded sinistar.%02d -> bank %d ($%04X overlay / $D000 window)\n",
                   i + 1, i, i * WMS_ROM_SIZE);
        }

        // Fixed ROMs
        snprintf(path, sizeof(path), "%s/sinistar.10", romDir);
        f = fopen(path, "rb");
        if (!f) { fprintf(stderr, "Missing ROM: %s\n", path); return false; }
        if (fread(romE000, 1, WMS_ROM_SIZE, f) != WMS_ROM_SIZE) {
            fprintf(stderr, "Short read: sinistar.10\n"); fclose(f); return false;
        }
        fclose(f);
        printf("  Loaded sinistar.10 -> $E000-$EFFF (fixed)\n");

        snprintf(path, sizeof(path), "%s/sinistar.11", romDir);
        f = fopen(path, "rb");
        if (!f) { fprintf(stderr, "Missing ROM: %s\n", path); return false; }
        if (fread(romF000, 1, WMS_ROM_SIZE, f) != WMS_ROM_SIZE) {
            fprintf(stderr, "Short read: sinistar.11\n"); fclose(f); return false;
        }
        fclose(f);
        printf("  Loaded sinistar.11 -> $F000-$FFFF (fixed, vectors)\n");

        // Sound ROM
        snprintf(path, sizeof(path), "%s/sinistar.snd", romDir);
        f = fopen(path, "rb");
        if (f) {
            if (fread(soundROM, 1, WMS_ROM_SIZE, f) != WMS_ROM_SIZE)
                fprintf(stderr, "Short read: sinistar.snd\n");
            fclose(f);
            printf("  Loaded sinistar.snd\n");
        }

        // Speech ROMs (order: ic7, ic5, ic6, ic4 per MAME)
        const char* speechFiles[] = {"speech.ic7", "speech.ic5", "speech.ic6", "speech.ic4"};
        for (int i = 0; i < 4; i++) {
            snprintf(path, sizeof(path), "%s/%s", romDir, speechFiles[i]);
            f = fopen(path, "rb");
            if (f) {
                if (fread(&speechData[i * WMS_ROM_SIZE], 1, WMS_ROM_SIZE, f) != WMS_ROM_SIZE)
                    fprintf(stderr, "Short read: %s\n", speechFiles[i]);
                fclose(f);
                printf("  Loaded %s\n", speechFiles[i]);
            }
        }

        return true;
    }

    // ================================================================
    // Memory access
    // ================================================================

    static uint8_t memReadStatic(uint16_t addr, void* ctx) {
        return ((WilliamsHardware*)ctx)->memRead(addr);
    }
    static void memWriteStatic(uint16_t addr, uint8_t val, void* ctx) {
        ((WilliamsHardware*)ctx)->memWrite(addr, val);
    }

    uint8_t memRead(uint16_t addr) {
        // $0000-$8FFF: Video RAM or ROM overlay
        if (addr <= 0x8FFF) {
            if (romBankEnabled) {
                // ROM overlay: ROMs 01-09 mapped linearly
                int bank = addr >> 12; // 0-8
                if (bank < WMS_NUM_BANK_ROMS) {
                    return romBank[bank][addr & 0x0FFF];
                }
                return 0xFF;
            }
            return ram[addr]; // Video RAM
        }

        // $9000-$BFFF: Always RAM
        if (addr <= 0xBFFF) {
            return ram[addr];
        }

        // $C000-$C00F: Palette RAM (mirrored through $C3FF)
        if (addr >= 0xC000 && addr <= 0xC3FF) {
            return paletteRAM[addr & 0x0F];
        }

        // $C400-$C7FF: Gap / unmapped
        if (addr >= 0xC400 && addr <= 0xC7FF) {
            return 0xFF;
        }

        // $C800-$C803: Unmapped on Sinistar (PIA 0 is at $C804)
        if (addr >= 0xC800 && addr <= 0xC803) {
            return 0xFF;
        }

        // $C804-$C807: PIA 0 (widget board: buttons, joystick)
        if (addr >= 0xC804 && addr <= 0xC807) {
            return readPIA(0, addr & 0x03);
        }

        // $C808-$C80B: unused
        if (addr >= 0xC808 && addr <= 0xC80B) {
            return 0xFF;
        }

        // $C80C-$C80F: PIA 1 (sound board interface)
        if (addr >= 0xC80C && addr <= 0xC80F) {
            return readPIA(1, addr & 0x03);
        }

        // $C900: VRAM select (write-only typically; read returns open bus)
        if (addr == 0xC900) {
            return 0xFF;
        }

        // $CA00-$CA07: Blitter (write-only)
        if (addr >= 0xCA00 && addr <= 0xCA07) {
            return 0xFF;
        }

        // $CB00: Read = video counter (current scanline)
        if (addr == 0xCB00) {
            return (uint8_t)scanline;
        }

        // $CC00-$CFFF: NVRAM
        if (addr >= 0xCC00 && addr <= 0xCFFF) {
            return nvram[addr - 0xCC00];
        }

        // $D000-$DFFF: Banked ROM window
        if (addr >= 0xD000 && addr <= 0xDFFF) {
            int bank = d000BankSelect;
            if (bank < WMS_NUM_BANK_ROMS) {
                return romBank[bank][addr & 0x0FFF];
            }
            return 0xFF;
        }

        // $E000-$EFFF: sinistar.10 (fixed)
        if (addr >= 0xE000 && addr <= 0xEFFF) {
            return romE000[addr & 0x0FFF];
        }

        // $F000-$FFFF: sinistar.11 (fixed)
        if (addr >= 0xF000) {
            return romF000[addr & 0x0FFF];
        }

        return 0xFF;
    }

    void memWrite(uint16_t addr, uint8_t val) {
        // $0000-$BFFF: Always write to RAM (even when ROM is overlaid for reads)
        if (addr <= 0xBFFF) {
            ram[addr] = val;
            return;
        }

        // $C000-$C3FF: Palette RAM
        if (addr >= 0xC000 && addr <= 0xC3FF) {
            int idx = addr & 0x0F;
            paletteRAM[idx] = val;
            updatePalette(idx);
            return;
        }

        // $C800-$C803: Unmapped writes (ignored)
        if (addr >= 0xC800 && addr <= 0xC803) {
            return;
        }

        // $C804-$C807: PIA 0 (widget board)
        if (addr >= 0xC804 && addr <= 0xC807) {
            writePIA(0, addr & 0x03, val);
            return;
        }

        // $C80C-$C80F: PIA 1 (sound board)
        if (addr >= 0xC80C && addr <= 0xC80F) {
            writePIA(1, addr & 0x03, val);
            return;
        }

        // $C900: VRAM/ROM select + D000 bank select (Sinistar-specific)
        // Bit 0: ROM overlay on $0000-$8FFF (0=VRAM, 1=ROM)
        // Bits 2-4: ROM bank for $D000-$DFFF (0-7 = sinistar.01-08)
        if (addr == 0xC900) {
            romBankEnabled = (val & 0x01) != 0;
            d000BankSelect = (val >> 2) & 0x07;
            return;
        }

        // $CA00-$CA07: Blitter registers
        if (addr >= 0xCA00 && addr <= 0xCA07) {
            int reg = addr - 0xCA00;
            blitReg[reg] = val;
            if (reg == 0) {
                // Writing to register 0 triggers the blit
                executeBlitter();
            }
            return;
        }

        // $CB00: Read = video counter (handled in read path)
        // $CBFF: CMOS enable (allows writes to NVRAM)
        if (addr >= 0xCB00 && addr <= 0xCBFF) {
            // Watchdog/CMOS enable - acknowledged, no action needed
            return;
        }

        // $CC00-$CFFF: NVRAM
        if (addr >= 0xCC00 && addr <= 0xCFFF) {
            nvram[addr - 0xCC00] = val;
            return;
        }

        // Writes to ROM area are ignored
    }

    // ================================================================
    // PIA (6821) emulation
    // ================================================================

    uint8_t readPIA(int which, int reg) {
        PIA& p = pia[which];
        switch (reg) {
            case 0: // Port A data/DDR
                if (p.portA_ctrl & 0x04) {
                    // Data register: read input on DDR=0 bits, output on DDR=1 bits
                    return (p.portA_input & ~p.portA_ddr) | (p.portA_data & p.portA_ddr);
                } else {
                    return p.portA_ddr;
                }
            case 1: // Control A
                return p.portA_ctrl;
            case 2: // Port B data/DDR
                if (p.portB_ctrl & 0x04) {
                    return (p.portB_input & ~p.portB_ddr) | (p.portB_data & p.portB_ddr);
                } else {
                    return p.portB_ddr;
                }
            case 3: // Control B
                return p.portB_ctrl;
        }
        return 0xFF;
    }

    void writePIA(int which, int reg, uint8_t val) {
        PIA& p = pia[which];
        switch (reg) {
            case 0:
                if (p.portA_ctrl & 0x04) {
                    p.portA_data = val;
                    // PIA 1 port A output = sound command latch
                    if (which == 1) {
                        handleSoundCommand(val);
                    }
                } else {
                    p.portA_ddr = val;
                }
                break;
            case 1:
                p.portA_ctrl = val;
                break;
            case 2:
                if (p.portB_ctrl & 0x04) {
                    p.portB_data = val;
                } else {
                    p.portB_ddr = val;
                }
                break;
            case 3:
                p.portB_ctrl = val;
                break;
        }
    }

    // ================================================================
    // Palette: Williams BBGGGRRR format
    // ================================================================

    void updatePalette(int idx) {
        uint8_t val = paletteRAM[idx];
        // BBGGGRRR: bits 7-6=Blue(2), bits 5-3=Green(3), bits 2-0=Red(3)
        uint8_t r3 = val & 0x07;
        uint8_t g3 = (val >> 3) & 0x07;
        uint8_t b2 = (val >> 6) & 0x03;

        // Scale to 8-bit
        // 3-bit: 0->0, 1->36, 2->73, 3->109, 4->146, 5->182, 6->219, 7->255
        uint8_t r = (r3 << 5) | (r3 << 2) | (r3 >> 1);
        uint8_t g = (g3 << 5) | (g3 << 2) | (g3 >> 1);
        // 2-bit: 0->0, 1->85, 2->170, 3->255
        uint8_t b = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;

        paletteRGBA[idx] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
    }

    // ================================================================
    // Blitter (Williams custom DMA)
    //
    // Register layout (from MAME williams_v.cpp):
    //   $CA00: Control (write triggers blit)
    //   $CA01: Mask (transparency/solid fill color)
    //   $CA02: Source address high
    //   $CA03: Source address low
    //   $CA04: Destination address high
    //   $CA05: Destination address low
    //   $CA06: Width (in bytes, each = 2 pixels)
    //   $CA07: Height (in rows)
    //
    // Control bits:
    //   Bit 0: Solid fill (use mask as source instead of reading memory)
    //   Bit 1: Window enable (XOR with width for clipping)
    //   Bit 2: Slow mode (CPU halted during blit)
    //   Bit 3: Right nibble shift (shift source 4 bits right)
    // ================================================================

    void executeBlitter() {
        uint8_t control = blitReg[0];
        uint8_t mask    = blitReg[1];
        uint16_t src    = ((uint16_t)blitReg[2] << 8) | blitReg[3];
        uint16_t dst    = ((uint16_t)blitReg[4] << 8) | blitReg[5];
        int w           = blitReg[6];
        int h           = blitReg[7];

        bool solidFill   = (control & 0x01) != 0;
        // bool windowEn = (control & 0x02) != 0;
        bool slowMode    = (control & 0x04) != 0;
        bool nibbleShift = (control & 0x08) != 0;

        if (w == 0) w = 1;
        if (h == 0) h = 1;

        // Perform the blit
        // Source is read linearly (row by row, left to right)
        // Destination is column-major: x advances by 256, y advances by 1
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                uint8_t srcByte;
                if (solidFill) {
                    srcByte = mask;
                } else {
                    uint16_t sAddr = (uint16_t)(src + row * w + col);
                    srcByte = memRead(sAddr);
                }

                if (nibbleShift) {
                    // Shift right by one nibble
                    srcByte = (srcByte >> 4) | ((srcByte & 0x0F) << 4);
                }

                // Destination address: column-major
                // Each column in video RAM is 256 bytes apart
                uint16_t dAddr = (uint16_t)(dst + col * 256 + row);

                // Only write to video RAM area
                if (dAddr < 0x9800) {
                    if (mask != 0xFF) {
                        // Transparency: only write non-zero nibbles
                        uint8_t dstByte = ram[dAddr];
                        uint8_t hiSrc = srcByte & 0xF0;
                        uint8_t loSrc = srcByte & 0x0F;
                        if (hiSrc) dstByte = (dstByte & 0x0F) | hiSrc;
                        if (loSrc) dstByte = (dstByte & 0xF0) | loSrc;
                        ram[dAddr] = dstByte;
                    } else {
                        ram[dAddr] = srcByte;
                    }
                }
            }
        }

        // Blitter timing: approximately 1 cycle per byte
        int blitCycles = w * h;
        if (slowMode) {
            cpu.cycles += blitCycles;
        } else {
            // CPU can continue, but we still count some cycles
            cpu.cycles += 4; // startup overhead
        }
    }

    // ================================================================
    // Video: convert VRAM to screen buffer
    // ================================================================

    void renderFrame() {
        // Williams video: column-major, 4bpp packed
        // address = (x/2) * 256 + y
        // Each byte: high nibble = left pixel (even x), low nibble = right pixel (odd x)
        //
        // Screen is 304 x 256; that's 152 byte-columns of 256 rows each
        // VRAM range: $0000 to $97FF (152 * 256 = 38912 = $9800)

        for (int y = 0; y < WMS_SCREEN_H; y++) {
            for (int byteCol = 0; byteCol < 152; byteCol++) {
                uint16_t addr = (uint16_t)(byteCol * 256 + y);
                uint8_t pixels = ram[addr];

                int x = byteCol * 2;
                int pix0 = (pixels >> 4) & 0x0F; // left pixel
                int pix1 = pixels & 0x0F;         // right pixel

                int idx = y * WMS_SCREEN_W + x;
                screenBuffer[idx]     = paletteRGBA[pix0];
                screenBuffer[idx + 1] = paletteRGBA[pix1];
            }
        }
    }

    // ================================================================
    // Input mapping
    //
    // Sinistar uses a 49-way optical joystick (7 positions per axis)
    // that connects through the Williams widget board.
    //
    // PIA 0 port A reads:
    //   Bit 0: Fire button (active low)
    //   Bit 1: Sinibomb button (active low)
    //   Bit 2: 1P Start (active low)
    //   Bit 3: 2P Start (active low)
    //   Bit 4: Left coin (active low)
    //   Bit 5: Center coin (active low)
    //   Bit 6: Right coin (active low)
    //   Bit 7: Slam tilt (active low)
    //
    // PIA 0 port B reads:
    //   Joystick X analog value (0-255, center ~128)
    //
    // The 49-way joystick analog is read via PIA in a muxed fashion.
    // Simplified: we return analog values for the joystick axes.
    // ================================================================

    void updateInput(const uint8_t* keys) {
        joystickX = joystickY = 0;
        fireButton = false;
        sinibombButton = false;
        startButton = false;
        coinButton = false;

        // Joystick (49-way, 7 positions per axis: -3,-2,-1,0,1,2,3)
        // Map keyboard to analog range
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])  joystickX = -3;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) joystickX = 3;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W])    joystickY = -3;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S])  joystickY = 3;

        fireButton = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_LCTRL];
        sinibombButton = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_B];
        startButton = keys[SDL_SCANCODE_1];
        coinButton = keys[SDL_SCANCODE_5];

        // Build PIA 0 port A (active-low switches)
        uint8_t portA = 0xFF;
        if (fireButton)     portA &= ~0x01;
        if (sinibombButton) portA &= ~0x02;
        if (startButton)    portA &= ~0x04;
        if (coinButton)     portA &= ~0x10;
        pia[0].portA_input = portA;

        // PIA 0 port B: joystick analog value
        // 49-way: center=0x80, range from 0x00 to 0xFF
        // Map joystick X to 0-255 (we use this for one axis)
        pia[0].portB_input = (uint8_t)(128 + joystickX * 40);

        // PIA 1 inputs (typically diagnostic switches - all open)
        pia[1].portA_input = 0xFF;
        // For the joystick Y, we may need another read path
        // Williams 49-way uses multiplexed ADC; simplified here
        pia[1].portB_input = (uint8_t)(128 + joystickY * 40);
    }

    // ================================================================
    // Sound
    // ================================================================

    void handleSoundCommand(uint8_t cmd) {
        if (audioMutex) SDL_LockMutex(audioMutex);

        // The 6800 sound CPU interprets these commands.
        // Based on analysis of sinistar.snd ROM data tables:
        switch (cmd & 0x3F) {
            case 0x00: break; // silence
            case 0x01: playChannel(0, 880, 0.15f, 0.9997f, 80, 2); break;   // player shot
            case 0x02: playChannel(1, 120, 0.25f, 0.9994f, 250, 1); break;  // explosion
            case 0x03: // player death
                playChannel(1, 80, 0.3f, 0.9990f, 500, 1);
                playChannel(2, 200, 0.2f, 0.9993f, 400, 2);
                break;
            case 0x04: playChannel(3, 1200, 0.1f, 0.9990f, 100, 0); break;  // crystal
            case 0x05: playChannel(4, 300, 0.2f, 0.9992f, 350, 2); break;   // sinibomb
            case 0x06: // sinistar hit
                playChannel(1, 60, 0.35f, 0.9993f, 500, 1);
                playChannel(2, 40, 0.25f, 0.9996f, 700, 2);
                break;
            case 0x07: playChannel(3, 440, 0.1f, 0.9993f, 150, 0); break;   // build piece
            case 0x08: playChannel(5, 660, 0.12f, 0.9995f, 120, 0); break;  // warrior shot
        }

        // Speech triggers (upper bits of command)
        if (cmd & 0x80) {
            int speechIdx = (cmd >> 4) & 0x07;
            // Speech sample addresses in the concatenated speech ROM
            // These are approximate; real addresses from the speech pointer table
            static const int speechOffsets[] = {
                0x0000, 0x0800, 0x1000, 0x1800, 0x2000, 0x2800, 0x3000, 0x3800
            };
            static const int speechLengths[] = {
                0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800
            };
            if (speechIdx < 8) {
                decodeSpeech(speechOffsets[speechIdx], speechLengths[speechIdx]);
            }
        }

        if (audioMutex) SDL_UnlockMutex(audioMutex);
    }

    void playChannel(int ch, float freq, float vol, float decay, int durationMs, int waveform) {
        if (ch >= NUM_CHANNELS) return;
        channels[ch].freq = freq;
        channels[ch].phase = 0;
        channels[ch].volume = vol;
        channels[ch].decay = decay;
        channels[ch].samplesLeft = 44100 * durationMs / 1000;
        channels[ch].waveform = waveform;
    }

    // HC-55516 CVSD decoder
    void decodeSpeech(int offset, int length) {
        float integrator = 0;
        float slope = 0.5f;
        int lastBit = 0;
        int consecCount = 0;
        speechBufLen = 0;

        for (int i = 0; i < length && (offset + i) < (int)sizeof(speechData); i++) {
            uint8_t byte = speechData[offset + i];
            for (int bit = 7; bit >= 0; bit--) {
                int b = (byte >> bit) & 1;

                if (b == lastBit) {
                    consecCount++;
                    if (consecCount >= 3) {
                        slope *= 1.05f;
                        if (slope > 0.9f) slope = 0.9f;
                    }
                } else {
                    consecCount = 0;
                    slope *= 0.95f;
                    if (slope < 0.02f) slope = 0.02f;
                }

                integrator += b ? slope : -slope;
                integrator *= 0.999f;
                if (integrator > 1.0f) integrator = 1.0f;
                if (integrator < -1.0f) integrator = -1.0f;
                lastBit = b;

                // HC-55516 runs at ~10kHz, upsample ~4.4x to 44100
                for (int up = 0; up < 4; up++) {
                    if (speechBufLen < (int)(sizeof(speechBuffer)/sizeof(speechBuffer[0])))
                        speechBuffer[speechBufLen++] = integrator * 0.4f;
                }
            }
        }
        speechBufPos = 0;
        speechActive = true;
    }

    // Audio callback
    static void audioCallbackStatic(void* ud, uint8_t* stream, int len) {
        ((WilliamsHardware*)ud)->audioCallback(stream, len);
    }

    void audioCallback(uint8_t* stream, int len) {
        int16_t* out = (int16_t*)stream;
        int samples = len / sizeof(int16_t);
        static uint32_t noiseState = 12345;

        SDL_LockMutex(audioMutex);

        for (int i = 0; i < samples; i++) {
            float mix = 0;

            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                auto& c = channels[ch];
                if (c.samplesLeft <= 0) continue;
                float val = 0;
                switch (c.waveform) {
                    case 0: val = (fmodf(c.phase, 1.0f) < 0.5f) ? 1.0f : -1.0f; break;
                    case 1:
                        noiseState ^= noiseState << 13;
                        noiseState ^= noiseState >> 17;
                        noiseState ^= noiseState << 5;
                        val = ((float)(noiseState % 2000) / 1000.0f) - 1.0f;
                        break;
                    case 2: {
                        float t = fmodf(c.phase, 1.0f);
                        val = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
                        break;
                    }
                }
                mix += val * c.volume;
                c.phase += c.freq / 44100.0f;
                c.volume *= c.decay;
                c.samplesLeft--;
            }

            if (speechActive && speechBufPos < speechBufLen) {
                mix += speechBuffer[speechBufPos++];
                if (speechBufPos >= speechBufLen) speechActive = false;
            }

            if (mix > 1.0f) mix = 1.0f;
            if (mix < -1.0f) mix = -1.0f;
            out[i] = (int16_t)(mix * 16000);
        }

        SDL_UnlockMutex(audioMutex);
    }

    void initAudio() {
        audioMutex = SDL_CreateMutex();
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof(want));
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = audioCallbackStatic;
        want.userdata = this;
        audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audioDev > 0) SDL_PauseAudioDevice(audioDev, 0);
    }

    // ================================================================
    // Frame execution
    //
    // Williams hardware: 6809E at ~894.886 kHz, 60.096 Hz frame rate
    // Cycles per frame: ~14900
    // IRQ fires at VBLANK (start of vertical blanking)
    // ================================================================

    void runFrame() {
        // Williams hardware: 6809E at 894886 Hz, 60.096 Hz frame rate
        // Total cycles per frame: 894886 / 60.096 ≈ 14891
        // 256 scanlines per frame: ~58 cycles per scanline
        //
        // IRQ fires at VBLANK (scanline 0 / start of frame)
        // The video counter ($CB00) increments each scanline

        static constexpr int CYCLES_PER_FRAME = 14891;
        static constexpr int CYCLES_PER_SCANLINE = CYCLES_PER_FRAME / 256;

        int frameCyclesRun = 0;

        for (scanline = 0; scanline < 256; scanline++) {
            // Fire IRQ at VBLANK (scanline 0)
            if (scanline == 0) {
                cpu.irqPending = true;
            }

            int scanlineStart = frameCyclesRun;
            while ((frameCyclesRun - scanlineStart) < CYCLES_PER_SCANLINE) {
                if (cpu.waiting || cpu.halted) {
                    // CPU is waiting for interrupt - just burn the remaining cycles
                    int remaining = CYCLES_PER_SCANLINE - (frameCyclesRun - scanlineStart);
                    cpu.totalCycles += remaining;
                    frameCyclesRun += remaining;
                    break;
                }
                int before = cpu.totalCycles;
                cpu.execute();
                int elapsed = cpu.totalCycles - before;
                if (elapsed == 0) {
                    cpu.totalCycles++;
                    frameCyclesRun++;
                }
                frameCyclesRun += elapsed;
            }
        }

        frameCount++;
        renderFrame();
    }

    void shutdown() {
        if (audioDev > 0) { SDL_CloseAudioDevice(audioDev); audioDev = 0; }
        if (audioMutex) { SDL_DestroyMutex(audioMutex); audioMutex = nullptr; }
    }
};
