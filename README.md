# SimulRepile

SimulRepile fournit l'architecture bi-carte complète d'un simulateur pédagogique de terrariums :

- `firmware/` : terminal graphique Waveshare ESP32-S3-Touch-LCD-7B avec LVGL 9.4/LovyanGFX, gestion SD, autosave et i18n.
- `core_firmware/` : cœur simulateur ESP32-S3-DevKitC publiant l'état des terrariums via le protocole Core Link UART.

Les deux firmwares partagent les structures `common/include/`. Consultez les README respectifs pour les instructions de build, de flash et les exigences matérielles.
