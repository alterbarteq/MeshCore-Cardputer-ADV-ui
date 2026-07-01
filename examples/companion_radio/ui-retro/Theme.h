#pragma once
#include <stdint.h>

// ─────────────────────────────────────────────────
//  RGB565 colour helpers
// ─────────────────────────────────────────────────
#define RGB565(r,g,b) ((uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|((b>>3))))

// ─────────────────────────────────────────────────
//  Raw terminal palette — near-mono grey on black,
//  single violet accent (no green-phosphor CRT look)
// ─────────────────────────────────────────────────
#define C_BG          RGB565( 10, 10, 14)   // near-black background
#define C_BG2         RGB565( 22, 22, 28)   // slightly lighter panel bg
#define C_BG3         RGB565( 16, 16, 21)   // status-bar bg
#define C_TEXT        RGB565(225,225,232)   // primary text (light grey)
#define C_TEXT_MID    RGB565(150,150,160)   // secondary text (mid grey)
#define C_TEXT_DIM    RGB565( 80, 80, 92)   // dim text / borders / disabled
#define C_WARN        RGB565(255,176,  0)   // amber   – warnings / relay
#define C_ACCENT      RGB565(168,120,255)   // violet  – own node / active tab / highlight
#define C_RED_ERR     RGB565(255, 60, 60)   // red     – errors
#define C_WHITE       RGB565(255,255,255)
#define C_BLACK       RGB565(  0,  0,  0)

// ─────────────────────────────────────────────────
//  Layout constants (240 × 135 landscape)
// ─────────────────────────────────────────────────
#define SCREEN_W      240
#define SCREEN_H      135
#define TOPBAR_H       13   // tab bar height
#define STATUSBAR_H    10   // bottom status bar
#define CONTENT_Y    (TOPBAR_H + 1)
#define CONTENT_H    (SCREEN_H - TOPBAR_H - 1 - STATUSBAR_H - 1)
// CONTENT_H = 135 - 13 - 1 - 10 - 1 = 110  px

// ─────────────────────────────────────────────────
//  Font sizes (M5GFX setTextSize)
// ─────────────────────────────────────────────────
#define FONT_SM   1    //  6×8 px  – status bar, dense lists
#define FONT_MD   1    //  same – we always use size 1 for the small 240×135
#define CHAR_W    6
#define CHAR_H    8
#define LINE_H    9    // 8px + 1px gap

// ─────────────────────────────────────────────────
//  Tabs
// ─────────────────────────────────────────────────
// F4 laczy dawny "MOJ WEZEL" i "USTAWIENIA" w jedna przewijalna liste
// (patrz ScreenSettings) — F5 jest teraz wolne.
enum class Tab : uint8_t {
    CHAT     = 0,
    NODES    = 1,
    MAP      = 2,
    SETTINGS = 3,
    COUNT    = 4
};

// Tab labels shown in top bar
static const char* const TAB_LABELS[] = {
    "F1:CZAT",
    "F2:WEZLY",
    "F3:MAPA",
    "F4:UST",
};
