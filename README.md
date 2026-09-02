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

## Surfaces procédurales

Le ray casting analytique donne à chaque fragment le point d'impact et la
normale **exacts**. Le bruit est donc évalué directement en espace objet 3D :
pas de coordonnées UV, donc pas de couture ni de distorsion polaire, et un
détail qui ne s'épuise jamais au zoom.

### Quatre catégories

| Catégorie | Traits |
|---|---|
| **Tellurique** | océans profonds/côtiers, continents, biomes par latitude et altitude, chaînes de montagnes, ceintures désertiques, calottes |
| **Désertique** | bassins basaltiques, cratères à bourrelet, réseaux de canyons, dunes anisotropes, plaques d'oxyde, calottes de volatils |
| **Glacée** | linéaments à la Europe, joints de plaques, dépôts organiques, givre |
| **Géante gazeuse** | bandes zonales, turbulence advectée en longitude, tourbillons ovales |

### Un génome par planète

Le CPU n'envoie que **deux nombres** par corps : la catégorie et la graine.
Toute la palette est dérivée sur le GPU. Cela tient dans le budget
d'uniformes (10 composantes par corps au lieu de 24) et surtout, c'est ce
qui donne le comportement voulu : deux mondes désertiques partagent la
famille chromatique de leur teinte, mais pas leur géologie.

Car la graine n'est pas un simple décalage du domaine de bruit — un
décalage donne la *même* planète vue ailleurs. Elle engendre une douzaine
de **paramètres de structure** : échelle des continents, niveau des mers,
force du relief, densité de cratères, ampleur des canyons, couverture de
dunes, étendue des calottes, contraste d'albédo, nombre de bandes… Mercure
sort criblé de cratères, Mars couvert de grands bassins sombres, Vénus
lisse et strié de canyons ténus.

### Détails qui comptent

- **Gradients normalisés.** Tirés composante par composante, ils se
  répartissent dans un cube : les diagonales sont √3 fois plus longues que
  les axes, ce qui aligne les iso-contours du bruit sur la grille et
  produit des terrasses rectangulaires sur les côtes.
- **Détail côtier.** Les extrema de Perlin se placent *sur* la grille, et
  un trait de côte est une iso-contour. Une octave fine ajoutée au champ
  la rend fractale et casse l'alignement résiduel.
- **Bruit cellulaire (Worley).** Indispensable aux cratères : aucune somme
  d'octaves ne produit d'anneaux. Sert aussi aux joints de plaques
  glaciaires, aux tourbillons gazeux et à la granulation solaire.
- **Relief.** La sphère étant analytiquement lisse, on perturbe la
  *normale* d'éclairage d'après le gradient du champ d'altitude.
- **Tone mapping sur la luminance.** Un Reinhard par canal comprime plus
  le canal fort que le canal faible : toute couleur saturée vire au
  pastel. Le rouille de Mars en ressortait beige.
- **Photosphère.** Une étoile émet, elle n'est pas éclairée. Rendue à une
  luminance d'ordre 1, elle ressemble à un caillou beige ; il faut émettre
  très au-dessus de 1 pour que le tone mapping la sature en blanc.

## Optimisation : le cache de surfaces

Évaluer le bruit par fragment coûte ~400 hachages en gros plan. Se poser au
sol, où la planète occupe tout l'écran, serait injouable. Les surfaces sont
donc **cuites une fois dans une texture**, puis simplement lues.

### Six faces de cube, pas une carte équirectangulaire

Une carte lat/lon a deux défauts rédhibitoires : elle écrase toute une ligne
de texels sur chaque pôle, et elle a une couture en longitude. Les six faces
d'un cube ont une densité de texels quasi uniforme et aucune singularité.
Elles tiennent dans **un seul** tableau de textures 2D — donc un seul
échantillonneur pour tous les corps, ce qui compte : GLSL 3.3 ne garantit
que 16 unités de texture.

### Octaves accordées à la résolution

Cuire plus d'octaves que la carte ne peut en résoudre revient à
échantillonner au-dessus de sa fréquence de Nyquist : la texture sort
aliasée. Une face de 2^k texels porte au plus k−2 octaves. Les fréquences
plus fines restent calculées à la volée, mais seulement quand le corps est
gros à l'écran, et sur deux octaves.

| Gros plan planète, 2880 × 1800 | |
|---|---|
| bruit procédural par fragment | 27,4 ms — 36 FPS |
| **cache de surfaces** | **8,4 ms — 118 FPS** |
| vue système | 1,9 ms — 515 FPS |

Les deux chemins donnent la même image (luminance moyenne du disque : 39,4
contre 39,4). `--nocache` permet de comparer, et la case du panneau Debug
bascule à chaud.

Limite connue : de fines coutures restent visibles aux arêtes du cube, le
filtrage linéaire n'ayant pas de texels au-delà du bord d'une face.

## Captures

```bash
./build/SolarSystem --shot img.bmp --body Mars --dist 2.6 --scale reel --size 800 800
./build/SolarSystem --bench 150 --body Terre --dist 2.2      # chronométrage
```

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

## Caméra

Deux modes, **V** bascule de l'un à l'autre. La bascule préserve exactement
la position et la direction du regard — rien ne saute.

### Orbite

Tourne autour d'un corps. Cibler un corps fait **entrer la caméra dans son
contexte**, et la cible devient littéralement `(0,0,0)` : le corps est donc
au centre exact de l'écran, indéfiniment, sans qu'aucune coordonnée ne soit
recalculée frame après frame. Mesuré sur les 10 corps et sur 10 ans
simulés, l'écart au centre reste à 5×10⁻⁸ en coordonnées écran — soit 10⁻⁵
pixel, qui est le plancher imposé par la matrice de rotation en float32.

### Vol libre

Déplacement 6 axes pour se balader dans le système. La **vitesse est
indexée sur l'altitude** au-dessus de la surface la plus proche : on
traverse en ~4 secondes l'espace libre devant soi, quelle que soit
l'échelle. Une vitesse fixe serait inutilisable — soit 300 ans pour aller
de la Terre à Mars, soit une planète traversée en une frame.

| Altitude | Vitesse |
|---|---|
| posé sur une surface | 50 m/s |
| 1 000 km | 250 km/s |
| orbite lunaire | 96 100 km/s |
| 1 UA | 3,7 × 10⁷ km/s |

Les sphères d'influence font basculer le contexte automatiquement pendant
le vol, par translation exacte — d'où l'absence de discontinuité.

## Taille des corps

Un système solaire réel est invisible : la Terre vue de 3 UA couvre 2×10⁻⁵
radian, soit 1/300 de pixel. Il faut donc grossir, mais grossir
*linéairement* casse tout — à ×800 la Terre fait 5,1 × 10⁶ km de rayon,
soit **7 fois le Soleil**. Trois régimes, chacun cohérent avec un usage :

| Mode | Principe |
|---|---|
| **Réel** | 1:1. La vérité physique. À explorer en vol libre. |
| **Cohérent** | Un facteur **unique** pour tous les corps, le plus grand qui ne fasse se toucher aucune paire (×37,5, fixé par le couple Terre–Lune). Les proportions réelles sont exactement préservées : le rapport Soleil/Terre rendu vaut 109,2, comme le vrai. |
| **Schématique** | `r_visuel = K·√r_réel`. La loi de puissance comprime la dynamique sans jamais inverser l'ordre des tailles : le Soleil reste le plus gros, Jupiter devant Mercure. C'est une carte, pas une photo — à fort K la Lune passe sous la surface de la Terre. |

Un facteur *par corps* serait une erreur de conception : le voisin le plus
proche d'Uranus étant à 1,1 × 10⁹ km, il grossirait 22 000× pendant que la
Terre resterait à 15×, et la hiérarchie n'aurait plus aucun sens.

## Commandes

| Touche | Orbite | Vol libre |
|---|---|---|
| **V** | passer en vol libre | passer en orbite (autour du corps le plus proche) |
| Clic gauche + glisser | tourner autour de la cible | regarder autour (convention FPS) |
| Molette | zoom | régler la vitesse |
| WASD | — | avancer / reculer / gauche / droite |
| Espace / C | pause | monter / descendre |
| Maj / Ctrl | — | turbo ×5 / précision ×0,2 |
| P | pause | pause |

Le panneau permet aussi de forcer un contexte, de tracer l'orbite de
n'importe quel corps, et de lire la résolution float64 effective à la
position courante comparée à ce qu'elle serait en coordonnées absolues.
