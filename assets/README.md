# Terrarium Media Assets

Ce répertoire regroupe les ressources binaires embarquées par l'application pour
représenter visuellement les substrats et décors disponibles dans les
terrariums virtuels. Les fichiers sont codés en RGB565 (24x24 pixels) et sont
chargés statiquement dans le firmware via `components/image/terrarium_assets.c`.

- `substrates/*.bin` : icônes de substrat (sable, tropical, roche).
- `decors/*.bin` : icônes de décors (lianes, rochers, caverne).

Ces fichiers peuvent être déployés tels quels sur la microSD au besoin pour un
usage externe, mais la compilation embarque déjà les versions intégrées.
