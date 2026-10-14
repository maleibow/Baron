/* The Adventures of Baron Munchausen - SGDK (Sega Genesis) rescue-and-fight game, v2.
 * Controls: D-pad move, B/C attack, A swap companion, START begin / pause.
 * Palette plan: PAL0 = round tileset (or title art)  PAL1 = active party member
 *               PAL2 = enemies/captives/effects (shared)  PAL3 = text
 * Positions are stored in 1/8 pixel units (>>3 gives pixels).
 * Sound: music + effects are driven directly on the PSG chip (no sound assets needed).
 */
#include <genesis.h>
#include "resources.h"
#include "palettes.h"
#include "titleart.h"
#include "music.h"

#define COLS 20
#define ROWS 12
#define OY 16
#define MAX_EN 16
#define MAX_CAP 2
#define MAX_BL 8
#define MAX_EB 12
#define MAX_FX 6
#define MAX_PK 6
#define NR NTHEME
#define SOLID_TILE (TILE_USER_INDEX + 32)
#define ABS(x) ((x) < 0 ? -(x) : (x))

enum { ST_TITLE, ST_BANNER, ST_PLAY, ST_OVER, ST_WIN };
enum { W_SHOT, W_MELEE, W_GUST };
enum { E_GUARD, E_GUNNER, E_JAN, E_BOSS };
enum { SF_NONE, SF_SHOT, SF_HIT, SF_KILL, SF_HURT, SF_SWAP, SF_RESCUE, SF_SWING, SF_GUST, SF_BOSS, SF_CLEAR, SF_PICK, SF_DIE };

typedef struct {
    const char *name; const SpriteDefinition *def; const u16 *pal;
    s16 spd; u8 type, dmg, cd; s16 bspd, life; u8 pierce; const char *quote;
} CharDef;

static const CharDef CH[5] = {
    { "BARON",    &spr_baron,    pal_baron,    10, W_SHOT,  1, 18, 26, 34, 0, "ALL TRUE, I ASSURE YOU!" },
    { "ALBRECHT", &spr_albrecht, pal_albrecht,  6, W_MELEE, 3, 28,  0,  0, 0, "STRONG AS AN OX!" },
    { "BERTHOLD", &spr_berthold, pal_berthold, 17, W_SHOT,  1, 12, 24, 23, 0, "SWIFTER THAN THE WIND!" },
    { "ADOLPHUS", &spr_adolphus, pal_adolphus,  8, W_SHOT,  2, 36, 40, 70, 1, "I NEVER MISS!" },
    { "GUSTAVUS", &spr_gustavus, pal_gustavus,  8, W_GUST,  1, 38,  0,  0, 0, "HOLD ONTO YOUR HATS!" }
};
static const TileSet *TS[NR] = { &ts_siege, &ts_sea, &ts_moon, &ts_etna, &ts_whale, &ts_palace };
static const char *RT[NR] = { "THE TURKISH SIEGE", "THE STORMY SEA", "THE MOON", "MOUNT ETNA",
                              "THE BELLY OF THE WHALE", "THE SULTAN'S PALACE" };
static const char *RS[NR] = { "THE SULTAN'S GUARDS HOLD MY FRIENDS!", "ADRIFT ON A SEA OF PERIL, GENTLEMEN!",
                              "WE CLIMBED A BEANSTALK TO THE MOON!", "THROUGH THE FIRE OF VULCAN'S FORGE!",
                              "SWALLOWED WHOLE, YET UNDAUNTED!", "ONE LAST TALE TO TELL, GENTLEMEN!" };
static const u8 NCAP[NR] = { 1, 1, 2, 1, 2, 0 };
static const s8 TR[NR]   = { 0, 2, 3, 5, 7, 0 };
static const u32 solidTile[8] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                                  0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

typedef struct { u8 id; s8 hp; } Member;
typedef struct { s16 x8, y8, cd, tm, vx, vy; s8 hp; u8 type, on, st, w, stun; Sprite *spr; } Enemy;
typedef struct { s16 x8, y8; u8 on, st; Sprite *spr; } Cap;
typedef struct { s16 x8, y8, vx, vy, life; u8 dmg, pierce, on; u16 hit; Sprite *spr; } Bul;
typedef struct { Sprite *spr; u8 t; } FxS;
typedef struct { s16 x8, y8, life; u8 kind, on; Sprite *spr; } Pick;

static u8 grid[ROWS][COLS];
static Enemy en[MAX_EN]; static Cap cap[MAX_CAP]; static Bul bl[MAX_BL], eb[MAX_EB];
static FxS fxs[MAX_FX]; static Pick pk[MAX_PK];
static Member party[5];
static u8 state, round_, pn, act, hudDirty, pstate, locked, paused, combo, cph;
static u32 score;
static u16 tick, prevJp, cbase[3], comboT;
static s16 stateT, px8, py8, cd, inv, msgT;
static s8 face_x, face_y;
static Sprite *pspr;

/* ================= PSG sound ================= */
#define PSGP (*(vu8 *)0xC00011)
static void psgTone(u8 ch, u16 n) { PSGP = 0x80 | (ch << 5) | (n & 15); PSGP = (n >> 4) & 63; }
static void psgVol(u8 ch, u8 a) { PSGP = 0x90 | (ch << 5) | (a & 15); }
static void psgNoise(u8 m) { PSGP = 0xE0 | (m & 7); }
static void psgNoiseVol(u8 a) { PSGP = 0xF0 | (a & 15); }
static void psgSilence(void) { u8 i; for (i = 0; i < 3; i++) psgVol(i, 15); psgNoiseVol(15); }
static u16 fdiv(u16 f) { u32 d; if (f < 1) f = 1; d = 111861UL / f; return d > 1023 ? 1023 : (u16)d; }

static const Song *song; static u16 mpos; static u8 mtick, mon[2], mage[2], hatK, nage;
static s8 mtrans; static u8 sfxId, sfxT, sfxNoise;
static const u8 SPRI[13] = { 0, 1, 2, 4, 5, 3, 6, 3, 3, 5, 7, 3, 7 };
static const u16 AR1[6] = { 523, 659, 784, 1047, 784, 1047 };
static const u16 AR2[8] = { 523, 523, 523, 659, 784, 659, 784, 1047 };

static void musicPlay(u8 id, s8 trans) {
    song = &SONGS[id]; mpos = 0; mtick = 0; mtrans = trans; mon[0] = mon[1] = 0; hatK = 0; psgSilence();
}
static void musicStop(void) { song = NULL; psgSilence(); }
static void sfx(u8 id) {
    if (sfxId && SPRI[id] < SPRI[sfxId]) return;
    sfxId = id; sfxT = 0;
}
static void sfxEnd(void) {
    sfxId = 0; psgVol(2, 15);
    if (sfxNoise) { psgNoiseVol(15); sfxNoise = 0; }
}
static void sfxTick(void) {
    u8 t, n;
    if (!sfxId) return;
    t = sfxT++;
    switch (sfxId) {
    case SF_SHOT:  if (t < 8) { psgTone(2, fdiv(1500 - t * 130)); psgVol(2, 2 + t / 2); } else sfxEnd(); break;
    case SF_HIT:   if (t < 5) { psgNoise(0x04); psgNoiseVol(2 + t * 2); sfxNoise = 1; } else sfxEnd(); break;
    case SF_KILL:  if (t < 14) { psgTone(2, fdiv(700 - t * 40)); psgVol(2, 3 + t / 3);
                                 psgNoise(0x05); psgNoiseVol(3 + t / 2); sfxNoise = 1; } else sfxEnd(); break;
    case SF_HURT:  if (t < 16) { psgTone(2, fdiv(230 - t * 8)); psgVol(2, 2 + t / 4); } else sfxEnd(); break;
    case SF_SWAP:  if (t < 8) { psgTone(2, fdiv(t < 4 ? 880 : 1320)); psgVol(2, 3); } else sfxEnd(); break;
    case SF_RESCUE: n = t / 4; if (n < 6) { psgTone(2, fdiv(AR1[n])); psgVol(2, 2 + (t & 3)); } else sfxEnd(); break;
    case SF_SWING: if (t < 8) { psgTone(2, fdiv(160 + t * 6)); psgVol(2, 4);
                                psgNoise(0x05); psgNoiseVol(2 + t); sfxNoise = 1; } else sfxEnd(); break;
    case SF_GUST:  if (t < 20) { psgTone(2, fdiv(250 + t * 20)); psgVol(2, 6); psgNoise(0x04);
                                 psgNoiseVol(4 + t / 4); sfxNoise = 1; } else sfxEnd(); break;
    case SF_BOSS:  if (t < 30) { psgTone(2, fdiv(130 + ((t >> 2) & 1) * 25)); psgVol(2, 2); } else sfxEnd(); break;
    case SF_CLEAR: n = t / 5; if (n < 8) { psgTone(2, fdiv(AR2[n])); psgVol(2, 2 + (t % 5)); } else sfxEnd(); break;
    case SF_PICK:  if (t < 6) { psgTone(2, fdiv(1200 + t * 100)); psgVol(2, 3); } else sfxEnd(); break;
    case SF_DIE:   if (t < 24) { psgTone(2, fdiv(500 - t * 16)); psgVol(2, 2 + t / 6); } else sfxEnd(); break;
    default: sfxEnd(); break;
    }
}
static void musicTick(void) {
    u8 ch, m, v; u16 note; u8 a;
    if (!song) return;
    if (mtick == 0) {
        for (ch = 0; ch < 2; ch++) {
            m = (ch ? song->bass : song->mel)[mpos];
            if (m > 1) {
                note = NT[(u8)(m + mtrans)];
                if (note) { psgTone(ch, note); mon[ch] = 1; mage[ch] = 0; } else mon[ch] = 0;
            } else if (m == 0) mon[ch] = 0;
        }
        v = song->perc[mpos];
        if (!sfxNoise && v) { psgNoise(v == 1 ? 0x04 : 0x02); hatK = v; nage = 0; }
    }
    for (ch = 0; ch < 2; ch++) {
        if (!mon[ch]) { psgVol(ch, 15); continue; }
        a = mage[ch] / 3; if (a > 6) a = 6;
        psgVol(ch, (ch ? 2 : 3) + a);
        if (mage[ch] < 250) mage[ch]++;
    }
    if (!sfxNoise) {
        if (hatK) {
            a = (hatK == 2 ? 2 + nage * 2 : 6 + nage * 2);
            if (a >= 15) { a = 15; hatK = 0; }
            psgNoiseVol(a); nage++;
        } else psgNoiseVol(15);
    }
    if (++mtick >= song->step) {
        mtick = 0;
        if (++mpos >= song->len) {
            if (song->loop) mpos = 0; else { song = NULL; psgSilence(); }
        }
    }
}

/* ================= helpers ================= */
static bool solid(s16 x, s16 y) {
    s16 cx, cy;
    if (x < 0 || y < OY) return TRUE;
    cx = x >> 4; cy = (y - OY) >> 4;
    if (cx >= COLS || cy >= ROWS) return TRUE;
    return grid[cy][cx];
}
static bool freeAt(s16 x, s16 y) {
    return !(solid(x - 5, y - 5) || solid(x + 5, y - 5) || solid(x - 5, y + 5) || solid(x + 5, y + 5));
}
static s16 dist(s16 dx, s16 dy) {
    s16 a = ABS(dx), b = ABS(dy);
    return a > b ? a + (b >> 1) : b + (a >> 1);
}
static Sprite *mk(const SpriteDefinition *d, s16 x, s16 y, u16 pal) {
    Sprite *s = SPR_addSprite(d, x, y, TILE_ATTR(pal, 0, FALSE, FALSE));
    if (s) SPR_setAutoAnimation(s, FALSE);
    return s;
}
static void rel(Sprite **s) { if (*s) { SPR_releaseSprite(*s); *s = NULL; } }
static void setAF(Sprite *s, u8 *st, u8 a, u8 f, u8 h) {
    u8 n = a | (f << 2) | (h << 4);
    if (!s || *st == n) return;
    *st = n; SPR_setAnimAndFrame(s, a, f); SPR_setHFlip(s, h);
}
static void faceAF(Sprite *s, u8 *st, s16 dx, s16 dy, u8 f) {
    if (dx != 0 && ABS(dx) >= ABS(dy)) setAF(s, st, 2, f, dx < 0);
    else if (dy < 0) setAF(s, st, 1, f, 0);
    else setAF(s, st, 0, f, 0);
}
static void center(const char *s, u16 row) { VDP_drawText(s, (40 - strlen(s)) / 2, row); }
static void say(const char *s) { VDP_clearTextArea(0, 26, 40, 2); center(s, 26); msgT = 150; }
static void addScore(u16 n) { score += n; hudDirty = 1; }

static void hud(void) {
    char b[48], t[8]; u8 i;
    VDP_clearTextArea(0, 0, 40, 2);
    strcpy(b, "RD "); intToStr(round_ + 1, b + 3, 1); VDP_drawText(b, 1, 0);
    if (combo > 1) { strcpy(b, "COMBO X"); intToStr(combo > 5 ? 5 : combo, b + 7, 1); VDP_drawText(b, 11, 0); }
    strcpy(b, "SCORE "); uintToStr(score, b + 6, 6); VDP_drawText(b, 27, 0);
    b[0] = 0;
    for (i = 0; i < pn; i++) {
        t[0] = (i == act) ? '>' : ' ';
        memcpy(t + 1, CH[party[i].id].name, 3);
        t[4] = ':'; t[5] = '0' + party[i].hp; t[6] = ' '; t[7] = 0;
        strcat(b, t);
    }
    VDP_drawText(b, 0, 1);
}

/* ================= entity management ================= */
static void clearEntities(void) {
    u8 i;
    for (i = 0; i < MAX_EN; i++)  { rel(&en[i].spr); en[i].on = 0; }
    for (i = 0; i < MAX_CAP; i++) { rel(&cap[i].spr); cap[i].on = 0; }
    for (i = 0; i < MAX_BL; i++)  { rel(&bl[i].spr); bl[i].on = 0; }
    for (i = 0; i < MAX_EB; i++)  { rel(&eb[i].spr); eb[i].on = 0; }
    for (i = 0; i < MAX_FX; i++)  { rel(&fxs[i].spr); fxs[i].t = 0; }
    for (i = 0; i < MAX_PK; i++)  { rel(&pk[i].spr); pk[i].on = 0; }
}
static void spawnPlayer(void) {
    const CharDef *c = &CH[party[act].id];
    rel(&pspr);
    PAL_setPalette(PAL1, c->pal, CPU);
    pspr = mk(c->def, (px8 >> 3) - 8, (py8 >> 3) - 12, PAL1);
    pstate = 255;
}
static void spawnFx(s16 x, s16 y, u8 frame) {
    u8 i;
    for (i = 0; i < MAX_FX; i++) if (!fxs[i].t) {
        fxs[i].spr = mk(&spr_fx, x - 4, y - 4, PAL2);
        if (fxs[i].spr) SPR_setFrame(fxs[i].spr, frame);
        fxs[i].t = 10; return;
    }
}
static void dropItem(s16 x8, s16 y8) {
    u8 i, r = random() % 100, kind;
    if (r < 14) kind = 4; else if (r < 40) kind = 5; else return;
    for (i = 0; i < MAX_PK; i++) if (!pk[i].on) {
        pk[i].on = 1; pk[i].kind = kind; pk[i].x8 = x8; pk[i].y8 = y8; pk[i].life = 600;
        pk[i].spr = mk(&spr_fx, (x8 >> 3) - 4, (y8 >> 3) - 4, PAL2);
        if (pk[i].spr) SPR_setFrame(pk[i].spr, kind);
        return;
    }
}
static void freePos(s16 *ox, s16 *oy) {
    u8 i, x, y; s16 wx, wy;
    for (i = 0; i < 200; i++) {
        x = random() % COLS; y = 1 + random() % (ROWS - 3);
        wx = x * 16 + 8; wy = y * 16 + OY + 8;
        if (!grid[y][x] && dist(wx - 160, wy - 184) > 90) { *ox = wx * 8; *oy = wy * 8; return; }
    }
    *ox = 160 * 8; *oy = 60 * 8;
}
static void block(u8 k, u8 cx, u8 cy) {
    u16 b = TILE_USER_INDEX + 2 * k, x = cx * 2, y = cy * 2 + 2;
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b),      x,     y);
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b + 1),  x + 1, y);
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b + 16), x,     y + 1);
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b + 17), x + 1, y + 1);
}
static void drawExit(void) { u8 x; for (x = 8; x < 12; x++) block(locked ? 7 : 6, x, 0); }
static void buildRound(void) {
    u8 x, y, i, n, t; s16 ax, ay;
    clearEntities();
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    PAL_setPalette(PAL0, TP[round_], CPU); PAL_setColor(15, 0x0200);   /* colour 15 = banner panel */
    for (i = 0; i < 3; i++) cbase[i] = TP[round_][CYI[round_][i]];
    cph = 0;
    VDP_loadTileSet(TS[round_], TILE_USER_INDEX, CPU);
    VDP_loadTileData(solidTile, SOLID_TILE, 1, CPU);
    locked = (round_ == NR - 1);
    for (y = 0; y < ROWS; y++) for (x = 0; x < COLS; x++) grid[y][x] = 0;
    n = 14 + round_ * 3;
    for (i = 0; i < n; i++) grid[1 + random() % (ROWS - 3)][random() % COLS] = 1;
    for (x = 7; x < 13; x++) grid[1][x] = 0;
    for (y = 0; y < ROWS; y++) for (x = 0; x < COLS; x++) {
        if (grid[y][x]) block(4 + ((x + y * 3) % 3 == 0), x, y);
        else block(((x * 7 + y * 13) & 7) == 0 ? 2 : (((x * 5 + y * 11) & 7) == 1 ? 3 : (((x + y) & 3) == 0 ? 1 : 0)), x, y);
    }
    drawExit();
    px8 = 160 * 8; py8 = 184 * 8; face_x = 0; face_y = -1; cd = 0; inv = 60; combo = 0; comboT = 0;
    n = (round_ == NR - 1) ? 6 : 4 + 2 * round_; if (n > MAX_EN) n = MAX_EN;
    for (i = 0; i < n + (round_ == NR - 1); i++) {
        Enemy *e = &en[i];
        if (round_ == NR - 1 && i == n) {                       /* the Sultan */
            e->type = E_BOSS; e->hp = 18; e->x8 = 160 * 8; e->y8 = (OY + 40) * 8;
            e->spr = mk(&spr_sultan, 144, OY + 10, PAL2);
        } else {
            t = E_GUARD;
            if (round_ >= 1 && i % 3 == 2) t = E_GUNNER;
            if (round_ >= 2 && i % 4 == 3) t = E_JAN;
            e->type = t;
            e->hp = (t == E_GUARD) ? 1 + (round_ >= 3) : (t == E_GUNNER ? 2 : 2 + (round_ >= 4));
            freePos(&ax, &ay); e->x8 = ax; e->y8 = ay;
            e->spr = mk(t == E_GUNNER ? &spr_gunner : (t == E_JAN ? &spr_janissary : &spr_guard),
                        (ax >> 3) - 8, (ay >> 3) - 12, PAL2);
        }
        e->cd = 30 + random() % 90; e->on = 1; e->st = 255; e->w = 0; e->stun = 0; e->tm = 0;
    }
    for (i = 0; i < NCAP[round_]; i++) {
        freePos(&ax, &ay); cap[i].x8 = ax; cap[i].y8 = ay; cap[i].on = 1; cap[i].st = 255;
        cap[i].spr = mk(&spr_captive, (ax >> 3) - 8, (ay >> 3) - 12, PAL2);
    }
    spawnPlayer(); hudDirty = 1; msgT = 0;
    musicPlay(round_ == NR - 1 ? SONG_BOSS : SONG_BATTLE, TR[round_]);
}

/* ================= screens ================= */
static void panel(u16 y, u16 h) {
    VDP_fillTileMapRect(BG_A, TILE_ATTR_FULL(PAL0, 1, 0, 0, SOLID_TILE), 4, y, 32, h);
}
static void showBanner(void) {
    char b[12]; strcpy(b, "ROUND "); intToStr(round_ + 1, b + 6, 1);
    panel(9, 8); center(b, 10); center(RT[round_], 12); center(RS[round_], 14);
}
static void showOverlay(const char *a, const char *b) {
    char s[24]; panel(9, 8); center(a, 10);
    strcpy(s, "SCORE "); uintToStr(score, s + 6, 6); center(s, 12);
    if (b) center(b, 13);
    center("PRESS START", 15);
}
static void showTitle(void) {
    u16 x, y;
    clearEntities(); rel(&pspr);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    PAL_setPalette(PAL0, pal_title, CPU);
    VDP_loadTileData(titleTiles, TILE_USER_INDEX, TITLE_NTILES, CPU);
    for (y = 0; y < 28; y++) for (x = 0; x < 40; x++)
        VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_USER_INDEX + titleMap[y * 40 + x]), x, y);
    state = ST_TITLE; paused = 0;
    musicPlay(SONG_TITLE, 0);
}
static void startGame(void) {
    setRandomSeed(tick | 1);
    score = 0; round_ = 0; pn = 1; act = 0; party[0].id = 0; party[0].hp = 3;
    VDP_clearTextArea(0, 23, 40, 1);
    buildRound(); showBanner(); state = ST_BANNER; stateT = 0;
}

/* ================= combat ================= */
static void killBul(Bul *b) { rel(&b->spr); b->on = 0; }
static void hitEnemy(u8 i, u8 d) {
    Enemy *e = &en[i]; u16 pts;
    e->hp -= d;
    if (e->hp > 0) { sfx(SF_HIT); return; }
    pts = (e->type == E_BOSS) ? 3000 : (e->type == E_JAN ? 200 : (e->type == E_GUNNER ? 150 : 100));
    if (comboT > 0) combo++; else combo = 1;
    comboT = 90;
    addScore(pts * (combo > 5 ? 5 : combo));
    rel(&e->spr); e->on = 0;
    spawnFx(e->x8 >> 3, e->y8 >> 3, 2);
    if (e->type == E_BOSS) {
        u8 k; locked = 0; drawExit(); sfx(SF_CLEAR); say("THE SULTAN IS DEFEATED! TO THE EXIT!");
        for (k = 0; k < 3; k++) spawnFx((e->x8 >> 3) - 12 + k * 12, (e->y8 >> 3) - 8 + k * 6, 7);
    } else { sfx(SF_KILL); dropItem(e->x8, e->y8); }
}
static void push(Enemy *e, s16 ex, s16 ey, s16 d, s16 pixels) {
    s16 k, nx, ny;
    if (e->type == E_BOSS) return;
    for (k = 0; k < pixels / 2; k++) {
        nx = e->x8 + (ex * 16) / d; if (freeAt(nx >> 3, e->y8 >> 3)) e->x8 = nx;
        ny = e->y8 + (ey * 16) / d; if (freeAt(e->x8 >> 3, ny >> 3)) e->y8 = ny;
    }
}
static void hurt(void) {
    if (inv > 0) return;
    inv = 70; party[act].hp--; hudDirty = 1; sfx(SF_HURT);
    if (party[act].hp <= 0) {
        char m[40]; u8 i;
        strcpy(m, CH[party[act].id].name); strcat(m, " HAS FALLEN!"); say(m); sfx(SF_DIE);
        for (i = act; i + 1 < pn; i++) party[i] = party[i + 1];
        pn--;
        if (pn == 0) {
            state = ST_OVER; showOverlay("THE TALE ENDS HERE...", NULL); musicPlay(SONG_OVER, 0); return;
        }
        act %= pn; spawnPlayer(); inv = 90;
    }
}
static void shoot(const CharDef *c, s16 off) {
    u8 i; Bul *b; s16 vx, vy;
    for (i = 0; i < MAX_BL; i++) if (!bl[i].on) {
        b = &bl[i]; vx = face_x * c->bspd; vy = face_y * c->bspd;
        if (face_x && face_y) { vx = vx * 181 / 256; vy = vy * 181 / 256; }
        vx += -face_y * off; vy += face_x * off;
        b->x8 = px8; b->y8 = py8; b->vx = vx; b->vy = vy; b->life = c->life;
        b->dmg = c->dmg; b->pierce = c->pierce; b->hit = 0; b->on = 1;
        b->spr = mk(&spr_fx, (px8 >> 3) - 4, (py8 >> 3) - 4, PAL2);
        if (b->spr) SPR_setFrame(b->spr, 0);
        return;
    }
}
static void attack(const CharDef *c) {
    u8 i; s16 R, ex, ey, d; s32 dot; bool melee = (c->type == W_MELEE);
    cd = c->cd;
    if (c->type == W_SHOT) {
        sfx(SF_SHOT);
        if (party[act].id == 0 && pn >= 5)      { shoot(c, 0); shoot(c, 7); shoot(c, -7); }   /* Baron + full company */
        else if (party[act].id == 0 && pn >= 3) { shoot(c, 4); shoot(c, -4); }
        else shoot(c, 0);
        return;
    }
    sfx(melee ? SF_SWING : SF_GUST);
    R = melee ? 26 : 56;
    for (i = 0; i < MAX_EN; i++) {
        if (!en[i].on) continue;
        ex = (en[i].x8 - px8) >> 3; ey = (en[i].y8 - py8) >> 3; d = dist(ex, ey); if (d < 1) d = 1;
        dot = (s32)ex * face_x + (s32)ey * face_y;
        if (d < (en[i].type == E_BOSS ? R + 14 : R) && (melee ? dot * 5 > d : dot * 10 > d * 3)) {
            hitEnemy(i, c->dmg);
            if (en[i].on) { push(&en[i], ex, ey, d, melee ? 6 : 26); if (!melee && en[i].type != E_BOSS) en[i].stun = 40; }
        }
    }
    for (i = 0; i < MAX_EB; i++) if (eb[i].on &&
        dist((eb[i].x8 - px8) >> 3, (eb[i].y8 - py8) >> 3) < R) killBul(&eb[i]);
    spawnFx((px8 >> 3) + face_x * 14, (py8 >> 3) + face_y * 14, melee ? 2 : 3);
}
static void fireEnemy(Enemy *e, s16 ex, s16 ey, s16 d, s16 k, u8 frame) {
    u8 j; s16 dx, dy;
    dx = ex * 13 / d; dy = ey * 13 / d;
    for (j = 0; j < MAX_EB; j++) if (!eb[j].on) {
        eb[j].x8 = e->x8; eb[j].y8 = e->y8; eb[j].vx = dx - dy * k / 3; eb[j].vy = dy + dx * k / 3;
        eb[j].life = 200; eb[j].on = 1;
        eb[j].spr = mk(&spr_fx, (e->x8 >> 3) - 4, (e->y8 >> 3) - 4, PAL2);
        if (eb[j].spr) SPR_setFrame(eb[j].spr, frame);
        return;
    }
}
static void moveEn(Enemy *e, s16 vx, s16 vy) {
    if (e->type == E_BOSS) {
        s16 nx = e->x8 + vx, ny = e->y8 + vy;
        if (nx > 24 * 8 && nx < 296 * 8) e->x8 = nx;
        if (ny > (OY + 30) * 8 && ny < (OY + 170) * 8) e->y8 = ny;
        return;
    }
    if (freeAt((e->x8 + vx) >> 3, e->y8 >> 3)) e->x8 += vx;
    if (freeAt(e->x8 >> 3, (e->y8 + vy) >> 3)) e->y8 += vy;
}

/* ================= per-frame play logic ================= */
static void playTick(u16 jp, u16 pr) {
    const CharDef *c = &CH[party[act].id];
    s16 dx = 0, dy = 0, sx, sy, ex, ey, d, sp, vx, vy, k, rad; u8 i, j, moving = 0;

    if (jp & BUTTON_LEFT) dx = -1; else if (jp & BUTTON_RIGHT) dx = 1;
    if (jp & BUTTON_UP) dy = -1; else if (jp & BUTTON_DOWN) dy = 1;
    if (dx || dy) {
        moving = 1; face_x = dx; face_y = dy; sx = dx * c->spd; sy = dy * c->spd;
        if (dx && dy) { sx = sx * 181 / 256; sy = sy * 181 / 256; }
        if (freeAt((px8 + sx) >> 3, py8 >> 3)) px8 += sx;
        if (freeAt(px8 >> 3, (py8 + sy) >> 3)) py8 += sy;
    }
    if (cd > 0) cd--;
    if (inv > 0) inv--;
    if (comboT > 0 && --comboT == 0) { combo = 0; hudDirty = 1; }
    if ((pr & BUTTON_A) && pn > 1) { act = (act + 1) % pn; spawnPlayer(); cd = 8; hudDirty = 1; sfx(SF_SWAP); }
    c = &CH[party[act].id];
    if ((jp & (BUTTON_B | BUTTON_C)) && cd <= 0) attack(c);

    /* enemies */
    for (i = 0; i < MAX_EN; i++) {
        Enemy *e = &en[i]; if (!e->on) continue;
        ex = (px8 - e->x8) >> 3; ey = (py8 - e->y8) >> 3; d = dist(ex, ey); if (d < 1) d = 1;
        rad = (e->type == E_BOSS) ? 22 : 11;
        if (e->stun > 0) e->stun--;
        else if (e->type == E_GUARD) {
            sp = 4 + ((round_ + 1) >> 1); moveEn(e, ex * sp / d, ey * sp / d);
        } else if (e->type == E_GUNNER) {
            sp = 3; vx = 0; vy = 0;
            if (d > 110) { vx = ex * sp / d; vy = ey * sp / d; }
            else if (d < 70) { vx = -ex * sp / d; vy = -ey * sp / d; }
            moveEn(e, vx, vy);
            if (--e->cd <= 0 && d < 220) { e->cd = 110; fireEnemy(e, ex, ey, d, 0, 1); }
        } else if (e->type == E_JAN) {
            if (e->w == 0) {
                moveEn(e, ex * 3 / d, ey * 3 / d);
                if (--e->cd <= 0 && d < 110) { e->w = 1; e->tm = 24; }
            } else if (e->w == 1) {
                if (--e->tm <= 0) { e->w = 2; e->tm = 22; e->vx = ex * 20 / d; e->vy = ey * 20 / d; }
            } else {
                moveEn(e, e->vx, e->vy);
                if (--e->tm <= 0) { e->w = 0; e->cd = 70 + random() % 60; }
            }
        } else {                                               /* Sultan */
            if (d > 60) moveEn(e, ex * 3 / d, ey * 3 / d);
            if (--e->cd <= 0) {
                e->cd = (e->hp > 9) ? 95 : 60; sfx(SF_BOSS);
                fireEnemy(e, ex, ey, d, -1, 6); fireEnemy(e, ex, ey, d, 0, 6); fireEnemy(e, ex, ey, d, 1, 6);
                if (e->hp <= 9) { fireEnemy(e, ex, ey, d, -2, 6); fireEnemy(e, ex, ey, d, 2, 6); }
            }
        }
        if (d < rad) hurt();
        if (state != ST_PLAY) return;
        if (e->type == E_BOSS) {
            setAF(e->spr, &e->st, ABS(ex) > ABS(ey) ? 2 : (ey < 0 ? 1 : 0), (tick >> 4) & 1, ABS(ex) > ABS(ey) && ex < 0);
            if (e->spr) SPR_setPosition(e->spr, (e->x8 >> 3) - 16, (e->y8 >> 3) - 30);
        } else {
            faceAF(e->spr, &e->st, ex, ey, (tick >> 3) & 3);
            if (e->spr) SPR_setPosition(e->spr, (e->x8 >> 3) - 8, (e->y8 >> 3) - 12);
        }
    }

    /* player bullets */
    for (j = 0; j < MAX_BL; j++) {
        Bul *b = &bl[j]; if (!b->on) continue;
        b->x8 += b->vx; b->y8 += b->vy;
        if (--b->life <= 0 || solid(b->x8 >> 3, b->y8 >> 3)) { killBul(b); continue; }
        for (i = 0; i < MAX_EN; i++) {
            if (!en[i].on || (b->hit & (1 << i))) continue;
            if (dist((en[i].x8 - b->x8) >> 3, (en[i].y8 - b->y8) >> 3) < (en[i].type == E_BOSS ? 20 : 9)) {
                hitEnemy(i, b->dmg); b->hit |= (1 << i);
                if (!b->pierce) { killBul(b); break; }
            }
        }
        if (b->on && b->spr) SPR_setPosition(b->spr, (b->x8 >> 3) - 4, (b->y8 >> 3) - 4);
    }
    /* enemy bullets */
    for (j = 0; j < MAX_EB; j++) {
        Bul *b = &eb[j]; if (!b->on) continue;
        b->x8 += b->vx; b->y8 += b->vy;
        if (--b->life <= 0 || solid(b->x8 >> 3, b->y8 >> 3)) { killBul(b); continue; }
        if (dist((b->x8 - px8) >> 3, (b->y8 - py8) >> 3) < 7) { killBul(b); hurt(); if (state != ST_PLAY) return; continue; }
        if (b->spr) SPR_setPosition(b->spr, (b->x8 >> 3) - 4, (b->y8 >> 3) - 4);
    }

    /* captives */
    for (i = 0; i < MAX_CAP; i++) {
        Cap *p = &cap[i]; if (!p->on) continue;
        if (dist((p->x8 - px8) >> 3, (p->y8 - py8) >> 3) < 13) {
            u8 id = 0; p->on = 0; rel(&p->spr);
            for (k = 1; k < 5 && !id; k++) {
                bool have = FALSE;
                for (j = 0; j < pn; j++) if (party[j].id == k) have = TRUE;
                if (!have) id = k;
            }
            if (id) {
                char m[48]; party[pn].id = id; party[pn].hp = 3; pn++;
                strcpy(m, CH[id].name); strcat(m, " JOINS! "); strcat(m, CH[id].quote); say(m);
            } else {
                for (j = 0; j < pn; j++) if (party[j].hp < 3) party[j].hp++;
                say("THE COMPANY IS REFRESHED!");
            }
            sfx(SF_RESCUE); addScore(500); hudDirty = 1;
        } else {
            setAF(p->spr, &p->st, 0, ((tick >> 4) & 1) ? 1 : 3, 0);
            if (p->spr) SPR_setPosition(p->spr, (p->x8 >> 3) - 8, (p->y8 >> 3) - 12);
        }
    }

    /* pickups */
    for (i = 0; i < MAX_PK; i++) {
        Pick *p = &pk[i]; if (!p->on) continue;
        if (--p->life <= 0) { rel(&p->spr); p->on = 0; continue; }
        if (dist((p->x8 - px8) >> 3, (p->y8 - py8) >> 3) < 11) {
            if (p->kind == 4) {
                u8 w = act;
                for (j = 0; j < pn; j++) if (party[j].hp < party[w].hp) w = j;
                if (party[w].hp < 3) { party[w].hp++; hudDirty = 1; } else addScore(200);
            } else addScore(300);
            sfx(SF_PICK); rel(&p->spr); p->on = 0; continue;
        }
        if (p->spr) {
            SPR_setVisibility(p->spr, (p->life < 120 && ((p->life >> 3) & 1)) ? HIDDEN : VISIBLE);
            SPR_setPosition(p->spr, (p->x8 >> 3) - 4, (p->y8 >> 3) - 4 + (((tick >> 4) & 1) ? 0 : -1));
        }
    }
    for (i = 0; i < MAX_FX; i++) if (fxs[i].t && --fxs[i].t == 0) rel(&fxs[i].spr);

    /* player sprite */
    faceAF(pspr, &pstate, face_x, face_y, moving ? (tick >> 3) & 3 : 0);
    if (pspr) {
        SPR_setPosition(pspr, (px8 >> 3) - 8, (py8 >> 3) - 12);
        SPR_setVisibility(pspr, (inv > 0 && ((inv >> 2) & 1)) ? HIDDEN : VISIBLE);
    }
    if (msgT > 0 && --msgT == 0) VDP_clearTextArea(0, 26, 40, 2);

    /* exit gate */
    if ((py8 >> 3) < OY + 14 && (px8 >> 3) > 128 && (px8 >> 3) < 192) {
        if (locked) {
            if (msgT < 100) say("DEFEAT THE SULTAN FIRST!");
        } else {
            u8 left = 0;
            for (i = 0; i < MAX_EN; i++) if (en[i].on) left = 1;
            if (!left) { addScore(1000); sfx(SF_CLEAR); }
            addScore(1000); round_++;
            if (round_ >= NR) {
                state = ST_WIN; showOverlay("EVERY WORD IS TRUE!", "THE BARON TELLS HIS TALE"); musicPlay(SONG_WIN, 0);
            } else {
                for (j = 0; j < pn; j++) if (party[j].hp < 3) party[j].hp++;
                buildRound(); showBanner(); state = ST_BANNER; stateT = 0;
            }
        }
    }
}

static void cycTick(void) {
    u8 k;
    if (!CYN[round_] || (tick % 10)) return;
    cph = (cph + 1) % 3;
    for (k = 0; k < 3; k++) PAL_setColor(CYI[round_][k], cbase[(k + cph) % 3]);
}

int main(bool hardReset) {
    u16 jp, pr; u8 i;
    VDP_setScreenWidth320();
    SPR_init();
    VDP_setTextPlane(BG_A); VDP_setTextPalette(PAL3); VDP_setTextPriority(1);
    for (i = 1; i < 16; i++) PAL_setColor(48 + i, 0x0EEE);          /* PAL3: white text */
    PAL_setPalette(PAL2, pal_enemy, CPU);
    VDP_setBackgroundColor(0);
    showTitle();

    while (TRUE) {
        jp = JOY_readJoypad(JOY_1); pr = jp & ~prevJp; prevJp = jp; tick++;
        switch (state) {
        case ST_TITLE:
            if ((tick & 63) == 0) center("PRESS START", 23);
            if ((tick & 63) == 40) VDP_clearTextArea(0, 23, 40, 1);
            if (pr & BUTTON_START) startGame();
            break;
        case ST_BANNER:
            cycTick();
            if (++stateT > 110 || (pr & BUTTON_START)) {
                VDP_clearTileMapRect(BG_A, 4, 9, 32, 8); state = ST_PLAY; hudDirty = 1;
            }
            break;
        case ST_PLAY:
            if (pr & BUTTON_START) {
                paused = !paused;
                if (paused) { psgSilence(); panel(12, 3); center("PAUSED", 13); }
                else VDP_clearTileMapRect(BG_A, 4, 12, 32, 3);
            }
            if (!paused) {
                cycTick();
                playTick(jp, pr);
                if (hudDirty && state == ST_PLAY) { hud(); hudDirty = 0; }
            }
            break;
        default:                                    /* game over / win */
            if (pr & BUTTON_START) showTitle();
            break;
        }
        if (!paused) { musicTick(); sfxTick(); }
        SPR_update();
        SYS_doVBlankProcess();
    }
    return 0;
}
