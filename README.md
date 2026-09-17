# pico-shared-drivers

Hardware drivers shared across multiple Pico 2 W projects (currently `bats`
and `bat gyros`, both of which have the same SSD1306 128x64 OLED wired up).

Consumed as a git submodule at `lib/shared` in each project; CMakeLists.txt
in each project points at `lib/shared/<driver>/...`.

## Layout

- `ssd1306/` - SSD1306 128x64 OLED driver (hardware I2C + a bit-banged
  software-I2C variant for a second display on different pins), plus
  `ssd1306_draw_text()` (any font, see `ssd1306_font_t`) and several bitmap
  fonts: `ssd1306_font_adafruit` (5x8 - Adafruit_GFX's own default font;
  follows CP437's code-page layout but isn't a real CP437 bitmap font),
  `ssd1306_font_chonk` (7x11), `ssd1306_font_narrow` (7x11, thinner
  strokes than chonk), `ssd1306_font_mini` (6x7), `ssd1306_font_micro`
  (4x7), `ssd1306_font_big` (10x12), `ssd1306_font_cp437` (the full CP437
  at its authentic VGA text-mode 9x16 size - see `generate_cp437.py`, a
  one-shot generator separate from the PNG pipeline below, since this
  one's sourced from the classic VGA ROM font's byte data, not an image),
  `ssd1306_font_main_menu` (37x27, 3 system UI icon glyphs, not text -
  `ui_only` is set on it, a flag any screen enumerating fonts for display
  should check and skip; see `ssd1306_font_t`). Font source images live in
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
- `mpr121/` - MPR121 12-key capacitive touch sensor over i2c (polled, not
  interrupt-driven - the IRQ pin, if wired up, is only used internally to
  skip an i2c read when there's nothing new). Doesn't call `i2c_init()`
  itself; expects an already-initialized i2c instance, since that
  instance's SDA/SCL can be muxed onto more than one physical pin pair on
  RP2040/RP2350 - see `mpr121.h`'s own comment.
- `joystick/` - analog 2-axis "PS2-style" joystick module (two
  potentiometers over the RP2040/RP2350's ADC, plus a momentary push
  button under the stick), polled. Self-calibrates each axis' rest/center
  reading at init, normalizes both axes to -1.0..+1.0 with a small
  deadzone around center, then applies a per-instance, sign-preserving
  response-curve exponent (`joystick_set_curve_exponent()` - 1.0/linear
  by default) that every future `joystick_read()` (and so every
  `joystick_repeater_update()` too) reflects immediately, from whichever
  screen last set it, until changed again. Also `joystick_repeater.h` - turns that
  continuous analog position into discrete, auto-repeating step events
  (e.g. moving a grid/list cursor) at a rate that scales with how far the
  stick is pushed, via a configurable (non-linear) curve - and
  `joystick_hold.h` - detects a sustained directional hold (e.g. holding
  left for 1s as a "back" gesture), firing once when the hold duration is
  reached. Both use the same two-threshold hysteresis (a deliberate push
  past `threshold` to start, only cancelled by dropping back below a
  lower `release_threshold`) to stay robust against ADC/pot noise or hand
  tremor wobbling right at a single boundary value.

More drivers belong here too - add a new subdirectory per driver rather
than growing one grab-bag file.
