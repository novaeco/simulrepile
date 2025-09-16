# Cadre réglementaire applicatif

Ce document synthétise les obligations juridiques utilisées par l'interface
Simulation Repile. Chaque vérification logicielle référence explicitement ces
sources pour assurer la traçabilité des contrôles embarqués.

## Dimensions minimales

- **Arrêté du 8 octobre 2018** fixant les règles générales de détention d'animaux
  d'espèces non domestiques (JORF n°0240 du 17 octobre 2018, texte n° 15).
  - Annexe 2 : dimensions minimales par taxon pour les terrariums et vivariums.
  - Article 2 et annexe 4 : obligation d'installer un dispositif garantissant le
    respect permanent de ces dimensions.
- Implémentation : la fonction `species_db_dimensions_satisfied()` et les tests
  réalisés dans `main/reptile_game.c` bloquent la sélection d'espèces lorsque les
  dimensions du terrarium sont inférieures à ce seuil réglementaire.

## Certificat de capacité

- **Code de l'environnement**, article L413-2 : obligation de certificat de
  capacité (CDC) et d'autorisation d'ouverture d'établissement (AOE) pour les
  établissements détenant des animaux d'espèces non domestiques.
- **Arrêté du 8 octobre 2018**, articles 3 et 9 : rappel des pièces à conserver
  et modalités de présentation aux services de contrôle.
- Implémentation : le module `certificate_is_available()` recherche un fichier de
  justificatif CDC/AOE (`/certificates/<code>`) avant d'autoriser la sélection de
  l'espèce. Les écrans de mise en garde et de quiz contextualisés rappellent ces
  obligations.

## Espèces protégées

- **Règlement (CE) n° 338/97 du Conseil** du 9 décembre 1996 relatif à la
  protection des espèces de faune et de flore sauvages (JO L 61 du 3.3.1997).
- **Règlement (CE) n° 865/2006 de la Commission** du 4 mai 2006 fixant les
  modalités d'application du règlement (CE) n° 338/97 (JO L 166 du 19.6.2006).
- Ces textes intègrent les annexes CITES (Convention de Washington) et imposent
  la présentation d'un certificat intra-UE (CITES) pour tout spécimen inscrit à
  l'annexe B (ex. *Python regius*).
- Implémentation : les entrées `species_db` marquent les espèces protégées et les
  contrôles de `main/reptile_game.c` exigent la présence du justificatif CITES
  avant validation. Un quiz explicatif rappelle la référence réglementaire.

## Documentation embarquée

Le menu « Règlementation » de l'écran principal résume ces obligations et pointe
vers ce fichier (`docs/reglementation.md`) stocké sur la carte SD ou dans le
répertoire du projet afin de garantir la disponibilité hors ligne des sources
juridiques.
