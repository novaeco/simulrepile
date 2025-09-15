# Real mode hardware

## Capteurs
- **SHT31** : capteur température/hygrométrie sur bus I\u00b2C. Adresse configurée par `sht31_addr`.
- **BH1750** : capteur de luminosité sur bus I\u00b2C. Adresse configurée par `bh1750_addr`.
- **MH-Z19B** : capteur de CO\u2082 sur bus UART. Broches TX/RX configurées par `uart_tx_gpio`/`uart_rx_gpio`.

## Actionneurs
- **Chauffage**, **UV**, **Néon**, **Pompe**, **Ventilation**, **Humidificateur** : pins déclarées dans `terrarium_hw_t`.
- Les seuils de régulation (température, humidité, luminosité, CO\u2082) sont paramétrables via `terrarium_hw_t`.
