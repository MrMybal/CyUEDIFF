# Validation initiale — 3 novembre 2026

Environnement vérifié : Windows x64, MSVC 19.40, CMake 3.31.6, configuration Release. Aucun lancement d'Unreal Editor.

## Vérifications reproductibles

`python scripts/build.py --desktop --cyui <checkout CyUI>` compile le cœur, la CLI, la bibliothèque UI et l’application Windows puis lance :

- `core` : **64 vérifications**, dont six transitions de format, tags anciens/récents, tableaux de structures, valeurs entières/booléennes/chaînes, SHA-256, exports séparés, changement de propriété, changement opaque, réorganisation des noms, références cycliques, version future, compte négatif, pointeur LFS, troncatures, montages de projet et moteur explicite.
- `decoders` : **110 vérifications** des profils, entrées bornées, structures natives, tableaux, fins d’exports, priorité explicite, erreurs et JSON de résultat.
- `descriptor` : **15 vérifications** du JSON de descripteurs Unreal, commentaires, virgules finales de tableaux, chaînes, refus de syntaxe et limites de profondeur.
- `blueprint` : **30 vérifications** des pins natifs, connexions, identités, profils et bornes de lecture.
- `mesh` : **47 vérifications** de géométrie, références, profils, bornes et callbacks de décompression.
- `texture` : **56 vérifications** des sources, index de payload, formats, mips, pixels, limites et diagnostics.
- `material` : **81 vérifications** des entrées natives, constantes, profils, identités de graphes, connexions, paramètres d’instances et valeurs opaques.
- `git` : **57 vérifications** de révisions, compagnons, LFS local, durée de vie, chemins, contextes Git et limites ; nécessite Git 2.45+ pour être exécuté.
- `git_project` : **56 vérifications** des contextes historiques, plugins et moteurs sélectionnés par côté.
- `desktop_model` : **28 vérifications** du chargement, contexte, erreurs, reprise et priorité des demandes asynchrones.
- `catalog` : **21 vérifications** des montages, métadonnées, budgets, annulation et union avant/après.
- `desktop_browser` : **25 vérifications** du chargement, sélection, annulation, priorité des demandes et erreurs du catalogue.
- `desktop_git` : **35 vérifications** du chargement de révisions, LFS local/externe, compagnons, ajouts/suppressions, renommage, contexte historique, durée de vie et conservation du document après erreur.
- `desktop_renderer` : fenêtre cachée rendue avec le backend Direct3D 11/WARP réel.
- `ui_smoke` : **30 frames ImGui**, cinq thèmes et langues anglais/français, avec inspecteur, comparaison, graphes Blueprint et matériau, tableau d’instance, image et filaire mesh préparés. Test sans renderer : production des commandes de dessin, sans revue visuelle native.

Les quinze tests passent (625 vérifications, 30 frames UI sans renderer et un test de rendu natif). Les fixtures binaires synthétiques sont générées pendant le test et supprimées après réussite. Aucun asset propriétaire n'est inclus dans les sources.

## Essais sur six fichiers réels locaux

| Fichier | Version enregistrée | Objets | Flux de propriétés décodé | Flux partiel | Objet opaque |
| --- | --- | ---: | ---: | ---: | ---: |
| `TEST.uasset` — Blueprint | 5.5.4 | 24 | 21 | 3 | 0 |
| `MI_Water.uasset` — material instance | 5.3.2 | 4 | 3 | 1 | 0 |
| `T_caustic.uasset` — texture | 5.3.2 | 3 | 1 | 1 | 1 |
| `SM_wall_9.uasset` — static mesh | 5.3.2 | 7 | 4 | 2 | 1 |
| `BP_Volume_Fog.uasset` — Blueprint | 5.3.2 | 104 | 100 | 4 | 0 |
| `MF_Water_Waves.uasset` — material function | 5.3.2 | 10 | 7 | 3 | 0 |
| **Total** | | **152** | **136** | **14** | **2** |

« Flux décodé » ne signifie pas « asset intégralement décodé » : les régions natives restent opaques. Les deux objets entièrement opaques sont des sous-objets de données d'import de texture/mesh, dont le format ajoute une sérialisation spécifique. Les paramètres du matériau comprennent 15 valeurs scalaires, 2 références de textures et 9 valeurs vectorielles décodées.

Les rapports complets de cette session sont dans `build-native/sample-0.json` à `sample-5.json` et `build-native/corpus-validation.json`. Les petits fichiers de ce corpus ne constituent pas un benchmark représentatif de production. Aucun test réel de DataAsset, de map, d'UE4 ou d'UE5 5.6+ n'a été effectué.

## Diff contrôlé d'un Blueprint réel

Une copie dans le dossier de build a reçu une modification binaire ciblée de la valeur entière de `NodePosX`. L'original est resté intact. La CLI a renvoyé exactement un changement :

```json
{
  "op": "replace",
  "path": "/objects/TEST.EventGraph.K2Node_CallFunction_0/properties/NodePosX/value",
  "before": 256,
  "after": 320
}
```

Code de sortie : `1`, statut `changed`. Le résultat est conservé dans `build-native/real-diff-validation.json`. L'empreinte du fichier calculée par le cœur a été comparée avec Python `hashlib.sha256`.

Le même fichier comparé à lui-même renvoie `identical`, code `0`. Une tentative d'utiliser ce fichier d'entrée comme sortie JSON est refusée, code `2`, et son empreinte reste inchangée.

## Réutilisation du SDK

Le cœur a été installé dans `build-native/install`, puis l'exemple `examples/consumer` a été configuré dans un autre build avec `find_package(CyUEDiff CONFIG REQUIRED)`. Cette application cliente, sans CyUI, a lu les 24 exports du Blueprint UE 5.5.

Le contexte du projet local a aussi été ouvert avec son installation source UE 5.5.4 explicite : lecture de `Build.version`, résolution de `/Game/TEST`, préparation des contenus moteur/plugins.

Ces essais établissent le fonctionnement de cette tranche sur les entrées indiquées. Ils ne valident pas une compatibilité universelle des formats Unreal ni le décodage universel des pixels, de la géométrie, de Nanite, d’IoStore ou des ressources virtualisées. La validation des pins ajoutée en 0.2 est décrite ci-dessous.

## Suite Blueprint 0.2

La suite ajoute 30 vérifications Blueprint (94 avec les 64 vérifications du cœur) et inclut la vue `CyUI::Nodes` dans les 30 frames UI. Les graphes réels et essais de pins sont détaillés dans [BLUEPRINTS.md](BLUEPRINTS.md). Les décodeurs de pins de cette tranche sont implémentés et testés sur les deux fichiers indiqués ; le bytecode reste opaque.

## Suite Texture 0.3

La lecture des tableaux d'enums anciens décode désormais `Source.LayerFormat` de la texture réelle. La table du corpus ci-dessus conserve les mesures de la tranche initiale ; les rapports actualisés sont dans les mêmes chemins de build.

La suite Texture apporte 56 vérifications et la vue `CyUI::Images` est exercée dans les 30 frames UI. Le script `scripts/validate_texture.py` vérifie un réglage sans changement de pixels, une modification de source JPEG avec masque, sept refus de CLI et la préservation de l'original. Les aperçus réels JPEG 512 × 512 et PNG 4096 × 4096 et le refus explicite Oodle sont détaillés dans [TEXTURES.md](TEXTURES.md).

La consommation du cœur installé est aussi vérifiée en décodant et écrivant une texture PNG depuis l'exemple client sans CyUI.

## Suite Mesh / codecs 0.4 — 4 novembre 2026

La géométrie source de trois meshes statiques UE 5.3.2 a été lue et exportée : mur (10 962 triangles), rocher (686) et plante (2 600). L'adaptateur Oodle a été construit séparément depuis le SDK 2.9.12 du moteur local. Aucun éditeur n'a été lancé.

Le script `scripts/validate_mesh.py` vérifie une coordonnée source modifiée sur copie, un réglage de lightmap sans changement de géométrie, l'export OBJ, six refus de CLI et les empreintes intactes de l'original. Le contenu BLAKE3 de cette copie de test n'est pas recalculé. Le lecteur n'est pas un outil d'écriture Unreal. Rapports et limites : [MESHES.md](MESHES.md).

La texture Oodle `T_Noise` a aussi produit un aperçu 2048 × 2048 avec cet adaptateur. Le client CMake séparé a consommé le core installé, décodé un mesh et exporté ses 10 962 triangles, sans CyUI ; le module de codec reste fourni explicitement et n'est pas installé avec le SDK.

Les tests UI incluent un filaire, avec trois arêtes projetées vérifiées dans les 30 frames. Ils ne valident pas un backend graphique ni une application desktop autonome.

## Suite Matériaux 0.5 — 4 novembre 2026

La suite ajoute 81 vérifications du décodage des huit types d’entrées, profils FName/FString, masques et constantes, identités GUID, collections, ports de fonctions, reroutages nommés, connexions invalides, ordre des paramètres, associations/indices, surcharges statiques et conservation des valeurs opaques. Les tests UI exercent un graphe de matériau connecté et le tableau d’une instance dans les 30 frames sans renderer.

Six graphes locaux UE 5.3.2 ont été inspectés, de 2 à 58 nœuds, ainsi que les 29 paramètres enregistrés de `MI_Water`. `M_Master` reste partiel à cause de quatre noms de canaux en FText opaques. Les ports de sortie provenant de valeurs de classe omises sont explicitement marqués inférés.

`scripts/validate_material.py` vérifie un recâblage sur copie, une valeur scalaire de `100` à `101`, la cohérence avec `diff.materialChanges`, les connexions non résolues conservées, le refus d’égalité d’un graphe incomplet, un profil futur opaque et trois refus de CLI. Les empreintes des originaux restent intactes. Le core seul est construit sans CyUI ni codec et le client CMake séparé consomme également les projections de matériaux du SDK installé. Détails et limites : [MATERIALS.md](MATERIALS.md).

## Suite Git/LFS 0.6 — 4 novembre 2026

Le module Git séparé est vérifié avec Git for Windows 2.45.1, sans besoin d’exécuter Git LFS. La suite ajoute 56 vérifications : commits et tags, noms Unicode, `.uexp` LFS distinct par révision, compagnon retiré, ajout/suppression/renommage, checkout sale intact, références de remplacement ignorées, dépôt bare, worktree lié, Git SHA-256, stockage relatif/explicite, contenu manquant/corrompu, budgets, timeout et nettoyage des révisions partagées.

Le script `scripts/validate_git.py` place deux versions réelles de `MI_Water` dans un dépôt jetable avec cache LFS externe. Le diff via Git reproduit le diff direct, dont un unique changement spécialisé de paramètre de `100` à `101`. Sept refus de CLI sont vérifiés sous Windows, dont les chemins de métadonnées avec casse différente. Les originaux, le checkout modifié, l’index et la configuration restent intacts ; les dossiers temporaires sont libérés.

Le SDK `CyUEDiff::Git` est aussi installé et consommé via `find_package` dans `examples/git_consumer`, sans CyUI, avec le même résultat réel. Un build distinct avec `CYUEDIFF_BUILD_GIT=OFF`, sans CyUI ni Oodle, conserve les cinq suites du core. Les limites de cette tranche sont décrites dans [GIT.md](GIT.md).

## Contextes historiques 0.7 — 4 novembre 2026

Les 56 vérifications `git_project` couvrent les descripteurs du bon commit, leur stockage LFS, `/Game`, les plugins et contenus moteur, les associations/racines distinctes, la désactivation explicite, le masquage, les doublons, les moteurs locaux copiés et empreintés, les cartes, catalogues incomplets, limites, checkout sale, worktree et dépôt bare. La suite `descriptor` apporte 15 vérifications supplémentaires du dialecte JSON réellement observé dans les plugins moteur.

Le Blueprint réel `TEST.uasset` et des copies du `.uproject` local ont servi à vérifier un changement de contexte sans changement de package. Dix refus de CLI, la préservation des originaux, de l’index et du checkout, et le nettoyage des copies sont contrôlés par `scripts/validate_git_project.py`. Le catalogue de l’installation source UE 5.5.4 locale est également inspecté sans lancer Unreal : 666 documents, 283 racines de contenu, `complete:true`. Ces racines ne constituent pas une évaluation des plugins actifs d’une cible. Le SDK installé est consommé depuis `examples/git_consumer` pour reproduire ce diff contextuel. Le build sans Git/CyUI/Oodle reste vérifié séparément. Détails : [PROJECTS.md](PROJECTS.md).

## Registre de décodeurs 0.8 — 4 novembre 2026

La suite `decoders` apporte 110 vérifications : contrats invalides, intervalles UE4/UE5 et GUID requis, lecture des noms/FString/références/GUID, structures et tableaux dans six versions de fichier, exports intégrés/séparés, consommation exacte, exceptions, limites, résultats JSON invalides, priorité sur un lecteur intégré et refus d’un en-tête futur/licensee. Le flux conserve les propriétés suivantes et le SHA-256 original lors d’un échec. La suite Git/LFS vérifie également que `ReadOptions::decoders` atteint les deux packages d’un diff historique.

Les dix suites passent en Release : 516 vérifications et 30 frames UI sans renderer. Les sept suites du build Core sans Git/CyUI/Oodle passent aussi, soit 403 vérifications. Le registre n’ajoute aucune dépendance à Unreal.

Le SDK installé est consommé depuis `examples/decoder_consumer`, compilé séparément avec `find_package`. Le changement de structure contrôlé `42 → 44`, entre un export intégré et un `.uexp`, produit exactement un remplacement de champ. Le script `scripts/validate_decoders.py` confirme aussi l’égalité sur soi-même, les entrées intactes et la sortie inchangée avec registre vide sur sept assets réels locaux : les six du corpus initial, ainsi que `M_Water`. Le rapport est conservé dans `build-native/decoder-validation.json`. Ces essais n’étendent pas la certification à des schémas natifs réels de plugins ; le schéma personnalisé est synthétique. Contrat et limites : [DECODERS.md](DECODERS.md).

## Application desktop 0.9 — 4 novembre 2026

Les douze suites passent en Release, dont le worker asynchrone (28 vérifications), la fenêtre DX11/WARP et les composants CyUI. Les sept suites du core seul passent également, avec Desktop/Git/UI/Oodle désactivés. La fenêtre native reste une cible optionnelle et n’ajoute pas de dépendance au SDK installé.

`scripts/validate_desktop.py` produit 21 captures PNG depuis le back buffer réel : cinq thèmes et deux langues sur le Blueprint, puis inspecteur, chemin Unicode, états vide/erreur, comparaison, texture source 512 × 512, mesh de 686 triangles et graphe de matériau. Les captures ont été inspectées visuellement. Le diff d’une copie de Blueprint (`NodePosX` de 256 à 320) correspond au résultat de la CLI, et la vue se positionne sur ce premier changement. Sept refus de paramètres/sorties et les empreintes inchangées des entrées sont vérifiés. Rapports conservés dans `build-native/desktop-validation/validation.json` et captures à côté.

Cette validation concerne le renderer WARP et les fichiers indiqués. L’interaction manuelle avec les dialogues Windows, les transitions DPI et le backend matériel ne sont pas certifiés par ces captures. Les aperçus de cette tranche ciblent la première source de chaque côté ; l’annulation en cours de décodage et la navigation Git dans la fenêtre restent à ajouter. Détails : [DESKTOP.md](DESKTOP.md).

## Desktop Git/LFS 0.10 — 4 novembre 2026

`scripts/validate_desktop_git.py` crée un dépôt jetable à partir de copies d’assets explicitement fournis, sans remote. Les 22 captures natives couvrent les cinq thèmes et deux langues, les propriétés et comparaisons d’ajouts/suppressions, l’égalité, un changement de contexte seul, un cache LFS externe, les aperçus texture/mesh et une erreur LFS. Six refus couvrent les options invalides et les sorties vers les métadonnées Git, le cache LFS ou des liens physiques vers les entrées. Le résultat Git correspond à la CLI ; l’index, le checkout et les assets sources restent inchangés. Rapports/captures ignorés dans `build-native/desktop-git-validation/`.

La configuration desktop sans Git passe dix suites, dont le rejet explicite d’une demande Git non prise en charge. Les vues embarquées restent utilisables indépendamment de l’adaptateur Git et de l’hôte Windows.

## Catalogue et navigateur 0.11 — 4 novembre 2026

Le build natif passe quinze suites et 625 vérifications. Le desktop sans Git passe douze suites ; le Core seul passe huit suites, sans UI/Desktop/Git/Oodle. Un consommateur externe `examples/catalog_consumer` a été compilé avec `find_package` contre le SDK Core installé et exécuté sur le projet local. Le SDK n’installe ni les vues CyUI ni Git dans cette configuration.

`scripts/validate_browser.py` produit 21 captures natives et vérifie cinq refus de sorties, dont un catalogue CLI partiel qui tente d’écrire dans les sources. Les essais couvrent contenus projet/plugins/moteur, cinq thèmes et deux langues, recherche avec ou sans résultat, présences Git, sélection locale/Git, erreur de projet/révision/sélection et LFS manquant. Les catalogues correspondent à la CLI et les empreintes des entrées, de l’index et du checkout restent inchangées. Rapports dans `build-native/browser-validation/`, ignoré. La présence de fichiers volontairement vides dans les fixtures montre que l’index n’essaie pas de décoder les packages ; leur validité n’est vérifiée qu’à l’ouverture.


## 0.13 runtime and distribution validation

The Windows desktop passes 17 CTest suites (860 checks), 30 UI frames and native DX11/WARP rendering. Core-only passes ten suites. The desktop-without-Git configuration passes fourteen suites. Cancellation is exercised at every reader checkpoint in a representative package, inside texture/mesh decoding and while a direct Git child is active. Cache tests include same-size, restored-timestamp edits and companion creation/removal.

The CyTrace project supplied locally adds 150 original loose packages (UE 5.3 / serializer 1009), 12,396 exports, 12 rejected truncated headers and unchanged original package/companion hashes. Of those exports, 11,575 expose tagged properties, 818 remain partial and three remain opaque; these counts are coverage observations, not a claim of complete native decoding. The supplied build also contains a version-11 Pak and version-5 IoStore TOCs; their presence alone does not establish container/cooked support. No `.usmap` was found in that project tree.

A desktop CPack archive was extracted and validated with its CLI, native WARP smoke and an external Core consumer compiled and run against the extracted SDK. The archive excludes source assets and the optional locally built Oodle module, while preserving selected third-party notices. CI YAML was parsed locally; hosted Windows/Linux jobs await a user-authorized push.


## 0.14 extended-property and HDR validation

The desktop passes 19 suites with 914 checks. Extended-property fixtures cover legacy/full type tags, map/set order independence and deltas, localized text histories, native transforms/ticks, bounded unknown histories and retained experimental-extension metadata. The original 150-package CyTrace corpus remains unchanged by SHA-256; full tagged-property coverage rises to 12,091 exports, with 302 partial and three opaque exports. Twelve corrupt headers are still rejected.

HDR tests preserve finite FP16/FP32 and RGBE samples before display conversion, including negatives, subnormals and values above one. A source-sample change hidden by identical RGBA8 previews remains detectable. Real RGBA16F/32F sources decode using the explicit local codec. Comparing two real 1024-square HDR sources reports 1,048,576 changed pixels with maximum sample delta 1.0; the native French desktop and CLI reports match exactly. Self-comparison, unsupported HDR use through byte-domain image-diff, exposure bounds and unchanged original hashes are checked.


## 0.15 container validation

The Windows desktop passes 21 CTest suites: 1,011 checks, 30 renderer-free UI frames and native DX11/WARP rendering. The new suites add 65 native container checks and 32 CLI checks. Core without Git/UI/Desktop/Oodle passes 14 suites; desktop without Git passes 18 suites. An external container consumer compiles and runs against the extracted Core SDK.

The original CyTrace UE 5.3 packaged build validates Pak 11 and TOC 5. `scripts/validate_containers.py` reconstructs all 1,523 Pak members, 4,695 project IoStore chunks and one global chunk: 231,432,265 decoded bytes. An independent Python index/block parser produces exactly matching SHA-256 values using the same explicit local codec. All three self-diffs are identical, extracted sample hashes match, 19 truncated-input/output-overlap refusals pass and original Pak/TOC/UCAS SHA-256 fingerprints remain unchanged. Reports stay in `build-native/container-validation-015-independent/` and are excluded from Git. Stored IoStore BLAKE3 hashes and RSA authenticity are not checked; this is decoded-byte container validation, not cooked UObject coverage.


## 0.16 cooked Zen validation

The Windows desktop passes 23 suites with 1,109 checks, including 69 native cooked checks and 29 cooked CLI checks. Core without Git/UI/Desktop/Oodle passes 16 suites; desktop without Git passes 20. The installed SDK is exercised by a separate cooked consumer with explicit profile selection and an original synthetic copy-block package.

`scripts/validate_zen.py` independently parses the supplied UE 5.3 corpus: 3,507 packages, 7,502 exports and 31,616 global script objects. Object names/classes/references, import/bulk metadata and body hashes match Core exactly. Every package uses unversioned properties; their values remain opaque. A one-byte edit on a copy of a real Blueprint produces exactly one `/opaque/sha256` change for its generated-class object. Self-comparison is identical, seven protected-output/truncated-input refusals pass and original TOC/UCAS hashes remain unchanged. Reports/copies stay in `build-native/zen-validation-016/`, excluded from Git. The independent parser shares the explicit local codec implementation; it is not an independent codec validation. UE 5.5 header alignment is tested with fixtures only.


## 0.17 explicit USMAP and property validation

The Windows desktop passes 25 CTest suites with 1,227 checks, 30 English/French UI frames and native DX11/WARP rendering. Core without Git/UI/Desktop/Oodle passes 18 suites; desktop without Git passes 22. The new suites add 96 native schema checks and 22 CLI checks. Coverage includes base USMAP versions 0–4, version metadata, enum layouts, inheritance/static arrays, zero masks, typed values, UTF-16 surrogate pairs, exact property changes, explicit reflected structs, opaque fallback, work/text bounds, cancellation, every truncated mapping prefix and named codec dispatch/output-size checks. Identity-callback compression tests are not a validation of a compression codec.

CLI tests exercise schema fingerprints, distinct before/after bindings, wrong profiles/offsets, duplicate/unknown fields, UTF-8 paths, and refusal to overwrite mapping/context inputs or their hard links. Original synthetic input fingerprints remain unchanged. An extracted 0.17 desktop/SDK archive passes its CLI and WARP smoke, dependency notices and four independently built Core consumers, including a minimal USMAP/property buffer generated independently in Python.

The supplied CyTrace corpus is rechecked in `build-native/zen-validation-017/`: 3,507 packages, 7,502 exports and 31,616 global script objects still match the independent metadata/body parser, with seven refusals and unchanged original hashes. No matching USMAP is available in that project, so this real run validates the existing object/opaque path only. Real mapped property values, matching class offsets, native struct serializers and USMAP extensions remain uncertified. `mappingVerified:false` and unresolved defaults are explicit in successful property reports.


## 0.18 USMAP extension validation

Windows Release passes 26 CTest suites and 1,286 checks, with 19 suites for Core without Git/UI/Desktop/Oodle and 23 for desktop without Git. The extension suite adds 48 checks; schema CLI coverage increases from 22 to 33 checks. Tests cover CEXT layout 0, PPTH/EATR/ENVP version 0, legacy and CEXT MODL, the 255/256 module-index boundary, extension reordering, signed 64-bit enum replacements, high property-flag bits, homonymous types, qualified bases, valid homonymous inheritance, self/cross-type cycles and escaped property diff paths.

Malformed tests exercise duplicate/conflicting/unknown extensions, unknown versions, count/name-index mismatches, invalid tags, unsupported type kinds, bounded extension windows, extra payload bytes and every truncated extension prefix with corrected outer sizes. Cancellation is exercised inside extension traversal. Resolved names, identities and static-array type copies are bounded as well as serialized input.

The extracted 0.18 archive and four standalone SDK consumers are validated. Its schema consumer additionally reads independently generated Python fixtures containing CEXT/PPTH/EATR/ENVP, verifies a qualified property value, and checks lossless attribute metadata through the extracted CLI. Proprietary `.usmap` files are rejected by the distribution validator alongside Unreal assets and local codecs. The real CyTrace build still has no matching schema: these extensions and mapped values are certified on synthetic fixtures only, and native serializers/default reconstruction remain outside this coverage.


## 0.19 unversioned collection validation

Windows Release passes 28 suites and 1,350 checks; Core without Git/UI/Desktop/Oodle passes 21 suites, and desktop without Git passes 25. Two new suites add 46 native collection checks and 18 CLI checks. Fixtures cover map/set removal and addition buckets, canonical key escaping, order changes, duplicate identities, re-additions, reflected struct values/elements, enum elements, nested arrays/maps/sets, resolved object keys, explicitly cleared containers and the UE 5.5 map replacement marker. The marker stays unsupported in the UE 5.3 profile.

Every truncated collection input window is rejected. Limits cover declared counts, recursive values, canonical identity work and text before allocation; cancellation is checked during removals, canonical identity traversal and immediately before returning a completed zero-container read. Unsupported or duplicate bound properties retain their opaque export fingerprint, while limits/cancellation propagate. CLI checks verify precise key/value diff paths, iteration-only binary evidence, catalogue coverage counts, distinct before/after profiles/contexts, UTF-8 manifest paths, protected inputs/hard links and unchanged source hashes.

The extracted 0.19 archive is exercised by four external Core consumers. The schema consumer additionally reads an independently encoded Python `Map<int, Set<int16>>` fixture and verifies both removal and addition buckets through the installed SDK. The new cooked values remain validated on synthetic data only: no matching USMAP is available for the supplied CyTrace build. Neither this validation nor successful schema reads certify runtime key equality, native struct serializers, inherited/default reconstruction or a complete cooked asset.


## 0.20 milestone: cooked references

The Core-only configuration passes 22 suites. A new reference suite checks soft package/asset/subpath separation, numbered FNames, UTF-16 subpaths, exact windows and every truncated soft payload, null/interface indices, four-word GUID order, serialized zeros, limits/cancellation, and member-level Zen diffs for the explicit UE 5.3/5.5 profiles. These tests use original synthetic mappings; no matching USMAP has become available for the CyTrace build. Full desktop and distribution validation is performed after the following milestones.

## 0.21 milestone: property inspection

Windows Release passes all 29 CTest suites. Model checks cover unversioned type unwrapping, retained tag metadata and single-pass JSON pointer decoding. UI checks exercise value search, valid/invalid selection and filtered selection across 30 frames in five themes and both languages. Native Direct3D11/WARP captures of the supplied loose NewDataAsset package were inspected in English/mid-grey and French/light-grey; the property table, selected preview and copy actions fit their panel. The source asset was opened read-only.

## 0.22 milestone: preferences and combined release

Windows Release passes 31 CTest suites with 1,439 reported checks, plus 30 renderer-free UI frames. The Core-only configuration passes 22 suites without Git/UI/Desktop/Oodle; desktop without Git passes 28. Preference coverage adds 39 model checks and 24 native CLI checks: Unicode filenames, all built-in themes, missing-file defaults, restart restoration, explicit overrides, malformed/future/duplicate/oversized/deep input, unrelated-file preservation, atomic replacement and input/report hard-link protection. Hidden smoke tests use isolated preference files and leave the real user profile untouched.

Ten additional native DX11/WARP captures check preferences restored in each of the five themes in English and French, including the filtered empty property state for an identical comparison. Representative captures in all five themes were visually inspected. The two 0.21 captures separately check the populated table and selected preview. Original source asset hashes remain unchanged. Soft/interface/lazy reference support remains validated with synthetic mapped fixtures, without a matching real CyTrace USMAP.

## 0.23 milestone: Git history API and CLI

The desktop configuration passes 32 suites before the GUI history integration, and the new CLI integration adds 20 checks. History tests cover NUL record boundaries, truncation, metadata/record limits, cancellation, duplicate IDs, merge parents, Unicode and punctuation, SHA-1/SHA-256 repositories and pages pinned across a moving branch. The CLI tests hash every file in a disposable repository before and after reads and rejected output requests; the hashes remain identical.

## 0.24 milestone: desktop history and combined release

The combined Windows Release configuration passes 36 suites and 1,603 reported checks, plus 60 renderer-free UI frames (30 asset views, 30 history views). Core without Git/UI/Desktop/Oodle passes 22 suites; desktop without Git passes 28. History adds 108 native adapter checks, 20 CLI checks, 15 worker/model checks and 21 native desktop CLI checks. The history worker is checked for superseded requests, bounded pending work, stale publication rejection, retained pages on failure, null results and cancellation during shutdown.

Native tests select two commits and compare generated packages through the same action as the dialog, verify desktop/CLI change parity, open the revision catalogue when no path is selected, compare identical commits, exercise invalid selections/revisions/options and protect Git-storage outputs. Every source and Git-storage file in the disposable repository is hashed before/after; the hashes remain equal. Ten native DX11/WARP captures cover all five themes in English/French; representative captures in each theme were visually inspected. Filtering and disabled loading/error states also have renderer-free coverage. The existing real cooked-schema coverage limitation is unchanged.

Distribution validation additionally compiles and runs the installed Git SDK history consumer alongside the four Core consumers. These local checks do not certify a hosted CI run.

## 0.25 milestone: desktop container model

Windows Release passes 37 suites. New model checks cover metadata-only index reads without codec loading, decoded SHA-256 and bounded hex prefixes, missing/deleted members, payload limits, cancellation, explicit Zen profiles, global script class resolution, source protection and re-reading a modified source. The existing `DocumentSession` owns these requests, preserving its tested cancellation and stale-result behavior. Core checks verify summary/full metadata parity without constructing entry JSON in the summary.

## 0.26 milestone: native container browsing

Windows Release passes 39 suites and 1,662 checks, with 120 renderer-free UI frames (30 asset, 30 history and 60 container frames). Core without Git/UI/Desktop/Oodle passes 22 suites; desktop without Git passes 31. Container additions include 22 model checks, 35 native CLI checks and two Core summary checks. Coverage includes Unicode product paths, search/selection filtering, explicit profile refusal, Pak bytes, partitioned IoStore reads, Zen/CLI parity, script classes, bounded prefixes, missing/deleted members, invalid options and report aliases to a Pak, a secondary UCAS partition and global-script UCAS. Codec-resolution failures now produce stable English errors instead of localized filesystem bytes that could break JSON reports.

Ten native container captures check all five themes in English and French; representative captures in each theme were inspected. The host-owned CyTrace corpus adds native views of its Pak and 4,695-member IoStore (3,626 named members, 3,507 Zen package chunks). The selected `BP_CyTrace_Demo_DirectionalTraces` package has 12 exports and matches the CLI report exactly with the explicit UE 5.3 profile, global script objects and local Oodle adapter. Source Pak/TOC/UCAS and codec hashes remain unchanged. Reports and captures remain in ignored build directories. This does not add real USMAP or native cooked-payload coverage.

The extracted distribution and its four Core SDK consumers plus Git history consumer are checked again; the container consumer exercises the new summary API through the installed library. No source project assets or codec modules are included in the archive.

## 0.27 milestone: container comparison model

The reusable Core pairing API now supplies the desktop comparison model. Tests cover sorted identities, added/removed members, omitted tombstones, metadata-only unchecked pairs, exact aggregate byte boundaries and refusal before payload I/O. Desktop checks cover selected opaque Zen differences, Core diff parity, independent after profiles, one-sided member inspection, both-side source protection and explicit aggregate limits. Full UI/distribution validation follows the desktop integration milestone.

## 0.28 milestone: desktop container comparisons

Windows Release passes 39 suites with 1,710 reported checks and 280 renderer-free UI frames (30 asset, 30 history, 220 container). Core without Git/UI/Desktop/Oodle passes 22 suites; desktop without Git passes 31. Container coverage now includes 72 Core checks, 36 desktop model checks and 64 native CLI checks. New checks exercise pairing without decoding, exact aggregate limits before payload I/O, cancellation during a full scan, explicit after profiles/global scripts, absent peers, unchecked equality, complete/selected diff parity and after-side partition/global hard-link protection. A peer without a Zen decode never implies removed objects. French translation assertions guard the UTF-8 strings checked in the native captures.

Eighteen native DX11/WARP captures cover ten theme/language combinations, verified changed object fingerprints, paired member states, byte inspection, the comparison inspector, unchecked and identical rows, and one-sided/partial cooked coverage. Representative captures in all five themes and the partial/comparison/member views were visually inspected. The real CyTrace selected Blueprint is compared against itself in both explicit UE 5.3 contexts: 12 exports, identical byte/Zen evidence and unchanged Pak/TOC/UCAS/codec hashes. These checks retain the existing limits: real mapped values still lack a matching USMAP, and native cooked payloads remain opaque.

The installed container SDK consumer additionally exercises the reusable pairing API. The extracted 0.28 archive is validated with four independently built Core SDK consumers and the Git SDK history consumer, as well as its CLI and WARP executable. Validation reports/captures remain under ignored build directories; no source project data or proprietary decoder is distributed. Hosted CI has not been run or published.

## 0.29 milestone: reusable context manifests

Core without Git/UI/Desktop/Oodle passes 23 suites. New native context checks cover manifest-only loading, Unicode relative paths, provenance, explicit profiles, resolved named Zen properties, wrong mappings, missing/invalid/duplicate fields, unsigned offsets, missing schemas, reflected binding duplicates, malformed UTF-8, text/depth/entry/byte bounds, cancellation and unchanged mapping hashes. Existing schema CLI suites pass after replacing their manifest parser with the Core API. Combined desktop and distribution validation follows the next milestones.

## 0.30 milestone: desktop mapped-property requests

Desktop model and native CLI checks exercise metadata-only manifest reads without codec loading, named before/after property values, CLI parity, independent/empty after bindings, schema/profile mismatches, absent mappings, invalid offsets with whole-body fallback and manifest/USMAP hard-link protection on both sides. Combined view and distribution checks follow the dedicated inspection milestone.

## 0.31 milestone: named property and provenance views

Windows Release validation covers 40 suites, 1,790 reported checks and 380 renderer-free UI frames (30 asset, 30 history, 320 container). Core without Git/UI/Desktop/Oodle passes 23 suites, and desktop without Git covers 32. Context tests add 41 native checks; container model/native CLI coverage now reports 49/90 checks. The final changed container views and WARP host are rechecked in both desktop configurations after correcting a stale-table reset and adding French translation assertions.

Typed UI tests verify Health 42-to-99 differences, unbound/opaque regions, one-sided property inspection without false removals, property-table clearing on index requests and provenance selection. Native checks retain exact Core/CLI package/diff parity and protect both context JSON files and both USMAP files through hard-link aliases. Synthetic context/mapping/container source hashes remain unchanged.

Eighteen native DX11/WARP captures cover all ten theme/language combinations plus single/paired/partial properties, opaque failure, schema provenance, object details, empty properties, metadata-only mapping selection and context-only differences. Representative captures in all five themes and the partial/provenance/failure views are visually inspected. These mapped values are synthetic; the supplied real CyTrace corpus still has no matching USMAP. Native cooked payloads and defaults remain outside semantic coverage.

The extracted 0.31 distribution is checked with its CLI/WARP host, four separately built Core consumers and the installed Git history consumer. The independent Python SDK fixture additionally exercises manifest loading, relative mapping resolution and a bound integer through the installed schema consumer. Proprietary mappings/assets/codecs, captures and reports remain outside Git and distribution archives. Hosted CI has not been run or published.
