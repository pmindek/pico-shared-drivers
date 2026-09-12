# pico-shared-drivers

Hardware drivers shared across multiple Pico 2 W projects (currently `bats`
and `bat gyros`, both of which have the same SSD1306 128x64 OLED wired up).

Consumed as a git submodule at `lib/shared` in each project; CMakeLists.txt
in each project points at `lib/shared/<driver>/...`.

## Layout

- `ssd1306/` - SSD1306 128x64 OLED driver (hardware I2C + a bit-banged
  software-I2C variant for a second display on different pins), plus
  `ssd1306_draw_text()` (any font, see `ssd1306_font_t`) and several bitmap
  fonts: `ssd1306_font_5x7` (CP437), `ssd1306_font_7x11` ("chonk"),
  `ssd1306_font_7x11_narrow`, `ssd1306_font_6x7` ("mini"),
  `ssd1306_font_4x7` ("micro"), `ssd1306_font_10x12` ("big"). Font source
  images live in
  `ssd1306/fonts/<name>_<W>x<H>.png` (a horizontal strip of WxH glyph cells
  starting at ASCII 32); the `.h` glyph data next to them is generated from
  those images via `ssd1306/generate_fonts.py`, not hand-written - run it
  (needs Pillow) after adding or editing a font PNG to regenerate its
  header, or just double-click `ssd1306/update_fonts.bat`, which runs it
  and also commits the result here and propagates/rebuilds/commits it in
  both `bats` and `bat gyros` (see that file's own comment for the exact
  steps and the hardcoded paths it assumes). New fonts still need a
  couple lines in `ssd1306.h`/`.c` to expose them as a named
  `ssd1306_font_t` - neither script does that part for you.
  `ssd1306_set_target()` redirects all of
  the above to draw into an off-screen `ssd1306_canvas_t` instead of the
  display, and `ssd1306_console.c` (a simple cursor-tracking text console
  built on `ssd1306_draw_text()`) works the same way against either. Built
  on top of that: `ssd1306_scrollbar.h` (a reusable vertical scrollbar
  overlay) and `ssd1306_window.h` (a reusable "window" - title, divider,
  scrollable content area backed by an off-screen canvas, scrollbar drawn
  automatically when the content doesn't fit).
- `encoder/` - KY-040-style rotary encoder (CLK/DT/SW), interrupt-driven
  rotation + polled/debounced button.
- `button/` - a single debounced momentary push button (polled), same
  debounce logic as the encoder driver's SW pin, split out standalone for
  buttons that aren't part of an encoder.

More drivers belong here too - add a new subdirectory per driver rather
than growing one grab-bag file.
