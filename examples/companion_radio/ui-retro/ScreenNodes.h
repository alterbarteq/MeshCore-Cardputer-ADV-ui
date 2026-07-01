#pragma once
#include "ScreenBase.h"
#include <helpers/ContactInfo.h>
#include <helpers/AdvertDataHelpers.h>

// ─────────────────────────────────────────────────────────────────────────────
//  ScreenNodes
//  Tabular list of all known contacts: callsign | hops | type | age
//  Up/Down to scroll, Fn+Enter to show detail of selected node.
// ─────────────────────────────────────────────────────────────────────────────

#define NODES_MAX   200
#define NODES_ROWS  ((CONTENT_H - LINE_H) / LINE_H)   // header + rows

struct NodeEntry {
    char    name[32];
    uint8_t type;         // ADV_TYPE_*
    int8_t  out_path_len; // hop count (-1 = unknown)
    int32_t gps_lat;
    int32_t gps_lon;
    uint32_t last_seen;   // millis()-based estimate (seconds ago)
};

class ScreenNodes : public ScreenBase {
public:
    void setNodes(const NodeEntry* entries, int count) {
        int n = count < NODES_MAX ? count : NODES_MAX;
        memcpy(_nodes, entries, n * sizeof(NodeEntry));
        _count = n;
        _need_redraw = true;
    }

    void onEnter() override { _need_redraw = true; }

    // ── draw ─────────────────────────────────────────────────────────────────
    void draw() override {
        if (!_need_redraw) return;
        _need_redraw = false;

        M5GFX& d = M5Cardputer.Display;
        clearContent();

        if (_count == 0) {
            textLine(4, CONTENT_Y + 20, "Brak wezlow w sieci...", C_TEXT_DIM);
            return;
        }

        // ── column header ─────────────────────────────────────────────────
        d.setTextSize(FONT_SM);
        d.setTextColor(C_TEXT_DIM, C_BG);
        d.setCursor(2,  CONTENT_Y + 1); d.print("CALLSIGN");
        d.setCursor(116, CONTENT_Y + 1); d.print("HOP");
        d.setCursor(140, CONTENT_Y + 1); d.print("TYP");
        d.setCursor(180, CONTENT_Y + 1); d.print("OSTATNI");
        hline(CONTENT_Y + LINE_H - 1);

        // ── rows ──────────────────────────────────────────────────────────
        int first = _scroll;
        int last  = min(_count, first + NODES_ROWS);

        for (int i = first; i < last; i++) {
            int row = i - first;
            int y   = CONTENT_Y + LINE_H + row * LINE_H;
            NodeEntry& n = _nodes[i];

            bool selected = (i == _sel);
            if (selected) selBar(y);

            // callsign colour: violet = self placeholder, amber = relay, plain = chat
            uint16_t fg = C_TEXT;
            if (n.type == ADV_TYPE_REPEATER) fg = C_WARN;

            uint16_t row_bg = selected ? C_TEXT : C_BG;
            uint16_t row_fg = selected ? C_BG   : fg;
            textTrunc(2, y, n.name, 18, row_fg, row_bg);

            // hops
            char buf[8];
            if (n.out_path_len < 0)      snprintf(buf, sizeof(buf), "?");
            else if (n.out_path_len == 0) snprintf(buf, sizeof(buf), "0");
            else                          snprintf(buf, sizeof(buf), "%d", n.out_path_len);
            textLine(116, y, buf, selected ? C_BG : C_TEXT_MID, row_bg);

            // type badge
            const char* type_str = "CHAT";
            if      (n.type == ADV_TYPE_REPEATER) type_str = "RLY";
            else if (n.type == ADV_TYPE_ROOM)     type_str = "ROOM";
            else if (n.type == ADV_TYPE_SENSOR)   type_str = "SNS";
            textLine(140, y, type_str,
                     selected ? C_BG : (n.type == ADV_TYPE_REPEATER ? C_WARN : C_TEXT_MID),
                     row_bg);

            // last seen
            char age[12];
            uint32_t s = n.last_seen;
            if      (s < 60)    snprintf(age, sizeof(age), "%lus",     (unsigned long)s);
            else if (s < 3600)  snprintf(age, sizeof(age), "%lum",     (unsigned long)(s/60));
            else                snprintf(age, sizeof(age), "%luh",     (unsigned long)(s/3600));
            uint16_t age_col = s < 120 ? C_TEXT : (s < 600 ? C_WARN : C_TEXT_DIM);
            textLine(180, y, age, selected ? C_BG : age_col, row_bg);
        }

        // scroll bar
        if (_count > NODES_ROWS) {
            int bar_h = (CONTENT_H - LINE_H) * NODES_ROWS / _count;
            int bar_y = CONTENT_Y + LINE_H + (CONTENT_H - LINE_H) * _scroll / _count;
            d.fillRect(SCREEN_W - 2, CONTENT_Y + LINE_H, 2, CONTENT_H - LINE_H, C_BG2);
            d.fillRect(SCREEN_W - 2, bar_y, 2, max(2, bar_h), C_TEXT_MID);
        }

        // node count label bottom-right of content
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "[%d/%d]", _sel + 1, _count);
        textLine(SCREEN_W - strlen(cnt) * CHAR_W - 2,
                 CONTENT_Y + 1, cnt, C_TEXT_DIM, C_BG);
    }

    // ── key handling ─────────────────────────────────────────────────────────
    bool onKey(Keyboard_Class::KeysState& ks) override {
        bool consumed = false;

        if (ks.up) {
            if (_sel > 0) { _sel--; _clampScroll(); }
            consumed = true;
        }
        if (ks.down) {
            if (_sel < _count - 1) { _sel++; _clampScroll(); }
            consumed = true;
        }

        if (consumed) _need_redraw = true;
        return consumed;
    }

private:
    NodeEntry _nodes[NODES_MAX] = {};
    int  _count       = 0;
    int  _sel         = 0;
    int  _scroll      = 0;
    bool _need_redraw = true;

    void _clampScroll() {
        if (_sel < _scroll)              _scroll = _sel;
        if (_sel >= _scroll + NODES_ROWS) _scroll = _sel - NODES_ROWS + 1;
    }
};
