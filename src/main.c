/* The Adventures of Baron Munchausen - SGDK (Sega Genesis) rescue-and-fight game.
 * Controls: D-pad move, B/C attack, A swap companion, START begin/continue.
 *
 * Palette plan:  PAL0 = round tileset   PAL1 = active party member
 *                PAL2 = enemies/captives/effects (shared)   PAL3 = text
 * Positions are stored in 1/8 pixel units (>>3 gives pixels).
 */
#include <genesis.h>
#include "resources.h"
#include "palettes.h"

#define COLS 20
#define ROWS 12
#define OY 16                       /* top 2 tile rows = HUD */
#define MAX_EN 16
#define MAX_CAP 2
#define MAX_BL 8
#define MAX_FX 4
#define SOLID_TILE (TILE_USER_INDEX + 16)
#define ABS(x) ((x) < 0 ? -(x) : (x))

enum { ST_TITLE, ST_BANNER, ST_PLAY, ST_OVER, ST_WIN };
enum { W_SHOT, W_MELEE, W_GUST };

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
static const TileSet *TS[4]  = { &ts_siege, &ts_sea, &ts_moon, &ts_whale };
static const u16 *TP[4]      = { pal_tiles_siege, pal_tiles_sea, pal_tiles_moon, pal_tiles_whale };
static const char *RT[4] = { "THE TURKISH SIEGE", "THE STORMY SEA", "THE MOON", "THE BELLY OF THE WHALE" };
static const char *RS[4] = { "THE SULTAN'S GUARDS HOLD MY FRIENDS!", "ADRIFT ON A SEA OF PERIL, GENTLEMEN!",
                             "WE CLIMBED A BEANSTALK TO THE MOON!", "SWALLOWED WHOLE, YET UNDAUNTED!" };
static const u8 NCAP[4] = { 1, 1, 2, 1 };
static const u32 solidTile[8] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                                  0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

typedef struct { u8 id; s8 hp; } Member;
typedef struct { s16 x8, y8, cd; s8 hp; u8 type, on, st; Sprite *spr; } Enemy;
typedef struct { s16 x8, y8; u8 on, st; Sprite *spr; } Cap;
typedef struct { s16 x8, y8, vx, vy, life; u8 dmg, pierce, on; u16 hit; Sprite *spr; } Bul;
typedef struct { Sprite *spr; u8 t; } FxS;

static u8 grid[ROWS][COLS];
static Enemy en[MAX_EN]; static Cap cap[MAX_CAP]; static Bul bl[MAX_BL], eb[MAX_BL]; static FxS fxs[MAX_FX];
static Member party[5];
static u8 state, round_, pn, act, hudDirty, pstate;
static u32 score;
static u16 tick, prevJp;
static s16 stateT, px8, py8, cd, inv, msgT;
static s8 face_x, face_y;
static Sprite *pspr;

/* ---------- helpers ---------- */
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
    u8 n = a | (f << 2) | (h << 3);
    if (!s || *st == n) return;
    *st = n; SPR_setAnimAndFrame(s, a, f); SPR_setHFlip(s, h);
}
static void faceAF(Sprite *s, u8 *st, s16 dx, s16 dy, u8 f) {
    if (dx != 0 && ABS(dx) >= ABS(dy)) setAF(s, st, 2, f, dx < 0);
    else if (dy < 0) setAF(s, st, 1, f, 0);
    else setAF(s, st, 0, f, 0);
}
static void center(const char *s, u16 row) { VDP_drawText(s, (40 - strlen(s)) / 2, row); }
static void say(const char *s) {
    VDP_clearTextArea(0, 26, 40, 2); center(s, 26); msgT = 150;
}
static void addScore(u16 n) { score += n; hudDirty = 1; }

static void hud(void) {
    char b[48], t[8]; u8 i;
    VDP_clearTextArea(0, 0, 40, 2);
    strcpy(b, "RD "); intToStr(round_ + 1, b + 3, 1); VDP_drawText(b, 1, 0);
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

/* ---------- entity management ---------- */
static void clearEntities(void) {
    u8 i;
    for (i = 0; i < MAX_EN; i++)  { rel(&en[i].spr); en[i].on = 0; }
    for (i = 0; i < MAX_CAP; i++) { rel(&cap[i].spr); cap[i].on = 0; }
    for (i = 0; i < MAX_BL; i++)  { rel(&bl[i].spr); bl[i].on = 0; rel(&eb[i].spr); eb[i].on = 0; }
    for (i = 0; i < MAX_FX; i++)  { rel(&fxs[i].spr); fxs[i].t = 0; }
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
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b),     x,     y);
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b + 1), x + 1, y);
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b + 8), x,     y + 1);
    VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, b + 9), x + 1, y + 1);
}
static void buildRound(void) {
    u8 x, y, i, n; s16 ax, ay;
    clearEntities();
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    PAL_setPalette(PAL0, TP[round_], CPU); PAL_setColor(15, 0x0200);   /* colour 15 = banner panel */
    VDP_loadTileSet(TS[round_], TILE_USER_INDEX, CPU);
    VDP_loadTileData(solidTile, SOLID_TILE, 1, CPU);
    for (y = 0; y < ROWS; y++) for (x = 0; x < COLS; x++) grid[y][x] = 0;
    n = 16 + round_ * 4;
    for (i = 0; i < n; i++) grid[1 + random() % (ROWS - 3)][random() % COLS] = 1;
    for (x = 7; x < 13; x++) grid[1][x] = 0;
    for (y = 0; y < ROWS; y++) for (x = 0; x < COLS; x++) {
        if (grid[y][x]) block(2, x, y);
        else if (y == 0 && x >= 8 && x < 12) block(3, x, y);
        else block(((x * 7 + y * 13) & 3) == 0 ? 1 : 0, x, y);
    }
    px8 = 160 * 8; py8 = 184 * 8; face_x = 0; face_y = -1; cd = 0; inv = 60;
    n = 5 + 3 * round_; if (n > MAX_EN) n = MAX_EN;
    for (i = 0; i < n; i++) {
        Enemy *e = &en[i]; e->type = (round_ > 0 && i % 3 == 2);
        e->hp = e->type ? 2 : 1 + (round_ > 2); e->cd = 30 + random() % 90; e->on = 1; e->st = 255;
        freePos(&ax, &ay); e->x8 = ax; e->y8 = ay;
        e->spr = mk(e->type ? &spr_gunner : &spr_guard, (ax >> 3) - 8, (ay >> 3) - 12, PAL2);
    }
    for (i = 0; i < NCAP[round_]; i++) {
        freePos(&ax, &ay); cap[i].x8 = ax; cap[i].y8 = ay; cap[i].on = 1; cap[i].st = 255;
        cap[i].spr = mk(&spr_captive, (ax >> 3) - 8, (ay >> 3) - 12, PAL2);
    }
    spawnPlayer(); hudDirty = 1; msgT = 0;
}

/* ---------- screens ---------- */
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
    clearEntities(); rel(&pspr);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    PAL_setPalette(PAL1, pal_baron, CPU);
    center("THE ADVENTURES OF", 5); center("BARON MUNCHAUSEN", 8);
    center("RESCUE YOUR COMPANIONS", 18); center("REACH THE EXIT", 19);
    center("PRESS START", 23);
    pspr = mk(&spr_baron, 152, 100, PAL1); pstate = 255; state = ST_TITLE;
}
static void startGame(void) {
    setRandomSeed(tick | 1);
    score = 0; round_ = 0; pn = 1; act = 0; party[0].id = 0; party[0].hp = 3;
    buildRound(); showBanner(); state = ST_BANNER; stateT = 0;
}

/* ---------- combat ---------- */
static void killBul(Bul *b) { rel(&b->spr); b->on = 0; }
static void hitEnemy(u8 i, u8 d) {
    Enemy *e = &en[i]; e->hp -= d;
    if (e->hp <= 0) { rel(&e->spr); e->on = 0; addScore(100); spawnFx(e->x8 >> 3, e->y8 >> 3, 2); }
}
static void push(Enemy *e, s16 ex, s16 ey, s16 d, s16 pixels) {
    s16 k, nx, ny;
    for (k = 0; k < pixels / 2; k++) {
        nx = e->x8 + (ex * 16) / d; if (freeAt(nx >> 3, e->y8 >> 3)) e->x8 = nx;
        ny = e->y8 + (ey * 16) / d; if (freeAt(e->x8 >> 3, ny >> 3)) e->y8 = ny;
    }
}
static void hurt(void) {
    if (inv > 0) return;
    inv = 70; party[act].hp--; hudDirty = 1;
    if (party[act].hp <= 0) {
        char m[40]; strcpy(m, CH[party[act].id].name); strcat(m, " HAS FALLEN!"); say(m);
        for (u8 i = act; i + 1 < pn; i++) party[i] = party[i + 1];
        pn--;
        if (pn == 0) { state = ST_OVER; showOverlay("THE TALE ENDS HERE...", NULL); return; }
        act %= pn; spawnPlayer(); inv = 90;
    }
}
static void attack(const CharDef *c) {
    u8 i; s16 R, ex, ey, d; s32 dot; bool melee = (c->type == W_MELEE);
    cd = c->cd;
    if (c->type == W_SHOT) {
        for (i = 0; i < MAX_BL; i++) if (!bl[i].on) {
            Bul *b = &bl[i]; s16 vx = face_x * c->bspd, vy = face_y * c->bspd;
            if (face_x && face_y) { vx = vx * 181 / 256; vy = vy * 181 / 256; }
            b->x8 = px8; b->y8 = py8; b->vx = vx; b->vy = vy; b->life = c->life;
            b->dmg = c->dmg; b->pierce = c->pierce; b->hit = 0; b->on = 1;
            b->spr = mk(&spr_fx, (px8 >> 3) - 4, (py8 >> 3) - 4, PAL2);
            if (b->spr) SPR_setFrame(b->spr, 0);
            break;
        }
        return;
    }
    R = melee ? 26 : 56;
    for (i = 0; i < MAX_EN; i++) {
        if (!en[i].on) continue;
        ex = (en[i].x8 - px8) >> 3; ey = (en[i].y8 - py8) >> 3; d = dist(ex, ey); if (d < 1) d = 1;
        dot = (s32)ex * face_x + (s32)ey * face_y;
        if (d < R && (melee ? dot * 5 > d : dot * 10 > d * 3)) {
            hitEnemy(i, c->dmg);
            if (en[i].on) push(&en[i], ex, ey, d, melee ? 6 : 26);
        }
    }
    if (!melee) for (i = 0; i < MAX_BL; i++) if (eb[i].on &&
        dist((eb[i].x8 - px8) >> 3, (eb[i].y8 - py8) >> 3) < R) killBul(&eb[i]);
    spawnFx((px8 >> 3) + face_x * 14, (py8 >> 3) + face_y * 14, melee ? 2 : 3);
}

/* ---------- per-frame play logic ---------- */
static void playTick(u16 jp, u16 pr) {
    const CharDef *c = &CH[party[act].id];
    s16 dx = 0, dy = 0, sx, sy, ex, ey, d, sp, vx, vy, k; u8 i, j, moving = 0;

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
    if ((pr & BUTTON_A) && pn > 1) { act = (act + 1) % pn; spawnPlayer(); cd = 8; hudDirty = 1; }
    c = &CH[party[act].id];
    if ((jp & (BUTTON_B | BUTTON_C)) && cd <= 0) attack(c);

    /* enemies */
    for (i = 0; i < MAX_EN; i++) {
        Enemy *e = &en[i]; if (!e->on) continue;
        ex = (px8 - e->x8) >> 3; ey = (py8 - e->y8) >> 3; d = dist(ex, ey); if (d < 1) d = 1;
        sp = e->type ? 3 : 4 + ((round_ + 1) >> 1);
        vx = 0; vy = 0;
        if (!e->type || d > 110) { vx = ex * sp / d; vy = ey * sp / d; }
        else if (d < 70)         { vx = -ex * sp / d; vy = -ey * sp / d; }
        if (freeAt((e->x8 + vx) >> 3, e->y8 >> 3)) e->x8 += vx;
        if (freeAt(e->x8 >> 3, (e->y8 + vy) >> 3)) e->y8 += vy;
        if (e->type) {
            if (--e->cd <= 0 && d < 220) {
                e->cd = 110;
                for (j = 0; j < MAX_BL; j++) if (!eb[j].on) {
                    eb[j].x8 = e->x8; eb[j].y8 = e->y8; eb[j].vx = ex * 13 / d; eb[j].vy = ey * 13 / d;
                    eb[j].life = 200; eb[j].on = 1;
                    eb[j].spr = mk(&spr_fx, (e->x8 >> 3) - 4, (e->y8 >> 3) - 4, PAL2);
                    if (eb[j].spr) SPR_setFrame(eb[j].spr, 1);
                    break;
                }
            }
        } else if (d < 11) hurt();
        if (state != ST_PLAY) return;
        faceAF(e->spr, &e->st, ex, ey, (tick >> 4) & 1);
        if (e->spr) SPR_setPosition(e->spr, (e->x8 >> 3) - 8, (e->y8 >> 3) - 12);
    }

    /* player bullets */
    for (j = 0; j < MAX_BL; j++) {
        Bul *b = &bl[j]; if (!b->on) continue;
        b->x8 += b->vx; b->y8 += b->vy;
        if (--b->life <= 0 || solid(b->x8 >> 3, b->y8 >> 3)) { killBul(b); continue; }
        for (i = 0; i < MAX_EN; i++) {
            if (!en[i].on || (b->hit & (1 << i))) continue;
            if (dist((en[i].x8 - b->x8) >> 3, (en[i].y8 - b->y8) >> 3) < 9) {
                hitEnemy(i, b->dmg); b->hit |= (1 << i);
                if (!b->pierce) { killBul(b); break; }
            }
        }
        if (b->on && b->spr) SPR_setPosition(b->spr, (b->x8 >> 3) - 4, (b->y8 >> 3) - 4);
    }
    /* enemy bullets */
    for (j = 0; j < MAX_BL; j++) {
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
            addScore(500); hudDirty = 1;
        } else {
            setAF(p->spr, &p->st, 0, (tick >> 5) & 1, 0);
            if (p->spr) SPR_setPosition(p->spr, (p->x8 >> 3) - 8, (p->y8 >> 3) - 12);
        }
    }
    for (i = 0; i < MAX_FX; i++) if (fxs[i].t && --fxs[i].t == 0) rel(&fxs[i].spr);

    /* player sprite */
    faceAF(pspr, &pstate, face_x, face_y, moving ? (tick >> 4) & 1 : 0);
    if (pspr) {
        SPR_setPosition(pspr, (px8 >> 3) - 8, (py8 >> 3) - 12);
        SPR_setVisibility(pspr, (inv > 0 && ((inv >> 2) & 1)) ? HIDDEN : VISIBLE);
    }
    if (msgT > 0 && --msgT == 0) VDP_clearTextArea(0, 26, 40, 2);

    /* exit gate */
    if ((py8 >> 3) < OY + 14 && (px8 >> 3) > 128 && (px8 >> 3) < 192) {
        addScore(1000); round_++;
        if (round_ >= 4) { state = ST_WIN; showOverlay("EVERY WORD IS TRUE!", "THE BARON TELLS HIS TALE"); }
        else {
            for (j = 0; j < pn; j++) if (party[j].hp < 3) party[j].hp++;
            buildRound(); showBanner(); state = ST_BANNER; stateT = 0;
        }
    }
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
            faceAF(pspr, &pstate, 0, 1, (tick >> 4) & 1);
            if (pr & BUTTON_START) startGame();
            break;
        case ST_BANNER:
            if (++stateT > 110 || (pr & BUTTON_START)) {
                VDP_clearTileMapRect(BG_A, 4, 9, 32, 8); state = ST_PLAY; hudDirty = 1;
            }
            break;
        case ST_PLAY:
            playTick(jp, pr);
            if (hudDirty && state == ST_PLAY) { hud(); hudDirty = 0; }
            break;
        default:                                    /* game over / win */
            if (pr & BUTTON_START) showTitle();
            break;
        }
        SPR_update();
        SYS_doVBlankProcess();
    }
    return 0;
}
