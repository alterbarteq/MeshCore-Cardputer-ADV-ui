# MeshCore Cardputer ADV — Retro Terminal UI

A from-scratch, terminal-styled UI (`ui-retro`) for [MeshCore](https://github.com/meshcore-dev/MeshCore) mesh networking firmware, built for the M5Stack Cardputer ADV with the Cap LoRa-1262/868 module. Dark background, single violet accent, monospace text, reverse-video selection — no chat bubbles, no icon menus.

> **📌 Fork notice:** This is a personal fork of [Stachugit/MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV) by Stanisław "Stachu" Piskorski, who did all of the original groundwork this fork builds on (Cap LoRa-1262 support, BLE pairing, base companion-radio integration). This fork replaces his UI entirely with a custom retro terminal interface described below. Please support his work at [buymeacoffee.com/Stachu](https://buymeacoffee.com/Stachu).

## Hardware

- **M5Stack Cardputer ADV** (ESP32-S3FN8 — **no PSRAM**, despite some board labels suggesting otherwise)
- **Cap LoRa-1262/868** radio module attached (863–870 MHz band)
- microSD card (optional, used for offline map tiles — see Map tab below)

## Tabs

| Key | Tab | Contents |
|-----|-----|----------|
| **F1** | Chat | Group channel chat, one channel at a time |
| **F2** | Nodes | List of contacts heard on the mesh |
| **F3** | Map | Offline OSM map with your position + heard nodes |
| **F4** | Node & Settings | Device info + all configurable settings, one scrollable list |

Switch tabs with **F1–F4**, **Fn+1–4**, or **Opt+←/→**.

## Chat (F1)

- Messages are kept **separated per channel** — the channel you're currently viewing/composing to is always shown in the header at the top of the chat screen.
- If a message arrives on a channel you're **not** currently viewing, a yellow **`!`** appears next to `F1:CZAT` in the top bar until you switch to that channel.
- **Opt** opens the channel picker (pick which channel to view/send to).
- **Opt+N** opens the picker directly in "create new channel" mode. New channel names must start with `#` (e.g. `#warszawa`); the channel's PSK is derived automatically from the name (`SHA256("#name")`, first 16 bytes, base64) so another device typing the same name joins the same channel without manually exchanging keys.
- **Fn+↑ / Fn+↓** scroll the message history.
- **Enter** sends the typed message; **Backspace/Del** edits it.

## Nodes (F2)

Scrollable list of contacts heard on the mesh (**↑/↓**), each with signal path info.

## Map (F3)

Real OpenStreetMap tiles, loaded from a **microSD card** rather than fetched live over the network. This board has no PSRAM and not enough free heap left for a TLS handshake once mesh+WiFi+BLE+UI are all running, so live HTTPS tile fetching isn't viable here — SD is the workaround.

- Tile convention: standard Web Mercator slippy-map tiles, `/maptiles/{z}/{x}/{y}.png` on the card — the same layout most offline-map tools use, so tiles downloaded for your area of interest (e.g. from tile.openstreetmap.org, respecting their usage policy) can be copied on as-is.
- **Fn+↑ / Fn+↓** zoom in/out.
- Your position and any heard nodes' positions are plotted as dots over the tile.
- If no tile/card is available, a phosphor dot-grid placeholder is shown instead of a blank screen — the screen is never empty even without SD map data.
- ⚠️ SD card mounting is currently **disabled** in this build (a flaky card previously froze the whole UI on mount) — this is on the list to revisit.

## Node & Settings (F4)

One scrollable list combining device info and configuration:

- Node name (editable), node ID, firmware version, uptime, free RAM, battery
- GPS position + **GPS on/off** toggle
- BLE pairing PIN
- Radio frequency
- Path/message hash size — **1 / 2 / 3-byte**, cycled with Enter
- **↑/↓** navigate, **Enter** edits/toggles the selected item, **Del/Backspace** cancels an edit in progress.

## Sending an advert

Press **Tab** from any tab to open a popup to send a self-advertisement:

- **Advert (flood)** — broadcast to the whole mesh
- **Zero-hop** — advertise to directly-reachable nodes only

**↑/↓** to choose, **Enter** to send, **Del/Backspace/Tab** to cancel.

## Building from source

```bash
git clone https://github.com/alterbarteq/MeshCore-Cardputer-ADV-ui.git
cd MeshCore-Cardputer-ADV-ui
cp retro_env_only.ini platformio.local.ini   # picked up automatically via extra_configs
pio run -e M5stack_cardputer_retro_ui --target upload
```

## Initial setup

First-time configuration requires the MeshCore mobile app:

1. Flash the firmware.
2. Install the MeshCore app on your phone.
3. Pair over Bluetooth using the PIN shown on the **F4** (Node & Settings) screen.
4. Configure node name, region, radio parameters, and channels from the app or from F4/F1 directly on-device.

## Credits

- Built on [MeshCore](https://github.com/meshcore-dev/MeshCore) mesh networking firmware.
- Original Cardputer ADV / Cap LoRa-1262 groundwork by [Stachugit/MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV) (Stanisław "Stachu" Piskorski) — support at [buymeacoffee.com/Stachu](https://buymeacoffee.com/Stachu).
- Cap LoRa-1262 compatibility fixes based on work by [sosprz](https://github.com/sosprz/meshcore-cardputer-adv).
- Retro terminal UI (`ui-retro`) is this fork's own addition on top of the above.

## License

Same license as the original MeshCore firmware. See [license.txt](license.txt).

## Disclaimer

Independent UI fork of MeshCore. For core mesh networking questions, refer to the [original project](https://github.com/meshcore-dev/MeshCore).
