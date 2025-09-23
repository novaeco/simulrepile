# Interface microSD SDSPI — ESP32-S3 Touch LCD 7B

Cet environnement force l'utilisation d'un bus SDSPI natif **dédié** (SPI3_HOST)
pour la carte microSD du module Waveshare ESP32-S3 Touch LCD 7B. Le contrôleur
CH422G ne doit **jamais**
piloter la ligne `CS` de la microSD, car le driver SDSPI déclenche des callbacks
d'interruption pendant les transferts. Toute tentative de bascule `CS` via le
bus I²C engendre des délais importants, fait échouer `sdmmc_card_init` avec le
code `0x107` et produit les messages `CH422G transaction fallback ...`.

## Raccordement matériel

- **CS microSD** → souder un fil direct entre la broche `CS` du lecteur TF et
  le **GPIO34** de l'ESP32-S3.
- Conserver le routage d'origine pour `MOSI` (GPIO11), `MISO` (GPIO13) et
  `SCLK` (GPIO12).
- L'extenseur CH422G reste câblé au bus I²C partagé (SDA=GPIO8, SCL=GPIO9) afin
de conserver la commande du rétroéclairage, du VCOM et des multiplexeurs USB.

## Séquence d'initialisation

1. Initialisation du bus SPI3 (FSPI dédié) avec `MOSI=11`, `MISO=13`,
   `SCLK=12` et `CS=34`.
2. Fréquence d'amorçage bridée à **400 kHz** pour garantir la détection même
   avec des nappes longues ou des cartes récalcitrantes.
3. Montage automatique en lecture/écriture sous `/sdcard` via VFS FAT.

Après validation sur cible, il est possible d'augmenter `host.max_freq_khz`
vers 10–20 MHz. Toute montée en fréquence doit être testée à l'oscilloscope pour
confirmer l'intégrité des fronts et l'absence d'interférences sur le bus RGB.
