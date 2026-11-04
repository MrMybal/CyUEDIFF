# Matériaux — version 0.5

Le core reconstruit les graphes sérialisés de `Material` et `MaterialFunction`, y compris les variantes de fonctions de couches, sans ouvrir Unreal. Les instances exposent leurs paramètres enregistrés et leur référence parent. Aucune compilation de shader, évaluation des fonctions externes, résolution des valeurs héritées ni image du matériau n'est produite.

```powershell
cyuediff material Content/M_Water.uasset --out material.json
cyuediff material-diff before/M_Water.uasset after/M_Water.uasset --out diff.json
cyuediff material-diff before/MI_Water.uasset after/MI_Water.uasset
```

Les mêmes options de contexte projet et moteur que `diff` sont disponibles. `material-diff` renvoie `0` si les projections sérialisées sont identiques, `1` si elles changent, `2` si la projection est incomplète ou si la lecture échoue. Cette égalité porte sur les données projetées ; elle ne garantit pas l'égalité du rendu ou des octets. La commande générale `diff` continue de comparer les objets, blocs opaques et empreintes, avec un champ supplémentaire `materialChanges`.

```cpp
auto before = cyuediff::Package::open("before/M_Water.uasset");
auto after = cyuediff::Package::open("after/M_Water.uasset");
auto model = before.material_snapshot();
auto result = cyuediff::compare_materials(before, after);
std::cout << result.to_json().dump(2);
```

## Graphes

Le schéma `cyuediff.materials/1` contient `graphs` et `instances`. Les graphes sont indexés par chemin de matériau ; les expressions par `MaterialExpressionGuid`. Un identifiant absent ou répété entraîne un repli par chemin, sans supprimer l'objet, avec diagnostic et `complete:false`.

Les collections d'expressions historiques et `EditorOnlyData.ExpressionCollection` fournissent l'appartenance. Les exports appartenant au matériau restent visibles s'ils manquent à la collection. Les commentaires, positions, paramètres et propriétés des expressions sont conservés. Le nœud synthétique `$result` regroupe les entrées terminales du matériau. Une fonction conserve ses véritables expressions `FunctionOutput`.

Les entrées natives prises en charge sont `ExpressionInput`, `MaterialAttributesInput`, `ColorMaterialInput`, `ScalarMaterialInput`, `VectorMaterialInput`, `Vector2MaterialInput`, `ShadingModelMaterialInput` et `SubstrateMaterialInput`. Elles exposent la référence d'expression, l'indice de sortie, le nom et les cinq entiers de masque. Les types dérivés conservent le booléen `useConstant` et leur constante. Le vecteur natif utilise trois floats, même dans un package UE5 avec LWC ; les couleurs conservent leurs canaux nommés.

Les ports d'entrées de fonctions utilisent `ExpressionInputId` quand il existe, afin qu'un réordonnancement de leur cache ne renumérote pas les connexions. Les ports de sortie restent indexés selon la sérialisation Unreal. Si `Outputs` est enregistré, l'indice est vérifié. Sinon, les sorties utilisées sont déduites des connexions : `declared:false`, `outputVerified:false`, `outputDefinitionsComplete:false`. Les définitions omises dans les valeurs de classe par défaut ne sont pas inventées. `complete:true` indique que la projection sérialisée a pu être reconstruite ; les définitions de sortie et les dépendances externes gardent leurs indicateurs distincts.

Les usages de reroutages nommés ajoutent des liens `kind:"named-reroute"` vers leur déclaration. Le type de déclaration et son GUID sont contrôlés. Ces liens expriment une dépendance logique et se distinguent des fils d'entrées ordinaires. Une référence impossible, un indice invalide ou une entrée opaque reste représenté et rend le graphe incomplet.

Le décodeur utilise les versions personnalisées Core (sérialisation native depuis 1, versions connues jusqu'à 4) et Framework (FName depuis 31, versions connues jusqu'à 37). Les versions futures deviennent opaques. En l'absence de Core, un profil UE5 restreint aux versions Main 51–119 et Framework 31–37 est accepté ; aucun profil UE4 manquant n'est deviné. Les profils historiques avec FString sont testés par fixtures, pas certifiés sur un corpus UE4 réel. `ReadOptions::native_materials=false` permet de désactiver ce décodage. Les bornes de propriétés et la consommation exacte restent contrôlées par le lecteur.

## Instances

Les paramètres sont regroupés par type, puis identifiés par le tuple JSON `[association,index,nom]`. Les tableaux scalaires, vecteurs, textures, commutateurs statiques et masques statiques sont projetés ; les tableaux double-vecteur, police, textures virtuelles et volumiques utilisent le même chemin de lecture balisée, avec couverture synthétique ou générique seulement. Le parent reste une référence externe, `parentResolved:false`.

Chaque entrée contient la valeur enregistrée, `valueDecoded`, l'état de surcharge et les métadonnées restantes. Les paramètres de même nom associés à des couches ou indices distincts restent séparés. Les doublons sont conservés et diagnostiqués. Une valeur indisponible ou omise n'est pas remplacée par une valeur supposée héritée ; la projection devient incomplète. Les composantes de masque omises utilisent uniquement leur défaut natif connu `false`. Des métadonnées auxiliaires peuvent rester opaques, par exemple les chemins d'atlas scalaires ; leurs empreintes restent dans `properties` et dans le diff général.

## Interface et validation

`MaterialView::set_package` prépare les graphes et instances hors du rendu. `draw` affiche les nœuds en lecture seule via `CyUI::Nodes`, avec cadrage, navigation, diagnostics et sorties inférées signalées. Les reroutages nommés utilisent une couleur catégorielle différente. Les instances affichent les valeurs et surcharges dans un tableau ; l'identité association/index est disponible au survol. `select_object(path)` permet à l'hôte de choisir un document. `AssetView` ajoute l'onglet Matériaux avec sélection avant/après. Les libellés sont anglais/français.

Corpus local UE 5.3.2 vérifié :

| Asset | Nœuds, résultat inclus | Liens, reroutages inclus | Projection |
| --- | ---: | ---: | --- |
| `MF_Water_Waves` | 7 | 6 | complète, sorties partiellement inférées |
| `M_Water` | 26 | 31 | complète, dont 10 liens logiques de reroutage |
| `M_Rock_Master` | 41 | 51 | complète, sorties partiellement inférées |
| `M_Auto_Landscape` | 45 | 53 | complète, dont 11 reroutages |
| `M_Foliage_vertex_paint` | 2 | 1 | complète |
| `M_Master` | 58 | 58 | incomplète : quatre FText de noms de canaux restent opaques |

`MI_Water` contient 15 paramètres scalaires, 9 vecteurs, 2 textures et 3 commutateurs statiques. Les définitions des fonctions et parents externes ne sont pas chargées.

Le script suivant modifie seulement des copies de test dans le build :

```powershell
python scripts/validate_material.py --asset chemin/M_Water.uasset --instance chemin/MI_Water.uasset
```

Il vérifie un recâblage, la même projection dans `diff.materialChanges`, une connexion non résolue conservée, le refus d'égalité d'un graphe incomplet, un profil futur opaque et exactement une valeur scalaire d'instance modifiée, puis trois refus de CLI. Les SHA-256 des originaux restent identiques. Les éditions binaires de ce script ciblent des packages monolithiques UE5 avec anciens tags ; elles ne constituent pas un outil d'écriture Unreal. Rapports locaux : `build-native/material-validation/`.

Les tests C++ et les 30 frames ImGui couvrent les entrées natives, les limites, les graphes et instances. Ces frames sont produites sans renderer ; aucune revue visuelle native ni certification d'un backend graphique n'est revendiquée.

Références : [FExpressionInput](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FExpressionInput), [UMaterialEditorOnlyData](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UMaterialEditorOnlyData), [FMaterialExpressionCollection](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Materials/FMaterialExpressionCollection?application_version=5.5). Les installations locales ont servi à vérifier les dispositions sérialisées ; leurs sources et assets ne sont pas inclus dans le projet.
