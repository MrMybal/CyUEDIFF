# Validation initiale â€” 3 novembre 2026

Environnement vÃ©rifiÃ© : Windows x64, MSVC 19.40, CMake 3.31.6, configuration Release. Aucun lancement d'Unreal Editor.

## VÃ©rifications reproductibles

`python scripts/build.py --desktop --cyui <checkout CyUI>` compile le cÅ“ur, la CLI, la bibliothÃ¨que UI et lâ€™application Windows puis lance :

- `core` : **64 vÃ©rifications**, dont six transitions de format, tags anciens/rÃ©cents, tableaux de structures, valeurs entiÃ¨res/boolÃ©ennes/chaÃ®nes, SHA-256, exports sÃ©parÃ©s, changement de propriÃ©tÃ©, changement opaque, rÃ©organisation des noms, rÃ©fÃ©rences cycliques, version future, compte nÃ©gatif, pointeur LFS, troncatures, montages de projet et moteur explicite.
- `decoders` : **110 vÃ©rifications** des profils, entrÃ©es bornÃ©es, structures natives, tableaux, fins dâ€™exports, prioritÃ© explicite, erreurs et JSON de rÃ©sultat.
- `descriptor` : **15 vÃ©rifications** du JSON de descripteurs Unreal, commentaires, virgules finales de tableaux, chaÃ®nes, refus de syntaxe et limites de profondeur.
- `blueprint` : **30 vÃ©rifications** des pins natifs, connexions, identitÃ©s, profils et bornes de lecture.
- `mesh` : **47 vÃ©rifications** de gÃ©omÃ©trie, rÃ©fÃ©rences, profils, bornes et callbacks de dÃ©compression.
- `texture` : **56 vÃ©rifications** des sources, index de payload, formats, mips, pixels, limites et diagnostics.
- `material` : **81 vÃ©rifications** des entrÃ©es natives, constantes, profils, identitÃ©s de graphes, connexions, paramÃ¨tres dâ€™instances et valeurs opaques.
- `git` : **57 vÃ©rifications** de rÃ©visions, compagnons, LFS local, durÃ©e de vie, chemins, contextes Git et limites ; nÃ©cessite Git 2.45+ pour Ãªtre exÃ©cutÃ©.
- `git_project` : **56 vÃ©rifications** des contextes historiques, plugins et moteurs sÃ©lectionnÃ©s par cÃ´tÃ©.
- `desktop_model` : **28 vÃ©rifications** du chargement, contexte, erreurs, reprise et prioritÃ© des demandes asynchrones.
- `catalog` : **21 vÃ©rifications** des montages, mÃ©tadonnÃ©es, budgets, annulation et union avant/aprÃ¨s.
- `desktop_browser` : **25 vÃ©rifications** du chargement, sÃ©lection, annulation, prioritÃ© des demandes et erreurs du catalogue.
- `desktop_git` : **35 vÃ©rifications** du chargement de rÃ©visions, LFS local/externe, compagnons, ajouts/suppressions, renommage, contexte historique, durÃ©e de vie et conservation du document aprÃ¨s erreur.
- `desktop_renderer` : fenÃªtre cachÃ©e rendue avec le backend Direct3D 11/WARP rÃ©el.
- `ui_smoke` : **30 frames ImGui**, cinq thÃ¨mes et langues anglais/franÃ§ais, avec inspecteur, comparaison, graphes Blueprint et matÃ©riau, tableau dâ€™instance, image et filaire mesh prÃ©parÃ©s. Test sans renderer : production des commandes de dessin, sans revue visuelle native.

Les quinze tests passent (625 vÃ©rifications, 30 frames UI sans renderer et un test de rendu natif). Les fixtures binaires synthÃ©tiques sont gÃ©nÃ©rÃ©es pendant le test et supprimÃ©es aprÃ¨s rÃ©ussite. Aucun asset propriÃ©taire n'est inclus dans les sources.

## Essais sur six fichiers rÃ©els locaux

| Fichier | Version enregistrÃ©e | Objets | Flux de propriÃ©tÃ©s dÃ©codÃ© | Flux partiel | Objet opaque |
| --- | --- | ---: | ---: | ---: | ---: |
| `TEST.uasset` â€” Blueprint | 5.5.4 | 24 | 21 | 3 | 0 |
| `MI_Water.uasset` â€” material instance | 5.3.2 | 4 | 3 | 1 | 0 |
| `T_caustic.uasset` â€” texture | 5.3.2 | 3 | 1 | 1 | 1 |
| `SM_wall_9.uasset` â€” static mesh | 5.3.2 | 7 | 4 | 2 | 1 |
| `BP_Volume_Fog.uasset` â€” Blueprint | 5.3.2 | 104 | 100 | 4 | 0 |
| `MF_Water_Waves.uasset` â€” material function | 5.3.2 | 10 | 7 | 3 | 0 |
| **Total** | | **152** | **136** | **14** | **2** |

Â« Flux dÃ©codÃ© Â» ne signifie pas Â« asset intÃ©gralement dÃ©codÃ© Â» : les rÃ©gions natives restent opaques. Les deux objets entiÃ¨rement opaques sont des sous-objets de donnÃ©es d'import de texture/mesh, dont le format ajoute une sÃ©rialisation spÃ©cifique. Les paramÃ¨tres du matÃ©riau comprennent 15 valeurs scalaires, 2 rÃ©fÃ©rences de textures et 9 valeurs vectorielles dÃ©codÃ©es.

Les rapports complets de cette session sont dans `build-native/sample-0.json` Ã  `sample-5.json` et `build-native/corpus-validation.json`. Les petits fichiers de ce corpus ne constituent pas un benchmark reprÃ©sentatif de production. Aucun test rÃ©el de DataAsset, de map, d'UE4 ou d'UE5 5.6+ n'a Ã©tÃ© effectuÃ©.

## Diff contrÃ´lÃ© d'un Blueprint rÃ©el

Une copie dans le dossier de build a reÃ§u une modification binaire ciblÃ©e de la valeur entiÃ¨re de `NodePosX`. L'original est restÃ© intact. La CLI a renvoyÃ© exactement un changement :

```json
{
  "op": "replace",
  "path": "/objects/TEST.EventGraph.K2Node_CallFunction_0/properties/NodePosX/value",
  "before": 256,
  "after": 320
}
```

Code de sortie : `1`, statut `changed`. Le rÃ©sultat est conservÃ© dans `build-native/real-diff-validation.json`. L'empreinte du fichier calculÃ©e par le cÅ“ur a Ã©tÃ© comparÃ©e avec Python `hashlib.sha256`.

Le mÃªme fichier comparÃ© Ã  lui-mÃªme renvoie `identical`, code `0`. Une tentative d'utiliser ce fichier d'entrÃ©e comme sortie JSON est refusÃ©e, code `2`, et son empreinte reste inchangÃ©e.

## RÃ©utilisation du SDK

Le cÅ“ur a Ã©tÃ© installÃ© dans `build-native/install`, puis l'exemple `examples/consumer` a Ã©tÃ© configurÃ© dans un autre build avec `find_package(CyUEDiff CONFIG REQUIRED)`. Cette application cliente, sans CyUI, a lu les 24 exports du Blueprint UE 5.5.

Le contexte du projet local a aussi Ã©tÃ© ouvert avec son installation source UE 5.5.4 explicite : lecture de `Build.version`, rÃ©solution de `/Game/TEST`, prÃ©paration des contenus moteur/plugins.

Ces essais Ã©tablissent le fonctionnement de cette tranche sur les entrÃ©es indiquÃ©es. Ils ne valident pas une compatibilitÃ© universelle des formats Unreal ni le dÃ©codage universel des pixels, de la gÃ©omÃ©trie, de Nanite, dâ€™IoStore ou des ressources virtualisÃ©es. La validation des pins ajoutÃ©e en 0.2 est dÃ©crite ci-dessous.

## Suite Blueprint 0.2

La suite ajoute 30 vÃ©rifications Blueprint (94 avec les 64 vÃ©rifications du cÅ“ur) et inclut la vue `CyUI::Nodes` dans les 30 frames UI. Les graphes rÃ©els et essais de pins sont dÃ©taillÃ©s dans [BLUEPRINTS.md](BLUEPRINTS.md). Les dÃ©codeurs de pins de cette tranche sont implÃ©mentÃ©s et testÃ©s sur les deux fichiers indiquÃ©s ; le bytecode reste opaque.

## Suite Texture 0.3

La lecture des tableaux d'enums anciens dÃ©code dÃ©sormais `Source.LayerFormat` de la texture rÃ©elle. La table du corpus ci-dessus conserve les mesures de la tranche initiale ; les rapports actualisÃ©s sont dans les mÃªmes chemins de build.

La suite Texture apporte 56 vÃ©rifications et la vue `CyUI::Images` est exercÃ©e dans les 30 frames UI. Le script `scripts/validate_texture.py` vÃ©rifie un rÃ©glage sans changement de pixels, une modification de source JPEG avec masque, sept refus de CLI et la prÃ©servation de l'original. Les aperÃ§us rÃ©els JPEG 512 Ã— 512 et PNG 4096 Ã— 4096 et le refus explicite Oodle sont dÃ©taillÃ©s dans [TEXTURES.md](TEXTURES.md).

La consommation du cÅ“ur installÃ© est aussi vÃ©rifiÃ©e en dÃ©codant et Ã©crivant une texture PNG depuis l'exemple client sans CyUI.

## Suite Mesh / codecs 0.4 â€” 4 novembre 2026

La gÃ©omÃ©trie source de trois meshes statiques UE 5.3.2 a Ã©tÃ© lue et exportÃ©e : mur (10 962 triangles), rocher (686) et plante (2 600). L'adaptateur Oodle a Ã©tÃ© construit sÃ©parÃ©ment depuis le SDK 2.9.12 du moteur local. Aucun Ã©diteur n'a Ã©tÃ© lancÃ©.

Le script `scripts/validate_mesh.py` vÃ©rifie une coordonnÃ©e source modifiÃ©e sur copie, un rÃ©glage de lightmap sans changement de gÃ©omÃ©trie, l'export OBJ, six refus de CLI et les empreintes intactes de l'original. Le contenu BLAKE3 de cette copie de test n'est pas recalculÃ©. Le lecteur n'est pas un outil d'Ã©criture Unreal. Rapports et limites : [MESHES.md](MESHES.md).

La texture Oodle `T_Noise` a aussi produit un aperÃ§u 2048 Ã— 2048 avec cet adaptateur. Le client CMake sÃ©parÃ© a consommÃ© le core installÃ©, dÃ©codÃ© un mesh et exportÃ© ses 10 962 triangles, sans CyUI ; le module de codec reste fourni explicitement et n'est pas installÃ© avec le SDK.

Les tests UI incluent un filaire, avec trois arÃªtes projetÃ©es vÃ©rifiÃ©es dans les 30 frames. Ils ne valident pas un backend graphique ni une application desktop autonome.

## Suite MatÃ©riaux 0.5 â€” 4 novembre 2026

La suite ajoute 81 vÃ©rifications du dÃ©codage des huit types dâ€™entrÃ©es, profils FName/FString, masques et constantes, identitÃ©s GUID, collections, ports de fonctions, reroutages nommÃ©s, connexions invalides, ordre des paramÃ¨tres, associations/indices, surcharges statiques et conservation des valeurs opaques. Les tests UI exercent un graphe de matÃ©riau connectÃ© et le tableau dâ€™une instance dans les 30 frames sans renderer.

Six graphes locaux UE 5.3.2 ont Ã©tÃ© inspectÃ©s, de 2 Ã  58 nÅ“uds, ainsi que les 29 paramÃ¨tres enregistrÃ©s de `MI_Water`. `M_Master` reste partiel Ã  cause de quatre noms de canaux en FText opaques. Les ports de sortie provenant de valeurs de classe omises sont explicitement marquÃ©s infÃ©rÃ©s.

`scripts/validate_material.py` vÃ©rifie un recÃ¢blage sur copie, une valeur scalaire de `100` Ã  `101`, la cohÃ©rence avec `diff.materialChanges`, les connexions non rÃ©solues conservÃ©es, le refus dâ€™Ã©galitÃ© dâ€™un graphe incomplet, un profil futur opaque et trois refus de CLI. Les empreintes des originaux restent intactes. Le core seul est construit sans CyUI ni codec et le client CMake sÃ©parÃ© consomme Ã©galement les projections de matÃ©riaux du SDK installÃ©. DÃ©tails et limites : [MATERIALS.md](MATERIALS.md).

## Suite Git/LFS 0.6 â€” 4 novembre 2026

Le module Git sÃ©parÃ© est vÃ©rifiÃ© avec Git for Windows 2.45.1, sans besoin dâ€™exÃ©cuter Git LFS. La suite ajoute 56 vÃ©rifications : commits et tags, noms Unicode, `.uexp` LFS distinct par rÃ©vision, compagnon retirÃ©, ajout/suppression/renommage, checkout sale intact, rÃ©fÃ©rences de remplacement ignorÃ©es, dÃ©pÃ´t bare, worktree liÃ©, Git SHA-256, stockage relatif/explicite, contenu manquant/corrompu, budgets, timeout et nettoyage des rÃ©visions partagÃ©es.

Le script `scripts/validate_git.py` place deux versions rÃ©elles de `MI_Water` dans un dÃ©pÃ´t jetable avec cache LFS externe. Le diff via Git reproduit le diff direct, dont un unique changement spÃ©cialisÃ© de paramÃ¨tre de `100` Ã  `101`. Sept refus de CLI sont vÃ©rifiÃ©s sous Windows, dont les chemins de mÃ©tadonnÃ©es avec casse diffÃ©rente. Les originaux, le checkout modifiÃ©, lâ€™index et la configuration restent intacts ; les dossiers temporaires sont libÃ©rÃ©s.

Le SDK `CyUEDiff::Git` est aussi installÃ© et consommÃ© via `find_package` dans `examples/git_consumer`, sans CyUI, avec le mÃªme rÃ©sultat rÃ©el. Un build distinct avec `CYUEDIFF_BUILD_GIT=OFF`, sans CyUI ni Oodle, conserve les cinq suites du core. Les limites de cette tranche sont dÃ©crites dans [GIT.md](GIT.md).

## Contextes historiques 0.7 â€” 4 novembre 2026

Les 56 vÃ©rifications `git_project` couvrent les descripteurs du bon commit, leur stockage LFS, `/Game`, les plugins et contenus moteur, les associations/racines distinctes, la dÃ©sactivation explicite, le masquage, les doublons, les moteurs locaux copiÃ©s et empreintÃ©s, les cartes, catalogues incomplets, limites, checkout sale, worktree et dÃ©pÃ´t bare. La suite `descriptor` apporte 15 vÃ©rifications supplÃ©mentaires du dialecte JSON rÃ©ellement observÃ© dans les plugins moteur.

Le Blueprint rÃ©el `TEST.uasset` et des copies du `.uproject` local ont servi Ã  vÃ©rifier un changement de contexte sans changement de package. Dix refus de CLI, la prÃ©servation des originaux, de lâ€™index et du checkout, et le nettoyage des copies sont contrÃ´lÃ©s par `scripts/validate_git_project.py`. Le catalogue de lâ€™installation source UE 5.5.4 locale est Ã©galement inspectÃ© sans lancer Unreal : 666 documents, 283 racines de contenu, `complete:true`. Ces racines ne constituent pas une Ã©valuation des plugins actifs dâ€™une cible. Le SDK installÃ© est consommÃ© depuis `examples/git_consumer` pour reproduire ce diff contextuel. Le build sans Git/CyUI/Oodle reste vÃ©rifiÃ© sÃ©parÃ©ment. DÃ©tails : [PROJECTS.md](PROJECTS.md).

## Registre de dÃ©codeurs 0.8 â€” 4 novembre 2026

La suite `decoders` apporte 110 vÃ©rifications : contrats invalides, intervalles UE4/UE5 et GUID requis, lecture des noms/FString/rÃ©fÃ©rences/GUID, structures et tableaux dans six versions de fichier, exports intÃ©grÃ©s/sÃ©parÃ©s, consommation exacte, exceptions, limites, rÃ©sultats JSON invalides, prioritÃ© sur un lecteur intÃ©grÃ© et refus dâ€™un en-tÃªte futur/licensee. Le flux conserve les propriÃ©tÃ©s suivantes et le SHA-256 original lors dâ€™un Ã©chec. La suite Git/LFS vÃ©rifie Ã©galement que `ReadOptions::decoders` atteint les deux packages dâ€™un diff historique.

Les dix suites passent en Release : 516 vÃ©rifications et 30 frames UI sans renderer. Les sept suites du build Core sans Git/CyUI/Oodle passent aussi, soit 403 vÃ©rifications. Le registre nâ€™ajoute aucune dÃ©pendance Ã  Unreal.

Le SDK installÃ© est consommÃ© depuis `examples/decoder_consumer`, compilÃ© sÃ©parÃ©ment avec `find_package`. Le changement de structure contrÃ´lÃ© `42 â†’ 44`, entre un export intÃ©grÃ© et un `.uexp`, produit exactement un remplacement de champ. Le script `scripts/validate_decoders.py` confirme aussi lâ€™Ã©galitÃ© sur soi-mÃªme, les entrÃ©es intactes et la sortie inchangÃ©e avec registre vide sur sept assets rÃ©els locaux : les six du corpus initial, ainsi que `M_Water`. Le rapport est conservÃ© dans `build-native/decoder-validation.json`. Ces essais nâ€™Ã©tendent pas la certification Ã  des schÃ©mas natifs rÃ©els de plugins ; le schÃ©ma personnalisÃ© est synthÃ©tique. Contrat et limites : [DECODERS.md](DECODERS.md).

## Application desktop 0.9 â€” 4 novembre 2026

Les douze suites passent en Release, dont le worker asynchrone (28 vÃ©rifications), la fenÃªtre DX11/WARP et les composants CyUI. Les sept suites du core seul passent Ã©galement, avec Desktop/Git/UI/Oodle dÃ©sactivÃ©s. La fenÃªtre native reste une cible optionnelle et nâ€™ajoute pas de dÃ©pendance au SDK installÃ©.

`scripts/validate_desktop.py` produit 21 captures PNG depuis le back buffer rÃ©el : cinq thÃ¨mes et deux langues sur le Blueprint, puis inspecteur, chemin Unicode, Ã©tats vide/erreur, comparaison, texture source 512 Ã— 512, mesh de 686 triangles et graphe de matÃ©riau. Les captures ont Ã©tÃ© inspectÃ©es visuellement. Le diff dâ€™une copie de Blueprint (`NodePosX` de 256 Ã  320) correspond au rÃ©sultat de la CLI, et la vue se positionne sur ce premier changement. Sept refus de paramÃ¨tres/sorties et les empreintes inchangÃ©es des entrÃ©es sont vÃ©rifiÃ©s. Rapports conservÃ©s dans `build-native/desktop-validation/validation.json` et captures Ã  cÃ´tÃ©.

Cette validation concerne le renderer WARP et les fichiers indiquÃ©s. Lâ€™interaction manuelle avec les dialogues Windows, les transitions DPI et le backend matÃ©riel ne sont pas certifiÃ©s par ces captures. Les aperÃ§us de cette tranche ciblent la premiÃ¨re source de chaque cÃ´tÃ© ; lâ€™annulation en cours de dÃ©codage et la navigation Git dans la fenÃªtre restent Ã  ajouter. DÃ©tails : [DESKTOP.md](DESKTOP.md).

## Desktop Git/LFS 0.10 â€” 4 novembre 2026

`scripts/validate_desktop_git.py` crÃ©e un dÃ©pÃ´t jetable Ã  partir de copies dâ€™assets explicitement fournis, sans remote. Les 22 captures natives couvrent les cinq thÃ¨mes et deux langues, les propriÃ©tÃ©s et comparaisons dâ€™ajouts/suppressions, lâ€™Ã©galitÃ©, un changement de contexte seul, un cache LFS externe, les aperÃ§us texture/mesh et une erreur LFS. Six refus couvrent les options invalides et les sorties vers les mÃ©tadonnÃ©es Git, le cache LFS ou des liens physiques vers les entrÃ©es. Le rÃ©sultat Git correspond Ã  la CLI ; lâ€™index, le checkout et les assets sources restent inchangÃ©s. Rapports/captures ignorÃ©s dans `build-native/desktop-git-validation/`.

La configuration desktop sans Git passe dix suites, dont le rejet explicite dâ€™une demande Git non prise en charge. Les vues embarquÃ©es restent utilisables indÃ©pendamment de lâ€™adaptateur Git et de lâ€™hÃ´te Windows.

## Catalogue et navigateur 0.11 â€” 4 novembre 2026

Le build natif passe quinze suites et 625 vÃ©rifications. Le desktop sans Git passe douze suites ; le Core seul passe huit suites, sans UI/Desktop/Git/Oodle. Un consommateur externe `examples/catalog_consumer` a Ã©tÃ© compilÃ© avec `find_package` contre le SDK Core installÃ© et exÃ©cutÃ© sur le projet local. Le SDK nâ€™installe ni les vues CyUI ni Git dans cette configuration.

`scripts/validate_browser.py` produit 21 captures natives et vÃ©rifie cinq refus de sorties, dont un catalogue CLI partiel qui tente dâ€™Ã©crire dans les sources. Les essais couvrent contenus projet/plugins/moteur, cinq thÃ¨mes et deux langues, recherche avec ou sans rÃ©sultat, prÃ©sences Git, sÃ©lection locale/Git, erreur de projet/rÃ©vision/sÃ©lection et LFS manquant. Les catalogues correspondent Ã  la CLI et les empreintes des entrÃ©es, de lâ€™index et du checkout restent inchangÃ©es. Rapports dans `build-native/browser-validation/`, ignorÃ©. La prÃ©sence de fichiers volontairement vides dans les fixtures montre que lâ€™index nâ€™essaie pas de dÃ©coder les packages ; leur validitÃ© nâ€™est vÃ©rifiÃ©e quâ€™Ã  lâ€™ouverture.


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

The Windows desktop passes 25 CTest suites with 1,227 checks, 30 English/French UI frames and native DX11/WARP rendering. Core without Git/UI/Desktop/Oodle passes 18 suites; desktop without Git passes 22. The new suites add 96 native schema checks and 22 CLI checks. Coverage includes base USMAP versions 0â€“4, version metadata, enum layouts, inheritance/static arrays, zero masks, typed values, UTF-16 surrogate pairs, exact property changes, explicit reflected structs, opaque fallback, work/text bounds, cancellation, every truncated mapping prefix and named codec dispatch/output-size checks. Identity-callback compression tests are not a validation of a compression codec.

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
