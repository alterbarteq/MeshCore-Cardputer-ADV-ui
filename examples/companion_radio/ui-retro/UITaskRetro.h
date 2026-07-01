#pragma once

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "../MyMesh.h"
#include "Theme.h"
#include "TopBar.h"
#include "ScreenChat.h"
#include "ScreenNodes.h"
#include "ScreenMap.h"
#include "ScreenMyNode.h"
#include "ScreenSettings.h"
#include <M5Cardputer.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>

// ─────────────────────────────────────────────────────────────────────────────
//  UITaskRetro
//  Drop-in replacement for UITask that uses the retro BIOS-style UI.
//  Register in platformio.ini by including ui-retro/*.cpp instead of
//  ui-keyboard/*.cpp.
// ─────────────────────────────────────────────────────────────────────────────

class UITaskRetro : public AbstractUITask {
public:
    UITaskRetro(mesh::MainBoard* board, BaseSerialInterface* serial_iface);

    void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

    // ── AbstractUITask interface ──────────────────────────────────────────────
    void loop()     override;
    void msgRead(int msgcount) override;
    void newMsg(uint8_t path_len, const char* from_name,
                const char* text, int msgcount) override;
    void notify(UIEventType t = UIEventType::none) override;
    void syncChatHistoryToBLE(int max_messages = 10) override {}

private:
    // Sub-screens
    ScreenChat     _chat;
    ScreenNodes    _nodes;
    ScreenMap      _map;
    ScreenMyNode   _mynode;
    ScreenSettings _settings;

    // Current active tab
    Tab _active_tab = Tab::CHAT;

    // Hardware
    DisplayDriver* _display    = nullptr;
    SensorManager* _sensors    = nullptr;
    NodePrefs*     _node_prefs = nullptr;

    // Timing
    unsigned long _next_refresh   = 0;
    unsigned long _auto_off       = 0;
    unsigned long _screen_timeout = 300000;
    bool          _screen_sleeping = false;
    bool          _need_refresh    = true;

    // Stats updated from mesh
    uint32_t _tx_count  = 0;
    uint32_t _rx_count  = 0;
    uint8_t  _err_count = 0;
    bool     _gps_fix   = false;

    // Key debounce
    unsigned long _last_key_ms = 0;
    static const int KEY_DEBOUNCE_MS = 60;

    // Private helpers
    void _switchTab(Tab t);
    void _handleKeys();
    void _updateNodeList();
    void _updateMyNodeScreen();
    void _drawFrame();
    void _wakScreen();

    // Send callback (static trampoline)
    static void _onSend(const char* text, void* ctx);
    static void _onSettingChange(const char* key, const char* val, void* ctx);
};
