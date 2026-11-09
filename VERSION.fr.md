# Notes de version de CyUEDiff

[README français](README.fr.md) · [English](VERSION.md) · [Suivi des problèmes](https://github.com/MrMybal/CyUEDIFF/issues)

## 0.187.0 — Politiques de lecture source

Les limites de travail/taille source sont explicites dans le Core, la CLI et l’interface EN/FR, avec valeurs après indépendantes et conservation dans les espaces de travail 14. Un échec de limite/compteur rend la comparaison sémantique non vérifiable au lieu d’inventer des suppressions. Les politiques choisies gardent leur provenance sans créer de changements d’asset. Utilisez les en-têtes/bibliothèques 0.187 correspondants après les ajouts à Package/DiffResult. La dernière archive Windows locale vérifiée reste 0.182. Voir [SOURCE_READ_LIMITS.md](docs/SOURCE_READ_LIMITS.md).

## 0.186.0 — Métadonnées explicites des types source

Des contextes immuables et liés au profil complètent les identités absentes des anciennes maps/sets balisées. `source-types` prépare un catalogue depuis un USMAP explicite et des types racines sélectionnés ; les contrôles existants de la CLI/Git et de l’interface EN/FR le conservent par côté. Les balises restent intactes avec `resolvedType` à côté ; contradictions, alias ambigus et sérialiseurs natifs sans liaison restent indisponibles. Les propriétés/suites natives du tissu Cube réel sont accessibles, sans résolution des valeurs par défaut ni projection de géométrie physique source. Le Humanoid plus grand dépasse encore le budget source par défaut. Recompilez avec les en-têtes/bibliothèques 0.186 correspondants après les ajouts à `DecoderRegistry` et `Package`. La dernière archive Windows locale vérifiée reste 0.182. Voir [le contrat](docs/TAGGED_TYPES.md).

## 0.185.0 — Structures source hybrides

Le Core combine désormais un préfixe de propriétés balisées explicitement certifié et une suite native via les catalogues existants. Valeurs imbriquées/répétées, budgets partagés, repli opaque complet et accès temporaire au préfixe pour les décodeurs C++ conditionnels sont disponibles dans la CLI et l’inspection EN/FR. Des assets originaux de plugin sauvegardés puis rechargés par Unreal valident les valeurs exactes et différences nommées via un SDK installé séparément. Les types internes absents des anciennes maps/sets et les valeurs par défaut restent à traiter. Utilisez les en-têtes/bibliothèques 0.185 correspondants : DecoderContext et AssetDecoder ont été étendus. Voir [HYBRID_STRUCTS.md](docs/HYBRID_STRUCTS.md).

## 0.184.0 — Sources locales du tissu physique

Le Core indépendant et la CLI projettent désormais la géométrie physique de tissu UE 5.3 sélectionnée, les données d’os/poids/attaches et les transitions natives modernes. Les requêtes ciblées, différences stockées nommées, correspondances de pose de référence Chaos, export OBJ et contrôles explicites des indices de rendu sont disponibles via un nouvel exemple de SDK installé. Les deltas de cartes et valeurs par défaut de constructeurs absentes gardent une couverture incomplète ; l’interface EN/FR propose désormais des LODs physiques indépendants, les différences ciblées de sommets/attaches, la configuration stockée et les aperçus filaires de pose de référence. Les espaces de travail version 13 conservent les profils, objets, LODs, sélection et caméra ; les anciens formats migrent avec le nouveau lecteur désactivé. Voir [PHYSICAL_CLOTH.md](docs/PHYSICAL_CLOTH.md).

La dernière archive Windows locale vérifiée reste 0.182.0. Ce jalon des sources ne téléverse, ne publie et ne remplace pas cette archive.

## 0.183.0 — Développement local des sources

Ce jalon des sources et de l’API ajoute des liaisons de structures hybrides explicites : préfixe de propriétés réfléchies certifié, suivi de données natives déclaratives bornées. Les contextes de schéma version quatre conservent la provenance des descriptions et les identités avant/après indépendantes. Le profil de tissu UE 5.3 Windows sélectionné expose les propriétés physiques associées, les valeurs natives des attaches et les enregistrements bruts des transitions. Il ne déduit pas les valeurs par défaut absentes, n’exécute pas de simulation et ne découvre pas les sérialiseurs arbitraires des plugins.

Les structures C++ publiques d’options/contexte gagnent `struct_tails` ; recompilez les hôtes avec les en-têtes et la bibliothèque correspondants. Le changement de version mineure distingue ce contrat SDK de 0.182 selon la règle de compatibilité CMake. La dernière archive Windows locale vérifiée reste 0.182.0 ; cette modification des sources ne la remplace, ne la téléverse et ne la publie pas. Voir [le contrat et les preuves](docs/HYBRID_STRUCTS.md).

L’inspection source/cooked paginée ajoute des pages d’éléments directs, chemins imbriqués/balises, accès par indice et sélection EN/FR persistante. Vérification sur de gros jeux originaux, captures réelles de tissu/configuration et consommateur Core installé séparément ; l’archive 0.182 précédente reste inchangée.

## 0.182.0 — Aperçu de développement local

Cette version des sources regroupe les lecteurs natifs, comparaisons et vues desktop ajoutés après 0.181. La CLI, l’application Windows x64 et les SDK réutilisables Core/Git sont vérifiés dans une archive locale. Cette tâche ne téléverse pas de binaires, ne publie pas de release GitHub et ne prétend pas avoir exécuté une nouvelle CI hébergée. La vitrine publique est gérée séparément.

### Principaux ajouts

- Lecture explicite UE 5.3 Windows des mips de textures cuits, des LODs de rendu statiques/squelettiques, des géométries de collision Chaos natives et des valeurs d’animations/courbes compressées, avec contextes indépendants et fenêtres bulk vérifiées.
- Inspection sélectionnée des géométries et attributs Nanite natifs, correspondance de surfaces, requêtes de hiérarchie/clusters et vues desktop EN/FR.
- Profils de poids cuits nommés, morphs de rendu compressés, correspondances de tissu enregistrées, tables de sommets dupliqués conservées et canaux personnalisés float32/float16/UNorm8, avec API Core typée et inspecteurs persistants.
- Préparation de métadonnées UHT depuis les sources correspondantes, préfixes de déclarations réfléchies, intégrité IoStore obligatoire et prise en charge bornée d’autres structures sélectionnées de conteneurs.
- Le lecteur HTML hors ligne bilingue inclus, les chapitres de contexte natif à jour et des captures originales d’attributs cuits, avec styles/scripts/images locaux.
- Package CMake versionné, métadonnées de composants/format d’espace de travail et SDK Core excluant les en-têtes d’interface réservés aux sources. Le vérificateur compile les exemples extraits et contrôle toutes les dépendances des en-têtes installés.

### Disponibilité et limites

Le build Windows de développement actuel est vérifié localement avec MSVC 2022 et Direct3D 11/WARP. L’archive par défaut nécessite le runtime Microsoft Visual C++ x64 correspondant et une installation séparée de Git pour les fonctions Git. Les bibliothèques d’interface se compilent depuis les sources avec le checkout CyUI de l’hôte ; le SDK installé exporte le Core et éventuellement Git. Aucun desktop Linux/macOS n’est disponible. Les anciennes preuves de CI Windows/Ubuntu concernent 0.181 et ne valident pas un build 0.182 non poussé.

Les lecteurs utilisent des profils explicites de moteur/build/plateforme et des mappings correspondants. Les valeurs, pixels et géométries stockés diffèrent d’une évaluation finale de shaders, de skinning, de simulation de tissu ou de graphe d’animation. Les sérialiseurs arbitraires de plugins, les assets/configurations physiques de tissu, d’autres profils natifs/producteurs et les éléments du [suivi de complétude](docs/COMPLETION.md) ne sont pas déclarés terminés. Les assets privés, l’implémentation du moteur, les codecs appartenant à l’hôte et les références générées sont exclus de l’archive.

Voir les [contrôles de distribution](docs/DISTRIBUTION.md), la [validation actuelle](docs/VALIDATION.md), le [guide desktop](docs/DESKTOP.md) et les [formats couverts](docs/ROADMAP.md).

## 0.181.0 — Ancien jalon des sources


Il s’agissait de la version précédente des sources ; les notes ci-dessous décrivent ce jalon historique. Il n’existe pas de release GitHub de l’application ou de téléchargement desktop public. La [vitrine](https://mrmybal.github.io/CyUEDIFF/?lang=fr) est publiée séparément.

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
