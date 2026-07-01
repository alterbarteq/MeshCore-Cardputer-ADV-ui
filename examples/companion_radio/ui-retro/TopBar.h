#pragma once
#include <M5Cardputer.h>
#include "Theme.h"

// ─────────────────────────────────────────────────────────────────────────────
//  TopBar
//  Draws the two thin bars that frame the content area:
//    • top:    tab labels (F1–F5), one highlighted
//    • bottom: LoRa stats  |  node count  |  GPS  bat
// ─────────────────────────────────────────────────────────────────────────────
class TopBar {
public:
    // Call once per frame before drawing content
    static void draw(Tab active, int num_nodes,
                     uint16_t batt_mv, bool gps_fix,
                     uint32_t tx_count, uint32_t rx_count, uint8_t err_count,
                     bool chat_alert = false) {

        // Nothing shown here changed since the last call (e.g. user is just
        // typing in the chat compose bar) — skip the redraw so the top/bottom
        // bars don't flicker on every keystroke.
        static bool     s_drawn     = false;
        static Tab      s_active;
        static int      s_num_nodes;
        static uint16_t s_batt_mv;
        static bool     s_gps_fix;
        static uint32_t s_tx_count, s_rx_count;
        static uint8_t  s_err_count;
        static bool     s_chat_alert;

        if (s_drawn && active == s_active && num_nodes == s_num_nodes &&
            batt_mv == s_batt_mv && gps_fix == s_gps_fix &&
            tx_count == s_tx_count && rx_count == s_rx_count && err_count == s_err_count &&
            chat_alert == s_chat_alert) {
            return;
        }
        s_drawn     = true;
        s_active    = active;
        s_num_nodes = num_nodes;
        s_batt_mv   = batt_mv;
        s_gps_fix   = gps_fix;
        s_tx_count  = tx_count;
        s_rx_count  = rx_count;
        s_err_count = err_count;
        s_chat_alert = chat_alert;

        M5GFX& d = M5Cardputer.Display;

        // ── top bar background ───────────────────────────────────────────────
        // flat, dark — same as the rest of the screen, no colour banner
        d.fillRect(0, 0, SCREEN_W, TOPBAR_H, C_BG);

        // brand label (leftmost)
        d.setTextSize(FONT_SM);
        d.setTextColor(C_ACCENT, C_BG);
        d.setCursor(2, 3);
        d.print("\xE2\xAC\xA1MC"); // ⬡ MC  (approx – non-ASCII may render as '?', safe fallback)

        // tab labels — F4 skrocone do "F4:UST" zwalnia miejsce na wykrzyknik
        // powiadomienia zaraz po F1.
        const uint16_t tab_w[]  = { 44, 50, 44, 44 };  // widths per label
        const uint16_t tab_x[]  = { 26, 80, 131, 176 };

        for (int i = 0; i < (int)Tab::COUNT; i++) {
            bool is_active = ((int)active == i);
            if (is_active) {
                // reverse video: solid accent block behind the active tab
                d.fillRect(tab_x[i] - 1, 0, tab_w[i] + 2, TOPBAR_H, C_ACCENT);
                d.setTextColor(C_BG, C_ACCENT);
            } else {
                d.setTextColor(C_TEXT_MID, C_BG);
            }
            d.setCursor(tab_x[i], 3);
            d.print(TAB_LABELS[i]);
        }

        // Wykrzyknik: nowa wiadomosc czeka na innym kanale niz aktualnie
        // wybrany — widoczny niezaleznie od tego, ktora zakladka jest otwarta.
        if (chat_alert) {
            d.setTextColor(C_WARN, C_BG);
            d.setCursor(72, 3);
            d.print("!");
        }

        // thin separator line below tab bar
        d.drawFastHLine(0, TOPBAR_H, SCREEN_W, C_TEXT_DIM);

        // ── bottom status bar ────────────────────────────────────────────────
        int sy = SCREEN_H - STATUSBAR_H;
        d.drawFastHLine(0, sy - 1, SCREEN_W, C_TEXT_DIM);
        d.fillRect(0, sy, SCREEN_W, STATUSBAR_H, C_BG3);
        d.setTextColor(C_TEXT_MID, C_BG3);
        d.setTextSize(FONT_SM);

        // left: TX / RX / ERR
        char buf[64];
        d.setCursor(2, sy + 1);
        snprintf(buf, sizeof(buf), "TX:%lu RX:%lu", (unsigned long)tx_count, (unsigned long)rx_count);
        d.print(buf);

        // centre: node count
        snprintf(buf, sizeof(buf), "N:%d", num_nodes);
        int cx = SCREEN_W / 2 - (strlen(buf) * CHAR_W) / 2;
        d.setCursor(cx, sy + 1);
        d.print(buf);

        // right: GPS indicator + battery %
        uint8_t bat_pct = battPct(batt_mv);
        snprintf(buf, sizeof(buf), "%s %3d%%", gps_fix ? "GPS" : "---", bat_pct);
        int rx_pos = SCREEN_W - strlen(buf) * CHAR_W - 2;
        d.setCursor(rx_pos, sy + 1);
        d.setTextColor(gps_fix ? C_ACCENT : C_TEXT_DIM, C_BG3);
        d.print(buf);

        // error counter (red, if any)
        if (err_count > 0) {
            snprintf(buf, sizeof(buf), "E:%d", err_count);
            d.setCursor(SCREEN_W / 2 + 30, sy + 1);
            d.setTextColor(C_RED_ERR, C_BG3);
            d.print(buf);
        }
    }

private:
    static uint8_t battPct(uint16_t mv) {
        // LiPo: 4200 mV = 100%, 3500 mV = 0%
        if (mv >= 4200) return 100;
        if (mv <= 3500) return 0;
        return (uint8_t)((mv - 3500) * 100UL / 700UL);
    }
};
