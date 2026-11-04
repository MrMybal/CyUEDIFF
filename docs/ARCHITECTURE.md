# Architecture et étapes suivantes

Le contrat central est un snapshot d'objets sérialisés indépendant de l'interface. Les objets utilisent leur chaîne d'outer et leur nom ; les références numériques des tables deviennent des chemins. Les propriétés sont typées, et les parties indécodables restent identifiées par leur taille et leur empreinte. La sortie JSON est déterministe pour une lecture identique.

```text
Application hôte / CLI / vues CyUI
                 |
      Package + snapshot + compare
         /                   \
Archive binaire          ProjectContext
Bornes et SHA-256        .uproject / moteur / contenus
```

## Ce qui existe

`catalog.cpp` fournit les catalogues de métadonnées bornés des montages locaux et leur union avant/après. `GitRepository::catalog` liste les arbres Git sans lire les blobs. `CatalogView` reste embarquable et le worker desktop conserve les scans récents, annule ceux dépassés et publie leurs sélections. Voir [CATALOG.md](CATALOG.md).

`descriptor.cpp` partage le décodage borné des JSON de projets, plugins et moteurs : commentaires, virgules finales de tableaux et profondeur limitée. `archive.cpp` fournit les accès aléatoires bornés, FString UTF-16/ANSI et le SHA-256 portable. `package.cpp` lit le résumé, les tables, les exports et les tags de propriétés. Le lecteur utilise les plages de script enregistrées dans les exports récents. Les échecs ne déclenchent aucune recherche heuristique de propriétés dans des octets arbitraires.

`decoders.cpp` fournit `SerializationProfile`, `AssetDecoder` et le registre immuable par type. Les callbacks explicites utilisent une entrée séquentielle bornée et publient du JSON identifié par schéma ; les ambiguïtés et erreurs restent opaques. Les profils sont choisis par les versions réellement sérialisées, y compris les GUID requis. Voir [DECODERS.md](DECODERS.md).

`native.cpp` décode les pins EdGraph dans une fenêtre limitée à l’export ; `blueprint.cpp` construit les graphes et vérifie leurs connexions. Le snapshot de graphe et `graph_changes` ajoutent une projection identifiée par GUID. Les parties natives inconnues restent opaques. Voir [BLUEPRINTS.md](BLUEPRINTS.md).

`texture.cpp` lit le profil natif Texture2D et les index de payload ; `image.cpp` décode explicitement les aperçus et compare leurs pixels. Les payloads ne sont pas copiés dans les snapshots ; ils y sont décrits et empreintés. Voir [TEXTURES.md](TEXTURES.md).

`mesh.cpp` décode les sources MeshDescription, leurs éléments et attributs, puis reconstruit une géométrie à identifiants stables. Les données de bulk partagent la validation et `BlockDecoder` ; `codec.cpp` fournit un chargeur explicite et `codecs/oodle_adapter.cpp` un adaptateur optionnel au SDK local. Voir [MESHES.md](MESHES.md) et [CODECS.md](CODECS.md).

`material_io.cpp` décode les entrées natives de matériaux selon leurs versions personnalisées. `material.cpp` projette les graphes par GUID et les paramètres d’instances par nom/association/index. `compare_materials` distingue les changements d’une projection incomplète ; le diff général publie aussi `materialChanges`. Voir [MATERIALS.md](MATERIALS.md).

`project.cpp` décrit les contenus disponibles du projet et de l'installation moteur. La résolution des dépendances donne les chemins ; le lecteur ne charge pas récursivement tous les assets. `diff.cpp` compare les arbres et publie les valeurs avant/après. Les empreintes du package restent distinctes des changements de propriétés.

Les données binaires lourdes ne sont pas copiées dans les snapshots. Les tables sont chargées en mémoire ; les valeurs sont décodées à la demande de l'appel `Package::open` selon `ReadOptions`, puis conservées. Le parcours n'est pas encore une API paresseuse par export. Le SHA-256 natif relit les plages utiles et le fichier : il reste une marge d'optimisation pour les gros assets.

`CyUEDiff::Git` est un module optionnel séparé : `git.cpp` résout et matérialise des groupes de packages historiques, `git_process.cpp` exécute Git directement avec sorties binaires sur disque et limites. Les objets LFS locaux sont copiés et vérifiés ; les révisions possèdent leurs dossiers temporaires. Le core ne dépend pas de cet adaptateur. `git_project.cpp` décrit les contextes par commit et leurs racines de contenus, avec provenance locale des installations moteur. Le diff peut signaler des différences de contexte indépendantes du package. Voir [GIT.md](GIT.md) et [PROJECTS.md](PROJECTS.md).

`CyUEDiff::UI` dépend du cœur et de CyUI, jamais l'inverse. `set_packages` prépare la comparaison hors du rendu ; `draw` n'effectue aucune opération de fichiers. Le client peut charger les données sur ses propres workers et les publier via des `shared_ptr<const Package>`.

`apps/desktop` fournit l’hôte Windows optionnel depuis la version 0.9. Le modèle de document charge les packages, le diff et les aperçus sur un worker avec une demande en attente et publication de la plus récente. La fenêtre Win32, le renderer Direct3D 11, les dialogues et les ressources GPU restent dans l’exécutable. Les vues reçoivent les données à la publication, avant la frame ; le core et son installation restent indépendants de cet hôte. Voir [DESKTOP.md](DESKTOP.md).

## Prochaines tranches

1. **Contrat de backends et corpus** : le registre C++ par structure/classe et les contrats `SerializationProfile` / `AssetDecoder` existent en 0.8. Étendre le contrat à des sources de packages et schémas de réflexion complets, au-delà des fenêtres de données actuellement exposées. Constituer des fixtures redistribuables générées dans chaque version UE ciblée, avec les résultats attendus.
2. **Étendre Blueprint et matériaux** : les pins et connexions Blueprint et leurs identités par GUID existent en 0.2. Étendre les historiques FText, les classes de plugins, les profils moteur et les classes de matériaux personnalisées ; ajouter une présentation spécialisée des variables, fonctions et expressions.
3. **Étendre les textures** : la lecture source, les aperçus RGBA8 pris en charge et le diff avec seuil existent en 0.3. Oodle est maintenant disponible par adaptateur explicite. Ajouter LZ4, bulk data anciens, HDR/pleine précision, cooked, blocs UDIM et ressources virtualisées.
4. **Étendre les meshes** : la géométrie source MeshDescription, le diff, l’OBJ et le filaire existent en 0.4. Ajouter les formats anciens, sections de rendu et LOD reconstruits, squelettes, collisions, puis Nanite. Étendre les attributs et la correspondance après renumérotation.
5. **Types de projets** : schémas pour classes/structs natives, propriétés non versionnées et conteneurs complexes. Un backend auxiliaire compilé pour un moteur cible pourra être proposé lorsque le parseur autonome ne dispose pas des schémas. Son éventuel lancement devra être une capacité explicite distincte de la lecture autonome.
6. **Application CyUI** : la fenêtre native, l’ouverture de fichiers et les chargements asynchrones existent en 0.9, avec les vues de graphes, images, filaire et diff. Le navigateur de contenus locaux/Git est disponible en 0.11. Ajouter plusieurs documents, un inspecteur typé, les contextes distincts par côté et un navigateur d’historique Git. La saisie des révisions Git/LFS, les ajouts/suppressions et les contextes historiques sont disponibles en 0.10. Étendre la sélection des sources/mips/LOD, l’annulation et les métriques. Utiliser respectivement `Assets`, `Inspector`, `Nodes`, `Images`, `Viewport` et `Diff` selon les données réellement prêtes.
7. **Étendre Git/LFS** : les révisions et compagnons locaux sont matérialisés et comparés en 0.6. Les descripteurs de projets et plugins historiques ainsi que les moteurs par côté sont disponibles en 0.7. Étendre les règles d’activation, dépendances et configurations INI, puis les renommages automatiques, un téléchargement LFS explicite, un index/cache adressé par empreinte et profil, une API par export, annulation et métriques.

Les numéros de moteur ne suffisent pas pour garantir une lecture : versions de fichier, versions personnalisées, plugins et branches modifiées participent au profil. Tout format futur doit être rejeté avant d'interpréter la suite d'un en-tête incompatible.

## Références de format

- [FPackageFileSummary, Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/FPackageFileSummary)
- [CyUI et son guide d'intégration](https://github.com/MrMybal/CyUI/blob/main/docs/AI_GUIDE.md)

L'implémentation ne contient ni copie ni dépendance des sources du moteur. Les installations locales ont servi à vérifier les champs et les transitions de format. Les assets locaux utilisés pour les essais ne sont pas redistribués.
