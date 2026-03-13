// Sinistar - Native reimplementation using original ROM assets
// Original game: Williams Electronics, 1983
// ROM files required in source/ directory
//
// Build: make
// Run: ./sinistar

#include "assets.h"
#include "game.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>

static constexpr int SCALE = 3;

static Assets assets;
static Game game;
static uint32_t screenBuf[SCR_W * SCR_H];

static void setPixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H)
        screenBuf[y * SCR_W + x] = color;
}

static void fillScreen(uint32_t color) {
    for (int i = 0; i < SCR_W * SCR_H; i++) screenBuf[i] = color;
}

static void drawSprite(const Sprite& spr, int sx, int sy) {
    for (int py = 0; py < spr.h; py++) {
        int dy = sy + py;
        if (dy < 0 || dy >= SCR_H) continue;
        for (int px = 0; px < spr.w; px++) {
            uint8_t c = spr.pixels[py * spr.w + px];
            if (c == 0) continue;
            int dx = sx + px;
            if (dx >= 0 && dx < SCR_W)
                screenBuf[dy * SCR_W + dx] = assets.palette[c];
        }
    }
}

static void drawSpriteCenter(const Sprite& spr, int sx, int sy) {
    drawSprite(spr, sx - spr.cx, sy - spr.cy);
}

static void drawChar(char ch, int x, int y, uint32_t color) {
    if ((unsigned char)ch > 127) return;
    const uint8_t* glyph = SINISTAR_FONT[(unsigned char)ch];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x40 >> col))
                setPixel(x + col, y + row, color);
        }
    }
}

static constexpr int CHAR_SPACING = 8; // FONT_W + 1 gap

static void drawText(const char* text, int x, int y, uint32_t color) {
    for (int i = 0; text[i]; i++)
        drawChar(text[i], x + i * CHAR_SPACING, y, color);
}

static void drawTextCenter(const char* text, int y, uint32_t color) {
    int len = (int)strlen(text);
    drawText(text, (SCR_W - len * CHAR_SPACING) / 2, y, color);
}

static void drawNumber(int num, int x, int y, uint32_t color) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    drawText(buf, x, y, color);
}

// Draw text at 2x or 3x scale for title screen
static void drawCharLarge(char ch, int x, int y, uint32_t color, int scale) {
    if ((unsigned char)ch > 127) return;
    const uint8_t* glyph = SINISTAR_FONT[(unsigned char)ch];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x40 >> col)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        setPixel(x + col*scale + sx, y + row*scale + sy, color);
            }
        }
    }
}

static void drawTextLarge(const char* text, int x, int y, uint32_t color, int scale) {
    for (int i = 0; text[i]; i++)
        drawCharLarge(text[i], x + i * CHAR_SPACING * scale, y, color, scale);
}

static void drawTextCenterLarge(const char* text, int y, uint32_t color, int scale) {
    int len = (int)strlen(text);
    int w = len * CHAR_SPACING * scale - scale;
    drawTextLarge(text, (SCR_W - w) / 2, y, color, scale);
}

static void drawRightAligned(const char* text, int x, int y, uint32_t color) {
    int len = (int)strlen(text);
    drawText(text, x - len * CHAR_SPACING, y, color);
}

static bool worldToScreen(Vec2 worldPos, Vec2 cam, int& sx, int& sy) {
    Vec2 d = wrapDelta(cam, worldPos);
    sx = (int)d.x; sy = (int)d.y;
    return (sx >= -64 && sx < SCR_W + 64 && sy >= -64 && sy < SCR_H + 64);
}

static int angleToFrame(float angle, int numFrames) {
    int f = (int)(angle / TAU * numFrames + 0.5f) % numFrames;
    if (f < 0) f += numFrames;
    return f;
}

// === CREDIT/START DISPLAY (DCREDITS from INITALL.ASM, ATTMSGS.ASM) ===

static void drawCredits() {
    uint32_t white = assets.palette[1];
    uint32_t yellow = assets.palette[15];
    int y = SCR_H - 18;

    if (game.credits > 0) {
        char cbuf[32];
        snprintf(cbuf, sizeof(cbuf), "CREDITS %d", game.credits);
        drawTextCenter(cbuf, y, white);
        drawTextCenter("PRESS START", y + 10, yellow);
    } else {
        // Blink "INSERT COIN" (from original: alternating visibility)
        static float blinkTimer = 0;
        blinkTimer += 0.016f;
        if (((int)(blinkTimer * 2)) % 2 == 0)
            drawTextCenter("INSERT COIN", y + 4, white);
    }
}

// === STARFIELD RENDERING ===

static void drawStarfield() {
    for (auto& s : game.stars) {
        float parallax = 0.3f + s.layer * 0.35f;
        Vec2 starScreen;
        starScreen.x = s.pos.x - game.camera.x * parallax;
        starScreen.y = s.pos.y - game.camera.y * parallax;
        while (starScreen.x < 0) starScreen.x += SCR_W * 3;
        while (starScreen.y < 0) starScreen.y += SCR_H * 3;
        int sx = (int)fmodf(starScreen.x, (float)SCR_W);
        int sy = (int)fmodf(starScreen.y, (float)SCR_H);
        uint32_t col = assets.palette[s.color];
        if (s.layer == 0) col = (col & 0xFFFFFF00) | 0x80;
        setPixel(sx, sy, col);
    }
}

// === GAMEPLAY ENTITY RENDERING ===

static void drawEntities() {
    auto drawEntity = [&](const Entity& e) {
        if (!e.active) return;
        int sx, sy;
        if (!worldToScreen(e.pos, game.camera, sx, sy)) return;

        switch (e.type) {
            case ENT_PLAYER: {
                int f = angleToFrame(e.angle, 32);
                drawSpriteCenter(assets.playerShip.frame(f), sx, sy);
                break;
            }
            case ENT_WARRIOR:
                drawSpriteCenter(assets.warrior.frame(0), sx, sy);
                break;
            case ENT_WORKER: {
                int f = angleToFrame(e.angle, assets.workerShip.count());
                drawSpriteCenter(assets.workerShip.frame(f), sx, sy);
                break;
            }
            case ENT_PLANETOID:
                drawSpriteCenter(assets.planetoid[e.subtype % 5], sx, sy);
                break;
            case ENT_CRYSTAL: {
                static const int crystalColors[] = {1, 15, 13, 2, 9};
                int ci = ((int)(e.timer * 8)) % 5;
                if (assets.crystal.w > 0 && assets.crystal.h > 0) {
                    const Sprite& spr = assets.crystal;
                    for (int py = 0; py < spr.h; py++) {
                        int dy = sy - spr.cy + py;
                        if (dy < 0 || dy >= SCR_H) continue;
                        for (int px = 0; px < spr.w; px++) {
                            uint8_t p = spr.pixels[py * spr.w + px];
                            if (p == 0) continue;
                            int dx = sx - spr.cx + px;
                            if (dx >= 0 && dx < SCR_W)
                                screenBuf[dy * SCR_W + dx] = assets.palette[crystalColors[ci]];
                        }
                    }
                } else {
                    uint32_t c = assets.palette[crystalColors[ci]];
                    setPixel(sx, sy-1, c);
                    setPixel(sx-1, sy, c); setPixel(sx, sy, c); setPixel(sx+1, sy, c);
                    setPixel(sx, sy+1, c);
                }
                break;
            }
            case ENT_SINIBOMB:
                if (assets.sinibomb.count() > 0)
                    drawSpriteCenter(assets.sinibomb.frame(e.frame), sx, sy);
                else {
                    uint32_t c = assets.palette[9];
                    for (int dy=-2;dy<=2;dy++) for(int dx=-2;dx<=2;dx++)
                        if (dx*dx+dy*dy<=5) setPixel(sx+dx,sy+dy,c);
                }
                break;
            case ENT_SINISTAR:
                drawSpriteCenter(assets.sinistarFace, sx, sy);
                if (game.sinistarPieces < 13) {
                    char buf[16]; snprintf(buf, sizeof(buf), "%d", game.sinistarPieces);
                    drawText(buf, sx - 4, sy - 28, assets.palette[15]);
                }
                break;
            case ENT_BULLET_P: {
                uint32_t c = assets.palette[1];
                setPixel(sx, sy, c); setPixel(sx+1, sy, c); setPixel(sx, sy+1, c);
                break;
            }
            case ENT_BULLET_W: {
                uint32_t c = assets.palette[13];
                setPixel(sx, sy, c); setPixel(sx+1, sy, c);
                break;
            }
            case ENT_EXPLOSION: {
                int colors[] = {1, 15, 13, 3};
                uint32_t c = assets.palette[colors[e.subtype % 4]];
                int size = (int)(e.timer * 6) + 1;
                for (int dy = -size; dy <= size; dy++)
                    for (int dx = -size; dx <= size; dx++)
                        if (dx*dx+dy*dy <= size*size) setPixel(sx+dx, sy+dy, c);
                break;
            }
            default: break;
        }
    };

    for (auto& e : game.ents) if (e.type == ENT_PLANETOID) drawEntity(e);
    for (auto& e : game.ents) if (e.type == ENT_CRYSTAL) drawEntity(e);
    for (auto& e : game.ents) if (e.type == ENT_BULLET_P || e.type == ENT_BULLET_W) drawEntity(e);
    for (auto& e : game.ents) if (e.type == ENT_SINIBOMB) drawEntity(e);
    for (auto& e : game.ents) if (e.type == ENT_WORKER) drawEntity(e);
    for (auto& e : game.ents) if (e.type == ENT_WARRIOR) drawEntity(e);
    for (auto& e : game.ents) if (e.type == ENT_SINISTAR) drawEntity(e);
    drawEntity(game.ents[0]);
    for (auto& e : game.ents) if (e.type == ENT_EXPLOSION) drawEntity(e);
}

// === HUD ===

static void drawHUD() {
    uint32_t white = assets.palette[1];
    uint32_t yellow = assets.palette[15];

    // Score (top left)
    drawNumber(game.score, 4, 4, white);
    // High score (top center)
    char hsbuf[32]; snprintf(hsbuf, sizeof(hsbuf), "HI %d", game.topScore());
    drawTextCenter(hsbuf, 4, yellow);
    // Wave/Zone (top right)
    char wavebuf[16]; snprintf(wavebuf, sizeof(wavebuf), "W%d", game.wave);
    drawText(wavebuf, SCR_W - 20, 4, white);
    // Lives (bottom left)
    for (int i = 0; i < game.lives - 1; i++) {
        int lx = 4 + i * 14;
        drawSpriteCenter(assets.playerShip.frame(0), lx + 6, SCR_H - 12);
    }
    // Sinibombs (bottom right)
    for (int i = 0; i < game.sinibombs; i++) {
        int bx = SCR_W - 8 - i * 6;
        setPixel(bx, SCR_H-8, assets.palette[9]);
        setPixel(bx+1, SCR_H-8, assets.palette[9]);
        setPixel(bx, SCR_H-7, assets.palette[9]);
        setPixel(bx+1, SCR_H-7, assets.palette[9]);
    }

    // Radar
    int rw = 50, rh = 50;
    int rx = SCR_W - rw - 4, ry = SCR_H - rh - 20;
    for (int x = rx; x < rx+rw; x++) { setPixel(x, ry, assets.palette[5]); setPixel(x, ry+rh-1, assets.palette[5]); }
    for (int y = ry; y < ry+rh; y++) { setPixel(rx, y, assets.palette[5]); setPixel(rx+rw-1, y, assets.palette[5]); }
    Vec2 pp = game.ents[0].pos;
    for (auto& e : game.ents) {
        if (!e.active || e.type == ENT_BULLET_P || e.type == ENT_BULLET_W ||
            e.type == ENT_EXPLOSION || e.type == ENT_NONE) continue;
        Vec2 d = wrapDelta(pp, e.pos);
        int rdx = rx + rw/2 + (int)(d.x / WORLD_W * rw * 4);
        int rdy = ry + rh/2 + (int)(d.y / WORLD_H * rh * 4);
        if (rdx < rx+1 || rdx >= rx+rw-1 || rdy < ry+1 || rdy >= ry+rh-1) continue;
        uint32_t c = assets.palette[5];
        switch (e.type) {
            case ENT_PLAYER:    c = assets.palette[9]; break;
            case ENT_WARRIOR:   c = assets.palette[13]; break;
            case ENT_WORKER:    c = assets.palette[15]; break;
            case ENT_PLANETOID: c = assets.palette[5]; break;
            case ENT_CRYSTAL:   c = assets.palette[2]; break;
            case ENT_SINISTAR:  c = assets.palette[1]; break;
            case ENT_SINIBOMB:  c = assets.palette[9]; break;
            default: break;
        }
        setPixel(rdx, rdy, c);
    }

    // Messages
    if (game.messageTimer > 0 && game.message)
        drawTextCenter(game.message, SCR_H / 2 - 3, yellow);
}

// === SCREEN-SPECIFIC RENDERING ===

// Marquee/title screen (from MARQUEE.ASM)
static void renderAttractTitle() {
    memset(screenBuf, 0, sizeof(screenBuf));
    drawStarfield();

    uint32_t red = assets.palette[13];
    uint32_t grey = assets.palette[5];
    uint32_t yellow = assets.palette[15];

    // "SINISTAR" in large text
    drawTextCenterLarge("SINISTAR", SCR_H / 4 - 10, red, 3);

    // Sinistar face below title
    drawSpriteCenter(assets.sinistarFace, SCR_W / 2, SCR_H / 4 + 40);

    // Copyright (from ROM: "COPYRIGHT 1983 WILLIAMS ELECTRONICS, INC.")
    drawTextCenter("WILLIAMS ELECTRONICS 1983", SCR_H / 4 + 72, grey);

    // Cycling instructional tips (from ATTMSGS.ASM)
    static const char* tips[] = {
        "BLAST CRYSTALS OFF PLANETOIDS",
        "COLLECT CRYSTALS FOR SINIBOMBS",
        "ONLY SINIBOMBS DESTROY SINISTAR",
        "MINE CRYSTALS TO MAKE SINIBOMBS",
    };
    drawTextCenter(tips[game.tipIndex], SCR_H - 50, yellow);

    // Controls hint
    drawTextCenter("ARROWS-MOVE SPACE-FIRE B-BOMB", SCR_H - 36, grey);

    drawCredits();
}

// High score table display (from ZZATTRAC.ASM ATTRACT routine)
static void renderAttractHstd() {
    memset(screenBuf, 0, sizeof(screenBuf));

    uint32_t white = assets.palette[1];
    uint32_t yellow = assets.palette[15];
    uint32_t red = assets.palette[13];
    uint32_t grey = assets.palette[5];

    drawTextCenter("HIGH SCORES", 16, red);

    // Draw line under header
    for (int x = SCR_W/2 - 50; x < SCR_W/2 + 50; x++)
        setPixel(x, 28, grey);

    for (int i = 0; i < NUM_HIGH_SCORES; i++) {
        int y = 38 + i * 22;
        uint32_t col = (i < 3) ? yellow : white;

        // Rank
        char rank[8]; snprintf(rank, sizeof(rank), "%2d.", i + 1);
        drawText(rank, 30, y, grey);

        // Initials
        drawText(game.highScores[i].initials, 60, y, col);

        // Score (right-aligned)
        char sbuf[16]; snprintf(sbuf, sizeof(sbuf), "%d", game.highScores[i].score);
        drawRightAligned(sbuf, SCR_W - 30, y, col);
    }

    drawCredits();
}

// Points reference screen (from ZPNTSCRN.ASM)
static void renderAttractPoints() {
    memset(screenBuf, 0, sizeof(screenBuf));

    uint32_t red = assets.palette[13];
    uint32_t yellow = assets.palette[15];
    uint32_t blue = assets.palette[9];

    drawTextCenter("POINT VALUES", 20, red);

    struct { const char* name; int pts; } entries[] = {
        {"WORKER",          PTS_WORKER},
        {"CRYSTAL",         PTS_CRYSTAL},
        {"WARRIOR",         PTS_WARRIOR},
        {"SINISTAR PIECE",  PTS_SINI_PIECE},
        {"SINISTAR",        PTS_SINISTAR},
    };

    for (int i = 0; i < 5; i++) {
        int y = 60 + i * 36;
        uint32_t nameCol = (i == 4) ? red : yellow; // Sinistar name in red
        drawText(entries[i].name, 20, y, nameCol);

        // Dots
        int nameLen = (int)strlen(entries[i].name);
        int dotsStart = 20 + nameLen * 6 + 6;
        int dotsEnd = SCR_W - 60;
        for (int x = dotsStart; x < dotsEnd; x += 4)
            setPixel(x, y + 3, assets.palette[5]);

        // Points value
        char pbuf[16]; snprintf(pbuf, sizeof(pbuf), "%d", entries[i].pts);
        drawRightAligned(pbuf, SCR_W - 20, y, blue);
    }

    drawCredits();
}

// Status screen between turns (from STATUS.ASM)
static void renderStatus() {
    memset(screenBuf, 0, sizeof(screenBuf));
    drawStarfield();

    uint32_t white = assets.palette[1];
    uint32_t red = assets.palette[13];
    uint32_t blue = assets.palette[9];
    uint32_t yellow = assets.palette[15];

    int y = 30;

    // Player announcement
    drawTextCenter("PLAYER 1", y, white);
    y += 20;

    if (game.justDestroyedSini) {
        // Post-Sinistar-destruction status (from STATUS.ASM congratulations path)
        drawTextCenter("CONGRATULATIONS", y, yellow);
        y += 16;
        drawTextCenter("YOU DEFEATED THE SINISTAR", y, white);
        y += 24;

        if (game.sinistarKills > 0) {
            char kbuf[48];
            snprintf(kbuf, sizeof(kbuf), "YOU HAVE SMASHED %d SINISTAR%s",
                     game.sinistarKills, game.sinistarKills != 1 ? "S" : "");
            drawTextCenter(kbuf, y, white);
            y += 20;
        }
    } else {
        // Normal status: bomb and piece counts (from STATUS.ASM)
        char bbuf[48];
        snprintf(bbuf, sizeof(bbuf), "YOU HAVE %d SINIBOMBS", game.sinibombs);
        drawTextCenter(bbuf, y, blue);
        y += 16;

        char pbuf[48];
        snprintf(pbuf, sizeof(pbuf), "SINISTAR HAS %d PIECES", game.sinistarPieces);
        drawTextCenter(pbuf, y, red);
        y += 20;

        // Mining instruction (from STATUS.ASM: shown when pieces > bombs)
        if (game.sinistarPieces > game.sinibombs) {
            drawTextCenter("MINE CRYSTALS TO", y, yellow);
            y += 12;
            drawTextCenter("MAKE SINIBOMBS", y, yellow);
            y += 20;
        }
    }

    // Sinistar face
    drawSpriteCenter(assets.sinistarFace, SCR_W / 2, y + 30);
    y += 70;

    // Zone announcement
    char zbuf[48];
    snprintf(zbuf, sizeof(zbuf), "%s %s ZONE",
             game.justDestroyedSini ? "ENTERING" : "NOW IN",
             ZONE_NAMES[game.zoneIndex()]);
    drawTextCenter(zbuf, y, white);
    y += 16;

    char wbuf[16]; snprintf(wbuf, sizeof(wbuf), "WAVE %d", game.wave);
    drawTextCenter(wbuf, y, blue);
}

// Game over screen (from ZGAMOVER.ASM)
static void renderGameOver() {
    // Show gameplay behind the text
    memset(screenBuf, 0, sizeof(screenBuf));
    drawStarfield();
    drawEntities();

    // "GAME OVER" in RED at screen center (from ZGAMOVER.ASM: PHRASE RED,35,GAME,OVER)
    drawTextCenterLarge("GAME OVER", SCR_H / 2 - 12, assets.palette[13], 2);

    // Score
    char sbuf[32]; snprintf(sbuf, sizeof(sbuf), "SCORE %d", game.score);
    drawTextCenter(sbuf, SCR_H / 2 + 16, assets.palette[1]);

    drawCredits();
}

// High score entry screen (from HSTDTE.ASM CENTRY routine)
static void renderHstdEntry() {
    memset(screenBuf, 0, sizeof(screenBuf));

    uint32_t white = assets.palette[1];
    uint32_t yellow = assets.palette[15];
    uint32_t blue = assets.palette[9];
    uint32_t cream = assets.palette[2];

    // Congratulations text (from CENTRY: BLUE shadow + YELLOW foreground)
    drawTextCenter("CONGRATULATIONS", 30, yellow);

    drawTextCenter("YOUR SCORE RANKS", 54, white);
    drawTextCenter("AMONG THE TOP 10", 66, white);

    // Score
    char sbuf[32]; snprintf(sbuf, sizeof(sbuf), "%d", game.score);
    drawTextCenter(sbuf, 86, yellow);

    drawTextCenter("ENTER YOUR INITIALS", 110, cream);

    // Draw initial entry boxes
    int bx = SCR_W / 2 - 30;
    int by = 140;
    for (int i = 0; i < 3; i++) {
        int x = bx + i * 20;
        // Box outline
        for (int px = 0; px < 14; px++) {
            setPixel(x + px, by - 2, blue);
            setPixel(x + px, by + 10, blue);
        }
        for (int py = -2; py <= 10; py++) {
            setPixel(x, by + py, blue);
            setPixel(x + 13, by + py, blue);
        }

        if (i < game.hstdPos) {
            // Already entered — show confirmed letter
            drawChar(game.hstdInitials[i], x + 4, by, cream);
        } else if (i == game.hstdPos && game.hstdPos < 3) {
            // Current position — show current letter with cursor blink
            char ch = 'A' + game.hstdLetter;
            bool blink = ((int)(game.hstdTimeout * 4)) % 2 == 0;
            drawChar(ch, x + 4, by, blink ? yellow : blue);
        } else {
            // Not yet reached — show underscore
            drawChar('_', x + 4, by, blue);
        }
    }

    drawTextCenter("JOYSTICK UP/DOWN", 180, cream);
    drawTextCenter("TO SELECT LETTER", 192, cream);
    drawTextCenter("FIRE TO ADVANCE", 210, cream);

    // Timeout indicator
    char tbuf[16]; snprintf(tbuf, sizeof(tbuf), "%d", (int)game.hstdTimeout);
    drawText(tbuf, SCR_W - 20, 30, blue);
}

// Gameplay screen with flash effects
static void renderGameplay() {
    // Background: flash during Sinistar explosion (SNXBFT from ZSNXQUE.ASM)
    if (game.gameState == GS_SINI_EXPLODE && game.flashTimer > 0) {
        // Alternate yellow/red every few frames
        uint32_t flashCol = ((int)(game.flashTimer * 8) % 2 == 0) ?
            assets.palette[15] : assets.palette[13];
        fillScreen(flashCol);
    } else {
        memset(screenBuf, 0, sizeof(screenBuf));
    }

    // Death flash (brief white screen)
    if (game.gameState == GS_DEATH && game.flashTimer > 0) {
        fillScreen(assets.palette[1]);
        return; // pure white flash, no entities visible briefly
    }

    drawStarfield();
    drawEntities();
    drawHUD();
}

// === MAIN RENDER DISPATCHER ===

static void render() {
    switch (game.gameState) {
    case GS_ATTRACT_TITLE:
        renderAttractTitle();
        break;
    case GS_ATTRACT_HSTD:
        renderAttractHstd();
        break;
    case GS_ATTRACT_POINTS:
        renderAttractPoints();
        break;
    case GS_STATUS:
        renderStatus();
        break;
    case GS_PLAYING:
    case GS_DEATH:
    case GS_SINI_EXPLODE:
        renderGameplay();
        break;
    case GS_GAMEOVER:
        renderGameOver();
        break;
    case GS_HSTD_ENTRY:
        renderHstdEntry();
        break;
    }
}

// === MAIN ===

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    const char* romDir = "source";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-roms") == 0 && i + 1 < argc) romDir = argv[++i];
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Sinistar - Native reimplementation using original ROM assets\n");
            printf("Usage: %s [-roms <dir>]\n", argv[0]);
            printf("\nControls:\n  Arrow keys / WASD - Move\n  Space - Fire\n");
            printf("  B - Sinibomb\n  5 - Insert coin\n  1 - Start\n  Escape - Quit\n");
            return 0;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }

    SDL_Window* win = SDL_CreateWindow("SINISTAR - Williams Electronics 1983",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCR_W * SCALE, SCR_H * SCALE, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "Window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_DestroyWindow(win); SDL_Quit(); return 1; }

    SDL_RenderSetLogicalSize(ren, SCR_W, SCR_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);

    printf("Loading ROMs from: %s/\n", romDir);
    if (!assets.load(romDir)) {
        fprintf(stderr, "Failed to load ROMs from %s/\n", romDir);
        SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win); SDL_Quit(); return 1;
    }
    assets.initAudio();

    game.init();

    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                printf("Controller: %s\n", SDL_GameControllerName(controller));
                break;
            }
        }
    }
    if (!controller) printf("No game controller found, using keyboard\n");

    bool running = true;
    Uint32 lastTick = SDL_GetTicks();
    int fpsCount = 0;
    Uint32 fpsTimer = lastTick;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            if (ev.type == SDL_CONTROLLERDEVICEADDED && !controller) {
                controller = SDL_GameControllerOpen(ev.cdevice.which);
                if (controller)
                    printf("Controller connected: %s\n", SDL_GameControllerName(controller));
            }
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && controller) {
                if (ev.cdevice.which == SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(controller))) {
                    printf("Controller disconnected\n");
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                }
            }
        }

        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        InputState input;
        if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) input.joyX = -1;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) input.joyX = 1;
        if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) input.joyY = 1;
        if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) input.joyY = -1;
        input.fire = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_LCTRL];
        input.bomb = keys[SDL_SCANCODE_B] || keys[SDL_SCANCODE_LSHIFT];
        input.start = keys[SDL_SCANCODE_1];
        input.coin = keys[SDL_SCANCODE_5];

        if (controller) {
            static constexpr float DEADZONE = 0.15f;
            float lx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            if (fabsf(lx) < DEADZONE) lx = 0;
            if (fabsf(ly) < DEADZONE) ly = 0;
            if (lx != 0 || ly != 0) { input.joyX = lx; input.joyY = -ly; }
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  input.joyX = -1;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) input.joyX = 1;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))    input.joyY = 1;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  input.joyY = -1;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) ||
                SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000)
                input.fire = true;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B) ||
                SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000)
                input.bomb = true;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) input.start = true;
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK))  input.coin = true;
        }

        float jl = sqrtf(input.joyX*input.joyX + input.joyY*input.joyY);
        if (jl > 1.0f) { input.joyX /= jl; input.joyY /= jl; }

        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTick) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastTick = now;

        game.update(dt, input);

        if (game.soundTrigger >= 0) assets.playSound(game.soundTrigger);
        if (game.speechTrigger >= 0) assets.playSpeech(game.speechTrigger);

        render();

        SDL_UpdateTexture(tex, nullptr, screenBuf, SCR_W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);

        fpsCount++;
        if (now - fpsTimer >= 1000) {
            char title[128];
            snprintf(title, sizeof(title), "SINISTAR | %d fps | Wave %d | Score %d",
                     fpsCount, game.wave, game.score);
            SDL_SetWindowTitle(win, title);
            fpsCount = 0; fpsTimer = now;
        }
    }

    if (controller) SDL_GameControllerClose(controller);
    assets.shutdown();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("Sinistar shut down.\n");
    return 0;
}
