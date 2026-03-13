// Sinistar Game Configuration - loads tunable parameters from text config files
// Falls back to built-in defaults if files are missing.
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>

static constexpr int MAX_SPEED_ENTRIES = 16;

struct SpeedEntry { float distThresh, maxSpeed; int shift; };

struct ZoneParams {
    int planetoids, warriors, workers;
    int crystalsPerRock;
    float warSpeed, wkrSpeed, siniSpeed;
    int siniStartPieces;
    float aggression;
};

struct GameConfig {
    // [points]
    int ptsWorker = 150;
    int ptsCrystal = 200;
    int ptsWarrior = 500;
    int ptsSiniPiece = 500;
    int ptsSinistar = 15000;
    int freeLifeFirst = 20000;
    int freeLifeEvery = 25000;

    // [player]
    float thrust = 800.0f;
    float maxSpeed = 250.0f;
    float fireCD = 0.12f;
    float bombCD = 0.5f;
    float bulletSpeed = 500.0f;
    float sinibombSpeed = 350.0f;
    int maxSinibombs = 20;
    float playerDrag = 1.5f;

    // [warrior]
    float aggroDistBase = 60.0f;
    float aggroDistScale = 150.0f;
    float fireCDBase = 5.0f;
    float fireCDAggrScale = 2.5f;
    float fireCDRand = 2.5f;
    float warBulletSpeed = 300.0f;
    float warMaxSpeed = 250.0f;

    // [worker]
    float deliverSpeedMul = 1.3f;
    float deliverDist = 35.0f;
    float crystalTimer = 10.0f;
    float wkrMaxSpeed = 250.0f;

    // [sinistar]
    float dormantMaxSpeed = 50.0f;
    float thinkInterval = 16.0f / 60.0f;
    int inhibitorTicks = 12;
    int stunPerBomb = 2;

    // [timing]
    float deathTime = 3.5f;
    float statusTime = 4.0f;
    float gameoverTime = 5.0f;
    float attractTitleTime = 7.5f;
    float attractHstdTime = 5.0f;
    float attractPointsTime = 7.0f;
    float siniExplodeTime = 3.0f;
    float deathFlashTime = 0.08f;
    float siniFlashTime = 2.0f;

    // [difficulty]
    float diffScalePerCycle = 0.08f;
    float aggrScalePerCycle = 0.05f;
    int maxWarriors = 15;

    // Build thresholds from ROM ($4FAA): deliveries needed per piece at each difficulty level
    // Index by min(zoneCycle, 7). Level 0 = 1 delivery/piece, Level 7 = 15 deliveries/piece.
    int buildThresholds[8] = { 1, 2, 3, 4, 5, 9, 13, 15 };

    // Zone data (from zones.cfg)
    ZoneParams zones[4] = {
        { 20,  2,  5, 15,  80.f,  60.f, 150.f, 0, 0.05f },
        { 20,  5,  8, 12, 100.f,  80.f, 200.f, 3, 0.20f },
        { 18,  4,  7, 10, 110.f,  90.f, 230.f, 5, 0.15f },
        { 12,  7, 10,  8, 130.f, 100.f, 270.f, 7, 0.40f },
    };
    const char* zoneNames[4] = {"WORKER", "WARRIOR", "PLANETOID", "VOID"};

    // Speed tables (from zones.cfg)
    SpeedEntry chaseTable[MAX_SPEED_ENTRIES] = {
        {4096.f, 480.f, 5}, {2048.f, 280.f, 5}, {1024.f, 200.f, 4},
        {512.f,  150.f, 4}, {256.f,  130.f, 3}, {128.f,  120.f, 3},
        {64.f,   100.f, 2}, {32.f,    80.f, 2}, {0.f,     25.f, 2},
    };
    int chaseTableN = 9;

    SpeedEntry orbitTable[MAX_SPEED_ENTRIES] = {
        {4096.f, 800.f, 5}, {2048.f, 500.f, 5}, {1024.f, 350.f, 4},
        {512.f,  200.f, 4}, {256.f,  120.f, 3}, {128.f,   60.f, 3},
        {64.f,    30.f, 2}, {32.f,    10.f, 2}, {16.f,     0.f, 1}, {0.f, 0.f, 1},
    };
    int orbitTableN = 10;
};

// --- Parser helpers ---

static inline void trimRight(char* s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0;
}

static inline bool startsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Set a float field if key matches
static inline bool setF(const char* key, const char* val, const char* name, float& f) {
    if (strcmp(key, name) == 0) { f = (float)atof(val); return true; }
    return false;
}

// Set an int field if key matches
static inline bool setI(const char* key, const char* val, const char* name, int& i) {
    if (strcmp(key, name) == 0) { i = atoi(val); return true; }
    return false;
}

static inline bool loadGameConfig(GameConfig& cfg, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { printf("Config: %s not found, using defaults\n", path); return true; }

    char line[256], section[64] = "";
    while (fgets(line, sizeof(line), f)) {
        trimRight(line);
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == 0) continue;

        // Section header
        if (*p == '[') {
            sscanf(p, "[%63[^]]]", section);
            continue;
        }

        // key = value
        char key[64], val[64];
        if (sscanf(p, "%63s = %63s", key, val) != 2) continue;

        if (strcmp(section, "points") == 0) {
            setI(key, val, "worker", cfg.ptsWorker);
            setI(key, val, "crystal", cfg.ptsCrystal);
            setI(key, val, "warrior", cfg.ptsWarrior);
            setI(key, val, "sini_piece", cfg.ptsSiniPiece);
            setI(key, val, "sinistar", cfg.ptsSinistar);
            setI(key, val, "free_life_first", cfg.freeLifeFirst);
            setI(key, val, "free_life_every", cfg.freeLifeEvery);
        } else if (strcmp(section, "player") == 0) {
            setF(key, val, "thrust", cfg.thrust);
            setF(key, val, "max_speed", cfg.maxSpeed);
            setF(key, val, "fire_cooldown", cfg.fireCD);
            setF(key, val, "bomb_cooldown", cfg.bombCD);
            setF(key, val, "bullet_speed", cfg.bulletSpeed);
            setF(key, val, "sinibomb_speed", cfg.sinibombSpeed);
            setI(key, val, "max_sinibombs", cfg.maxSinibombs);
            setF(key, val, "drag", cfg.playerDrag);
        } else if (strcmp(section, "warrior") == 0) {
            setF(key, val, "aggro_dist_base", cfg.aggroDistBase);
            setF(key, val, "aggro_dist_scale", cfg.aggroDistScale);
            setF(key, val, "fire_cd_base", cfg.fireCDBase);
            setF(key, val, "fire_cd_aggr_scale", cfg.fireCDAggrScale);
            setF(key, val, "fire_cd_rand", cfg.fireCDRand);
            setF(key, val, "bullet_speed", cfg.warBulletSpeed);
            setF(key, val, "max_speed", cfg.warMaxSpeed);
        } else if (strcmp(section, "worker") == 0) {
            setF(key, val, "deliver_speed_mul", cfg.deliverSpeedMul);
            setF(key, val, "deliver_dist", cfg.deliverDist);
            setF(key, val, "crystal_timer", cfg.crystalTimer);
            setF(key, val, "max_speed", cfg.wkrMaxSpeed);
        } else if (strcmp(section, "sinistar") == 0) {
            setF(key, val, "dormant_max_speed", cfg.dormantMaxSpeed);
            setF(key, val, "think_interval", cfg.thinkInterval);
            setI(key, val, "inhibitor_ticks", cfg.inhibitorTicks);
            setI(key, val, "stun_per_bomb", cfg.stunPerBomb);
        } else if (strcmp(section, "timing") == 0) {
            setF(key, val, "death", cfg.deathTime);
            setF(key, val, "status", cfg.statusTime);
            setF(key, val, "gameover", cfg.gameoverTime);
            setF(key, val, "attract_title", cfg.attractTitleTime);
            setF(key, val, "attract_hstd", cfg.attractHstdTime);
            setF(key, val, "attract_points", cfg.attractPointsTime);
            setF(key, val, "sini_explode", cfg.siniExplodeTime);
            setF(key, val, "death_flash", cfg.deathFlashTime);
            setF(key, val, "sini_flash", cfg.siniFlashTime);
        } else if (strcmp(section, "difficulty") == 0) {
            setF(key, val, "scale_per_cycle", cfg.diffScalePerCycle);
            setF(key, val, "aggr_per_cycle", cfg.aggrScalePerCycle);
            setI(key, val, "max_warriors", cfg.maxWarriors);
        }
    }
    fclose(f);
    printf("Config: loaded %s\n", path);
    return true;
}

static inline bool loadZoneConfig(GameConfig& cfg, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { printf("Config: %s not found, using defaults\n", path); return true; }

    char line[256], section[64] = "";
    int zoneIdx = 0, chaseIdx = 0, orbitIdx = 0;

    while (fgets(line, sizeof(line), f)) {
        trimRight(line);
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == 0) continue;

        if (*p == '[') {
            sscanf(p, "[%63[^]]]", section);
            continue;
        }

        if (strcmp(section, "zones") == 0 && zoneIdx < 4) {
            char name[32];
            ZoneParams& z = cfg.zones[zoneIdx];
            if (sscanf(p, "%31s %d %d %d %d %f %f %f %d %f",
                       name, &z.planetoids, &z.warriors, &z.workers,
                       &z.crystalsPerRock, &z.warSpeed, &z.wkrSpeed,
                       &z.siniSpeed, &z.siniStartPieces, &z.aggression) == 10) {
                // Store zone name (static storage, up to 4)
                static char znames[4][32];
                strncpy(znames[zoneIdx], name, 31);
                znames[zoneIdx][31] = 0;
                cfg.zoneNames[zoneIdx] = znames[zoneIdx];
                zoneIdx++;
            }
        } else if (strcmp(section, "chase_table") == 0 && chaseIdx < MAX_SPEED_ENTRIES) {
            SpeedEntry& e = cfg.chaseTable[chaseIdx];
            if (sscanf(p, "%f %f %d", &e.distThresh, &e.maxSpeed, &e.shift) == 3)
                chaseIdx++;
        } else if (strcmp(section, "orbit_table") == 0 && orbitIdx < MAX_SPEED_ENTRIES) {
            SpeedEntry& e = cfg.orbitTable[orbitIdx];
            if (sscanf(p, "%f %f %d", &e.distThresh, &e.maxSpeed, &e.shift) == 3)
                orbitIdx++;
        }
    }

    cfg.chaseTableN = chaseIdx > 0 ? chaseIdx : cfg.chaseTableN;
    cfg.orbitTableN = orbitIdx > 0 ? orbitIdx : cfg.orbitTableN;

    fclose(f);
    printf("Config: loaded %s (%d zones, %d chase entries, %d orbit entries)\n",
           path, zoneIdx, cfg.chaseTableN, cfg.orbitTableN);
    return true;
}
