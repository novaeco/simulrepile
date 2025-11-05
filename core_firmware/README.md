# SimulRepile Core Firmware (ESP32-S3-DevKitC-1)

Ce projet ESP-IDF cible l'ESP32-S3-DevKitC-1 (module ESP32-S3-WROOM-2-N32R16V). Il implémente le "cœur"
maître de l'architecture SimulRepile option B :

- Génération de l'état simulé des terrariums (jusqu'à 4) via `state/core_state_manager.*`.
- Publication périodique et sur demande des instantanés vers la carte Waveshare via le protocole UART
  `core_link` partagé (`common/include/link/core_link_protocol.h`).
- Gestion des événements tactiles remontés par la Waveshare afin d'ajuster la simulation.

## Compilation

```bash
idf.py set-target esp32s3
idf.py build
```

## Flash & moniteur

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

## Paramètres clé (menuconfig)

`SimulRepile Core Configuration` expose :

- Ports/broches UART (`UART port`, `TX pin`, `RX pin`, `Baud rate`).
- Delai de handshake et intervalle d'émission.
- Epoch de référence des timestamps.

Les valeurs par défaut correspondent à un câblage croisé direct UART1 entre DevKitC et Waveshare :

| DevKitC (GPIO) | Waveshare (GPIO) | Fonction |
| --- | --- | --- |
| 17 | 44 | TX -> RX |
| 18 | 43 | RX <- TX |
| GND | GND | Masse commune |

## Dépendances

Le projet s'appuie sur ESP-IDF ≥ 5.5. Aucun composant externe n'est nécessaire ; les structures de
protocole partagées se trouvent dans `../firmware/common/include/`.
