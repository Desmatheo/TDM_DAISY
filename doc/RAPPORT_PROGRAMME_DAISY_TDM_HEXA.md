# Rapport simple et ludique : comment marche ce programme

Ce projet est un petit “robot audio” qui reçoit du son depuis un Teensy, le traite, puis le renvoie.
L’idée principale est simple :

- le son arrive par un câble audio numérique,
- le programme le regarde échantillon par échantillon,
- il applique un effet d’octave,
- puis il renvoie le résultat.

Tu peux imaginer cela comme un petit parcours de “son qui voyage”.

---

## 1. L’idée générale du programme

Le programme sert à traiter 6 canaux audio (une idée proche de 6 cordes ou 6 pistes) avec un effet appelé EarthEffect.

Le cœur du programme est dans :
- [src/main.cpp](src/main.cpp)
- [src/Utils/audio_processing.h](src/Utils/audio_processing.h)
- [src/EffetEarth/Earth.cpp](src/EffetEarth/Earth.cpp)
- [src/EffetEarth/Earth.h](src/EffetEarth/Earth.h)

En pratique, cela marche ainsi :

1. le matériel reçoit des échantillons audio,
2. une fonction spéciale les traite très souvent,
3. un effet transforme le son,
4. le son modifié est renvoyé.

---

## 2. Le chemin du son : version “mini aventure”

Voici le chemin du son comme si c’était un personnage qui traverse un labyrinthe.

### Étape 1 : le son entre
Le son vient du Teensy, via un protocole appelé TDM.

C’est géré par :
- [src/Utils/daisy_tdm_slave.h](src/Utils/daisy_tdm_slave.h)

Le programme ne “parle” pas directement avec l’audio comme un ordinateur classique. Il utilise la carte Daisy Seed comme une petite machine audio embarquée.

### Étape 2 : le son est récupéré par la callback audio
La vraie machine de traitement s’appelle `AudioCallback`.

Elle est définie dans :
- [src/Utils/audio_processing.h](src/Utils/audio_processing.h)

Cette fonction est appelée très souvent, à chaque bloc d’échantillons audio.

Elle ne fait pas des choses “lentes”. Elle travaille à la fréquence audio, donc très vite.

Exemple concret :

```cpp
static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{
    for(size_t i = 0; i < size; i++)
    {
        for (size_t j = 0; j < 6; j++) 
        {
            float in_sample = in[j][i];
            earth_effects[j]->update(in_ptrs, out_ptrs, 0);
            out[j][i] = out_arr[0][0] * 0.5f;
        }
    }
}
```

Ce bout de code montre la logique :
- on lit le son entrant,
- on le donne à l’effet,
- on récupère le son traité,
- on l’envoie en sortie.

### Étape 3 : chaque canal reçoit son propre effet
Le programme crée 6 instances d’effet, une par canal.

Elles sont stockées dans :
- [src/main.cpp](src/main.cpp)

Le tableau `earth_effects[6]` contient 6 pointeurs vers 6 objets `EarthEffect`.

Donc :
- canal 1 -> effet 1
- canal 2 -> effet 2
- canal 3 -> effet 3
- canal 4 -> effet 4
- canal 5 -> effet 5
- canal 6 -> effet 6

### Étape 4 : l’effet traite le son
Pour chaque canal, le callback appelle :

- `earth_effects[j]->update(...)`

Cette fonction se trouve dans :
- [src/EffetEarth/Earth.cpp](src/EffetEarth/Earth.cpp)

Elle fait plusieurs choses :

1. elle récupère un échantillon d’entrée,
2. elle le stocke dans un petit tampon,
3. elle découpe/traite ce tampon,
4. elle calcule un “effet d’octave”,
5. elle mélange le son original et le son traité,
6. elle donne le résultat en sortie.

### Étape 5 : le son modifié repart
Le résultat n’est pas perdu. Il est envoyé vers la sortie audio.

Le callback remplit ensuite les sorties `out[j][i]`.

Donc le chemin final est :

Entrée audio -> callback -> effet -> sortie audio

---

## 3. Le rôle de la fonction `main`

Dans [src/main.cpp](src/main.cpp), `main` ne fait pas le traitement audio lui-même.

Son rôle est plutôt d’initier le système.

Elle fait ceci :

1. initialise la carte Daisy,
2. configure l’audio TDM,
3. démarre la callback audio,
4. crée les 6 effets,
5. configure leurs paramètres,
6. entre dans une boucle qui affiche des infos de diagnostic.

### Ce que fait `main` concrètement

Voici le code de départ dans [src/main.cpp](src/main.cpp) :

```cpp
int main(void)
{
    hw.Init(true);
    hw.StartAudio(AudioCallback);

    memset(earth_mem, 0, 6 * sizeof(EarthEffect));

    for (int j = 0; j < 6; j++) 
        earth_effects[j] = new(&earth_mem[j * sizeof(EarthEffect)]) EarthEffect((float)DaisyTdmSlave::kSampleRate);
```

- `hw.Init(true);`
  - initialise la carte hardware.

- `hw.StartAudio(AudioCallback);`
  - dit à la carte : “quand tu as du son, appelle cette fonction”.

- `memset(earth_mem, 0, 6 * sizeof(EarthEffect));`
  - nettoie la zone mémoire réservée pour les effets.

- `new(&earth_mem[j * sizeof(EarthEffect)]) EarthEffect(...)`
  - construit chaque objet `EarthEffect` à un endroit précis de la mémoire.

- `setOctaveMode(...)` et `setMix(...)`
  - configurent le comportement de chaque effet.

La boucle `while(1)` ne traite pas le son. Elle sert surtout à surveiller et à afficher des infos.

---

## 4. Comment l’initialisation se passe

L’initialisation est une étape très importante.

### A. Initialisation du matériel
C’est fait dans [src/Utils/daisy_tdm_slave.h](src/Utils/daisy_tdm_slave.h).

La classe `DaisyTdmSlave` configure :

- la carte Daisy Seed,
- le bus audio SAI2,
- le mode TDM,
- les broches de réception et d’émission,
- la fréquence d’échantillonnage.

### B. Initialisation de l’audio
Le programme ne s’appuie pas sur un simple “audio PC”.
Il est conçu pour fonctionner comme un petit appareil embarqué qui reçoit un flux audio d’un maître externe (le Teensy).

### C. Initialisation des effets
Dans [src/main.cpp](src/main.cpp), les 6 effets sont créés un par un.

C’est une étape cruciale : si les effets ne sont pas créés, la callback ne pourrait pas les appeler.

---

## 5. Comment la mémoire est allouée

C’est l’un des points les plus importants.

### La grande idée
Le programme n’utilise pas un `malloc` classique pour créer les effets.

Il utilise une technique appelée “placement new”.

### Ce qui est fait
Dans [src/main.cpp](src/main.cpp), on voit ceci :

- `earth_mem` : un tableau de bytes
- `earth_effects` : un tableau de pointeurs

```cpp
alignas(EarthEffect) static uint8_t earth_mem[6 * sizeof(EarthEffect)];
```

Cela veut dire :

- on réserve une zone de mémoire en statique,
- assez grande pour contenir 6 objets `EarthEffect`,
- puis on place chaque objet à l’intérieur de cette zone.

### Pourquoi faire ça ?
Parce que ce programme tourne sur une carte embarquée, avec peu de ressources.

On veut :

- éviter les allocations dynamiques trop imprévisibles,
- garder la mémoire stable,
- contrôler précisément où les objets vivent.

### Donc, où les objets sont-ils stockés ?
Ils sont stockés dans :
- la zone `earth_mem`,
- qui est une mémoire statique (pas un tas “heap” classique).

### Ce que ça veut dire pour toi
Tu peux penser à ça comme une boîte avec 6 emplacements prédéfinis.

Chaque effet a sa propre petite case.

Pas besoin de demander au système “donne-moi de la mémoire”.
On dit déjà : “il y a 6 cases, mettons les effets là”.

Voici la structure en code :

```cpp
EarthEffect* earth_effects[6] = {nullptr};
alignas(EarthEffect) static uint8_t earth_mem[6 * sizeof(EarthEffect)];
```

- `earth_effects` = tableau de pointeurs, un pour chaque effet,
- `earth_mem` = zone mémoire brute où les objets seront réellement construits.

---

## 6. Comment les fonctions se déclenchent

Voici la logique la plus simple à retenir.

### Ordre de fonctionnement

1. `main()` démarre.
2. `hw.Init(...)` initialise le matériel.
3. `hw.StartAudio(AudioCallback)` active la boucle audio.
4. quand du son est disponible, `AudioCallback` est appelée.
5. `AudioCallback` appelle `earth_effects[j]->update(...)` pour chaque canal.
6. `EarthEffect::update(...)` traite le signal.
7. le résultat est envoyé en sortie.

### Version ultra simple

```text
son entrant
  -> callback audio
  -> effet EarthEffect
  -> son sortant
```

---

## 7. Ce que fait `EarthEffect::update`

Dans [src/EffetEarth/Earth.cpp](src/EffetEarth/Earth.cpp), la fonction `update` est le cœur de l’effet.

Elle fait trois grandes choses :

### A. Elle garde un historique du son
Elle stocke les échantillons dans un tampon `buff`.

C’est un peu comme si l’effet disait :
“je garde un peu du son récent pour le traiter plus tard”.

Exemple :

```cpp
buff[bin_counter] = inputL;
```

Chaque nouveau sample est mis dans un emplacement précis du tampon.

### B. Elle crée un effet d’octave
Le code utilise :

- `decimate2(...)`
- `interpolate(...)`
- `octave.update(...)`

Cela permet de générer une version “octavée” du son.

Exemple :

```cpp
std::span<const float, resample_factor> in_chunk(&(buff[0]), resample_factor);
const auto sample = decimate2(in_chunk); 

octave.update(sample, effect_mode);
```

Ici, le programme prend un petit bloc de son, le traite, puis produit une version modifiée.

En clair :

- le son original reste présent,
- une autre version du son est créée à une octave différente,
- puis elle est mélangée au son original.

### C. Elle mélange dry/wet
Le résultat final est obtenu avec :

```cpp
out[0][idx] = (inputL * dryMix + effect_output * wetMix) * volume;
```

C’est très simple à lire :
- `inputL` = son original,
- `effect_output` = son traité,
- `dryMix` = combien de son original on garde,
- `wetMix` = combien de son traité on entend,
- `volume` = volume final.

Cela veut dire :

- `dryMix` = quantité de son brut,
- `wetMix` = quantité de son traité,
- `volume` = volume global.

Donc si `mix` est faible, on entend surtout le son original.
Si `mix` est fort, on entend surtout l’effet.

---

## 8. Les objets utiles dans l’effet

L’objet `EarthEffect` contient plusieurs briques importantes :

- `buff` et `buff_out` : tampons mémoire temporaires,
- `octave` : l’algorithme qui produit l’octave,
- `decimate2` : réduction de la fréquence d’échantillonnage pour l’effet,
- `interpolate` : remise à l’échelle du signal traité,
- `dryMix`, `wetMix`, `volume` : paramètres de mixage.

C’est un peu comme un mini studio de traitement intégré dans chaque canal.

---

## 9. Ce que contient le programme, mais qui n’est pas utilisé dans le flux actuel

Le dossier contient aussi des classes de réverbération Dattorro.

Elles sont dans :
- [src/EffetEarth/Dattorro/Dattorro.cpp](src/EffetEarth/Dattorro/Dattorro.cpp)
- [src/EffetEarth/Dattorro/Dattorro.hpp](src/EffetEarth/Dattorro/Dattorro.hpp)

Mais dans le chemin actuel du programme, le son ne passe pas par cette réverbération.

Le flux actif est plutôt :

- réception audio
- EarthEffect
- sortie audio

Donc, pour l’instant, le programme principal est centré sur l’effet d’octave.

---

## 10. Mini schéma mental

Voici une version très simple à retenir :

```text
[Teensy / son entrant]
        ↓
 [Daisy TDM slave]
        ↓
 [AudioCallback]
        ↓
 [6 effets EarthEffect]
        ↓
 [mix + octave + sortie]
        ↓
[Teensy / son sortant]
```

---

## 11. Résumé en une phrase

Ce programme reçoit du son, le traite dans une boucle audio très rapide, applique un effet d’octave à 6 canaux, et renvoie le son modifié, avec une mémoire allouée de façon statique et précise pour éviter les surprises.

---

## 12. Si tu veux comprendre encore mieux

Le meilleur ordre de lecture est celui-ci :

1. [src/main.cpp](src/main.cpp)
2. [src/Utils/audio_processing.h](src/Utils/audio_processing.h)
3. [src/EffetEarth/Earth.cpp](src/EffetEarth/Earth.cpp)
4. [src/EffetEarth/Earth.h](src/EffetEarth/Earth.h)
5. [src/Utils/daisy_tdm_slave.h](src/Utils/daisy_tdm_slave.h)

Si tu veux, je peux aussi te faire une version encore plus “ludique” sous forme de :

- histoire en comics,
- carte mentale,
- schéma “le son en train de voyager”,
- ou guide “debutant pas à pas” avec une explication ligne par ligne.
