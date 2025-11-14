# Guide d'installation des firmwares SimulRepile

## 1. Objectif
Ce document décrit, étape par étape, la préparation de l'environnement de développement, la compilation, le flash et la validation des deux firmwares ESP-IDF du projet SimulRepile :

- **Firmware afficheur** pour la carte Waveshare ESP32-S3-Touch-LCD-7B (répertoire `firmware/`).
- **Firmware cœur** pour la carte ESP32-S3-DevKitC-1 (répertoire `core_firmware/`).

Chaque section fournit les prérequis matériels, la configuration logicielle et les procédures de test afin d'obtenir un système bi-carte opérationnel.

## 2. Vue d'ensemble matérielle
| Carte | MCU | Rôle | Interfaces principales |
| --- | --- | --- | --- |
| Waveshare ESP32-S3-Touch-LCD-7B | ESP32-S3-WROOM-1-N16R8 | Interface utilisateur LVGL, gestion SD, Core Link UART | LCD RGB 1024×600, tactile I²C, slot microSD, USB FS, EXIO (CH422G) |
| ESP32-S3-DevKitC-1 | ESP32-S3-WROOM-2-N32R16V | Génération d'états simulés, publication Core Link, gestion profils terrarium | UART1 (Core Link), SPI/SD optionnel, USB-UART (flash) |

### 2.1 Câblage UART croisé recommandé
| DevKitC (GPIO) | Waveshare (GPIO) | Fonction |
| --- | --- | --- |
| 17 | 44 | UART TX DevKitC → RX Waveshare |
| 18 | 43 | UART RX DevKitC ← TX Waveshare |
| GND | GND | Masse commune |

> **Astuce :** conservez un câble USB par carte pour le flash/monitoring et un câble UART croisé dédié entre les deux cartes pour le protocole Core Link.

## 3. Préparation de l'environnement commun
1. **Installer les dépendances système** (Linux) :
   ```bash
   sudo apt update
   sudo apt install git wget flex bison gperf python3 python3-venv python3-pip cmake ninja-build ccache libffi-dev libssl-dev dfu-util
   ```
   > Sous Windows/macOS, suivez la procédure officielle ESP-IDF pour installer les dépendances équivalentes.
2. **Cloner le dépôt** :
   ```bash
   git clone https://github.com/<organisation>/simulrepile.git
   cd simulrepile
   ```
3. **Installer ESP-IDF ≥ 6.1** :
   ```bash
   cd $HOME
   git clone -b v6.1 https://github.com/espressif/esp-idf.git esp-idf-v6.1
   cd esp-idf-v6.1
   ./install.sh esp32s3
   ```
4. **Activer l'environnement ESP-IDF** (à exécuter dans chaque shell de build) :
   ```bash
   . $HOME/esp-idf-v6.1/export.sh
   ```
5. **Vérifier les ports série** :
   - Linux : `ls /dev/ttyACM*` (Waveshare) et `ls /dev/ttyUSB*` (DevKitC).
   - Windows : `Gestionnaire de périphériques > Ports (COM & LPT)`.
   - macOS : `ls /dev/cu.usbserial*`.

## 4. Préparation du support SD (firmware afficheur)
1. Formatez une carte microSD en FAT32 (allocation ≥ 32 Ko).
2. Montez la carte puis copiez l'arborescence fournie :
   ```bash
   cd simulrepile/firmware/data
   rsync -av --progress ./ /media/<user>/SDCARD/
   ```
3. Vérifiez la présence minimale des dossiers :`i18n/`, `docs/`, `saves/`.
4. Éjectez proprement la carte SD et insérez-la dans le slot de la Waveshare.

## 5. Installation du firmware afficheur (Waveshare ESP32-S3-Touch-LCD-7B)
### 5.1 Configuration du projet
1. Placez-vous dans le répertoire firmware :
   ```bash
   cd simulrepile/firmware
   ```
2. Sélectionnez la cible ESP32-S3 :
   ```bash
   idf.py set-target esp32s3
   ```
3. (Optionnel) Ouvrez `menuconfig` pour ajuster les paramètres :
   ```bash
   idf.py menuconfig
   ```
   Paramètres clés :
   - `SimulRepile Display Configuration → Core Link UART` (broches TX/RX si différentes du câblage par défaut).
   - `BSP Configuration → SD Bus Width` (1-bit ou 4-bit selon le câblage).
   - `Application → Autosave interval` (intervalle de sauvegarde automatique).

### 5.2 Compilation
```bash
idf.py build
```
Le binaire final est généré dans `build/firmware.bin` avec la table de partitions `partitions.csv` et les paramètres `sdkconfig.defaults` fournis.

### 5.3 Flash
1. Connectez la Waveshare via USB (mode USB natif, interrupteur positionné sur USB).
2. Flashez et ouvrez le moniteur :
   ```bash
   idf.py -p /dev/ttyACM0 flash monitor
   ```
   Ajustez `-p` avec le port détecté précédemment. `Ctrl+]` pour quitter le moniteur.
3. Attendez l'initialisation complète : vérifiez dans les logs la montée en puissance de LVGL, le montage de la SD et l'établissement du lien Core Link.

### 5.4 Validation fonctionnelle
- Naviguez dans l'interface : Dashboard → Documents → Paramètres → À propos.
- Déclenchez une sauvegarde manuelle et vérifiez la création d'un fichier dans `/sdcard/saves/`.
- Basculez de langue (FR/EN/DE/ES) et confirmez la mise à jour dynamique des textes.
- Si la carte cœur est absente, confirmez le mode autonome (les métriques simulées évoluent localement).

## 6. Installation du firmware cœur (ESP32-S3-DevKitC-1)
### 6.1 Préparation des profils terrarium
1. Créez un dossier `profiles/` sur la SD (partagée ou dédiée) ou utilisez `/spiffs/profiles`.
2. Ajoutez jusqu'à quatre fichiers JSON avec les clés `scientific_name`, `common_name` et `environment` (voir exemple dans `core_firmware/README.md`).
3. Assurez-vous que la carte SD est accessible via un lecteur SPI/SD raccordé au DevKitC si l'option SD est activée.

### 6.2 Configuration du projet
1. Placez-vous dans le répertoire cœur :
   ```bash
   cd simulrepile/core_firmware
   ```
2. Sélectionnez la cible ESP32-S3 :
   ```bash
   idf.py set-target esp32s3
   ```
3. (Optionnel) Ajustez les paramètres via `idf.py menuconfig` :
   - `SimulRepile Core Configuration → UART port / TX pin / RX pin / Baud rate`.
   - `Profiles → Primary SD path` et `Fallback SPIFFS path`.
   - `Simulation → Handshake timeout` et `Snapshot interval`.

### 6.3 Compilation
```bash
idf.py build
```
Le firmware est produit dans `build/core_firmware.bin` avec la table de partitions `partitions.csv` et les paramètres par défaut `sdkconfig.defaults`.

### 6.4 Flash
1. Connectez le DevKitC via son port USB-UART (CH343/CP210x selon la version).
2. Flashez et lancez le moniteur :
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   Ajustez `-p` selon le port reconnu (`COMx` sous Windows).
3. Surveillez les logs pour vérifier :
   - Le chargement des profils terrarium.
   - L'initialisation du protocole Core Link et les trames périodiques (`STATE_FULL` ou `STATE_DELTA`).

### 6.5 Validation fonctionnelle
- Confirmez la détection de la Waveshare dans les logs (handshake réussi).
- Vérifiez la transmission régulière des états (timestamps et métriques mis à jour).
- Simulez des commandes entrantes (touch events) depuis la Waveshare et observez la réponse (ajustement des cycles, logs dédiés).

## 7. Synchronisation et tests intégrés
1. **Reliez les deux cartes via l'UART croisé** (section 2.1) après flash individuel.
2. **Démarrez le DevKitC**, attendez la publication d'un premier état.
3. **Alimentez la Waveshare** : elle réalise le handshake, synchronise l'horloge et affiche les métriques reçues.
4. **Contrôlez la cohérence** :
   - Les terrariums listés correspondent aux profils JSON chargés.
   - Les sauvegardes déclenchées côté Waveshare intègrent les deltas reçus.
   - En cas de déconnexion du DevKitC, la Waveshare affiche une alerte Core Link et repasse en simulation locale.

## 8. Dépannage rapide
| Symptôme | Cause probable | Actions recommandées |
| --- | --- | --- |
| `Failed to mount /sdcard` sur Waveshare | Mauvais câblage SD, carte non formatée, bus width incohérent | Revoir la section 4, vérifier `BSP_SD_BUS_WIDTH`, reformater la carte. |
| `Core Link handshake timeout` côté DevKitC | UART non raccordé ou mauvais port/baudrate | Vérifier le câblage (section 2.1), confirmer les broches dans `menuconfig`, aligner les baudrates. |
| `LVGL buffer alloc failed` | PSRAM non activée | Activer PSRAM dans `menuconfig` (`Component config → ESP32S3-specific → Support for external, SPI-connected RAM`). |
| Aucun profil chargé | Dossier vide ou chemins erronés | Vérifier les chemins SD/SPIFFS dans `menuconfig`, valider la structure JSON. |

## 9. Bonnes pratiques supplémentaires
- Versionnez vos `sdkconfig` customisés (`idf.py save-defconfig`) pour garantir la reproductibilité.
- Utilisez `idf.py fullclean` après une mise à jour ESP-IDF majeure ou un changement de composant critique.
- Automatisez les builds via CI/CD (GitHub Actions) avec cache `ccache` pour accélérer les compilations.
- Surveillez la consommation via un multimètre USB pendant l'utilisation conjointe des deux cartes.

En suivant ce guide, vous disposez d'un pipeline complet pour déployer et maintenir les deux firmwares SimulRepile en environnement de développement ou de démonstration.
