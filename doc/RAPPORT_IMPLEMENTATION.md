# Rapport d'implémentation — Refonte « daisy-tdm-hexa »

> Refonte complète du projet : Daisy Seed = **esclave TDM 8 slots sur SAI2**, Teensy 4.x = master, le tout à **48 kHz** (Teensy recompilée avec `AUDIO_SAMPLE_RATE_EXACT=48000.0f`).
> 6 canaux Teensy → Daisy (hexaphonique), 8 canaux Daisy → Teensy.
> Ce document décrit ce qui a changé, fichier par fichier, et pourquoi.
> Les causes racines des bugs corrigés sont détaillées dans [RAPPORT_DEBUG.md](RAPPORT_DEBUG.md) ; l'utilisation au quotidien dans [GUIDE_UTILISATION.md](GUIDE_UTILISATION.md).

---

## 1. Architecture après refonte

```
                        ┌──────────────────────────────┐
   Teensy 4.x (MASTER)  │        Daisy Seed (SLAVE)    │
   ┌────────────┐       │  ┌────────┐    ┌──────────┐  │
   │ AudioOutput│ BCLK──┼─>│ SAI2   │    │ audio.cpp│  │
   │ TDM        │ FS  ──┼─>│ bloc B │DMA>│ Internal │  │
   │ (8 slots × │ DATA──┼─>│ RX/asyn│    │ Callback │->│ AudioCallback(in[8], out[8], 32)
   │  32 bits @ │       │  │ bloc A │<DMA│ float<·> │  │   in[0..5] = 6 canaux hexa
   │  48 kHz)   │<──DATA┼──│ TX/sync│    │ int32    │  │   out[0..7] = 8 canaux retour
   └────────────┘       │  └────────┘    └──────────┘  │
                        └──────────────────────────────┘
```

- **Bloc B de SAI2** : esclave **asynchrone** — il échantillonne BCLK (D28/PA2) et FS (D27/PG9) sur ses broches, et reçoit les données Teensy sur D25/PA0. C'est lui qui possède physiquement les pins d'horloge sur le Seed.
- **Bloc A de SAI2** : esclave **synchrone** — horloges reçues en interne depuis le bloc B, transmet vers la Teensy sur D26/PD11.
- **Un seul périphérique SAI actif** : SAI1/codec interne ne sont plus utilisés (horloge indépendante ⇒ dérive inévitable, cf. §5.3).
- **Le callback audio est cadencé par la Teensy** : DMA circulaire sur SAI2, IRQ à la demi-trame et à la trame complète, soit 48000/32 = **1500 callbacks/s**.

---

## 2. Modifications libDaisy (submodule, fork heartwerker)

### 2.1 `src/per/sai.h` — deux nouveaux champs de `SaiHandle::Config`

```cpp
enum class TdmFsPolarity { ACTIVE_LOW, ACTIVE_HIGH };
enum class ExtClockBlock { BLOCK_A, BLOCK_B };

size_t        tdm_slots       = 0;                          // inchangé
TdmFsPolarity tdm_fs_polarity = TdmFsPolarity::ACTIVE_LOW;  // nouveau
ExtClockBlock ext_clock_block = ExtClockBlock::BLOCK_B;     // nouveau
```

- `tdm_fs_polarity` : polarité du pulse frame-sync en mode TDM. Défaut `ACTIVE_LOW` = comportement historique du fork (AK4619). `ACTIVE_HIGH` = trame i.MX/Teensy.
- `ext_clock_block` : en configuration « deux blocs esclaves », désigne le bloc câblé aux broches FS/SCK externes (celui qui sera mis en `SAI_ASYNCHRONOUS`). Défaut `BLOCK_B`, qui correspond au câblage physique de SAI2 sur Daisy Seed et Patch. Sans effet si l'un des blocs est MASTER.

Les défauts sont rétro-compatibles : une config existante (AK4619 master, I²S stéréo…) se comporte exactement comme avant.

### 2.2 `src/per/sai.cpp` — quatre changements

1. **`Init` — résolution du bug « deux esclaves »** : calcul de `both_slave` ; le bloc désigné par `ext_clock_block` passe en `SAI_ASYNCHRONOUS`, l'autre reste `SAI_SYNCHRONOUS`. C'est la configuration que le RM0433 §51.4.4 décrit comme la seule valide à deux esclaves. (Cause racine n°1 du rapport de debug.)

2. **`InitProtocolTDM` — polarité FS configurable** : `FSPolarity` est piloté par `tdm_fs_polarity` au lieu d'être codé en dur `ACTIVE_LOW`. Le reste de la trame est inchangé et correspond déjà à la Teensy : `SAI_FS_STARTFRAME`, `SAI_FS_BEFOREFIRSTBIT` (équivalent exact du *early frame sync* FSE de l'i.MX), `ActiveFrameLength = 1`, slots 32 bits, `FrameLength = 32 × nbslot`, MSB first, `SLOTACTIVE_ALL`.

3. **`InitProtocolTDM` — `ClockStrobing` documenté et explicite** : conservation du mapping original TX→`FALLINGEDGE` / RX→`RISINGEDGE`, qui produit CKSTR=1 sur les deux blocs (le HAL traduit la constante selon la direction du bloc — piège documenté au §7 du rapport de debug).

4. **`StartDmaTransfer` — ordre d'activation générique** : le bloc synchrone est démarré avant le bloc asynchrone (RM0433 : *« It is recommended to enable the slave device before enabling the master »*), l'asynchrone étant déterminé par MASTER ou, à défaut, par `ext_clock_block`. L'ancien code supposait « un master et un esclave ». `dma_offset` est remis à zéro à chaque démarrage (sinon `GetOffset()` pouvait rapporter la moitié de buffer d'une exécution précédente pendant le premier bloc).

5. **`GetBlockSize`** : tenait pour acquis 2 canaux par trame ; tient maintenant compte de `tdm_slots` (API publique, aucun consommateur interne).

Nettoyage : suppression d'un `uint32_t nbslot;` inutilisé (warning).

**Note opportune** : `InitPins` ignorait déjà la broche MCLK quand aucun bloc n'est master (`is_master == false`) — en esclave intégral, D24 reste donc un GPIO libre, aucun conflit avec le MCLK que la Teensy sort sur sa pin 23 (non câblée).

### 2.3 `src/hid/audio.cpp` — réécriture

L'ancienne implémentation supposait : SAI pilote = stéréo 2 canaux, TDM possible uniquement en second SAI, et 4 slots exactement. La nouvelle est générique.

**Buffers DMA** — un flux RX + un flux TX par périphérique SAI :

```cpp
static const size_t kAudioMaxBufferSize = 1024;            // samples par flux
static const size_t kMaxSaiChannels    = 8;                // slots max par SAI
static int32_t DMA_BUFFER_MEM_SECTION dsy_audio_rx_buffer[2][kAudioMaxBufferSize];
static int32_t DMA_BUFFER_MEM_SECTION dsy_audio_tx_buffer[2][kAudioMaxBufferSize];
```

L'ancien dimensionnement `[kAudioMaxChannels/2][...]` (et son commentaire sur le TDM « débordant » dans la rangée suivante) disparaît : chaque SAI a sa rangée, point. Contrainte par SAI : `blocksize × 2 × canaux ≤ 1024`, soit **blocksize ≤ 64 pour 8 slots**.

**Canaux** — `ChannelsOnSai(sai)` retourne `tdm_slots` si TDM, sinon 2 ; `GetChannels()` fait la somme des deux SAI. Le slot logique `sai1_` (le SAI pilote) supporte donc enfin le TDM, ce qui permet `audio_handle.Init(cfg, sai_tdm)` à un seul SAI — la configuration de ce projet.

**Format « fil »** — `WireDepth(sai)` : un flux TDM transporte toujours des slots 32 bits (réalité du protocole, conversions logicielles ensuite), un flux I²S garde son `bit_depth` configuré.

**`InternalCallback`** — générique :

```cpp
ch1 = ChannelsOnSai(sai1_);  ch2 = ChannelsOnSai(sai2_);
block = size / ch1;                       // size = demi-buffer du SAI pilote
DeinterleaveToFloat(in,  fin, 0,   ch1, block, WireDepth(sai1_), gain);
if(ch2) DeinterleaveToFloat(buff_rx_[1] + sai2_.GetOffset(), fin, ch1, ch2, ...);
cb(fin, fout, block);                     // callback utilisateur
InterleaveFromFloat(out, fout, 0, ch1, ...);  // + symétrique sai2_
```

Mapping utilisateur : `in[0..ch1-1]` = SAI pilote (ordre des slots sur le fil), `in[ch1..]` = second SAI éventuel. Pour ce projet (un seul SAI, 8 slots) : `in[k]` = slot k, directement.

**Scratch float statique au lieu de VLA dans l'IRQ** : l'ancien code allouait `float finbuff[chns*block]` sur la pile d'interruption (VLA). Remplacé par deux tampons statiques de `kAudioMaxBufferSize` floats (4 KB chacun, en BSS), dont la borne tient par construction : la somme des demi-buffers des deux SAI ne peut pas dépasser 1024 échantillons par direction. Déterministe, zéro risque de débordement de pile.

**Gardes ajoutées** : `Start()` retourne `ERR` si le SAI pilote n'est pas initialisé (l'ancien code partait en déréférencement nul — bug réellement rencontré, cf. rapport de debug §6) ; `Init(config, sai)` purge un éventuel `sai2_` résiduel d'une init précédente (cas réel : `DaisySeed::Init` configure l'audio interne avant que l'application ré-initialise) ; `GetOffset()` n'est plus appelé sur un handle non initialisé ; le chemin callback entrelacé est explicitement limité à 2 canaux (comme avant, mais avec un garde propre).

`SetBlockSize` borne désormais dynamiquement selon le SAI le plus gourmand en canaux, et **`Init` applique cette borne au blocksize passé en config** (l'ancien code l'acceptait sans contrôle : avec 8 slots, un blocksize stéréo classique de 128/256 aurait programmé un DMA circulaire au-delà des buffers). Le snapshot de `GetOffset()` est pris une seule fois par callback (lire deux fois exposait à traiter deux moitiés différentes si l'IRQ du second SAI tombait pendant le callback), et le pointeur de callback utilisateur est installé avant l'armement du DMA.

### 2.4 `src/hid/audio.h`

Commentaire de `GetChannels()` mis à jour (l'ancien décrivait le comportement « 2 ou 4 canaux »).

---

## 3. Refonte du code applicatif (`src/`)

| Avant | Après |
|---|---|
| `heartware_pod_prototype.h` (DaisyPod + PMOD + SAI1 commentés, mélange de code mort) | **supprimé** → remplacé par `daisy_tdm_slave.h` |
| `heartware/eurorack-pmod.h` (driver PMOD/AK4619, inutilisé) | **supprimé** (récupérable dans l'historique git et dans le repo upstream) |
| `audio_processing.h` (debug PrintLine dans l'IRQ, globals dans le header) | réécrit |
| `main.cpp` | réécrit |
| `Makefile` : `TARGET = iobay` | `TARGET = daisy_tdm_hexa` |

### 3.1 `src/daisy_tdm_slave.h` — classe `DaisyTdmSlave`

Toute la configuration matérielle au même endroit, avec le câblage documenté en tête de fichier :

```cpp
class DaisyTdmSlave {
    static constexpr size_t kTdmSlots   = 8;
    static constexpr size_t kNumInputs  = 6;    // hexa : slots 0..5
    static constexpr size_t kNumOutputs = 8;
    static constexpr size_t kBlockSize  = 32;
    static constexpr float  kSampleRate = 48000.f; // imposé par la Teensy (recompilée à 48 kHz)
    daisy::DaisySeed seed;
    void Init(bool boost = true);   // seed.Init + InitTdmAudio
    void StartAudio(AudioCallback); // seed.audio_handle.Start
};
```

Points notables :

- `cfg.sr = SAI_48KHZ` est **nominal uniquement** : en esclave, le générateur d'horloge du SAI est éteint (RM0433 §51.4.8 : MCKDIV/NOMCK ignorés) ; la cadence réelle est celle de la Teensy — 48 kHz dans ce projet. **Pour les coefficients DSP, utiliser `DaisyTdmSlave::kSampleRate` (48000), jamais `seed.AudioSampleRate()`** (valeur nominale, non fiable par construction en mode esclave — elle coïncide ici par hasard).
- `pin_config.mclk = {DSY_GPIOX, 0}` : sentinelle « pas de pin » ; non muxée car aucun bloc n'est master.
- `seed.Init()` initialise toujours le codec interne (pas d'option pour l'éviter dans libDaisy) ; `InitTdmAudio()` ré-initialise ensuite `audio_handle` avec le seul SAI2 — la purge de `sai2_` ajoutée dans `audio.cpp` rend cette ré-init propre.

### 3.2 `src/audio_processing.h` — `AudioCallback`

- `struct AudioDiagnostics` (`callback_count`, `in_peak[6]`, tous `volatile`) : le callback écrit, la boucle principale lit/efface. **Aucun print dans le callback.**
- Comportement démo (à remplacer par le vrai DSP) :
  - `out[0..5] = in[0..5]` — passthrough hexaphonique,
  - `out[6]` = **LA 440 Hz** généré localement — valide le sens Daisy→Teensy même sans signal entrant,
  - `out[7] = in[0]` — copie de contrôle,
  - LED allumée si un signal > 0,05 est présent sur une entrée.

### 3.3 `src/main.cpp`

Init, `StartLog(false)` (non bloquant), `StartAudio`, puis boucle de statut à 1 Hz : `callbacks/s` (attendu 1500) et les 6 peaks d'entrée, avec snapshot local des `volatile` avant formatage.

---

## 4. Dimensionnement et empreinte mémoire

| Élément | Valeur | Justification |
|---|---|---|
| Buffers DMA | 4 × 4 KB = **16 KB** en RAM_D2 (SRAM1, non cachée) | 2 SAI × (RX+TX) × 1024 × int32 |
| Scratch float | 2 × 4 KB = 8 KB en BSS | borné par construction (cf. §2.3) |
| Blocksize | 32 (max 64 avec 8 slots) | `32 × 2 × 8 = 512 ≤ 1024` |
| Charge IRQ | 1500 callbacks/s, conversions 2 × 8 × 32 échantillons | négligeable à 480 MHz (M7 + FPU) |
| BCLK | 12,288 MHz (256 × 48 kHz) | PCLK APB ≫ 2 × BCLK requis par RM0433 §51.4.4 — large marge |
| Flash | ≈ 78 KB / 128 KB | build `-O2`, cf. §7 |

---

## 5. Décisions de conception

1. **8 slots partagés RX/TX** : une trame TDM a un seul nombre de slots pour les deux directions. 6 entrées utiles + 8 sorties ⇒ `tdm_slots = 8` ; les slots RX 6-7 arrivent dans `in[6..7]` et sont simplement ignorés par le DSP.
2. **Transport 32 bits** (`bit_depth = SAI_32BIT`) : les slots Teensy font 32 bits dont les 16 bits hauts portent l'audio (ports pairs de la lib Teensy). Lire le slot en int32 et convertir via `s322f` donne directement la bonne échelle ; symétriquement `f2s32` en sortie expose l'audio dans les 16 bits hauts pour la Teensy. Aucun cas particulier nécessaire.
3. **SAI unique plutôt que SAI1+SAI2** : le codec interne tournerait sur l'horloge STM32 pendant que SAI2 tourne sur l'horloge Teensy — deux domaines d'horloge sans asservissement = dérive et glitches garantis à terme. On le désactive ; si un jour il faut les deux, il faudra un SRC asynchrone ou un suivi d'horloge (hors périmètre).
4. **Le TDM va dans le slot logique `sai1_` (SAI pilote)** plutôt que d'inverser le pilotage du callback vers `sai2_` : modification plus locale, et c'est sémantiquement juste — il n'y a qu'un SAI.
5. **Rétro-compatibilité du fork préservée** : défauts `ACTIVE_LOW`/`BLOCK_B`, chemin AK4619 master inchangé, chemin deux-SAI stéréo+TDM inchangé (mais désormais paramétrique en nombre de slots).
6. **48 kHz aux deux bouts** : la lib Audio Teensy (44,1 kHz par défaut) est recompilée avec `AUDIO_SAMPLE_RATE_EXACT=48000.0f` — la PLL audio fractionnaire de l'i.MX RT atteint 48 000 Hz exactement (MCLK 24,576 MHz, BCLK 12,288 MHz), structure de trame inchangée. Côté Daisy, l'esclave suit aveuglément ; seule la constante `kSampleRate` documente la cadence pour le DSP. Procédure côté Teensy : guide §3.0.

---

## 6. Limites connues / hors périmètre

1. **Pas encore validé sur matériel** : la conformité est établie registre par registre contre RM0433 + sources Teensy, mais le banc réel reste à passer (checklist au §10 du rapport de debug).
1bis. **Pas de récupération automatique sur erreur de trame** (AFSDET/LFSDET) : l'IRQ globale SAI2 n'est pas activée dans le NVIC. Un glitch sur les fils ou un reset de la Teensy en cours de stream peut laisser l'esclave décalé d'un slot jusqu'à un reset de la Daisy (l'esclave se recale normalement sur le FS suivant, mais sans garantie après une erreur détectable). Si le besoin apparaît : activer `SAI2_IRQn` + `HAL_SAI_ErrorCallback` avec resynchronisation (stop/start dans l'ordre synchrone-avant-asynchrone), ou surveiller les flags AFSDET/LFSDET depuis la boucle principale.
2. **Callback entrelacé** (`InterleavingAudioCallback`) : toujours limité à 2 canaux — assumé, le callback non entrelacé est la voie normale.
3. **Bit depths hétérogènes entre deux SAI simultanés** : gérés par `WireDepth` par SAI, mais la config `samplerate` reste commune (comportement historique).
4. **`AudioSampleRate()` ment en mode esclave** (rapporte la valeur nominale) : contourné par `kSampleRate`, pas corrigé dans libDaisy (la lib n'a pas d'enum 44,1 kHz).
5. **Dérive multi-horloges** si on réactive SAI1 en parallèle : voir §5.3.

---

## 7. Build et artefacts

```bash
make -C libDaisy -j8     # bibliothèque (à refaire après toute modif libDaisy)
make -C DaisySP -j8      # une fois (le Makefile racine la référence au link)
make -j8                 # application -> build/daisy_tdm_hexa.{elf,hex,bin}
```

Résultat : **FLASH 78 392 B (59,8 %)**, RAM_D2 16 KB (buffers DMA), zéro warning sur les fichiers du projet.

Attention : le Makefile racine ne déclare pas `libdaisy.a` comme dépendance du `.elf` — après une modif libDaisy, forcer le re-link (`rm build/daisy_tdm_hexa.elf && make`).

Le dépôt enregistre désormais **DaisySP comme vrai submodule** (il était déclaré dans `.gitmodules` mais absent de l'index — le clone `--recursive` ne le ramenait pas, d'où un échec de link `cannot find -ldaisysp` sur machine vierge). L'entrée vestigiale `stmlib` (jamais référencée par le build) a été retirée de `.gitmodules`.

---

## 8. Fichiers touchés — vue d'ensemble

```
libDaisy/ (submodule)
  src/per/sai.h        ~  +TdmFsPolarity, +ExtClockBlock
  src/per/sai.cpp      ~  Init (both-slave), InitProtocolTDM (FS pol., CKSTR), StartDmaTransfer (ordre)
  src/hid/audio.h      ~  doc GetChannels
  src/hid/audio.cpp    ~~ réécriture (générique multi-slots)

racine
  Makefile             ~  TARGET = daisy_tdm_hexa
  .gitmodules          ~  DaisySP enregistré, stmlib retiré, URL libDaisy → fork projet
  src/daisy_tdm_slave.h    +  nouvelle classe board
  src/audio_processing.h   ~~ réécrit (diagnostics + démo passthrough)
  src/main.cpp             ~~ réécrit (statut 1 Hz)
  src/heartware_pod_prototype.h    -  supprimé
  src/heartware/eurorack-pmod.h    -  supprimé
  doc/RAPPORT_DEBUG.md             +
  doc/RAPPORT_IMPLEMENTATION.md    +  (ce document)
  doc/GUIDE_UTILISATION.md         +
  doc/SAI_TDM_guide_Paul.md        ~  bandeau « document historique »
  README.md                        ~~ réécrit
```
