# Real mode hardware

## Capteurs
- **SHT31** : capteur température/hygrométrie sur bus I\u00b2C. Adresse configurée par `sht31_addr`.
- **BH1750** : capteur de luminosité sur bus I\u00b2C. Adresse configurée par `bh1750_addr`.
- **MH-Z19B** : capteur de CO\u2082 sur bus UART. Broches TX/RX configurées par `uart_tx_gpio`/`uart_rx_gpio`.

## Actionneurs
- **Chauffage**, **UV**, **Néon**, **Pompe**, **Ventilation**, **Humidificateur** : pins déclarées dans `terrarium_hw_t`.
- Les seuils de régulation (température, humidité, luminosité, CO\u2082) sont paramétrables via `terrarium_hw_t`.

## Sécurités
- Temporisation anti-flash de 500 ms sur chaque sortie pour éviter les commutations rapides et protéger les relais (NF C15-100, §531).
- Coupure d'urgence : tous les actionneurs sont arrêtés dès que la température interne dépasse 40 °C, limitant les risques d'incendie (NF C15-100, NF EN 60335).
- Watchdog de communication capteurs (timer ESP-IDF) : si aucune donnée valide n'est reçue pendant 10 s, toutes les sorties sont désactivées afin de satisfaire aux principes de sûreté de fonctionnement de la NF EN 61508.
