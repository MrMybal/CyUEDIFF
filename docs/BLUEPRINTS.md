# Graphes Blueprint — version 0.2

Le cœur décode maintenant la partie native des nœuds EdGraph reconnus : pins, types, directions, valeurs par défaut et références de connexion. Il reconstruit les graphes à partir des propriétés de graphe/nœud et de ces pins, sans lancer Unreal.

## API et CLI

```cpp
auto package = cyuediff::Package::open("BP_Player.uasset");
auto graphs = package.blueprint_snapshot();
auto result = cyuediff::compare(before, after);
// result.changes : objets, propriétés et données natives.
// result.graph_changes : représentation de graphe identifiée par GUID.
```

```powershell
build-native/Release/cyuediff.exe graph Content/BP_Player.uasset --out graphs.json
build-native/Release/cyuediff.exe diff before/BP_Player.uasset after/BP_Player.uasset
```

`inspect` inclut une section `blueprint`. Le snapshot de graphe porte le schéma `cyuediff.blueprint/1`, avec une table `graphs`, puis des tables `nodes`, `pins` et `links`. Les GUID de graphe, de nœud et de pin servent d'identité. En leur absence ou en cas de doublon, le chemin est conservé comme repli explicite et le graphe est marqué incomplet.

Exemple de chemin dans `graphChanges` :

```text
/graphs/<GraphGuid>/nodes/<NodeGuid>/pins/<PinId>/defaultValue
```

Les changements de position, de propriétés de nœud, de type de pin, de valeur par défaut et de connexion sont observables. Les tableaux `changes` et `graphChanges` sont deux projections du même fichier : une modification peut donc apparaître dans les deux.

## Données décodées

- Identifiants, noms, noms amicaux et tooltips de pins.
- Direction entrée/sortie, catégorie, sous-catégorie, référence de classe/struct, type de conteneur et type de valeur des maps.
- Références de membres, const/reference/weak/object-wrapper, précision des valeurs flottantes par défaut.
- Valeur par défaut, valeur générée, objet et texte par défaut.
- Pins parents, sous-pins, connexions de passage, GUID persistants et indicateurs de visibilité/orphelin.
- Ordre des pins, préservé pour l'affichage. Les références `linkedTo` gardent également leur ordre dans les données natives du snapshot général.

Les deux références réciproques d'une connexion produisent un seul lien orienté sortie → entrée dans le modèle de graphe. Cette table de liens ne décrit pas un ordre d'exécution ; l'ordre brut des connexions reste accessible dans les données natives.

Chaque lien indique `resolved`, `reciprocal` et `validDirection`. Les liens sans cible restent présents dans le JSON et produisent des diagnostics. Les nœuds exportés absents de la liste `Nodes`, les membres absents et les identifiants dupliqués sont également signalés. `complete` décrit uniquement la reconstruction du graphe éditeur, sans certifier le bytecode ou les ressources externes.

## Profils et repli opaque

La sélection utilise les versions personnalisées enregistrées dans le package, avec des bornes explicites pour les profils de sérialisation vérifiés. Les classes reconnues sont les familles de noms `K2Node_`, `EdGraphNode` et `AnimGraphNode_` avec une propriété `NodeGuid`. Cela ne valide pas toutes les classes tierces dérivées de ces types.

Le texte natif des pins prend en charge les historiques FText vides, invariants et de base localisés. Un historique composé, une version future, une extension inconnue ou un flux tronqué provoque un repli sur l'empreinte opaque du bloc, avec un diagnostic ; les propriétés déjà lues du nœud sont conservées. Les autres données natives éventuellement sérialisées après les pins gardent leur empreinte séparée.

Chaque décodeur natif dispose d'une fenêtre de lecture bornée à son export. Une chaîne ou un tableau corrompu ne peut pas consommer l'export suivant. `ReadOptions::native_graphs = false` permet de conserver la lecture des propriétés en désactivant ce décodeur.

Le modèle n'exécute pas le Blueprint. Le bytecode, la recompilation, les règles de compatibilité des connexions, les pins reconstruits par des classes C++ de plugins et les graphes de matériaux demandent encore du travail.

## Vue CyUI

`BlueprintView` prépare une liste de graphes avec positions et ordre des pins du fichier, puis dessine avec `CyUI::Nodes`. Elle permet le choix du graphe, le cadrage, le pan et le zoom. Elle reste en lecture seule. `AssetView` expose cette vue dans l'onglet « Blueprint graphs », avec un choix avant/après lorsque deux packages sont chargés.

L'hôte fournit toujours sa fenêtre et son backend ImGui. Aucune application desktop autonome n'est ajoutée dans cette version.

## Validation

Les tests synthétiques couvrent le décodage natif, les liens réciproques, leur suppression, les valeurs par défaut, la stabilité des GUID lors d'un renommage, les références absentes, les identifiants dupliqués et les données corrompues. Voir `tests/blueprint_tests.cpp`.

Essais réels :

| Asset | UE | Graphes | Nœuds | Pins | Connexions |
| --- | --- | ---: | ---: | ---: | ---: |
| `TEST.uasset` | 5.5.4 | 4 | 8 | 17 | 2 |
| `BP_Volume_Fog.uasset` | 5.3.2 | 3 | 90 | 262 | 83 |
| **Total** | | **7** | **98** | **279** | **85** |

Sur ces fichiers, tous les liens sont résolus, réciproques et orientés correctement. Cela ne constitue pas une validation de tous les formats UE4/UE5.

Le script suivant vérifie un diff natif de pin sur des copies locales. Il demande un Blueprint monolithique avec une connexion et une valeur numérique par défaut non vide :

```powershell
python scripts/validate_blueprint.py --asset chemin/TEST.uasset
```

Sur le fichier réel UE 5.5, une valeur `DeltaSeconds` passe de `0.0` à `1.0` et produit exactement un `graphChanges`. Un GUID de connexion inconnu est conservé comme lien non résolu. Une version Framework future provoque un repli opaque. L'empreinte du fichier d'origine reste identique.

Les rapports de session sont dans `build-native/blueprint-validation/`. Les tests CyUI dessinent aussi le nouveau graphe en cinq thèmes et deux langues, sans backend graphique : ils vérifient les commandes de dessin et l'absence de modification du package, sans remplacer une revue visuelle native.
