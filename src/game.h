// Sinistar Game Logic - Native reimplementation using original ROM assets
// State machine and flow based on original INITALL.ASM / ZZATTRAC.ASM / STATUS.ASM
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "config.h"

static constexpr float WORLD_W = 1024.0f;
static constexpr float WORLD_H = 1024.0f;
static constexpr float PI = 3.14159265f;
static constexpr float TAU = 2.0f * PI;
static constexpr int MAX_ENTS = 256;
static constexpr int NUM_STARS = 200;

struct Vec2 {
    float x = 0, y = 0;
    Vec2 operator+(Vec2 b) const { return {x+b.x, y+b.y}; }
    Vec2 operator-(Vec2 b) const { return {x-b.x, y-b.y}; }
    Vec2 operator*(float s) const { return {x*s, y*s}; }
    float len() const { return sqrtf(x*x + y*y); }
    float len2() const { return x*x + y*y; }
    Vec2 norm() const { float l = len(); return l > 0.001f ? Vec2{x/l, y/l} : Vec2{0,0}; }
};

static Vec2 wrapDelta(Vec2 a, Vec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    if (dx > WORLD_W/2) dx -= WORLD_W; else if (dx < -WORLD_W/2) dx += WORLD_W;
    if (dy > WORLD_H/2) dy -= WORLD_H; else if (dy < -WORLD_H/2) dy += WORLD_H;
    return {dx, dy};
}

static float wrapDist(Vec2 a, Vec2 b) { return wrapDelta(a, b).len(); }

static Vec2 wrapPos(Vec2 p) {
    while (p.x < 0) p.x += WORLD_W;
    while (p.x >= WORLD_W) p.x -= WORLD_W;
    while (p.y < 0) p.y += WORLD_H;
    while (p.y >= WORLD_H) p.y -= WORLD_H;
    return p;
}

enum EntType {
    ENT_NONE = 0, ENT_PLAYER, ENT_WARRIOR, ENT_WORKER,
    ENT_PLANETOID, ENT_CRYSTAL, ENT_SINIBOMB,
    ENT_SINISTAR, ENT_BULLET_P, ENT_BULLET_W, ENT_EXPLOSION
};

// Game states from original state machine (INITALL.ASM)
// IAMODE → attract, GAMEINI/TURNINI → status, INIVECT → playing
enum GameState {
    GS_ATTRACT_TITLE,     // Marquee/title screen (448 frames / 7.5s from ROM)
    GS_ATTRACT_HSTD,      // High score table (300 frames / 5s from ROM)
    GS_ATTRACT_POINTS,    // Points reference screen (420 frames / 7s from ROM)
    GS_STATUS,            // Between-turn status screen (4s)
    GS_PLAYING,           // Active gameplay (INIVECT)
    GS_DEATH,             // Player death sequence (120 frames / 2s from ROM)
    GS_GAMEOVER,          // Game over display (300 frames / 5s from ROM)
    GS_HSTD_ENTRY,        // High score initial entry (CENTRY)
    GS_SINI_EXPLODE,      // Sinistar destruction sequence (KABOOM, 3s)
};

struct HighScoreEntry {
    int score;
    char initials[4]; // 3 chars + null
};
static constexpr int NUM_HIGH_SCORES = 10;

struct Entity {
    Vec2 pos, vel;
    float angle = 0;
    EntType type = ENT_NONE;
    int health = 1;
    int state = 0;
    float timer = 0;
    float timer2 = 0;
    bool active = false;
    int subtype = 0;
    int frame = 0;
    float radius = 6;
    int crystals = 0;
};

struct Star { Vec2 pos; int color; int layer; };

static float siniAxisVel(float vel, float delta, const SpeedEntry* tbl, int n, float dm) {
    float dist = fabsf(delta);
    float maxSpd = tbl[n-1].maxSpeed;
    int sh = tbl[n-1].shift;
    for (int i = 0; i < n; i++) {
        if (dist >= tbl[i].distThresh) { maxSpd = tbl[i].maxSpeed; sh = tbl[i].shift; break; }
    }
    maxSpd *= dm;
    float desired = (delta > 0) ? maxSpd : -maxSpd;
    return vel + (desired - vel) / (float)(1 << sh);
}

struct InputState {
    float joyX = 0, joyY = 0;
    bool fire = false, bomb = false;
    bool start = false, coin = false;
};

struct Game {
    GameConfig cfg;
    Entity ents[MAX_ENTS];
    Star stars[NUM_STARS];
    Vec2 camera;

    // Scoring
    int score = 0;
    int nextFreeLife;
    HighScoreEntry highScores[NUM_HIGH_SCORES];

    // Player state
    int lives = 3, sinibombs = 0;
    int wave = 1;
    float playerAngle = 0;
    float fireCD = 0, bombCD = 0;

    // Sinistar state
    int sinistarPieces = 0;
    int siniDeliveries = 0;  // crystal deliveries toward next piece (ROM build threshold system)
    int siniInhibitor = 12;
    int siniStun = 0;
    float siniThinkAccum = 0;
    float siniOrbitDir = 1.0f;
    float sinistarSpeechTimer = 0;
    int sinistarKills = 0;

    // Game flow state machine
    GameState gameState = GS_ATTRACT_TITLE;
    float stateTimer = 0;
    int credits = 0;
    bool justDestroyedSini = false;

    // Attract mode
    int tipIndex = 0;
    float tipTimer = 0;

    // Messages
    float messageTimer = 0;
    const char* message = nullptr;

    // Audio triggers
    int speechTrigger = -1;
    int soundTrigger = -1;

    // High score entry (CENTRY from HSTDTE.ASM)
    int hstdPos = 0;
    int hstdLetter = 0;     // 0=A .. 25=Z
    char hstdInitials[4] = {' ',' ',' ',0}; // entered initials
    float hstdTimeout = 0;
    float hstdInputCD = 0;
    int hstdRank = -1;

    // Input edge detection
    bool prevCoin = false, prevStart = false;

    // Visual effects
    float flashTimer = 0;

    Entity& player() { return ents[0]; }
    const ZoneParams& zone() const { return cfg.zones[(wave - 1) % 4]; }
    int zoneCycle() const { return (wave - 1) / 4; }
    float diffMul() const { return 1.0f + zoneCycle() * cfg.diffScalePerCycle; }
    int zoneIndex() const { return (wave - 1) % 4; }

    // Award points with free life check (original awards at 20K, then every 25K)
    void addScore(int pts) {
        score += pts;
        while (score >= nextFreeLife) {
            lives++;
            soundTrigger = 3; // chime for extra life
            nextFreeLife += cfg.freeLifeEvery;
        }
    }

    int topScore() const {
        int best = 0;
        for (int i = 0; i < NUM_HIGH_SCORES; i++)
            if (highScores[i].score > best) best = highScores[i].score;
        return best;
    }

    int findFree() {
        for (int i = 1; i < MAX_ENTS; i++)
            if (!ents[i].active) return i;
        return -1;
    }

    int countType(EntType t) {
        int n = 0;
        for (auto& e : ents) if (e.active && e.type == t) n++;
        return n;
    }

    Entity* findType(EntType t) {
        for (auto& e : ents) if (e.active && e.type == t) return &e;
        return nullptr;
    }

    Vec2 randomPos() {
        return {(float)(rand() % (int)WORLD_W), (float)(rand() % (int)WORLD_H)};
    }

    // === INITIALIZATION (SYSTINI/INITALL) ===

    void initHighScores() {
        // Developer team initials as defaults (found "TDF" in ROM01 graphics data)
        static const struct { int s; const char n[4]; } defs[] = {
            {30000,"SAM"}, {25000,"RJM"}, {20000,"NJF"}, {18000,"RDW"},
            {15000,"JOH"}, {12000,"JAC"}, {10000,"KEN"}, {8000,"TDF"},
            {6000,"SIN"}, {5000,"WMS"},
        };
        for (int i = 0; i < NUM_HIGH_SCORES; i++) {
            highScores[i].score = defs[i].s;
            memcpy(highScores[i].initials, defs[i].n, 4);
        }
    }

    void init() {
        for (auto& e : ents) e = Entity{};
        initHighScores();
        score = 0; lives = 3; sinibombs = 0;
        nextFreeLife = cfg.freeLifeFirst;
        wave = 1; sinistarPieces = 0; sinistarKills = 0;
        gameState = GS_ATTRACT_TITLE;
        stateTimer = cfg.attractTitleTime;
        tipIndex = 0; tipTimer = 2.5f;
        fireCD = 0; bombCD = 0; credits = 0;
        message = nullptr; messageTimer = 0;
        prevCoin = false; prevStart = false;
        justDestroyedSini = false;
        flashTimer = 0;
        // Stars
        for (auto& s : stars) {
            s.pos = randomPos();
            s.layer = rand() % 3;
            static const int starCols[] = {1,2,4,5,1,5,4,2};
            s.color = starCols[rand() % 8];
        }
    }

    // GAMEINI + PLAYINI from INITALL.ASM
    void startGame() {
        for (auto& e : ents) e = Entity{};
        score = 0; lives = 3; sinibombs = 0;
        nextFreeLife = cfg.freeLifeFirst;
        sinistarPieces = 0; siniDeliveries = 0; wave = 1; sinistarKills = 0;
        fireCD = 0; bombCD = 0;
        justDestroyedSini = false;
        sinistarSpeechTimer = 0;
        spawnWave();
        // Go to STATUS screen (TURNINI)
        gameState = GS_STATUS;
        stateTimer = cfg.statusTime;
        soundTrigger = -1; speechTrigger = -1;
    }

    void spawnPlayer() {
        auto& p = player();
        p = Entity{};
        p.active = true; p.type = ENT_PLAYER;
        p.pos = {WORLD_W/2, WORLD_H/2};
        // Spawn away from Sinistar
        Entity* sini = findType(ENT_SINISTAR);
        if (sini) {
            for (int t = 0; t < 20; t++) {
                p.pos = randomPos();
                if (wrapDist(p.pos, sini->pos) > 350) break;
            }
        }
        p.vel = {0,0}; p.angle = 0; p.radius = 5; p.health = 1;
        playerAngle = 0;
        camera = p.pos - Vec2{SCR_W/2.0f, SCR_H/2.0f};
    }

    void spawnWave() {
        const auto& z = zone();
        float dm = diffMul();
        int numPlan = z.planetoids;
        for (int i = 0; i < numPlan; i++) {
            int idx = findFree(); if (idx < 0) break;
            auto& e = ents[idx];
            e.active = true; e.type = ENT_PLANETOID;
            e.pos = randomPos(); e.vel = {0,0};
            e.subtype = rand() % 5; e.health = 999;
            e.crystals = z.crystalsPerRock + rand() % 5;
            float sizes[] = {14,13,10,12,16};
            e.radius = sizes[e.subtype];
        }
        int numWarr = (int)(z.warriors * dm);
        if (numWarr > cfg.maxWarriors) numWarr = cfg.maxWarriors;
        for (int i = 0; i < numWarr; i++) spawnEnemy(ENT_WARRIOR);
        int numWork = z.workers;
        for (int i = 0; i < numWork; i++) spawnEnemy(ENT_WORKER);
        int si = findFree();
        if (si >= 0) {
            auto& e = ents[si];
            e.active = true; e.type = ENT_SINISTAR;
            e.pos = randomPos(); e.vel = {0,0};
            e.radius = 24; e.health = 13; e.state = 0;
            sinistarPieces = z.siniStartPieces + zoneCycle();
            if (sinistarPieces > 12) sinistarPieces = 12;
            siniDeliveries = 0;
            siniInhibitor = cfg.inhibitorTicks; siniStun = 0;
            siniThinkAccum = 0;
            siniOrbitDir = (rand() % 2) ? 1.0f : -1.0f;
        }
    }

    void spawnEnemy(EntType type) {
        int idx = findFree(); if (idx < 0) return;
        auto& e = ents[idx];
        e.active = true; e.type = type;
        Vec2 pp = player().pos;
        for (int t = 0; t < 10; t++) {
            e.pos = randomPos();
            if (wrapDist(e.pos, pp) > 300) break;
        }
        e.vel = {0,0}; e.angle = (float)(rand() % 628) / 100.0f;
        e.health = 1; e.state = 0; e.timer = 0;
        e.radius = type == ENT_WARRIOR ? 8 : 6;
    }

    void showMessage(const char* msg, float dur) {
        message = msg; messageTimer = dur;
    }

    // === MAIN UPDATE DISPATCHER ===

    void update(float dt, const InputState& input) {
        speechTrigger = -1;
        soundTrigger = -1;
        if (messageTimer > 0) messageTimer -= dt;

        // Edge-detected coin insert (works in all states)
        bool coinEdge = input.coin && !prevCoin;
        bool startEdge = input.start && !prevStart;
        prevCoin = input.coin;
        prevStart = input.start;

        if (coinEdge) credits++;

        switch (gameState) {
        case GS_ATTRACT_TITLE:
        case GS_ATTRACT_HSTD:
        case GS_ATTRACT_POINTS:
            updateAttract(dt, startEdge);
            break;
        case GS_STATUS:
            updateStatus(dt);
            break;
        case GS_PLAYING:
            updatePlaying(dt, input);
            break;
        case GS_DEATH:
            updateDeath(dt);
            break;
        case GS_GAMEOVER:
            updateGameOver(dt, startEdge);
            break;
        case GS_HSTD_ENTRY:
            updateHstdEntry(dt, input);
            break;
        case GS_SINI_EXPLODE:
            updateSiniExplode(dt);
            break;
        }
    }

    // === STATE HANDLERS ===

    // IAMODE attract loop: Title (7.5s) → HSTD (5s) → Points (7s) → repeat
    void updateAttract(float dt, bool startEdge) {
        stateTimer -= dt;

        // Cycle instructional tips on title screen (from ATTMSGS.ASM, ~2.5s each)
        if (gameState == GS_ATTRACT_TITLE) {
            tipTimer -= dt;
            if (tipTimer <= 0) { tipIndex = (tipIndex + 1) % 4; tipTimer = 2.5f; }
        }

        // START1 pressed with credits (from INITALL.ASM)
        if (startEdge && credits > 0) {
            credits--;
            startGame();
            return;
        }

        // Timer-driven screen transitions
        if (stateTimer <= 0) {
            switch (gameState) {
            case GS_ATTRACT_TITLE:
                gameState = GS_ATTRACT_HSTD;
                stateTimer = cfg.attractHstdTime;
                break;
            case GS_ATTRACT_HSTD:
                gameState = GS_ATTRACT_POINTS;
                stateTimer = cfg.attractPointsTime;
                break;
            case GS_ATTRACT_POINTS:
                gameState = GS_ATTRACT_TITLE;
                stateTimer = cfg.attractTitleTime;
                tipIndex = 0; tipTimer = 2.5f;
                break;
            default: break;
            }
        }
    }

    // TURNINI/STATUS screen between turns
    void updateStatus(float dt) {
        stateTimer -= dt;
        if (stateTimer <= 0) {
            // Transition to gameplay
            spawnPlayer();
            gameState = GS_PLAYING;
            justDestroyedSini = false;
        }
    }

    // INIVECT active gameplay
    void updatePlaying(float dt, const InputState& input) {
        updatePlayer(dt, input);
        updateGameEntities(dt);
        moveEntities(dt);
        checkCollisions();
        // Camera follows player
        Vec2 target = player().pos - Vec2{SCR_W/2.0f, SCR_H/2.0f};
        camera = camera + (wrapDelta(camera, target)) * std::min(1.0f, 8.0f * dt);
        camera = wrapPos(camera);
        // Respawn enemies to maintain zone counts
        if (countType(ENT_WARRIOR) < zone().warriors) spawnEnemy(ENT_WARRIOR);
        if (countType(ENT_WORKER) < zone().workers) spawnEnemy(ENT_WORKER);
        if (fireCD > 0) fireCD -= dt;
        if (bombCD > 0) bombCD -= dt;
    }

    // DEATH routine (from DEATH.ASM)
    void updateDeath(float dt) {
        updateGameEntities(dt);
        moveEntities(dt);
        flashTimer -= dt;
        stateTimer -= dt;
        if (stateTimer <= 0) {
            if (lives > 0) {
                gameState = GS_STATUS;
                stateTimer = cfg.statusTime;
            } else {
                gameState = GS_GAMEOVER;
                stateTimer = cfg.gameoverTime;
            }
        }
    }

    // TURN4S/ZGAMOVER game over display
    void updateGameOver(float dt, bool startEdge) {
        updateGameEntities(dt);
        moveEntities(dt);
        stateTimer -= dt;

        if (startEdge && credits > 0) {
            credits--;
            startGame();
            return;
        }

        if (stateTimer <= 0) {
            // HSTDENT: check if score qualifies for high score table
            hstdRank = checkHighScore();
            if (hstdRank >= 0) {
                gameState = GS_HSTD_ENTRY;
                hstdPos = 0; hstdLetter = 0;
                hstdInitials[0] = 'A'; hstdInitials[1] = ' ';
                hstdInitials[2] = ' '; hstdInitials[3] = 0;
                hstdTimeout = 30.0f;
                hstdInputCD = 0.5f;
            } else {
                // No high score - return to attract
                gameState = GS_ATTRACT_TITLE;
                stateTimer = cfg.attractTitleTime;
                tipIndex = 0; tipTimer = 2.5f;
            }
        }
    }

    // CENTRY high score initial entry (from HSTDTE.ASM)
    void updateHstdEntry(float dt, const InputState& input) {
        hstdTimeout -= dt;
        if (hstdInputCD > 0) hstdInputCD -= dt;

        if (hstdInputCD <= 0 && hstdPos < 3) {
            // Joystick up/down cycles letters (original uses joystick)
            if (input.joyY > 0.5f) {
                hstdLetter = (hstdLetter + 1) % 26;
                hstdInitials[hstdPos] = 'A' + hstdLetter;
                hstdInputCD = 0.15f;
            } else if (input.joyY < -0.5f) {
                hstdLetter = (hstdLetter + 25) % 26;
                hstdInitials[hstdPos] = 'A' + hstdLetter;
                hstdInputCD = 0.15f;
            }
            // Fire or right advances to next position
            if (input.fire || input.joyX > 0.5f) {
                hstdInitials[hstdPos] = 'A' + hstdLetter;
                hstdPos++;
                hstdLetter = 0;
                hstdInputCD = 0.3f;
            }
        }

        // Entry complete (3 initials entered or timeout)
        if (hstdPos >= 3 || hstdTimeout <= 0) {
            // Fill remaining positions if timeout
            for (int i = hstdPos; i < 3; i++)
                hstdInitials[i] = 'A' + hstdLetter;
            hstdInitials[3] = 0;
            insertHighScore(hstdRank, hstdInitials);
            gameState = GS_ATTRACT_HSTD;
            stateTimer = 5.0f;
        }
    }

    // KABOOM Sinistar destruction sequence (from ZSNXQUE.ASM)
    void updateSiniExplode(float dt) {
        updateGameEntities(dt);
        moveEntities(dt);
        flashTimer -= dt;
        stateTimer -= dt;
        if (stateTimer <= 0) {
            // Advance to next zone (WARP)
            wave++;
            justDestroyedSini = true;
            // Clear all non-player entities for new wave
            for (int i = 1; i < MAX_ENTS; i++) ents[i] = Entity{};
            spawnWave();
            gameState = GS_STATUS;
            stateTimer = cfg.statusTime;
        }
    }

    // === ENTITY SIMULATION ===

    void updateGameEntities(float dt) {
        for (int i = 1; i < MAX_ENTS; i++) {
            if (!ents[i].active) continue;
            switch (ents[i].type) {
                case ENT_WARRIOR:   updateWarrior(ents[i], dt); break;
                case ENT_WORKER:    updateWorker(ents[i], dt); break;
                case ENT_SINISTAR:  updateSinistar(ents[i], dt); break;
                case ENT_PLANETOID: updatePlanetoid(ents[i], dt); break;
                case ENT_SINIBOMB:  updateSinibomb(ents[i], dt); break;
                case ENT_BULLET_P: case ENT_BULLET_W: updateBullet(ents[i], dt); break;
                case ENT_CRYSTAL:   updateCrystal(ents[i], dt); break;
                case ENT_EXPLOSION: updateExplosion(ents[i], dt); break;
                default: break;
            }
        }
    }

    void moveEntities(float dt) {
        for (auto& e : ents) {
            if (!e.active) continue;
            e.pos = wrapPos(e.pos + e.vel * dt);
        }
    }

    void updatePlayer(float dt, const InputState& input) {
        auto& p = player();
        if (!p.active) return;
        float jx = input.joyX, jy = input.joyY;
        float jlen = sqrtf(jx*jx + jy*jy);
        if (jlen > 0.15f) {
            playerAngle = atan2f(-jy, jx);
            if (playerAngle < 0) playerAngle += TAU;
            float thr = cfg.thrust * jlen;
            p.vel.x += cosf(playerAngle) * thr * dt;
            p.vel.y += sinf(playerAngle) * thr * dt;
        }
        float speed = p.vel.len();
        if (speed > cfg.maxSpeed) p.vel = p.vel * (cfg.maxSpeed / speed);
        p.vel = p.vel * (1.0f - cfg.playerDrag * dt);
        p.angle = playerAngle;
        if (input.fire && fireCD <= 0) {
            int bi = findFree();
            if (bi >= 0) {
                auto& b = ents[bi];
                b.active = true; b.type = ENT_BULLET_P;
                b.pos = p.pos + Vec2{cosf(playerAngle)*10, sinf(playerAngle)*10};
                b.vel = Vec2{cosf(playerAngle)*cfg.bulletSpeed, sinf(playerAngle)*cfg.bulletSpeed} + p.vel * 0.3f;
                b.timer = 1.2f; b.radius = 3; b.health = 1;
                b.angle = playerAngle;
                fireCD = cfg.fireCD;
                soundTrigger = 0;
            }
        }
        if (input.bomb && bombCD <= 0 && sinibombs > 0) {
            Entity* sini = findType(ENT_SINISTAR);
            if (sini) {
                int bi = findFree();
                if (bi >= 0) {
                    auto& b = ents[bi];
                    b.active = true; b.type = ENT_SINIBOMB;
                    b.pos = p.pos;
                    b.vel = wrapDelta(p.pos, sini->pos).norm() * cfg.sinibombSpeed;
                    b.timer = 5.0f; b.radius = 4; b.health = 1;
                    sinibombs--;
                    bombCD = cfg.bombCD;
                    soundTrigger = 4;
                }
            }
        }
    }

    void updateWarrior(Entity& e, float dt) {
        float speed = zone().warSpeed * diffMul();
        if (speed > cfg.warMaxSpeed) speed = cfg.warMaxSpeed;
        float playerDist = wrapDist(e.pos, player().pos);
        float aggr = std::min(1.0f, zone().aggression + zoneCycle() * cfg.aggrScalePerCycle);

        // Aggro distance scales with aggression
        float aggroDist = cfg.aggroDistBase + aggr * cfg.aggroDistScale;
        // Fire cooldown scales with aggression
        float fireCD_base = cfg.fireCDBase - aggr * cfg.fireCDAggrScale;
        float fireCD_rand = cfg.fireCDRand;

        e.timer2 -= dt;
        if (e.timer2 <= 0) {
            e.timer2 = 5.0f + (float)(rand() % 500) / 100.0f;
            Entity* sini = findType(ENT_SINISTAR);
            bool siniComplete = sini && sini->state == 1;
            if (siniComplete) {
                e.state = 1; // all warriors chase when Sinistar is active
            } else {
                // Chase probability scales with aggression: 10-50%
                int chaseChance = (int)(1 + aggr * 4); // 1-5 out of 10
                int r = rand() % 10;
                if (r < chaseChance) e.state = 1;       // chase player
                else if (r < chaseChance + 4) e.state = 2; // mine planetoids
                else e.state = 3;                          // orbit sinistar
            }
            if (playerDist < aggroDist * 0.5f) e.state = 1; // forced chase only when very close
        }

        e.timer -= dt;

        switch (e.state) {
        case 0:
            e.vel = e.vel * (1.0f - 1.0f * dt);
            e.angle += 0.5f * dt;
            if (playerDist < aggroDist) e.state = 1;
            break;

        case 1: {
            Vec2 toPlayer = wrapDelta(e.pos, player().pos);
            Vec2 dir = toPlayer.norm();
            e.vel = e.vel + dir * (speed * 2.0f * dt);
            float sp = e.vel.len();
            if (sp > speed) e.vel = e.vel * (speed / sp);
            e.angle = atan2f(dir.y, dir.x);
            if (e.angle < 0) e.angle += TAU;
            float fireRange = aggroDist * 1.3f;
            if (e.timer <= 0 && playerDist < fireRange && playerDist > 30) {
                int bi = findFree();
                if (bi >= 0) {
                    auto& b = ents[bi];
                    b.active = true; b.type = ENT_BULLET_W;
                    b.pos = e.pos; b.vel = dir * cfg.warBulletSpeed;
                    b.timer = 1.5f; b.radius = 2; b.health = 1;
                    b.angle = e.angle;
                    soundTrigger = 6;
                }
                e.timer = fireCD_base + (float)(rand() % (int)(fireCD_rand * 100)) / 100.0f;
            }
            break;
        }

        case 2: {
            float bestDist = 999999;
            Entity* best = nullptr;
            for (auto& p : ents) {
                if (!p.active || p.type != ENT_PLANETOID || p.crystals <= 0) continue;
                float d = wrapDist(e.pos, p.pos);
                if (d < bestDist) { bestDist = d; best = &p; }
            }
            if (best) {
                Vec2 dir = wrapDelta(e.pos, best->pos).norm();
                if (bestDist > 60.0f) {
                    e.vel = e.vel + dir * (speed * 2.0f * dt);
                } else {
                    Vec2 perp = {-dir.y, dir.x};
                    e.vel = e.vel + perp * (speed * 1.0f * dt) + dir * (speed * 0.3f * dt);
                }
                float sp = e.vel.len();
                if (sp > speed * 0.8f) e.vel = e.vel * (speed * 0.8f / sp);
                e.angle = atan2f(dir.y, dir.x);
                if (e.angle < 0) e.angle += TAU;
                if (e.timer <= 0 && bestDist < 100.0f) {
                    if (rand() % 3 == 0) { // less trigger-happy when mining
                        int bi = findFree();
                        if (bi >= 0) {
                            auto& b = ents[bi];
                            b.active = true; b.type = ENT_BULLET_W;
                            b.pos = e.pos; b.vel = dir * cfg.warBulletSpeed;
                            b.timer = 0.5f; b.radius = 2; b.health = 1;
                            b.angle = e.angle;
                        }
                    }
                    e.timer = 1.2f + (float)(rand() % 150) / 100.0f;
                }
                // Only interrupt mining to chase if player is VERY close
                float mineInterruptDist = 60.0f + aggr * 80.0f; // 60-140px
                if (playerDist < mineInterruptDist) { e.state = 1; e.timer2 = 3.0f; }
            } else {
                e.state = 1;
            }
            break;
        }

        case 3: {
            Entity* sini = findType(ENT_SINISTAR);
            if (sini) {
                Vec2 toSini = wrapDelta(e.pos, sini->pos);
                float dist = toSini.len();
                Vec2 dir = toSini.norm();
                if (dist > 100.0f) {
                    e.vel = e.vel + dir * (speed * 2.0f * dt);
                } else {
                    Vec2 perp = {-dir.y, dir.x};
                    e.vel = e.vel + perp * (speed * 1.5f * dt);
                }
                float sp = e.vel.len();
                if (sp > speed * 0.7f) e.vel = e.vel * (speed * 0.7f / sp);
                e.angle = atan2f(dir.y, dir.x);
                if (e.angle < 0) e.angle += TAU;
                if (playerDist < aggroDist) {
                    Vec2 tp = wrapDelta(e.pos, player().pos).norm();
                    if (e.timer <= 0) {
                        int bi = findFree();
                        if (bi >= 0) {
                            auto& b = ents[bi];
                            b.active = true; b.type = ENT_BULLET_W;
                            b.pos = e.pos; b.vel = tp * cfg.warBulletSpeed;
                            b.timer = 1.5f; b.radius = 2; b.health = 1;
                            b.angle = atan2f(tp.y, tp.x);
                            soundTrigger = 6;
                        }
                        e.timer = fireCD_base + (float)(rand() % (int)(fireCD_rand * 100)) / 100.0f;
                    }
                    float orbitInterruptDist = aggroDist * 0.5f;
                    if (playerDist < orbitInterruptDist) { e.state = 1; e.timer2 = 3.0f; }
                }
            } else {
                e.state = 1;
            }
            break;
        }
        }
    }

    void updateWorker(Entity& e, float dt) {
        float speed = zone().wkrSpeed * diffMul();
        if (speed > cfg.wkrMaxSpeed) speed = cfg.wkrMaxSpeed;

        if (e.crystals > 0) {
            float deliverSpeed = speed * cfg.deliverSpeedMul;
            Entity* sini = findType(ENT_SINISTAR);
            if (sini) {
                Vec2 dir = wrapDelta(e.pos, sini->pos).norm();
                e.vel = e.vel + dir * (deliverSpeed * 3.0f * dt);
                float sp = e.vel.len();
                if (sp > deliverSpeed) e.vel = e.vel * (deliverSpeed / sp);
                e.angle = atan2f(dir.y, dir.x);
                if (e.angle < 0) e.angle += TAU;
                if (wrapDist(e.pos, sini->pos) < cfg.deliverDist) {
                    if (sinistarPieces < 13) {
                        // ROM build threshold system ($4FAA): multiple deliveries needed per piece
                        int level = std::min(zoneCycle(), 7);
                        int threshold = cfg.buildThresholds[level];
                        siniDeliveries++;
                        if (siniDeliveries >= threshold) {
                            siniDeliveries = 0;
                            sinistarPieces++;
                            if (sinistarPieces == 13) {
                                sini->state = 1;
                                siniInhibitor = cfg.inhibitorTicks; siniStun = 0;
                                siniThinkAccum = 0;
                                siniOrbitDir = (rand() % 2) ? 1.0f : -1.0f;
                                showMessage("SINISTAR LIVES!", 3.0f);
                                speechTrigger = 0; // "Beware, I live!"
                            }
                        }
                    }
                    e.active = false;
                }
            } else {
                e.crystals = 0;
                int ci = findFree();
                if (ci >= 0) {
                    auto& c = ents[ci];
                    c.active = true; c.type = ENT_CRYSTAL;
                    c.pos = e.pos;
                    c.vel = {(float)(rand()%40-20), (float)(rand()%40-20)};
                    c.timer = cfg.crystalTimer; c.radius = 3;
                }
            }
        } else {
            float bestCrysDist = 999999;
            Entity* bestCrys = nullptr;
            for (auto& p : ents) {
                if (!p.active || p.type != ENT_CRYSTAL) continue;
                float d = wrapDist(e.pos, p.pos);
                if (d < bestCrysDist) { bestCrysDist = d; bestCrys = &p; }
            }
            if (bestCrys) {
                Vec2 dir = wrapDelta(e.pos, bestCrys->pos).norm();
                e.vel = e.vel + dir * (speed * 3.0f * dt);
                float sp = e.vel.len();
                if (sp > speed * 1.2f) e.vel = e.vel * (speed * 1.2f / sp);
                e.angle = atan2f(dir.y, dir.x);
                if (e.angle < 0) e.angle += TAU;
            } else {
                Entity* orbitTarget = nullptr;
                float bestWDist = 999999;
                for (auto& p : ents) {
                    if (!p.active || p.type != ENT_WARRIOR || p.state != 2) continue;
                    float d = wrapDist(e.pos, p.pos);
                    if (d < bestWDist) { bestWDist = d; orbitTarget = &p; }
                }
                Vec2 target = (orbitTarget && bestWDist < 1000.0f) ?
                    orbitTarget->pos : player().pos;
                Vec2 toTarget = wrapDelta(e.pos, target);
                float dist = toTarget.len();
                Vec2 dir = toTarget.norm();
                Vec2 perp = {-dir.y, dir.x};
                if (dist > 150.0f) {
                    e.vel = e.vel + dir * (speed * 2.0f * dt);
                } else {
                    e.vel = e.vel + perp * (speed * 1.5f * dt) + dir * (speed * 0.3f * dt);
                }
                float sp = e.vel.len();
                if (sp > speed * 0.8f) e.vel = e.vel * (speed * 0.8f / sp);
                e.angle = atan2f(toTarget.y, toTarget.x);
                if (e.angle < 0) e.angle += TAU;
            }
        }
    }

    void updateSinistar(Entity& e, float dt) {
        sinistarSpeechTimer -= dt;
        if (e.state == 0) {
            e.timer2 += dt;
            if (e.timer2 > 3.0f) {
                e.timer2 = 0;
                float a = (float)(rand() % 628) / 100.0f;
                e.vel = e.vel + Vec2{cosf(a) * 30.0f, sinf(a) * 30.0f};
            }
            float sp = e.vel.len();
            if (sp > cfg.dormantMaxSpeed) e.vel = e.vel * (cfg.dormantMaxSpeed / sp);
            e.vel = e.vel * (1.0f - 0.3f * dt);
            e.angle += 0.15f * dt;
            if (e.angle > TAU) e.angle -= TAU;
            if (sinistarSpeechTimer <= 0 && sinistarPieces >= 5) {
                int clips[] = {1, 2, 4, 1};
                speechTrigger = clips[rand() % 4];
                sinistarSpeechTimer = 6.0f + (float)(rand() % 500) / 100.0f;
            }
            return;
        }

        siniThinkAccum += dt;

        while (siniThinkAccum >= cfg.thinkInterval) {
            siniThinkAccum -= cfg.thinkInterval;
            if (siniStun > 0) {
                e.vel = e.vel * 0.5f;
                siniStun--;
                continue;
            }
            Vec2 delta = wrapDelta(e.pos, player().pos);
            float dm = diffMul();
            if (siniInhibitor > 0) {
                siniInhibitor--;
                float orbitAngle = (PI / 2.0f) * ((float)(siniInhibitor + 1) / 12.0f);
                float cs = cosf(orbitAngle * siniOrbitDir);
                float sn = sinf(orbitAngle * siniOrbitDir);
                Vec2 rotDelta = {delta.x * cs - delta.y * sn,
                                 delta.x * sn + delta.y * cs};
                e.vel.x = siniAxisVel(e.vel.x, rotDelta.x, cfg.orbitTable, cfg.orbitTableN, dm);
                e.vel.y = siniAxisVel(e.vel.y, rotDelta.y, cfg.orbitTable, cfg.orbitTableN, dm);
            } else {
                e.vel.x = siniAxisVel(e.vel.x, delta.x, cfg.chaseTable, cfg.chaseTableN, dm);
                e.vel.y = siniAxisVel(e.vel.y, delta.y, cfg.chaseTable, cfg.chaseTableN, dm);
            }
        }

        Vec2 toP = wrapDelta(e.pos, player().pos);
        if (toP.len() > 1.0f) {
            float targetAngle = atan2f(toP.y, toP.x);
            if (targetAngle < 0) targetAngle += TAU;
            float da = targetAngle - e.angle;
            if (da > PI) da -= TAU;
            if (da < -PI) da += TAU;
            e.angle += da * std::min(1.0f, 6.0f * dt);
            if (e.angle < 0) e.angle += TAU;
            if (e.angle > TAU) e.angle -= TAU;
        }

        if (sinistarSpeechTimer <= 0) {
            float dist = toP.len();
            if (dist < 300) {
                speechTrigger = (rand() % 3 == 0) ? 6 : 3;
            } else {
                int clips[] = {1, 2, 4, 5};
                speechTrigger = clips[rand() % 4];
            }
            sinistarSpeechTimer = 4.0f + (float)(rand() % 300) / 100.0f;
        }
    }

    void updateSinibomb(Entity& e, float dt) {
        Entity* sini = findType(ENT_SINISTAR);
        if (sini) {
            Vec2 dir = wrapDelta(e.pos, sini->pos).norm();
            float speed = cfg.sinibombSpeed;
            e.vel = e.vel + dir * (speed * 4.0f * dt);
            float sp = e.vel.len();
            if (sp > speed) e.vel = e.vel * (speed / sp);
        }
        e.timer -= dt;
        if (e.timer <= 0) e.active = false;
        e.frame = ((int)(e.timer * 10)) % 3;
    }

    void updateBullet(Entity& e, float dt) {
        e.timer -= dt;
        if (e.timer <= 0) e.active = false;
    }

    void updateCrystal(Entity& e, float dt) {
        e.vel = e.vel * (1.0f - 0.5f * dt);
        e.timer -= dt;
        if (e.timer <= 0) e.active = false;
    }

    void updateExplosion(Entity& e, float dt) {
        e.timer -= dt;
        if (e.timer <= 0) e.active = false;
        e.vel = e.vel * (1.0f - 3.0f * dt);
    }

    void updatePlanetoid(Entity& e, float dt) {
        if (e.timer2 > 0) {
            e.timer2 -= 8.0f * dt;
            if (e.timer2 < 0) e.timer2 = 0;
        }
    }

    // === COLLISIONS ===

    void checkCollisions() {
        for (int i = 0; i < MAX_ENTS; i++) {
            if (!ents[i].active) continue;
            for (int j = i + 1; j < MAX_ENTS; j++) {
                if (!ents[j].active) continue;
                Entity& a = ents[i]; Entity& b = ents[j];
                float d = wrapDist(a.pos, b.pos);
                if (d >= a.radius + b.radius) continue;
                handleCollision(a, b);
            }
        }
    }

    void handleCollision(Entity& a, Entity& b) {
        auto playerBulletHit = [&](Entity& bullet, Entity& target) {
            if (target.type == ENT_WARRIOR || target.type == ENT_WORKER) {
                spawnExplosion(target.pos, 5);
                bullet.active = false;
                addScore(target.type == ENT_WARRIOR ? cfg.ptsWarrior : cfg.ptsWorker);
                if (target.type == ENT_WORKER && target.crystals > 0) {
                    int ci = findFree();
                    if (ci >= 0) {
                        auto& c = ents[ci];
                        c.active = true; c.type = ENT_CRYSTAL;
                        c.pos = target.pos;
                        c.vel = {(float)(rand()%40-20), (float)(rand()%40-20)};
                        c.timer = cfg.crystalTimer; c.radius = 3;
                    }
                }
                target.active = false;
            } else if (target.type == ENT_PLANETOID && target.crystals > 0) {
                bullet.active = false;
                mineHit(target);
            } else if (target.type == ENT_SINISTAR) {
                bullet.active = false;
            }
        };

        if (a.type == ENT_BULLET_P) playerBulletHit(a, b);
        else if (b.type == ENT_BULLET_P) playerBulletHit(b, a);
        else if ((a.type == ENT_BULLET_W && b.type == ENT_PLAYER) ||
                 (b.type == ENT_BULLET_W && a.type == ENT_PLAYER)) {
            Entity& bullet = a.type == ENT_BULLET_W ? a : b;
            bullet.active = false;
            killPlayer();
        }
        else if ((a.type == ENT_BULLET_W && b.type == ENT_PLANETOID) ||
                 (b.type == ENT_BULLET_W && a.type == ENT_PLANETOID)) {
            Entity& bullet = a.type == ENT_BULLET_W ? a : b;
            Entity& plan = a.type == ENT_PLANETOID ? a : b;
            bullet.active = false;
            if (plan.crystals > 0) mineHit(plan);
        }
        // Sinibomb vs Sinistar — scores from ROM: $0500 per piece, $7000+$8000 for destruction
        else if ((a.type == ENT_SINIBOMB && b.type == ENT_SINISTAR) ||
                 (b.type == ENT_SINIBOMB && a.type == ENT_SINISTAR)) {
            Entity& bomb = a.type == ENT_SINIBOMB ? a : b;
            Entity& sini = a.type == ENT_SINISTAR ? a : b;
            bomb.active = false;
            spawnExplosion(bomb.pos, 8);
            sinistarPieces--;
            siniStun += cfg.stunPerBomb;
            addScore(cfg.ptsSiniPiece);
            if (sinistarPieces <= 0) {
                // KABOOM — Sinistar destroyed
                spawnExplosion(sini.pos, 20);
                sini.active = false;
                addScore(cfg.ptsSinistar);
                sinistarKills++;
                soundTrigger = 5;
                // Transition to explosion sequence
                gameState = GS_SINI_EXPLODE;
                stateTimer = cfg.siniExplodeTime;
                flashTimer = cfg.siniFlashTime;
                showMessage("SINISTAR DESTROYED!", 3.0f);
            } else {
                if (sinistarPieces < 13 && sini.state == 1) {
                    sini.state = 0;
                    siniInhibitor = cfg.inhibitorTicks;
                }
                soundTrigger = 5;
            }
        }
        // Player vs crystal — 200 pts from ROM ($0200 BCD at $EF6A) + sinibomb conversion
        else if ((a.type == ENT_PLAYER && b.type == ENT_CRYSTAL) ||
                 (b.type == ENT_PLAYER && a.type == ENT_CRYSTAL)) {
            Entity& crys = a.type == ENT_CRYSTAL ? a : b;
            crys.active = false;
            addScore(cfg.ptsCrystal);
            sinibombs++;
            if (sinibombs > cfg.maxSinibombs) sinibombs = cfg.maxSinibombs;
            soundTrigger = 3;
        }
        else if ((a.type == ENT_WORKER && b.type == ENT_CRYSTAL) ||
                 (b.type == ENT_WORKER && a.type == ENT_CRYSTAL)) {
            Entity& worker = a.type == ENT_WORKER ? a : b;
            Entity& crys = a.type == ENT_CRYSTAL ? a : b;
            if (worker.crystals == 0) {
                crys.active = false;
                worker.crystals = 1;
            }
        }
        else if (a.type == ENT_PLAYER && (b.type == ENT_WARRIOR || b.type == ENT_WORKER)) {
            Vec2 d = wrapDelta(b.pos, a.pos).norm();
            a.vel = a.vel + d * 150.0f;
            b.vel = b.vel - d * 100.0f;
        }
        else if (b.type == ENT_PLAYER && (a.type == ENT_WARRIOR || a.type == ENT_WORKER)) {
            Vec2 d = wrapDelta(a.pos, b.pos).norm();
            b.vel = b.vel + d * 150.0f;
            a.vel = a.vel - d * 100.0f;
        }
        // Player vs planetoid — bounce off (planetoid is immovable)
        else if (a.type == ENT_PLAYER && b.type == ENT_PLANETOID) {
            Vec2 d = wrapDelta(b.pos, a.pos).norm();
            a.vel = d * 200.0f;
            // Push player out of overlap
            float overlap = a.radius + b.radius - wrapDist(a.pos, b.pos);
            if (overlap > 0) a.pos = wrapPos(a.pos + d * (overlap + 1.0f));
            soundTrigger = 7;
        }
        else if (b.type == ENT_PLAYER && a.type == ENT_PLANETOID) {
            Vec2 d = wrapDelta(a.pos, b.pos).norm();
            b.vel = d * 200.0f;
            float overlap = b.radius + a.radius - wrapDist(b.pos, a.pos);
            if (overlap > 0) b.pos = wrapPos(b.pos + d * (overlap + 1.0f));
            soundTrigger = 7;
        }
        else if ((a.type == ENT_PLAYER && b.type == ENT_SINISTAR && b.state == 1) ||
                 (b.type == ENT_PLAYER && a.type == ENT_SINISTAR && a.state == 1)) {
            Entity& sini = a.type == ENT_SINISTAR ? a : b;
            sini.vel = {0, 0};
            speechTrigger = 7;
            killPlayer();
        }
    }

    // === DEATH AND SCORING ===

    void killPlayer() {
        if (!player().active) return;
        if (gameState != GS_PLAYING) return; // can't die outside gameplay
        // Original DEATH.ASM: dramatic multi-fragment explosion lingers 2-3 seconds
        spawnPlayerExplosion(player().pos);
        player().active = false;
        lives--;
        soundTrigger = 2;
        gameState = GS_DEATH;
        stateTimer = cfg.deathTime;
        flashTimer = cfg.deathFlashTime;
    }

    void spawnExplosion(Vec2 pos, int count) {
        for (int i = 0; i < count; i++) {
            int idx = findFree(); if (idx < 0) return;
            auto& e = ents[idx];
            e.active = true; e.type = ENT_EXPLOSION;
            e.pos = pos;
            float a = (float)(rand() % 628) / 100.0f;
            float sp = 50.0f + (float)(rand() % 150);
            e.vel = {cosf(a) * sp, sinf(a) * sp};
            e.timer = 0.3f + (float)(rand() % 50) / 100.0f;
            e.radius = 1; e.subtype = rand() % 4;
        }
        soundTrigger = 1;
    }

    // Original DEATH.ASM: player ship breaks into fragments that spread wide and linger
    void spawnPlayerExplosion(Vec2 pos) {
        // Large bright fragments (ship pieces) - slow, long-lived
        for (int i = 0; i < 8; i++) {
            int idx = findFree(); if (idx < 0) return;
            auto& e = ents[idx];
            e.active = true; e.type = ENT_EXPLOSION;
            e.pos = pos;
            float a = (float)(rand() % 628) / 100.0f;
            float sp = 30.0f + (float)(rand() % 80);
            e.vel = {cosf(a) * sp, sinf(a) * sp};
            e.timer = 1.5f + (float)(rand() % 150) / 100.0f; // 1.5-3.0s
            e.radius = 2; e.subtype = rand() % 2; // bright colors
        }
        // Fast sparks/debris - quick scatter
        for (int i = 0; i < 15; i++) {
            int idx = findFree(); if (idx < 0) return;
            auto& e = ents[idx];
            e.active = true; e.type = ENT_EXPLOSION;
            e.pos = pos;
            float a = (float)(rand() % 628) / 100.0f;
            float sp = 80.0f + (float)(rand() % 200);
            e.vel = {cosf(a) * sp, sinf(a) * sp};
            e.timer = 0.5f + (float)(rand() % 80) / 100.0f; // 0.5-1.3s
            e.radius = 1; e.subtype = rand() % 4;
        }
        soundTrigger = 1;
    }

    void mineHit(Entity& plan) {
        if (plan.crystals <= 0) return;
        plan.timer2 += 16.0f;
        int prob = (int)(plan.timer2 * 1.0f);
        if (rand() % 256 < prob || plan.timer2 > 80.0f) {
            plan.crystals--;
            int ci = findFree();
            if (ci >= 0) {
                auto& c = ents[ci];
                c.active = true; c.type = ENT_CRYSTAL;
                float a = (float)(rand() % 628) / 100.0f;
                float sp = 30.0f + (float)(rand() % 40);
                c.pos = plan.pos + Vec2{cosf(a)*12, sinf(a)*12};
                c.vel = {cosf(a)*sp, sinf(a)*sp};
                c.timer = cfg.crystalTimer; c.radius = 3;
            }
            plan.timer2 -= 8.0f;
            if (plan.timer2 < 0) plan.timer2 = 0;
            soundTrigger = 7;
        }
        if (plan.timer2 >= 96.0f) {
            while (plan.crystals > 0) {
                plan.crystals--;
                int ci = findFree();
                if (ci >= 0) {
                    auto& c = ents[ci];
                    c.active = true; c.type = ENT_CRYSTAL;
                    float a = (float)(rand() % 628) / 100.0f;
                    float sp = 40.0f + (float)(rand() % 80);
                    c.pos = plan.pos + Vec2{cosf(a)*8, sinf(a)*8};
                    c.vel = {cosf(a)*sp, sinf(a)*sp};
                    c.timer = cfg.crystalTimer; c.radius = 3;
                }
            }
            spawnExplosion(plan.pos, 8);
            plan.active = false;
        }
    }

    // === HIGH SCORE MANAGEMENT (HSTDTE.ASM) ===

    int checkHighScore() {
        for (int i = 0; i < NUM_HIGH_SCORES; i++)
            if (score > highScores[i].score) return i;
        return -1;
    }

    void insertHighScore(int rank, const char* initials) {
        if (rank < 0 || rank >= NUM_HIGH_SCORES) return;
        for (int i = NUM_HIGH_SCORES - 1; i > rank; i--)
            highScores[i] = highScores[i-1];
        highScores[rank].score = score;
        memcpy(highScores[rank].initials, initials, 3);
        highScores[rank].initials[3] = 0;
    }
};
