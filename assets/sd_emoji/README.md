# SD-card emoji assets

Source: [Google Noto Color Emoji](https://github.com/googlefonts/noto-emoji), licensed
under the [SIL Open Font License 1.1](https://github.com/googlefonts/noto-emoji/blob/main/LICENSE)
(permissive — bundling/redistribution as part of an application is explicitly allowed).

Copied here pre-processed (named by Unicode codepoint, `u<HEX>.png`) from
[d4rkmen/plai](https://github.com/d4rkmen/plai)'s `emoji/` folder, which did that
conversion work first (their own project code is GPL-3.0, but that license does not
apply to these separately-licensed OFL font assets — see their README's Credits
section for the same attribution).

## Not part of the firmware build

These are **not** compiled into flash. They're meant to be copied as-is onto a
microSD card's `/emoji/` directory, for a future emoji-in-chat feature (rendering
not yet implemented in `ui-retro` — see `ScreenChat.h`).
