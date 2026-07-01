#pragma once
#include "ScreenBase.h"

// Number of channel rows visible at once
#define CH_ROWS  ((CONTENT_H - LINE_H - 3) / LINE_H)

class ScreenChannels : public ScreenBase {
public:
    // ── Callbacks wired up by UITaskRetro ────────────────────────────────────
    using CountFn     = int(*)(void*);
    using GetNameFn   = bool(*)(int idx, char* out, int maxlen, void* ctx);
    using AddFn       = bool(*)(const char* name, void* ctx);
    using GetActiveFn = int(*)(void*);
    using SetActiveFn = void(*)(int idx, void*);
    using GetUnreadFn = int(*)(int idx, void*);

    void setCallbacks(CountFn count, GetNameFn getName,
                      AddFn add,
                      GetActiveFn getAct, SetActiveFn setAct,
                      GetUnreadFn getUnread,
                      void* ctx) {
        _countFn     = count;
        _getNameFn   = getName;
        _addFn       = add;
        _getActiveFn = getAct;
        _setActiveFn = setAct;
        _getUnreadFn = getUnread;
        _ctx         = ctx;
        _need_redraw = true;
    }

    void onEnter() override { _adding = false; _need_redraw = true; }

    // Otwiera od razu w trybie dodawania nowego kanalu (np. z "opt+n" w czacie)
    void startAdding() {
        memset(_add_buf, 0, sizeof(_add_buf));
        _add_buf[0] = '#';
        _adding = true;
        _need_redraw = true;
    }

    // Potrzebne z zewnatrz, zeby odroznic "Enter zatwierdzil nazwe nowego
    // kanalu" (wracamy do listy w tej samej nakladce) od "Enter wybral
    // kanal z listy" (zamykamy nakladke i wracamy do czatu).
    bool isAdding() const { return _adding; }

    void draw() override {
        if (!_need_redraw) return;
        _need_redraw = false;

        M5GFX& d = M5Cardputer.Display;
        clearContent();
        d.setTextSize(FONT_SM);

        if (_adding) { _drawAdd(d); return; }

        int n      = _countFn ? _countFn(_ctx) : 0;
        int active = _getActiveFn ? _getActiveFn(_ctx) : 0;

        // ── Channel list ──────────────────────────────────────────────────────
        int first = _scroll;
        int last  = (n < first + CH_ROWS) ? n : first + CH_ROWS;
        int y = CONTENT_Y + 1;

        for (int i = first; i < last; i++) {
            char name[33] = {};
            if (_getNameFn) _getNameFn(i, name, sizeof(name), _ctx);

            bool sel = (i == _sel);
            bool act = (i == active);

            if (sel) selBar(y);

            uint16_t bg = sel ? C_TEXT : C_BG;

            // Active marker ► (ASCII 16)
            d.setCursor(2, y);
            d.setTextColor(sel ? C_BG : (act ? C_ACCENT : C_TEXT_DIM), bg);
            d.print(act ? ">" : " ");

            // Channel name
            uint16_t fg = sel ? C_BG : (act ? C_ACCENT : C_TEXT_MID);
            d.setTextColor(fg, bg);
            d.setCursor(10, y);
            // Truncate to fit screen
            char disp[38] = {};
            strncpy(disp, name, 37);
            d.print(disp);

            // Liczba nieprzeczytanych wiadomosci na tym kanale, na zolto —
            // znika, gdy uzytkownik faktycznie przelaczy sie na ten kanal
            // (patrz UITaskRetro::_syncActiveChannelToChat).
            int unread = _getUnreadFn ? _getUnreadFn(i, _ctx) : 0;
            if (unread > 0) {
                char cnt[16];
                snprintf(cnt, sizeof(cnt), " (%d)", unread);
                d.setTextColor(C_WARN, bg);
                d.print(cnt);
            }

            y += LINE_H;
        }

        if (n == 0) {
            textLine(4, CONTENT_Y + 28, "Brak kanalow.", C_TEXT_DIM);
            textLine(4, CONTENT_Y + 40, "fn+N = dodaj (#warszawa)", C_TEXT_DIM);
        }

        // ── Help bar ──────────────────────────────────────────────────────────
        int hy = CONTENT_Y + CONTENT_H - LINE_H;
        hline(hy - 1);
        textLine(2, hy, "Enter=aktywuj  N=nowy kanal", C_TEXT_DIM);
    }

    bool onKey(Keyboard_Class::KeysState& ks) override {
        bool consumed = false;
        int n = _countFn ? _countFn(_ctx) : 0;

        // ── Add-channel input mode ────────────────────────────────────────────
        if (_adding) {
            if (ks.enter) {
                if (strlen(_add_buf) > 1) {   // at least '#' + one char
                    if (_addFn) _addFn(_add_buf, _ctx);
                }
                _adding = false;
                consumed = true;
            } else if (ks.del || ks.backspace) {
                int l = strlen(_add_buf);
                if (l > 1) _add_buf[l-1] = '\0';
                else        _adding = false;   // cancel
                consumed = true;
            } else {
                for (auto c : ks.word) {
                    int l = strlen(_add_buf);
                    if (l < 31 && c >= 32 && c != ' ') {
                        _add_buf[l] = c; _add_buf[l+1] = '\0';
                    }
                    consumed = true;
                }
            }
            _need_redraw = true;
            return consumed;
        }

        // ── Normal navigation ─────────────────────────────────────────────────
        if (ks.up && _sel > 0)     { _sel--; _clampScroll(); consumed = true; }
        if (ks.down && _sel < n-1) { _sel++; _clampScroll(); consumed = true; }

        if (ks.enter && n > 0) {
            if (_setActiveFn) _setActiveFn(_sel, _ctx);
            consumed = true;
        }

        // N = new channel (no fn needed — no conflict in list navigation)
        for (auto c : ks.word) {
            if (c == 'n' || c == 'N') {
                memset(_add_buf, 0, sizeof(_add_buf));
                _add_buf[0] = '#';
                _adding = true;
                consumed = true;
            }
        }

        if (consumed) _need_redraw = true;
        return consumed;
    }

private:
    CountFn     _countFn     = nullptr;
    GetNameFn   _getNameFn   = nullptr;
    AddFn       _addFn       = nullptr;
    GetActiveFn _getActiveFn = nullptr;
    SetActiveFn _setActiveFn = nullptr;
    GetUnreadFn _getUnreadFn = nullptr;
    void*       _ctx         = nullptr;

    int  _sel = 0, _scroll = 0;
    bool _need_redraw = true;
    bool _adding      = false;
    char _add_buf[33] = {};

    void _clampScroll() {
        if (_sel < _scroll)              _scroll = _sel;
        if (_sel >= _scroll + CH_ROWS)   _scroll = _sel - CH_ROWS + 1;
    }

    void _drawAdd(M5GFX& d) {
        textLine(4, CONTENT_Y + 8,  "Nowy kanal:", C_TEXT_MID);
        textLine(4, CONTENT_Y + 20, "(np. #warszawa, #meshcore)", C_TEXT_DIM);

        // Input field
        int fy = CONTENT_Y + 38;
        d.fillRect(0, fy, SCREEN_W, LINE_H + 2, C_BG2);
        d.setTextColor(C_TEXT, C_BG2);
        d.setCursor(4, fy + 1);
        d.print(_add_buf);
        // Cursor blink
        if ((millis() / 400) % 2 == 0) {
            int cx = 4 + strlen(_add_buf) * CHAR_W;
            d.fillRect(cx, fy + 1, CHAR_W - 1, CHAR_H - 2, C_TEXT_MID);
        }

        textLine(4, CONTENT_Y + 60, "PSK = hash nazwy kanalu", C_TEXT_DIM);
        textLine(4, CONTENT_Y + 70, "(kompatybilny z innymi urzadzeniami)", C_TEXT_DIM);

        int hy = CONTENT_Y + CONTENT_H - LINE_H;
        hline(hy - 1);
        textLine(2, hy, "Enter=zapisz  Del=anuluj", C_TEXT_DIM);
    }
};
