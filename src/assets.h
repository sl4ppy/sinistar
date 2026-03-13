// Sinistar Assets - Loads sprites from PNG sprite sheets and speech from WAV files
// All assets loaded at runtime from assets/ directory - editable by developers
#pragma once
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <cmath>

#include "stb_image.h"

// Original Williams hardware: 304x256 framebuffer, CRT rotated 270 -> portrait 256x304
static constexpr int SCR_W = 256;
static constexpr int SCR_H = 304;

struct Sprite {
    int w = 0, h = 0;   // pixel dimensions
    int cx = 0, cy = 0; // center offset
    std::vector<uint8_t> pixels; // w*h palette indices (0 = transparent)
};

struct SpriteSet {
    std::vector<Sprite> frames;
    const Sprite& frame(int i) const {
        return frames[((i % (int)frames.size()) + (int)frames.size()) % (int)frames.size()];
    }
    int count() const { return (int)frames.size(); }
};

struct Assets {
    SpriteSet playerShip;  // 32 rotation frames
    SpriteSet workerShip;  // 16 rotation frames
    SpriteSet warrior;     // 1+ frames
    Sprite planetoid[5];   // 5 types
    SpriteSet sinibomb;    // 3 animation frames
    SpriteSet shots;       // shot frames
    Sprite crystal;
    Sprite sinistarFace;
    SpriteSet sinistarPieces; // 11 individual face pieces for piece-by-piece rendering

    uint32_t palette[16] = {};
    // Palette as separate R,G,B for color matching
    uint8_t palR[16] = {}, palG[16] = {}, palB[16] = {};

    // Decoded speech (one big buffer, split into samples)
    std::vector<float> speechPCM;
    struct SpeechClip { int start, len; };
    std::vector<SpeechClip> speechClips;

    // Audio
    SDL_AudioDeviceID audioDev = 0;
    SDL_mutex* audioMutex = nullptr;
    struct Channel { float freq, phase, vol, decay, freqMul; int left; int wave; };
    static constexpr int NCHANS = 8;
    Channel ch[NCHANS] = {};
    int curSpeechClip = -1;
    int speechPos = 0;

    static constexpr int SPK_COUNT = 8;

    bool load(const char* assetDir = "assets") {
        initPalette();
        if (!loadAllSprites(assetDir)) return false;
        loadSpeechWAVs(assetDir);
        return true;
    }

    void initPalette() {
        // From original source (SAMTABLE.ASM), format BBGGGRRR
        // Bits 7-6=Blue(2), 5-3=Green(3), 2-0=Red(3)
        static const uint8_t pal[] = {
            0x00,0xFF,0xBF,0xAE,0xAD,0xA4,0x9A,0x00,
            0x00,0xC9,0x50,0x4B,0x05,0x07,0x00,0x37
        };
        for (int i = 0; i < 16; i++) {
            uint8_t v = pal[i];
            int r3 = v & 7, g3 = (v >> 3) & 7, b2 = (v >> 6) & 3;
            palR[i] = (r3 << 5) | (r3 << 2) | (r3 >> 1);
            palG[i] = (g3 << 5) | (g3 << 2) | (g3 >> 1);
            palB[i] = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;
            palette[i] = ((uint32_t)palR[i] << 24) | ((uint32_t)palG[i] << 16) |
                         ((uint32_t)palB[i] << 8) | 0xFF;
        }
    }

    // Map an RGBA pixel to the nearest palette index (0 if transparent)
    uint8_t matchColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
        if (a < 128) return 0; // transparent
        int bestIdx = 1;
        int bestDist = 999999;
        for (int i = 1; i < 16; i++) {
            int dr = (int)r - palR[i];
            int dg = (int)g - palG[i];
            int db = (int)b - palB[i];
            int dist = dr*dr + dg*dg + db*db;
            if (dist < bestDist) { bestDist = dist; bestIdx = i; }
        }
        return (uint8_t)bestIdx;
    }

    // Load a sprite sheet PNG + metadata file
    bool loadSpriteSheet(const char* pngPath, const char* metaPath, SpriteSet& set) {
        // Read metadata
        FILE* meta = fopen(metaPath, "r");
        if (!meta) { fprintf(stderr, "Missing: %s\n", metaPath); return false; }

        char line[256];
        int nframes = 0, cellW = 0, cellH = 0;
        // Skip comment lines, read header
        while (fgets(line, sizeof(line), meta)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            if (sscanf(line, "%d %d %d", &nframes, &cellW, &cellH) == 3) break;
        }
        struct FrameMeta { int w, h, cx, cy; };
        std::vector<FrameMeta> frameMeta(nframes);
        int fi = 0;
        while (fi < nframes && fgets(line, sizeof(line), meta)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            sscanf(line, "%d %d %d %d", &frameMeta[fi].w, &frameMeta[fi].h,
                   &frameMeta[fi].cx, &frameMeta[fi].cy);
            fi++;
        }
        fclose(meta);

        // Load PNG
        int imgW, imgH, channels;
        uint8_t* img = stbi_load(pngPath, &imgW, &imgH, &channels, 4);
        if (!img) { fprintf(stderr, "Cannot load: %s\n", pngPath); return false; }

        // Extract frames
        set.frames.resize(nframes);
        for (int f = 0; f < nframes; f++) {
            auto& fm = frameMeta[f];
            auto& spr = set.frames[f];
            spr.w = fm.w; spr.h = fm.h;
            spr.cx = fm.cx; spr.cy = fm.cy;
            spr.pixels.resize(fm.w * fm.h, 0);

            int ox = f * cellW;
            for (int y = 0; y < fm.h && y < cellH; y++) {
                for (int x = 0; x < fm.w && x < cellW; x++) {
                    int px = ox + x;
                    if (px >= imgW || y >= imgH) continue;
                    int pi = (y * imgW + px) * 4;
                    spr.pixels[y * fm.w + x] = matchColor(img[pi], img[pi+1], img[pi+2], img[pi+3]);
                }
            }
        }

        stbi_image_free(img);
        return true;
    }

    bool loadAllSprites(const char* assetDir) {
        char png[512], meta[512];
        auto path = [&](const char* name, const char* ext) {
            snprintf(png, sizeof(png), "%s/sprites/%s.png", assetDir, name);
            snprintf(meta, sizeof(meta), "%s/sprites/%s.meta", assetDir, name);
        };

        path("player", ""); if (!loadSpriteSheet(png, meta, playerShip)) return false;
        path("worker", ""); if (!loadSpriteSheet(png, meta, workerShip)) return false;
        path("warrior", ""); if (!loadSpriteSheet(png, meta, warrior)) return false;
        path("sinibomb", ""); if (!loadSpriteSheet(png, meta, sinibomb)) return false;
        path("shots", ""); if (!loadSpriteSheet(png, meta, shots)) return false;

        // Planetoids: stored as a 5-frame sheet
        SpriteSet planSet;
        path("planetoid", "");
        if (!loadSpriteSheet(png, meta, planSet)) return false;
        for (int i = 0; i < 5 && i < planSet.count(); i++)
            planetoid[i] = planSet.frames[i];

        // Crystal and Sinistar face: single-frame sheets
        SpriteSet crystalSet, faceSet;
        path("crystal", ""); if (!loadSpriteSheet(png, meta, crystalSet)) return false;
        if (crystalSet.count() > 0) crystal = crystalSet.frames[0];

        path("sinistar_face", ""); if (!loadSpriteSheet(png, meta, faceSet)) return false;
        if (faceSet.count() > 0) sinistarFace = faceSet.frames[0];

        // Load individual Sinistar face pieces (optional - falls back to full face)
        path("sinistar_pieces", "");
        if (!loadSpriteSheet(png, meta, sinistarPieces)) {
            printf("Note: sinistar_pieces not found, piece-by-piece rendering disabled\n");
        }

        printf("Loaded sprites: %d player, %d worker, %d warrior, 5 planetoid, %d shot, %d sbomb, 1 crystal, 1 face, %d pieces\n",
               playerShip.count(), workerShip.count(), warrior.count(),
               shots.count(), sinibomb.count(), sinistarPieces.count());
        return true;
    }

    void loadSpeechWAVs(const char* assetDir) {
        static const char* wavFiles[SPK_COUNT] = {
            "speech/bewareil.wav", "speech/ihunger.wav", "speech/iamsinis.wav",
            "speech/runcowar.wav", "speech/bewareco.wav", "speech/ihungerc.wav",
            "speech/runrunru.wav", "speech/aargh.wav"
        };
        speechPCM.clear();
        speechClips.clear();
        speechClips.resize(SPK_COUNT, {0, 0});
        int loaded = 0;
        for (int i = 0; i < SPK_COUNT; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", assetDir, wavFiles[i]);
            SDL_AudioSpec spec;
            uint8_t* buf = nullptr;
            uint32_t len = 0;
            if (!SDL_LoadWAV(path, &spec, &buf, &len)) {
                fprintf(stderr, "Speech missing: %s (%s)\n", path, SDL_GetError());
                continue;
            }
            int srcSamples = (int)len;
            bool is16bit = (spec.format == AUDIO_S16LSB || spec.format == AUDIO_S16MSB ||
                            spec.format == AUDIO_S16SYS);
            if (is16bit) srcSamples = (int)(len / 2);
            float ratio = 44100.0f / (float)spec.freq;
            int dstSamples = (int)(srcSamples * ratio) + 1;
            int startIdx = (int)speechPCM.size();
            speechPCM.resize(startIdx + dstSamples);
            for (int j = 0; j < dstSamples; j++) {
                float srcPos = j / ratio;
                int s0 = (int)srcPos;
                float frac = srcPos - (float)s0;
                int s1 = s0 + 1;
                if (s0 >= srcSamples) s0 = srcSamples - 1;
                if (s1 >= srcSamples) s1 = srcSamples - 1;
                float v0, v1;
                if (is16bit) {
                    v0 = ((int16_t*)buf)[s0] / 32768.0f;
                    v1 = ((int16_t*)buf)[s1] / 32768.0f;
                } else {
                    v0 = ((int)buf[s0] - 128) / 128.0f;
                    v1 = ((int)buf[s1] - 128) / 128.0f;
                }
                speechPCM[startIdx + j] = v0 + (v1 - v0) * frac;
            }
            speechClips[i] = {startIdx, dstSamples};
            SDL_FreeWAV(buf);
            loaded++;
        }
        printf("Loaded %d/%d speech clips from WAV files\n", loaded, SPK_COUNT);
    }

    void initAudio() {
        audioMutex = SDL_CreateMutex();
        SDL_AudioSpec want = {}, have;
        want.freq = 44100; want.format = AUDIO_S16SYS;
        want.channels = 1; want.samples = 1024;
        want.callback = audioCallbackStatic; want.userdata = this;
        audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audioDev > 0) SDL_PauseAudioDevice(audioDev, 0);
    }

    void shutdown() {
        if (audioDev > 0) { SDL_CloseAudioDevice(audioDev); audioDev = 0; }
        if (audioMutex) { SDL_DestroyMutex(audioMutex); audioMutex = nullptr; }
    }

    void playChannel(int c, float freq, float vol, float decay, int ms, int wave, float freqMul = 1.0f) {
        if (c < 0 || c >= NCHANS) return;
        ch[c] = {freq, 0, vol, decay, freqMul, 44100 * ms / 1000, wave};
    }

    void playSound(int id) {
        SDL_LockMutex(audioMutex);
        switch (id) {
            case 0: playChannel(0, 2200, 0.14f, 0.9970f, 35, 0, 0.9980f); break;
            case 1: playChannel(1, 180, 0.22f, 0.9993f, 250, 1);
                    playChannel(7, 60, 0.15f, 0.9988f, 350, 2, 0.9990f); break;
            case 2: playChannel(0, 800, 0.25f, 0.9994f, 500, 2, 0.9975f);
                    playChannel(1, 250, 0.25f, 0.9992f, 700, 1);
                    playChannel(7, 400, 0.18f, 0.9988f, 600, 0, 0.9960f); break;
            case 3: playChannel(3, 1400, 0.10f, 0.9980f, 60, 0, 1.0040f); break;
            case 4: playChannel(4, 150, 0.15f, 0.9994f, 400, 2, 1.0025f);
                    playChannel(5, 100, 0.10f, 0.9992f, 350, 1); break;
            case 5: playChannel(5, 80, 0.30f, 0.9990f, 400, 1);
                    playChannel(6, 50, 0.22f, 0.9992f, 500, 2, 0.9985f); break;
            case 6: playChannel(6, 1600, 0.08f, 0.9965f, 40, 0, 0.9970f); break;
            case 7: playChannel(7, 400, 0.12f, 0.9975f, 80, 2, 0.9950f); break;
        }
        SDL_UnlockMutex(audioMutex);
    }

    void playSpeech(int clip) {
        SDL_LockMutex(audioMutex);
        if (clip >= 0 && clip < (int)speechClips.size()) {
            curSpeechClip = clip;
            speechPos = 0;
        }
        SDL_UnlockMutex(audioMutex);
    }

    static void audioCallbackStatic(void* ud, uint8_t* stream, int len) {
        ((Assets*)ud)->audioCallback(stream, len);
    }

    void audioCallback(uint8_t* stream, int len) {
        int16_t* out = (int16_t*)stream;
        int samples = len / sizeof(int16_t);
        static uint32_t noise = 12345;
        SDL_LockMutex(audioMutex);
        for (int i = 0; i < samples; i++) {
            float mix = 0;
            for (int c = 0; c < NCHANS; c++) {
                auto& s = ch[c];
                if (s.left <= 0) continue;
                float v = 0;
                float t = fmodf(s.phase, 1.0f);
                switch (s.wave) {
                    case 0: v = t < 0.5f ? 1.0f : -1.0f; break;
                    case 1: noise ^= noise<<13; noise ^= noise>>17; noise ^= noise<<5;
                            v = ((float)(noise%2000)/1000.0f)-1.0f; break;
                    case 2: v = t < 0.5f ? 4*t-1 : 3-4*t; break;
                }
                mix += v * s.vol; s.phase += s.freq / 44100.0f;
                s.vol *= s.decay; s.freq *= s.freqMul; s.left--;
            }
            if (curSpeechClip >= 0 && curSpeechClip < (int)speechClips.size()) {
                auto& clip = speechClips[curSpeechClip];
                if (speechPos < clip.len) {
                    mix += speechPCM[clip.start + speechPos++];
                } else { curSpeechClip = -1; }
            }
            if (mix > 1.0f) mix = 1.0f;
            if (mix < -1.0f) mix = -1.0f;
            out[i] = (int16_t)(mix * 16000);
        }
        SDL_UnlockMutex(audioMutex);
    }
};

// Authentic Sinistar font - 7x8 pixel glyphs, chunky Williams arcade style
static constexpr int FONT_W = 7;
static constexpr int FONT_H = 8;
static inline const uint8_t (*getSinistarFont())[8] {
    static uint8_t font[128][8] = {};
    static bool inited = false;
    if (!inited) {
        inited = true;
        auto set = [&](int ch, uint8_t r0, uint8_t r1, uint8_t r2, uint8_t r3,
                        uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7) {
            font[ch][0]=r0; font[ch][1]=r1; font[ch][2]=r2; font[ch][3]=r3;
            font[ch][4]=r4; font[ch][5]=r5; font[ch][6]=r6; font[ch][7]=r7;
        };
        set(' ', 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
        set('!', 0x0C,0x0C,0x0C,0x04,0x04,0x00,0x0C,0x0C);
        set('.', 0x00,0x00,0x00,0x00,0x00,0x00,0x0C,0x0C);
        set(',', 0x00,0x00,0x00,0x00,0x00,0x0C,0x04,0x04);
        set(':', 0x0C,0x0C,0x00,0x00,0x00,0x00,0x0C,0x0C);
        set('-', 0x00,0x00,0x00,0x3C,0x0F,0x00,0x00,0x00);
        set('?', 0x1E,0x13,0x07,0x06,0x04,0x00,0x06,0x06);
        set('(', 0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00);
        set(')', 0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00);
        set('/', 0x01,0x02,0x04,0x04,0x08,0x10,0x20,0x40);
        set('=', 0x00,0x00,0x3F,0x00,0x3F,0x00,0x00,0x00);
        set('\'',0x0C,0x0C,0x08,0x00,0x00,0x00,0x00,0x00);
        set('_', 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3F);
        set('0', 0x1E,0x33,0x21,0x21,0x21,0x33,0x3F,0x1E);
        set('1', 0x06,0x0E,0x06,0x06,0x06,0x06,0x06,0x0F);
        set('2', 0x1E,0x33,0x33,0x07,0x0C,0x19,0x3F,0x3F);
        set('3', 0x1E,0x13,0x03,0x0E,0x03,0x23,0x3F,0x1E);
        set('4', 0x0E,0x1E,0x36,0x26,0x3F,0x06,0x06,0x0F);
        set('5', 0x3E,0x10,0x18,0x3E,0x03,0x23,0x3F,0x1E);
        set('6', 0x1E,0x30,0x20,0x3E,0x23,0x31,0x3F,0x1E);
        set('7', 0x3F,0x22,0x06,0x06,0x0C,0x0C,0x1C,0x1E);
        set('8', 0x1E,0x12,0x1E,0x33,0x21,0x33,0x3F,0x1E);
        set('9', 0x1E,0x33,0x33,0x1F,0x03,0x06,0x0C,0x18);
        set('A', 0x1C,0x0E,0x2A,0x3A,0x3F,0x33,0x23,0x22);
        set('B', 0x3E,0x12,0x1A,0x3E,0x13,0x11,0x1B,0x3F);
        set('C', 0x0E,0x1B,0x31,0x30,0x30,0x39,0x1F,0x0E);
        set('D', 0x3E,0x13,0x11,0x33,0x13,0x13,0x37,0x3E);
        set('E', 0x3F,0x11,0x18,0x3E,0x18,0x19,0x1F,0x3F);
        set('F', 0x3F,0x11,0x18,0x3E,0x18,0x18,0x18,0x3C);
        set('G', 0x0E,0x1B,0x31,0x30,0x33,0x39,0x1F,0x0E);
        set('H', 0x33,0x11,0x11,0x3F,0x17,0x33,0x33,0x3B);
        set('I', 0x0C,0x04,0x04,0x0C,0x0C,0x0C,0x0C,0x1C);
        set('J', 0x1F,0x06,0x07,0x06,0x26,0x26,0x3E,0x1C);
        set('K', 0x33,0x16,0x38,0x1C,0x16,0x32,0x33,0x3B);
        set('L', 0x30,0x10,0x10,0x30,0x18,0x18,0x19,0x3F);
        set('M', 0x67,0x63,0x77,0x5D,0x49,0x41,0x43,0x63);
        set('N', 0x33,0x32,0x3A,0x2E,0x26,0x26,0x22,0x33);
        set('O', 0x1E,0x33,0x21,0x21,0x23,0x33,0x3F,0x1E);
        set('P', 0x3C,0x12,0x13,0x19,0x3F,0x18,0x18,0x3C);
        set('Q', 0x1C,0x33,0x21,0x21,0x25,0x37,0x1E,0x02);
        set('R', 0x3C,0x13,0x19,0x1B,0x36,0x32,0x33,0x3B);
        set('S', 0x1E,0x3A,0x38,0x2E,0x07,0x23,0x3F,0x1C);
        set('T', 0x1F,0x3E,0x04,0x0C,0x0C,0x0C,0x1C,0x38);
        set('U', 0x33,0x21,0x21,0x31,0x31,0x31,0x3F,0x3E);
        set('V', 0x21,0x21,0x33,0x12,0x12,0x16,0x1E,0x0C);
        set('W', 0x43,0x41,0x49,0x5D,0x7F,0x77,0x63,0x41);
        set('X', 0x33,0x12,0x16,0x0C,0x0C,0x1E,0x32,0x33);
        set('Y', 0x33,0x13,0x13,0x1F,0x0E,0x06,0x16,0x1C);
        set('Z', 0x1F,0x13,0x03,0x06,0x0C,0x18,0x3B,0x3F);
    }
    return font;
}
#define SINISTAR_FONT (getSinistarFont())
