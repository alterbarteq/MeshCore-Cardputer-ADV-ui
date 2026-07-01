#pragma once
#include "ScreenBase.h"
#include "ScreenNodes.h"
#include <WiFi.h>
#include <M5Cardputer.h>

// ScreenMap v0.1 — retro grid with node dots overlay
// WiFi tile download disabled until v0.2 (HTTPClient path issues)
// Shows phosphor dot-grid + node positions from GPS

#define DOT_SELF  4
#define DOT_NODE  3

enum class MapStatus { NO_WIFI, CONNECTING, OK, NO_GPS };

class ScreenMap : public ScreenBase {
public:
    void setWiFi(const char* ssid, const char* pass) {
        strncpy(_ssid, ssid, sizeof(_ssid)-1);
        strncpy(_pass, pass, sizeof(_pass)-1);
    }

    void setOwnPos(int32_t lat6, int32_t lon6) {
        _own_lat=lat6; _own_lon=lon6; _have_gps=true; _need_redraw=true;
    }

    void setNodes(const NodeEntry* entries, int count) {
        _nodes=entries; _node_count=count; _need_redraw=true;
    }

    void onEnter() override { _need_redraw=true; }

    void draw() override {
        if (!_need_redraw) return;
        _need_redraw=false;
        clearContent();
        M5GFX& d=M5Cardputer.Display;

        // phosphor dot-grid
        for (int x=0; x<SCREEN_W; x+=16)
            d.drawFastVLine(x, CONTENT_Y, CONTENT_H, C_TEXT_DIM);
        for (int y=CONTENT_Y; y<CONTENT_Y+CONTENT_H; y+=16)
            d.drawFastHLine(0, y, SCREEN_W, C_TEXT_DIM);

        if (!_have_gps) {
            textLine(4, CONTENT_Y+30, "Brak sygnalu GPS.", C_WARN);
            textLine(4, CONTENT_Y+42, "Mapa wymaga pozycji GPS.", C_TEXT_DIM);
            return;
        }

        // node dots
        int cx=SCREEN_W/2, cy=CONTENT_Y+CONTENT_H/2;
        if (_nodes) {
            for (int i=0; i<_node_count; i++) {
                const NodeEntry& n=_nodes[i];
                if (n.gps_lat==0 && n.gps_lon==0) continue;
                // simple pixel-offset from own position
                int32_t dlat = n.gps_lat - _own_lat;  // in 1e-6 deg
                int32_t dlon = n.gps_lon - _own_lon;
                // scale: ~1.5 px per 0.001 deg at z13
                int scale = 100 + _zoom * 15;
                int sx = cx + (int)(dlon * scale / 100000L);
                int sy = cy - (int)(dlat * scale / 100000L);
                if (sx<0||sx>=SCREEN_W||sy<CONTENT_Y||sy>=CONTENT_Y+CONTENT_H) continue;
                uint16_t col=(n.type==ADV_TYPE_REPEATER)?C_WARN:C_TEXT;
                d.fillCircle(sx,sy,DOT_NODE,col);
                char lbl[5]; strncpy(lbl,n.name,4); lbl[4]='\0';
                d.setTextSize(FONT_SM); d.setTextColor(col,C_BG);
                d.setCursor(sx+DOT_NODE+1, sy-3); d.print(lbl);
            }
        }

        // own dot
        d.fillCircle(cx, cy, DOT_SELF, C_ACCENT);
        d.drawCircle(cx, cy, DOT_SELF+2, C_ACCENT);

        // overlays
        char zbuf[16]; snprintf(zbuf,sizeof(zbuf),"z:%d",_zoom);
        textLine(2, CONTENT_Y+CONTENT_H-LINE_H-1, zbuf, C_TEXT_DIM, C_BG);

        char pos[32];
        snprintf(pos,sizeof(pos),"%.4fN %.4fE", _own_lat/1e6, _own_lon/1e6);
        textLine(30, CONTENT_Y+CONTENT_H-LINE_H-1, pos, C_TEXT_MID, C_BG);
    }

    bool onKey(Keyboard_Class::KeysState& ks) override {
        bool consumed=false;
        if (ks.fn) {
            if (ks.up   && _zoom<16) { _zoom++; consumed=true; }
            if (ks.down && _zoom>10) { _zoom--; consumed=true; }
        }
        if (consumed) _need_redraw=true;
        return consumed;
    }

private:
    char    _ssid[33]={}, _pass[65]={};
    int32_t _own_lat=0, _own_lon=0;
    bool    _have_gps=false;
    const NodeEntry* _nodes=nullptr;
    int     _node_count=0;
    int     _zoom=13;
    bool    _need_redraw=true;
};
