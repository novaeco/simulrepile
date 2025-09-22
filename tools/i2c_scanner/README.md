# Utilitaire `i2c_scanner`

Ce firmware minimal permet d’analyser l’état du bus I²C de la Waveshare ESP32-S3 Touch LCD 7B indépendamment de l’application principale SimulRepile. Il est particulièrement utile lorsque le CH422G ne répond plus et que la carte microSD n’est plus montée.

## Compilation et flash

```sh
cd tools/i2c_scanner
idf.py set-target esp32s3
idf.py -p <PORT> build flash monitor
```

> Remplacez `<PORT>` par le port série détecté (`COMx` sous Windows, `/dev/ttyACMx` ou `/dev/ttyUSBx` sous Linux).

Le fichier `sdkconfig.defaults` définit par défaut :

- `CONFIG_I2C_MASTER_SDA_GPIO=8`
- `CONFIG_I2C_MASTER_SCL_GPIO=9`
- `CONFIG_I2C_MASTER_FREQUENCY_HZ=100000`
- `CONFIG_ESP_TASK_WDT=n` (désactivation du watchdog pour éviter les resets pendant les scans)

Ajustez ces paramètres avec `idf.py menuconfig` si votre câblage diffère.

## Interprétation des journaux

Une fois le moniteur série lancé :

- `Bus levels: SDA=1 SCL=1 (0=bas, 1=haut).` confirme que les lignes sont libres.
- `CH422G candidat détecté à 0x2X` indique qu’un expander répond sur l’une des adresses 0x20–0x23.
- `Aucun périphérique n'a répondu entre 0x20 et 0x23.` signifie qu’aucun CH422G n’est détecté ; vérifier alimentation 3V3, nappes FFC, pull-ups et pont EXIO4→GPIO34.

Les scans sont répétés chaque seconde et le bus est remis dans un état idle (SDA/SCL = 1) avant la création du handle I²C afin de dégager un éventuel périphérique bloqué.

## Retour au firmware principal

Lorsque le CH422G répond à nouveau, quittez le moniteur (`Ctrl+]`), revenez à la racine du projet puis flashez l’application SimulRepile :

```sh
cd ../..
idf.py set-target esp32s3
idf.py -p <PORT> build flash monitor
```

Le firmware principal détectera automatiquement la présence du CH422G et relancera le montage microSD.
