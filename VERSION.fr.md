# Notes de version de CyUEDiff

[README français](README.fr.md) · [English](VERSION.md) · [Suivi des problèmes](https://github.com/MrMybal/CyUEDIFF/issues)

## 0.181.0 — En développement

Il s’agit de la version du projet source déclarée dans `CMakeLists.txt`. Le Core, la CLI et l’application optionnelle utilisent la même version. Il n’existe pas de release GitHub de l’application ou de téléchargement desktop public. La [vitrine](https://mrmybal.github.io/CyUEDIFF/?lang=fr) est publiée séparément.

### Changements de cette version des sources

- Les morph targets MeshDescription modernes exposent les deltas de position sources nommés et les deltas optionnels de normales par instance de sommet via des snapshots Core immuables, le JSON de géométrie et les différences de mesh nommées.
- L’onglet Meshes anglais/français ajoute une inspection des morphs consultable par recherche et persistante, et surligne les arêtes du mesh de base concernées par les changements de deltas sources. L’export OBJ conserve la géométrie de base prise en charge.
- Les lecteurs de composantes réfléchies des collisions/sockets possèdent leur stockage de noms de champs, ce qui corrige un problème de durée de vie identifié par GCC lors de la première CI Ubuntu.
- Les seuils de comparaison angulaire positifs tiennent compte de l’arrondi en double précision aux limites inclusives. Les comparaisons à tolérance nulle restent strictes ; les métriques angulaires rapportées sont inchangées.
- L’exemple de morphs installé, limité au Core, montre l’accès typé aux sources sans Git ni CyUI.

Voir les [contrats des morphs](docs/MORPHS.md) et l’[exemple du SDK installé](examples/mesh_morph_consumer/).

### Fonctions antérieures conservées

- **0.180** : os, poses enregistrées et profils bruts de poids nommés des MeshDescription squelettiques modernes, validés sur des jeux de test synthétiques. Voir les [données de skinning sources](docs/SKINNING.md).
- **0.179** : magasins locaux explicites de payloads d’éditeur virtualisés avec vérification BLAKE3 complète obligatoire des octets bruts. Voir les [sources virtualisées](docs/VIRTUALIZED.md).
- **0.173–0.178** : descriptions natives déclaratives bornées et décodage explicite ASCII, Latin-1, Windows-1252 et UTF-8. Voir les [descriptions binaires](docs/BINARY_LAYOUTS.md).
- **0.158–0.172** : différences binaires par blocs, manifestes compacts, composition bornée et reconstruction à partir de fichiers vérifiée par SHA-256. Voir les [deltas de blocs](docs/BINARY_DELTA.md).
- Les jalons précédents comprennent les différences visuelles de graphes/images/géométries, les propriétés typées, les contextes de schéma cooked, la navigation Git/conteneurs, les documents multiples, les espaces de travail persistants, les préférences de fenêtre/session et l’inspection des données enregistrées de squelette/animation/courbe. Leurs contrats restent dans le [guide desktop](docs/DESKTOP.md) et la [feuille de route](docs/ROADMAP.md).

### Plateformes et disponibilité

| Composant | Disponibilité actuelle |
| --- | --- |
| Application Windows x64 | Compilée et vérifiée localement avec MSVC 2022 et Direct3D 11. La compilation nécessite un dépôt CyUI séparé. Aucune release desktop publique pour l’instant. |
| Core/CLI Windows, avec/sans Git | CI hébergée réussie sur Windows 2022. |
| Core/CLI Linux, avec/sans Git | CI hébergée réussie sur Ubuntu 24.04. Cela ne garantit pas la compatibilité avec toute distribution/libc. |
| Application Linux/macOS | Aucun build disponible. |
| Core/CLI macOS | Aucun build validé annoncé. |

L’[exécution hébergée 0.181](https://github.com/MrMybal/CyUEDIFF/actions/runs/42369077581) réussit sur les deux systèmes avec/sans Git, ainsi que pour les exemples externes du SDK installé et le scan de secrets. La validation desktop Windows reste locale. Les artefacts CPack de la CI sont distincts d’une release publiée de l’application ; vérifiez leur plateforme/runtime avant utilisation. Les [conditions de distribution](docs/DISTRIBUTION.md) décrivent le contenu des archives et les choix de runtime MSVC.

### Limites documentées

- La prise en charge des formats est explicite. Sélectionner une racine de moteur fournit un contexte ; cela n’extrait pas les sérialiseurs C++ arbitraires d’un moteur ou plugin installé, ne lance pas Unreal et ne rend pas tous les formats lisibles.
- Les branches de versions de fichiers classiques couvrent UE4 518–522 et UE5 1000–1018. La validation sur fichiers réels a inclus UE 5.3.2 et 5.5.4 ; cela ne certifie pas tous les assets ou builds personnalisés de ces plages.
- Les valeurs non versionnées nécessitent des associations USMAP explicites et compatibles. Le corpus cooked réel ne possède pas de `.usmap` correspondant ; la sémantique des valeurs associées reste validée sur des jeux de test synthétiques. Les régions natives inconnues conservent leur nombre d’octets et leur empreinte opaque.
- La géométrie squelettique source moderne et les morphs restent validés sur des jeux de test synthétiques. Un corpus source réel indépendant et compatible reste nécessaire. Les buffers de skin/morph cooked ou legacy, le skinning et les poids de morph à l’exécution, les animations compressées, le retargeting à l’exécution, la physique native cooked et le décodage des payloads Nanite restent incomplets.
- Le bytecode Blueprint est opaque. Les expressions et paramètres de matériaux enregistrés sont inspectés sans compiler de shaders, résoudre toutes les fonctions externes ou effectuer un rendu des matériaux.
- La résolution des sources virtualisées utilise des magasins locaux explicites. Les backends distants, d’autres versions de conteneurs et les conteneurs chiffrés nécessitent du travail supplémentaire. Les payloads compressés peuvent nécessiter un codec compatible fourni explicitement.
- La lecture Git/LFS utilise les objets locaux et le contenu LFS local vérifié. Les objets distants manquants ne sont pas téléchargés. L’API de deltas binaires ne fournit pas de service d’hébergement/synchronisation distant.
- L’égalité binaire du package et sa couverture sémantique sont distinctes : les octets bruts peuvent changer sans différence dans la projection inspectée. Les données absentes/tronquées produisent un résultat incomplet. Les sources, valeurs par défaut et dépendances externes ne sont pas chargées automatiquement.
- Le moteur lit et compare les assets ; l’écriture/sauvegarde d’assets Unreal n’est pas implémentée.

Voir la [feuille de route des formats](docs/ROADMAP.md), les [contrats des schémas](docs/SCHEMAS.md) et les [limites d’exécution](docs/RUNTIME.md) pour le périmètre détaillé.

### Bugs connus et retours

Il n’existe pas de liste publiée de tickets de bugs confirmés propre à cette version. Cela ne signifie pas une absence de bugs. Les limites de couverture ci-dessus sont distinctes des défauts signalés. Signalez les problèmes reproductibles dans le [suivi des problèmes](https://github.com/MrMybal/CyUEDIFF/issues), avec la version des sources, le contexte moteur/build, la commande ou vue et les diagnostics. Ne partagez que les exemples que vous êtes autorisé à redistribuer.

[Releases de l’application](https://github.com/MrMybal/CyUEDIFF/releases) · [Historique des sources](https://github.com/MrMybal/CyUEDIFF/commits/main/)
