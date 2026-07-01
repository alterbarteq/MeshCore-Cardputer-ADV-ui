#pragma once
#include "ScreenBase.h"
#include <Arduino.h>
#include <string.h>

#define CHAT_MAX_MSGS   60
#define CHAT_MSG_LEN   112
#define CHAT_FROM_LEN   20
#define CHAT_CHANNEL_LEN 24
#define INPUT_MAX_LEN  100

#define COMPOSE_BAR_H  10
#define CHAT_HDR_H     LINE_H   // pasek z nazwa kanalu, rysowany raz na gorze
#define MSG_AREA_H    (CONTENT_H - CHAT_HDR_H - COMPOSE_BAR_H - 1)
#define MSG_ROWS      (MSG_AREA_H / LINE_H)
#define MSG_TOP       (CONTENT_Y + CHAT_HDR_H)
#define TYPEWRITER_MS  20   // ms na znak

// Transliteracja polskich znakow UTF-8 na ASCII
static void depolish(const char* src, char* dst, int dst_len) {
    int si = 0, di = 0;
    int slen = strlen(src);
    while (src[si] && di < dst_len - 1) {
        unsigned char c = (unsigned char)src[si];
        if (c == 0xC4 && si+1 < slen) {
            unsigned char c2 = (unsigned char)src[si+1];
            switch(c2) {
                case 0x84: dst[di++]='A'; break;
                case 0x85: dst[di++]='a'; break;
                case 0x86: dst[di++]='C'; break;
                case 0x87: dst[di++]='c'; break;
                case 0x98: dst[di++]='E'; break;
                case 0x99: dst[di++]='e'; break;
                default:   dst[di++]='?'; break;
            }
            si += 2;
        } else if (c == 0xC5 && si+1 < slen) {
            unsigned char c2 = (unsigned char)src[si+1];
            switch(c2) {
                case 0x81: dst[di++]='L'; break;
                case 0x82: dst[di++]='l'; break;
                case 0x83: dst[di++]='N'; break;
                case 0x84: dst[di++]='n'; break;
                case 0x9A: dst[di++]='S'; break;
                case 0x9B: dst[di++]='s'; break;
                case 0xB9: dst[di++]='Z'; break;
                case 0xBA: dst[di++]='z'; break;
                case 0xBB: dst[di++]='Z'; break;
                case 0xBC: dst[di++]='z'; break;
                default:   dst[di++]='?'; break;
            }
            si += 2;
        } else if (c == 0xC3 && si+1 < slen) {
            unsigned char c2 = (unsigned char)src[si+1];
            switch(c2) {
                case 0xB3: dst[di++]='o'; break;
                case 0x93: dst[di++]='O'; break;
                default:   dst[di++]='?'; break;
            }
            si += 2;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

struct ChatMsg {
    char from[CHAT_FROM_LEN];
    char text[CHAT_MSG_LEN];
    bool outgoing;
    bool is_channel;
    int  channel_idx;   // prawdziwy indeks kanalu mesh, do ktorego nalezy
    int  typed_chars;   // ile znakow juz wpisano (typewriter)
    bool typing_done;   // czy animacja skonczona
};

class ScreenChat : public ScreenBase {
public:
    using SendFn = void(*)(const char* text, void* ctx);
    void setSendCallback(SendFn fn, void* ctx) { _send_fn = fn; _send_ctx = ctx; }

    void onEnter() override { _need_redraw = true; }

    // Wywolywane z UITaskRetro za kazdym razem gdy zmienia sie aktywny kanal
    // (wybor z listy, nowo utworzony kanal, stan poczatkowy) — czat pokazuje
    // wtedy tylko wiadomosci nalezace do tego kanalu.
    void setActiveChannel(int channel_idx, const char* channel_name) {
        if (_active_channel_idx == channel_idx) return;
        _active_channel_idx = channel_idx;
        strncpy(_active_channel_name, channel_name, sizeof(_active_channel_name) - 1);
        _active_channel_name[sizeof(_active_channel_name) - 1] = '\0';
        _scroll_to_bottom = true;
        _doScrollToBottom();
        _need_redraw = true;
    }

    // Wywoluj z loop() co klatke
    void tick() {
        int idx = _lastMsgIdxForActiveChannel();
        if (idx < 0) return;
        ChatMsg& last = _msgs[idx];
        if (last.typing_done) return;

        uint32_t now = millis();
        if (now - _last_type_ms >= TYPEWRITER_MS) {
            _last_type_ms = now;
            int tlen = strlen(last.text);
            if (last.typed_chars < tlen) {
                last.typed_chars++;
                _need_redraw = true;
            } else {
                last.typing_done = true;
                _need_redraw = true;
            }
        }
    }

    bool isTyping() {
        int idx = _lastMsgIdxForActiveChannel();
        if (idx < 0) return false;
        return !_msgs[idx].typing_done;
    }

    void addMessage(const char* from, const char* text, bool outgoing, bool is_channel, int channel_idx) {
        int idx = _msg_count < CHAT_MAX_MSGS ? _msg_count : CHAT_MAX_MSGS - 1;
        if (_msg_count >= CHAT_MAX_MSGS)
            memmove(_msgs, _msgs + 1, sizeof(ChatMsg) * (CHAT_MAX_MSGS - 1));
        else
            _msg_count++;

        const char* sender = from;
        const char* body   = text;
        char sender_buf[CHAT_FROM_LEN];

        if (!outgoing) {
            // Wiadomosci grupowe/kanalowe przychodza jako "Nadawca: tresc"
            // zapisane w samym tekscie — `from` to w rzeczywistosci nazwa
            // kanalu (np. "Public"), ktora UITaskRetro juz przekazal osobno
            // przez setActiveChannel()/channel_idx. Tutaj wyciagamy tylko
            // prawdziwego nadawce z tekstu.
            const char* colon = strchr(text, ':');
            if (colon && colon > text && (colon - text) < CHAT_FROM_LEN - 1) {
                int name_len = colon - text;
                strncpy(sender_buf, text, name_len);
                sender_buf[name_len] = '\0';
                sender = sender_buf;
                body = colon + 1;
                while (*body == ' ') body++;
            }
        }

        depolish(sender, _msgs[idx].from, CHAT_FROM_LEN);
        depolish(body, _msgs[idx].text, CHAT_MSG_LEN);
        _msgs[idx].outgoing    = outgoing;
        _msgs[idx].is_channel  = is_channel;
        _msgs[idx].channel_idx = channel_idx;
        // tick()/isTyping() animuja wylacznie NAJNOWSZA wiadomosc aktywnego
        // kanalu — jesli wiadomosc trafia na kanal, ktory nie jest teraz
        // ogladany (np. kilka wiadomosci czeka, zanim uzytkownik w ogole
        // otworzy ten kanal), nigdy nie zostanie "domowa" przez tick() i
        // zostalaby trwale pusta (widac by bylo tylko nadawce, bez tresci).
        // Animacja ma sens tylko dla wiadomosci, ktora realnie przychodzi na
        // ekranie na zywo — reszta pojawia sie od razu w calosci.
        bool live = (channel_idx == _active_channel_idx);
        _msgs[idx].typed_chars = live ? 0 : strlen(_msgs[idx].text);
        _msgs[idx].typing_done = !live;
        _last_type_ms = millis();
        if (live && _scroll_to_bottom) _doScrollToBottom();
        _need_redraw = true;
    }

    void onNewMessage(const char* from, const char* text, bool is_channel) override {
        addMessage(from, text, false, is_channel, _active_channel_idx);
    }

    void draw() override {
        if (!_need_redraw && !_input_dirty) return;

        M5GFX& d = M5Cardputer.Display;
        d.setTextSize(FONT_SM);

        int char_w = d.textWidth("A");
        if (char_w < 1) char_w = 6;
        int cols = (SCREEN_W - 4) / char_w;

        if (_need_redraw) {
            _need_redraw = false;

            // Naglowek kanalu — raz na gorze, nie powtarzany przy kazdej wiadomosci
            d.fillRect(0, CONTENT_Y, SCREEN_W, CHAT_HDR_H, C_BG);
            d.setTextColor(C_ACCENT, C_BG);
            d.setCursor(2, CONTENT_Y + 1);
            d.print(_active_channel_name[0] ? _active_channel_name : "---");
            hline(CONTENT_Y + LINE_H - 1);

            // Clear only the message area — the compose bar below is redrawn
            // separately so typing doesn't repaint (and flicker) the whole list.
            d.fillRect(0, MSG_TOP, SCREEN_W, MSG_AREA_H, C_BG);

            int y = MSG_TOP;
            int max_y = MSG_TOP + MSG_AREA_H;
            int line = 0;
            int last_idx = _lastMsgIdxForActiveChannel();

            for (int i = 0; i < _msg_count; i++) {
                ChatMsg& m = _msgs[i];
                if (m.channel_idx != _active_channel_idx) continue;   // inny kanal — nie miesza sie z tym widokiem

                // Dla wiadomosci w trakcie pisania — ogranicz tekst
                char vis_text[CHAT_MSG_LEN];
                const char* txt_src = m.text;
                if (!m.typing_done) {
                    int n = min(m.typed_chars, (int)strlen(m.text));
                    strncpy(vis_text, m.text, n);
                    vis_text[n] = '\0';
                    txt_src = vis_text;
                }

                // Oblicz linie dla widocznego tekstu (zawijanie po slowach)
                int offsets[12], counts[12], nl = 0;
                _calcLines(m.from, txt_src, m.outgoing, cols, offsets, counts, nl, 12);

                for (int sl = 0; sl < nl; sl++) {
                    if (line >= _scroll_offset && y + LINE_H <= max_y) {
                        _drawLine(d, m.from, txt_src, m.outgoing, sl,
                                  offsets[sl], counts[sl], y,
                                  // kursor: ostatnia linia ostatniej wiadomosci (tego kanalu) w trakcie pisania
                                  (i == last_idx && !m.typing_done && sl == nl-1));
                        y += LINE_H;
                    }
                    line++;
                }
                if (y >= max_y) break;
            }

            // Scroll bar
            int total = _totalLines(cols);
            int vis = MSG_ROWS;
            if (total > vis) {
                int bh = max(2, MSG_AREA_H * vis / total);
                int by = MSG_TOP + (_scroll_offset * MSG_AREA_H / max(1, total));
                d.fillRect(SCREEN_W-2, MSG_TOP, 2, MSG_AREA_H, C_BG2);
                d.fillRect(SCREEN_W-2, by, 2, bh, C_TEXT_MID);
            }
        }

        _drawComposeBar(d, char_w);
    }

    bool onKey(Keyboard_Class::KeysState& ks) override {
        bool consumed = false;
        bool list_changed  = false;
        bool input_changed = false;

        if (ks.enter) {
            if (_input[0] != '\0' && _send_fn) {
                _send_fn(_input, _send_ctx);
                addMessage("Ty", _input, true, false, _active_channel_idx);
                // wlasna wiadomosc pojawia sie od razu (nie typewriter)
                _msgs[_msg_count-1].typed_chars = strlen(_msgs[_msg_count-1].text);
                _msgs[_msg_count-1].typing_done = true;
                _input[0] = '\0'; _input_len = 0;
                _scroll_to_bottom = true;
                input_changed = true;
            }
            consumed = true;
        }
        for (auto c : ks.word) {
            if (c == '\n' || c == '\r') {
                if (_input[0] != '\0' && _send_fn) {
                    _send_fn(_input, _send_ctx);
                    addMessage("Ty", _input, true, false, _active_channel_idx);
                    // wlasna wiadomosc pojawia sie od razu (nie typewriter)
                    _msgs[_msg_count-1].typed_chars = strlen(_msgs[_msg_count-1].text);
                    _msgs[_msg_count-1].typing_done = true;
                    _input[0] = '\0'; _input_len = 0;
                    _scroll_to_bottom = true;
                    input_changed = true;
                }
            } else if (c == '\b' || c == 127) {
                if (_input_len > 0) { _input[--_input_len] = '\0'; input_changed = true; }
            } else if (_input_len < INPUT_MAX_LEN - 1 && c >= 32) {
                _input[_input_len++] = c;
                _input[_input_len]   = '\0';
                input_changed = true;
            }
            consumed = true;
        }
        if (ks.del && _input_len > 0) {
            _input[--_input_len] = '\0'; consumed = true; input_changed = true;
        }
        if (ks.fn) {
            M5GFX& d = M5Cardputer.Display;
            d.setTextSize(FONT_SM);
            int char_w = d.textWidth("A"); if (char_w < 1) char_w = 6;
            int cols  = (SCREEN_W - 4) / char_w;
            int total = _totalLines(cols);
            int vis   = MSG_ROWS;

            if (ks.up && _scroll_offset > 0) {
                _scroll_offset--; _scroll_to_bottom = false; consumed = true; list_changed = true;
            }
            if (ks.down && _scroll_offset < total - vis) {
                _scroll_offset++;
                if (_scroll_offset >= total - vis) _scroll_to_bottom = true;
                consumed = true; list_changed = true;
            }
        }

        if (list_changed)  _need_redraw  = true;
        if (input_changed) _input_dirty  = true;
        return consumed;
    }

private:
    ChatMsg _msgs[CHAT_MAX_MSGS] = {};
    int  _msg_count     = 0;
    int  _scroll_offset = 0;
    bool _scroll_to_bottom = true;
    char _input[INPUT_MAX_LEN+1] = {};
    int  _input_len   = 0;
    bool _need_redraw = true;
    bool _input_dirty = false;
    uint32_t _last_type_ms = 0;
    int  _active_channel_idx = -1;   // -1 = jeszcze nie ustawiony przez UITaskRetro
    char _active_channel_name[CHAT_CHANNEL_LEN] = {};
    SendFn _send_fn   = nullptr;
    void*  _send_ctx  = nullptr;

    int _lastMsgIdxForActiveChannel() {
        for (int i = _msg_count - 1; i >= 0; i--) {
            if (_msgs[i].channel_idx == _active_channel_idx) return i;
        }
        return -1;
    }

    int _fitChars(const char* txt, int start, int max_c) {
        int tlen = strlen(txt);
        int avail = min(max_c, tlen - start);
        if (avail <= 0) return 0;
        if (start + avail >= tlen) return avail;
        if (txt[start + avail] == ' ') return avail;
        for (int i = avail - 1; i > 0; i--)
            if (txt[start + i] == ' ') return i;
        return avail;
    }

    void _calcLines(const char* from, const char* txt, bool outgoing, int cols,
                    int* offsets, int* counts, int& nl, int max_nl) {
        int tlen = strlen(txt);
        int prefix = min((int)strlen(from), 9) + 1;   // "Nazwa:" width
        int avail0 = max(1, cols - prefix);
        // Cudze wiadomosci: kazda linia wciecia pod kolumna nazwy, wiec
        // kazda linia musi byc zawijana do tej samej, zmniejszonej szerokosci
        // — inaczej zawiniety tekst wychodzi poza prawa krawedz ekranu i
        // wyglada jakby byl przeciety w polowie slowa.
        // Wlasne wiadomosci: wyrownane do prawej niezaleznie w kazdej linii,
        // wiec linie kontynuacji odzyskuja pelna szerokosc (bez wciecia).
        int availN = outgoing ? cols : avail0;
        nl = 0;
        if (tlen == 0) { offsets[0]=0; counts[0]=0; nl=1; return; }
        int n = _fitChars(txt, 0, avail0);
        offsets[nl]=0; counts[nl]=n; nl++;
        int pos = n;
        if (pos < tlen && txt[pos]==' ') pos++;
        while (pos < tlen && nl < max_nl) {
            n = _fitChars(txt, pos, availN);
            offsets[nl]=pos; counts[nl]=n; nl++;
            pos += n;
            if (pos < tlen && txt[pos]==' ') pos++;
        }
    }

    int _totalLines(int cols) {
        int total = 0;
        int offsets[12], counts[12], nl;
        for (int i = 0; i < _msg_count; i++) {
            if (_msgs[i].channel_idx != _active_channel_idx) continue;
            // Zawsze liczymy na podstawie docelowej (pelnej) tresci, nie tego
            // co juz "wpisal" typewriter — inaczej w momencie dodania nowej
            // wiadomosci (typed_chars==0) liczylaby sie ona jako pusta linia
            // i auto-scroll nie odslanial jej w calosci w miare pisania.
            _calcLines(_msgs[i].from, _msgs[i].text, _msgs[i].outgoing, cols, offsets, counts, nl, 12);
            total += nl;
        }
        return max(1, total);
    }

    void _doScrollToBottom() {
        M5GFX& d = M5Cardputer.Display;
        d.setTextSize(FONT_SM);
        int char_w = d.textWidth("A");
        if (char_w < 1) char_w = 6;
        int cols = (SCREEN_W - 4) / char_w;
        int total = _totalLines(cols);
        _scroll_offset = max(0, total - MSG_ROWS);
    }

    void _drawLine(M5GFX& d, const char* from, const char* txt,
                   bool outgoing, int sl, int offset, int count, int y,
                   bool show_cursor) {
        char buf[CHAT_MSG_LEN + 4];
        strncpy(buf, txt + offset, count); buf[count] = '\0';
        int char_w = d.textWidth("A");
        if (char_w < 1) char_w = 6;

        char fname[CHAT_FROM_LEN];
        strncpy(fname, from, 9); fname[9] = '\0';
        int header_len = strlen(fname) + 1; // "Nazwa:" szerokosc w znakach

        if (outgoing) {
            // Wlasne wiadomosci — dosuniete do prawej krawedzi, nazwa
            // uzytkownika (C_ACCENT) wyroznia je od cudzych.
            int line_chars = count + (sl == 0 ? header_len : 0);
            int x = max(0, SCREEN_W - line_chars * char_w - 2);
            d.setCursor(x, y);
            if (sl == 0) {
                d.setTextColor(C_ACCENT, C_BG);
                d.print(fname); d.print(":");
            }
            d.setTextColor(C_TEXT, C_BG);
            d.print(buf);
        } else {
            // Cudze wiadomosci — lewa krawedz, nazwa nadawcy (C_WARN) wyroznia
            // kto pisze.
            d.setCursor(2, y);
            if (sl == 0) {
                d.setTextColor(C_WARN, C_BG);
                d.print(fname); d.print(":");
            } else {
                for (int i = 0; i < header_len; i++) d.print(" ");
            }
            d.setTextColor(C_TEXT, C_BG);
            d.print(buf);
        }

        // Kursor typewriter (migajacy blok)
        if (show_cursor && (millis() / 300) % 2 == 0) {
            int cx = d.getCursorX();
            d.fillRect(cx, y, char_w, LINE_H - 1, C_TEXT);
        }
    }

    void _drawComposeBar(M5GFX& d, int char_w) {
        _input_dirty = false;

        int cy = MSG_TOP + MSG_AREA_H + 1;
        d.drawFastHLine(0, cy, SCREEN_W, C_TEXT_DIM);
        cy++;
        d.fillRect(0, cy, SCREEN_W, COMPOSE_BAR_H, C_BG2);
        d.setTextColor(C_TEXT_MID, C_BG2);
        d.setCursor(2, cy + 1); d.setTextSize(FONT_SM); d.print(">");
        int avail = (SCREEN_W - 14) / char_w;
        int ilen  = strlen(_input);
        const char* show = (ilen > avail) ? _input + (ilen - avail) : _input;
        d.setTextColor(C_TEXT, C_BG2);
        d.setCursor(10, cy + 1); d.print(show);
        if ((millis() / 400) % 2 == 0) {
            int cx = 10 + min(ilen, avail) * char_w;
            d.fillRect(cx, cy+1, char_w-1, CHAR_H-1, C_TEXT_MID);
        }
    }
};
