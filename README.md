# Solar System

Simulateur du système solaire à l'échelle 1:1, en C++17 / OpenGL 3.3.

Le sujet réel du projet n'est pas le rendu : c'est **la gestion de la
précision numérique aux échelles astronomiques**. Simuler Neptune à
4,5 milliards de kilomètres tout en posant un objet au mètre près sur une
lune est un problème de représentation des nombres avant d'être un problème
de graphismes.

## Lancer

```bash
./StartMac.sh          # macOS
StartWindows.bat       # Windows
```

Ou manuellement :

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/SolarSystem
```

Dépendances récupérées automatiquement par CMake (`FetchContent`) : GLFW 3.4,
GLM 1.0.1, Dear ImGui 1.91. Aucune installation préalable hors CMake et Git.

`-DNATIVE_ARCH=ON` active `-march=native` (binaire non distribuable).

## Le modèle numérique : contextes

La simulation tourne en **float64**, le rendu en **float32**, et la caméra
est toujours à l'origine de l'espace de rendu. C'est l'approche du framework
BRUTAL / Kitten Space Agency :

> *« Everything's contextual, so an object is drawn relative to something
> else. »*

La différence avec un *floating origin* classique est importante. Un floating
origin plat stocke tout en coordonnées absolues et ne soustrait la caméra
qu'au dernier moment. Ici, **rien n'est stocké en absolu** : chaque entité
vit dans un contexte, et connaît sa position relativement à ce contexte.

```
racine (barycentre, inertiel)
  └── Soleil ──┬── Mercure
               ├── Vénus
               ├── Terre ── Lune
               ├── Mars
               ├── Jupiter … Neptune
```

Un `Frame` n'a pas de position propre : son origine **est** la position du
corps qui l'ancre, exprimée dans le frame parent. Aucun état dupliqué, donc
rien à resynchroniser.

### Ce que ça change concrètement

Un `double` porte ~15–16 chiffres significatifs. La résolution atteignable
dépend donc de la magnitude du nombre stocké :

| Position d'un satellite à 1 000 km du centre de Neptune | Résolution float64 |
|---|---|
| stockée en absolu (4,5 × 10⁹ km) | 9,5 × 10⁻⁷ km ≈ **1 mm** |
| stockée dans le contexte « Neptune » | 1,1 × 10⁻¹³ km ≈ **0,1 nm** |

Soit un facteur 8,4 millions. Et pour le float32 envoyé au GPU, la même
position absolue serait fausse de **216 km**.

La précision devient **indépendante de la distance à l'étoile** — c'est
exactement ce qui permet de charger des planètes sans pause et de passer
d'un vaisseau à l'autre sans écran de chargement.

### Conséquences sur la physique

Un contexte ancré à un corps est **non inertiel**. Une position locale `p`
vérifie `P = p + O`, donc :

```
d²p/dt² = a_abs(corps) − a_abs(ancre du contexte)
```

Les repères ne tournant pas (translations pures), il n'y a **ni Coriolis ni
centrifuge** : une simple soustraction, exacte. Les séparations entre corps
passent par `FrameGraph::separation`, qui remonte à l'ancêtre commun — le
vecteur Terre→Lune est lu directement (384 400 km) au lieu d'être obtenu par
`1.496e8 − 1.496e8`.

Le changement de contexte (traversée d'une sphère d'influence) est une
translation exacte : rien ne bouge, rien ne « saute ».

## Architecture

Un header, une responsabilité.

| Fichier | Rôle |
|---|---|
| `Constants.hpp` | constantes physiques (km, kg, s) |
| `Body.hpp` | entité corps céleste (données pures) |
| `Frame.hpp` | **contextes** : arbre, sphères d'influence, conversions |
| `BodyFactory.hpp` | construction du système + de l'arbre de contextes |
| `Physics.hpp` | N-corps Velocity Verlet contextuel, diagnostics |
| `Simulation.hpp` | avancée du temps, trail d'orbite |
| `Camera.hpp` | caméra orbitale contextuelle |
| `Frustum.hpp` | culling en espace caméra-relatif |
| `Shaders.hpp` | GLSL : ray casting analytique + trail |
| `Renderer.hpp` | pipeline GL — **seul endroit où l'on passe en float32** |
| `GL.hpp` | accès direct à OpenGL 3.3, sans dépendance de loader |
| `UI.hpp` | panneau ImGui |

## Rendu

Ray casting analytique sphère/rayon dans un fragment shader plein écran :
pas de maillage, pas de tessellation, donc des sphères parfaites à n'importe
quel niveau de zoom. Éclairage Phong, ombres portées, assombrissement
centre-bord stellaire, couronne, fond étoilé procédural, tone mapping
Reinhard.

Le trail d'orbite est stocké dans le contexte du corps tracé — l'orbite
terrestre se referme donc exactement dans le repère du Soleil. Le ray caster
n'écrivant pas de depth buffer, l'occultation du trail par les corps est
calculée analytiquement dans son propre fragment shader.

## Intégrateur

Velocity Verlet, symplectique : l'énergie ne dérive pas.

Mesuré sur le système à 10 corps, 100 ans simulés :

```
dE/E                     = -1,14e-10
Terre–Soleil             = 1,000241 UA
Terre–Lune               = 378 005 km
déplacement du barycentre= 4,0e-07 km
```

L'UI affiche un garde-fou calculé depuis le temps dynamique réel du système
(`min √(r³/Gm)` sur les paires, soit la Lune ici) et signale quand le pas de
temps devient trop grand pour rester stable.

## Commandes

| | |
|---|---|
| Clic gauche + glisser | orbite |
| Molette | zoom |
| Espace | pause |

Le panneau permet de suivre un corps (la caméra entre alors dans son
contexte, la cible vaut exactement zéro), de forcer un contexte, de basculer
entre échelles réelles et planètes grossies, et de lire la résolution
float64 effective à la position courante.
