# pico-shared-drivers

Hardware drivers shared across multiple Pico 2 W projects (currently `bats`
and `bat gyros`, both of which have the same SSD1306 128x64 OLED wired up).

Consumed as a git submodule at `lib/shared` in each project; CMakeLists.txt
in each project points at `lib/shared/<driver>/...`.

## Layout

- `ssd1306/` - SSD1306 128x64 OLED driver (hardware I2C + a bit-banged
  software-I2C variant for a second display on different pins), plus a
  5x7 CP437 bitmap font and `ssd1306_draw_text()`.

More drivers (e.g. a rotary encoder, once both projects have one) belong
here too - add a new subdirectory per driver rather than growing one grab-bag
file.
