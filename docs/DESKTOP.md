# Application desktop — 0.9

`cyuediff_desktop` est un premier hôte Windows pour les vues CyUI. Il ouvre les packages locaux en lecture seule et permet de choisir deux versions à comparer, sans démarrer Unreal Editor. L’exécutable reste séparé du SDK Core, de la CLI et de la bibliothèque de vues embarquable.

## Construire et lancer

```powershell
python scripts/build.py --desktop --cyui D:/_Project/CyImgui_Interface
build-native/Release/cyuediff_desktop.exe
# Ouvrir un fichier, ou comparer directement deux versions :
build-native/Release/cyuediff_desktop.exe before/BP_Player.uasset after/BP_Player.uasset --locale fr
```

L’option CMake `CYUEDIFF_BUILD_DESKTOP=ON` active les vues UI et construit l’hôte Win32/Direct3D 11. Elle reste désactivée par défaut. Les backends ImGui sont compilés depuis le checkout CyUI fourni, avec le même ImGui que les vues ; ils ne sont pas copiés dans le core. Un hôte qui fournit déjà ses cibles CyUI doit aussi fournir `CYUEDIFF_IMGUI_BACKENDS_DIR` vers ses backends correspondants. Aucun téléchargement n’est effectué.

La fenêtre utilise le GPU disponible, avec repli sur Direct3D WARP. Les captures et tests ci-dessous forcent WARP. Le backend matériel, les dialogues de fichiers et les changements de DPI sont implémentés ; leurs interactions manuelles ne font pas partie de la validation automatisée de cette tranche.

## Utilisation

- `Ctrl+O` choisit le fichier avant ; `Ctrl+Shift+O` choisit le fichier après. Les boutons de la barre et le menu Fichier proposent les mêmes actions.
- Les champs de gauche acceptent aussi un chemin saisi. **Charger** ouvre la demande courante ; `Ctrl+R` la recharge. Inverser les fichiers échange les côtés et les charge.
- Le panneau Projet facultatif accepte un `.uproject` et une racine moteur. Avec un projet, les champs acceptent `/Game/...` ou un nom de package monté. Le même contexte s’applique aux deux côtés.
- Le menu Affichage permet de choisir les cinq thèmes CyUI et les langues anglais/français. L’anglais est la valeur initiale. Les noms d’assets, propriétés, chemins et messages détaillés du lecteur restent des données non traduites.
- L’onglet Propriétés permet de choisir avant/après, de filtrer les objets et de consulter leur JSON, leurs régions opaques et leurs données natives décodées.
- Les onglets Blueprint et Matériaux affichent les graphes enregistrés ; Textures montre l’aperçu source disponible ; Meshes affiche le filaire source navigable.
- Avec deux fichiers, Comparaison ouvre le snapshot côte à côte au premier changement. Les commandes CyUI permettent de passer au suivant, chercher et choisir la présentation unifiée.

Les erreurs de chargement restent visibles. Un fichier après invalide n’efface pas le document précédent. Les diagnostics du lecteur, du contexte et des aperçus se trouvent dans le panneau Diagnostics. Un aperçu indisponible conserve l’inspection des métadonnées.

Un contexte moteur peut aussi être fourni au lancement :

```powershell
build-native/Release/cyuediff_desktop.exe /Game/BP_Player --project D:/Game/Game.uproject --engine-root D:/Unreal/UE_5.5 --locale fr
```

Le format reste déterminé par les versions enregistrées dans le fichier. Le moteur sélectionné fournit des chemins et un contexte ; il n’est pas exécuté.

## Chargement et ressources

`apps/desktop/document.cpp` prépare les packages, le diff et les aperçus sur un worker unique. Une seule demande attend à la fois ; la demande la plus récente remplace celle en attente, et les résultats devenus anciens sont ignorés. La publication des deux côtés est atomique : une erreur conserve le document précédent. Les métadonnées et données publiées sont immuables pour les vues.

Le worker ne touche ni ImGui ni Direct3D. Le thread principal prépare les vues et leurs textes à la publication, puis transfère les images sur le GPU avant la frame suivante. Le JSON de l’inspecteur est préparé une fois par document. Le remplacement et la fermeture retirent les associations d’images avant de libérer leurs ressources. `draw` dans les vues reste sans accès aux fichiers ni décodage.

L’application prépare uniquement la première source Texture2D et le premier MeshDescription, LOD 0, de chaque côté. Les images utilisent une limite de 16 millions de pixels et 128 Mio pour les budgets de payload/décodage ; les sources mesh ont un budget de payload de 128 Mio et les limites d’éléments du core. Désactiver Charger les aperçus conserve une lecture de métadonnées et un diff de package.

Les sources Oodle nécessitent un codec fourni explicitement :

```powershell
build-native/Release/cyuediff_desktop.exe SM_Rock.uasset --codec build-native/engine-codecs/Release/cyuediff_oodle.dll --view meshes
```

Le module est du code hôte de confiance ; le core ne le découvre pas dans le projet ou un asset. Il n’est pas fourni avec le SDK Core. Voir [CODECS.md](CODECS.md).

Cette première fenêtre ne fournit pas encore de navigateur de contenus, de sélection de révisions Git, de profils/décodeurs configurables, de gestion de plusieurs documents ou de préférences persistantes. Les API Core/Git et les vues embarquables restent disponibles séparément. Le rendu des matériaux, des meshes skinnés ou de Nanite n’est pas ajouté : les limites des décodeurs précédents s’appliquent.

Une demande déjà en cours termine sa lecture avant de passer à la suivante. La fermeture attend ce worker ; il n’existe pas encore d’annulation au milieu d’un appel de lecture ou de décodage. La préparation des modèles CyUI et le transfert GPU se font à la publication sur le thread principal et peuvent coûter une frame sur un gros document.

## Validation du renderer

```powershell
build-native/Release/cyuediff_desktop.exe --smoke --before Content/BP_Player.uasset --view blueprint --locale fr --theme navy --capture build-native/preview.png --report build-native/preview.json
```

`--smoke` crée une fenêtre cachée, rend au moins douze frames stabilisées avec Direct3D 11/WARP et se termine. La capture PNG vient du back buffer réel, avec les polices, les images GPU et les commandes ImGui. `--view` accepte `properties`, `blueprint`, `materials`, `textures`, `meshes` et `comparison`. Les options capture/report exigent ce mode ; les sorties qui recouvrent un fichier d’entrée ou compagnon, y compris un lien physique, sont refusées. Le code de sortie est `0` pour une session chargée, même si son diff est modifié, et `2` pour un échec de session ou de capture. Le statut du diff reste dans le rapport JSON.

`desktop_model` vérifie 28 comportements : fichiers Unicode, exports séparés, diff et égalité, contexte projet/moteur, absence de codec si les aperçus sont désactivés, erreurs, conservation du document précédent, reprise, priorité de la dernière demande et exceptions du worker. `desktop_renderer` rend la fenêtre vide avec le backend réel. Les 30 frames `ui_smoke` continuent de vérifier les composants embarquables dans les cinq thèmes et deux langues.

Le script `scripts/validate_desktop.py` utilise des assets explicitement fournis. Dans cette session, il a produit **21 captures natives** : Blueprint dans cinq thèmes et deux langues, inspecteur, fichier Unicode, état vide, erreur, comparaison, texture, mesh et matériau. Il vérifie sept refus, le diff identique à la CLI et les empreintes d’entrée intactes. Les sources locales comprennent le Blueprint `TEST` UE 5.5.4, la texture `T_caustic` 512 × 512, le mesh `SM_rock_9` de 686 triangles et le matériau `M_Water` UE 5.3.2. Le codec Oodle provient du SDK local explicitement choisi. Le fichier après est la copie contrôlée du Blueprint dont `NodePosX` passe de 256 à 320 ; l’original n’est pas modifié.

Les rapports et captures sont dans `build-native/desktop-validation/`, ignoré par les sources. Ils ont été inspectés visuellement dans les cinq thèmes et pour les vues spécialisées. Aucun asset propriétaire ni capture de ces contenus n’est ajouté aux sources redistribuables.
