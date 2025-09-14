# simulrepile

Educational reptile breeding simulation game for the Waveshare ESP32-S3 Touch LCD 7B.

## Structure
- `main/` : application entry point and initialization.
- `components/lvgl_port` : LVGL + LovyanGFX display driver.
- `components/touch_gt911` : GT911 touch controller integration.
- `components/storage` : microSD save/load utilities.
- `components/game` : game logic and UI.
- `assets/` : images and textures (placeholder).

## Build
```
idf.py set-target esp32s3
idf.py build flash monitor
```
