# Guide d'utilisation — Daisy Seed esclave TDM ↔ Teensy 4.x master

> **Le système en une phrase** : la Teensy 4.x génère les horloges TDM (8 slots × 32 bits @ **48 kHz**) et envoie 6 canaux audio à la Daisy Seed, qui les traite dans son `AudioCallback` et renvoie 8 canaux.
>
> ⚠️ La lib Audio Teensy tourne à 44,1 kHz **par défaut** : pour ce projet elle doit être recompilée à 48 kHz (une ligne, voir §3.0).
>
> Pour le *pourquoi* des choix techniques : [RAPPORT_IMPLEMENTATION.md](RAPPORT_IMPLEMENTATION.md). Pour l'historique des bugs : [RAPPORT_DEBUG.md](RAPPORT_DEBUG.md).

---

## 1. Câblage

| Teensy 4.0/4.1 | Daisy Seed | Signal | Sens |
|---|---|---|---|
| **21** (BCLK1) | **D28** | Bit clock, 12,288 MHz | Teensy → Daisy |
| **20** (LRCLK1) | **D27** | Frame sync, pulse 1 BCLK @ 48 kHz | Teensy → Daisy |
| **7** (OUT1A) | **D25** | Données TDM (6 canaux hexa) | Teensy → Daisy |
| **8** (IN1) | **D26** | Données TDM (8 canaux retour) | Daisy → Teensy |
| **GND** | **GND** | Masse commune | **obligatoire** |
| 23 (MCLK1) | — | *non câblé* (la Daisy esclave n'en a pas besoin) | — |

Règles :

- **Fils courts** (< 10 cm idéalement) : BCLK est à 12,3 MHz.
- Les deux cartes sont en logique **3,3 V** — connexion directe, aucun level-shifter.
- D24 (MCLK de la Daisy) reste libre : en esclave intégral, libDaisy ne muxe pas cette broche.
- Alimentations indépendantes acceptées **tant que les masses sont reliées**.

---

## 2. Format de trame (référence rapide)

| Paramètre | Valeur |
|---|---|
| Slots par trame | 8 × 32 bits = 256 BCLK |
| Fréquence d'échantillonnage | **48 000 Hz exactement** (Teensy recompilée, voir §3.0 — le défaut de la lib est 44 100 Hz) |
| BCLK | 256 × Fs = 12,288 MHz |
| Frame sync | actif **HAUT**, large de **1 BCLK**, asserté **1 bit avant** le slot 0 |
| Ordre des bits | MSB first |
| Fronts | données émises sur front descendant de BCLK, échantillonnées sur front montant |
| Contenu d'un slot | 16 bits hauts = audio (ports **pairs** Teensy), 16 bits bas = port impair (inutilisé chez nous) |

---

## 3. Côté Teensy (master)

### 3.0 Configurer la lib Audio à 48 kHz (obligatoire)

La lib Audio Teensy est compilée à 44,1 kHz par défaut. Le projet tourne à **48 kHz** : il faut redéfinir `AUDIO_SAMPLE_RATE_EXACT` à la compilation. Sur Teensy 4.x, la PLL audio fractionnaire atteint 48 000 Hz **exactement** (MCLK 24,576 MHz, BCLK 12,288 MHz) — tous les objets de la lib (oscillateurs, filtres…) suivent automatiquement.

- **PlatformIO** (recommandé) — dans `platformio.ini` :

  ```ini
  build_flags = -DAUDIO_SAMPLE_RATE_EXACT=48000.0f
  ```

- **Arduino IDE** — pas de mécanisme de define par projet : éditer une fois
  `{Arduino}/hardware/teensy/avr/cores/teensy4/AudioStream.h` et remplacer
  `#define AUDIO_SAMPLE_RATE_EXACT 44100.0f` par `48000.0f`
  (la garde `#ifndef` existante rend la modif réversible et sans risque).

Vérification rapide : le moniteur série de la Daisy doit afficher `callbacks/s: 1500`. S'il affiche ~1378, la Teensy est restée à 44,1 kHz.

À savoir : avec ce define, l'USB-audio de la Teensy (objet `AudioInputUSB`/`AudioOutputUSB`), spécifié à 44,1 kHz, n'est plus conforme — sans impact sur ce projet qui ne l'utilise pas.

### 3.1 Convention de canaux — à lire absolument

`AudioOutputTDM`/`AudioInputTDM` exposent **16 ports de 16 bits**. La paire de ports (2k, 2k+1) remplit le slot 32 bits k : port **pair** = 16 bits **hauts**, port impair = 16 bits bas.

**Convention du projet : on n'utilise que les ports PAIRS** (un canal audio 16 bits par slot 32 bits, aligné MSB) :

| Canal logique | Port Teensy | Slot TDM | Côté Daisy |
|---|---|---|---|
| hexa 1..6 (vers Daisy) | `tdm_out` ports 0, 2, 4, 6, 8, 10 | slots 0..5 | `in[0..5]` |
| retour 1..8 (depuis Daisy) | `tdm_in` ports 0, 2, …, 14 | slots 0..7 | `out[0..7]` |

### 3.2 Sketch d'exemple (banc de test)

6 sinus distincts vers la Daisy + vérification du LA 440 Hz que la Daisy renvoie sur le slot 6 :

```cpp
// PRÉREQUIS : lib Audio recompilée à 48 kHz (AUDIO_SAMPLE_RATE_EXACT=48000.0f, cf. §3.0)
#include <Audio.h>

// La Teensy est MASTER TDM : BCLK pin 21, FS pin 20, data out 7, data in 8.
AudioOutputTDM     tdm_out;       // 16 ports -> 8 slots de 32 bits
AudioInputTDM      tdm_in;
AudioSynthWaveform osc[6];
AudioAnalyzePeak   peak_daisy;    // ce que la Daisy nous renvoie

// 6 canaux hexa vers la Daisy : ports PAIRS 0..10 = slots 0..5
AudioConnection c0(osc[0], 0, tdm_out, 0);
AudioConnection c1(osc[1], 0, tdm_out, 2);
AudioConnection c2(osc[2], 0, tdm_out, 4);
AudioConnection c3(osc[3], 0, tdm_out, 6);
AudioConnection c4(osc[4], 0, tdm_out, 8);
AudioConnection c5(osc[5], 0, tdm_out, 10);

// Retour : out[6] de la Daisy = slot 6 = port pair 12
AudioConnection c6(tdm_in, 12, peak_daisy, 0);

void setup()
{
    AudioMemory(50);   // TDM exige >= 16 blocs ; 50 = confortable
    for(int i = 0; i < 6; i++)
        osc[i].begin(0.5f, 110.0f * (i + 1), WAVEFORM_SINE); // 110..660 Hz
    Serial.begin(115200);
}

void loop()
{
    if(peak_daisy.available())
    {
        // Attendu : ~0.2 (amplitude du test tone 440 Hz généré par la Daisy)
        Serial.print("Retour Daisy slot 6, peak = ");
        Serial.println(peak_daisy.read(), 3);
    }
    delay(500);
}
```

Chaque canal porte une fréquence différente (110/220/330/440/550/660 Hz) : côté Daisy, on identifie immédiatement quel slot transporte quoi.

### 3.3 Contraintes de la lib Teensy

- `AudioMemory(50)` minimum recommandé : `AudioInputTDM` alloue **16 blocs d'un coup** à chaque update ; en dessous de 16, la trame entière est silencieusement jetée.
- **Un seul** objet `AudioOutputTDM` et un seul `AudioInputTDM` par sketch.
- Le TDM occupe le périphérique SAI1 de la Teensy : **incompatible** avec `AudioOutputI2S`/`AudioInputI2S`/SPDIF dans le même sketch.
- Les pins 7/8/20/21 sont imposées par le silicium, non déplaçables.

---

## 4. Côté Daisy (esclave)

### 4.1 Compiler et flasher

```bash
git clone --recursive git@github.com:sgtpepper335/daisy-tdm-hexa.git
cd daisy-tdm-hexa
make -C libDaisy -j8      # une fois (et après toute modif libDaisy)
make -C DaisySP -j8       # une fois
make -j8                  # -> build/daisy_tdm_hexa.bin
```

Flash par USB (DFU) : maintenir **BOOT**, appuyer **RESET**, relâcher BOOT, puis :

```bash
make program-dfu
```

> Après une modification dans `libDaisy/`, le Makefile racine ne re-linke pas tout seul :
> `make -C libDaisy -j8 && rm -f build/daisy_tdm_hexa.elf && make -j8`.

### 4.2 Où écrire le DSP

Tout se passe dans [`src/audio_processing.h`](../src/audio_processing.h), fonction `AudioCallback` :

```cpp
static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{
    // size = 32 échantillons par bloc
    // in[0..5]  : les 6 canaux hexa envoyés par la Teensy (slots 0..5)
    // in[6..7]  : slots inutilisés (silence)
    // out[0..7] : les 8 canaux renvoyés à la Teensy (slots 0..7)
    for(size_t i = 0; i < size; i++)
        for(int ch = 0; ch < 6; ch++)
            out[ch][i] = in[ch][i]; // <- remplacer par le vrai traitement
}
```

Règles d'or dans le callback :

1. **Jamais de `PrintLine`/USB/I2C/blocant** — contexte d'interruption. Écrire dans des variables `volatile`, imprimer depuis `main()`.
2. **Fréquence d'échantillonnage pour les coefficients : `DaisyTdmSlave::kSampleRate` (48 000 Hz)**. Ne pas utiliser `seed.AudioSampleRate()` (valeur nominale, non fiable en mode esclave).
3. Budget temps : ~667 µs par bloc de 32 ; rester largement en dessous.

Le comportement démo livré : passthrough `out[0..5] = in[0..5]`, LA 440 Hz sur `out[6]` (témoin du sens Daisy→Teensy), copie de `in[0]` sur `out[7]`, LED allumée quand du signal entre.

### 4.3 Moniteur série

Micro-USB de la Daisy → moniteur série (115200 ou autre, c'est du CDC). Sortie toutes les secondes :

```
callbacks/s: 1500 (attendu ~1500)
peaks in[0..5]: 0.499 0.498 0.500 0.497 0.499 0.498
```

Le firmware démarre même sans moniteur ouvert (`StartLog(false)`).

---

## 5. Procédure de validation (premier démarrage)

1. **Câbler** selon §1, alimenter les deux cartes, masses communes.
2. **Flasher** la Teensy avec le sketch §3.2 et la Daisy avec ce projet.
3. **Moniteur série Daisy** :
   - `callbacks/s: 1500` → les horloges arrivent, à la bonne cadence. ✔ (~1378 = Teensy restée à 44,1 kHz, cf. §3.0)
   - `peaks in[0..5]` ≈ 0,5 sur les 6 canaux → la réception fonctionne. ✔
   - LED Daisy allumée. ✔
4. **Moniteur série Teensy** : `Retour Daisy slot 6, peak = 0.200` → l'émission Daisy→Teensy fonctionne. ✔
5. **Endurance** : laisser 30 min ; les valeurs doivent rester stables (une seule horloge dans le système ⇒ aucune dérive possible).

---

## 6. Dépannage

| Symptôme | Cause probable | Vérification / remède |
|---|---|---|
| `callbacks/s: 0` | Pas de BCLK/FS : câblage 21→D28 ou 20→D27, masse manquante, sketch Teensy pas lancé | Scope sur D28 (12,29 MHz) et D27 (pulse 48 kHz). Vérifier que le sketch Teensy contient bien un objet TDM actif |
| `callbacks/s` ≈ 1378 | Teensy restée à 44,1 kHz (define oublié) | Appliquer §3.0 et recompiler le sketch |
| `callbacks/s` ≈ 3000 (4 slots configurés) ou ≈ 750 (16 slots) | Mauvais nombre de slots côté Daisy | `kTdmSlots = 8` dans `daisy_tdm_slave.h` |
| Peaks de l'ordre de 1e-5 à 2e-5 au lieu de ~0,5 | Le signal n'occupe que les 16 bits **bas** du slot : branché sur un port **impair** côté Teensy | N'utiliser que les ports pairs (§3.1) |
| Canaux décalés (le 110 Hz arrive sur `in[1]`…) | Décalage d'un slot : polarité/offset FS | Vérifier `tdm_fs_polarity = ACTIVE_HIGH` dans `daisy_tdm_slave.h` ; scope sur FS : pulse haut de ~81 ns |
| Audio périodiquement craquelé | Retour à deux horloges (SAI1/codec interne réactivé ?) ou callback trop lent | Un seul SAI actif ; mesurer le temps du callback |
| `cannot find -ldaisysp` au link | Submodules non clonés/compilés | `git submodule update --init --recursive` puis `make -C DaisySP -j8` |
| Modif libDaisy sans effet | `.elf` pas re-linké | `rm -f build/daisy_tdm_hexa.elf && make -j8` |
| Teensy : retour silencieux mais Daisy OK | Câble 8→D26 ; ou `AudioMemory` < 16 côté Teensy | Vérifier câblage et `AudioMemory(50)` |
| Ça marchait, puis plus rien après reset de la Teensy | La Daisy s'est resynchronisée sur une trame en cours | Reset de la Daisy après tout reset de la Teensy (l'esclave se cale sur le prochain FS, normalement automatique — si besoin, redémarrer les deux) |

---

## 7. FAQ

**Changer la taille de bloc ?** `kBlockSize` dans `daisy_tdm_slave.h`, maximum **64** avec 8 slots (contrainte buffer DMA `taille × 2 × 8 ≤ 1024`).

**Revenir à 44,1 kHz ?** Retirer le define §3.0 côté Teensy et mettre `kSampleRate = 44100.f` côté Daisy. La Daisy suit le master sans autre changement : la structure de trame est identique, seuls BCLK (11,2896 MHz) et la cadence des callbacks (~1378/s) changent.

**Utiliser plus de 6 entrées ?** Les 8 slots arrivent déjà dans `in[0..7]` ; brancher des sources sur les ports pairs 12/14 côté Teensy et lire `in[6..7]`.

**Réutiliser le codec interne de la Daisy en parallèle ?** Non recommandé : il tournerait sur l'horloge STM32 alors que SAI2 suit la Teensy — deux domaines d'horloge non asservis = dérive et glitches. Il faudrait un SRC asynchrone (hors périmètre).

**Du 24 bits vrai plutôt que 16 ?** Le transport fait déjà 32 bits par slot. Il faudrait remplacer les objets TDM 16 bits de la lib Teensy par une variante 32 bits (travail côté Teensy uniquement ; la Daisy lit déjà les slots en 32 bits via `s322f`).

**Brancher l'Eurorack-PMOD (AK4619) à la place ?** Le chemin d'origine du fork reste fonctionnel : `tdm_slots = 4`, `b_sync = MASTER`, `tdm_fs_polarity = ACTIVE_LOW` (défaut), pins du README historique. Voir le repo upstream `heartwerktech/daisy_seed_tdm_eurorack_pmod`.
