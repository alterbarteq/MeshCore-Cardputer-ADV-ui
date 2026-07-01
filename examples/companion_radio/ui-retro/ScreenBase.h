#pragma once
#include <M5Cardputer.h>
#include "Theme.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ScreenBase
//  Every tab screen inherits from this.
//  draw()     – called when the screen needs to repaint.
//  onKey()    – called with the current KeysState; return true if consumed.
//  onEnter()  – called when this tab becomes active.
//  onLeave()  – called when switching away from this tab.
// ─────────────────────────────────────────────────────────────────────────────
class ScreenBase {
public:
    virtual ~ScreenBase() {}

    // Must implement:
    virtual void draw() = 0;
    virtual bool onKey(Keyboard_Class::KeysState& ks) = 0;

    // Optional hooks:
    virtual void onEnter() {}
    virtual void onLeave() {}

    // Called by UITaskRetro when a new mesh message arrives
    virtual void onNewMessage(const char* from, const char* text, bool is_channel) {}

    // Called when mesh connection state changes
    virtual void onConnectionChange(bool connected) {}

protected:
    // Helpers for all screens

    // Fill the content area with the background colour
    static void clearContent() {
        M5Cardputer.Display.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    }

    // Draw a horizontal divider line inside the content area
    static void hline(int y, uint16_t color = C_TEXT_DIM) {
        M5Cardputer.Display.drawFastHLine(0, y, SCREEN_W, color);
    }

    // Draw one text line
    static void textLine(int x, int y, const char* str,
                         uint16_t fg = C_TEXT, uint16_t bg = C_BG,
                         uint8_t sz = FONT_SM) {
        M5Cardputer.Display.setTextSize(sz);
        M5Cardputer.Display.setTextColor(fg, bg);
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print(str);
    }

    // Draw text truncated to max_chars characters
    static void textTrunc(int x, int y, const char* str, int max_chars,
                          uint16_t fg = C_TEXT, uint16_t bg = C_BG) {
        char buf[128];
        int len = strlen(str);
        if (len > max_chars) {
            strncpy(buf, str, max_chars - 1);
            buf[max_chars - 1] = '>';
            buf[max_chars] = '\0';
        } else {
            strncpy(buf, str, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        }
        textLine(x, y, buf, fg, bg);
    }

    // Draw a reverse-video selection bar (fg/bg swap, like less/vim/ncurses)
    static void selBar(int y, int h = LINE_H) {
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, h, C_TEXT);
    }
};
