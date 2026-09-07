# Guide SAI / TDM sur Daisy Seed — pour Paul

> ⚠️ **DOCUMENT HISTORIQUE (pré-refonte du 2026-06-12)** — conservé pour comprendre le cheminement, mais remplacé par :
> [GUIDE_UTILISATION.md](GUIDE_UTILISATION.md) (utilisation), [RAPPORT_IMPLEMENTATION.md](RAPPORT_IMPLEMENTATION.md) (architecture actuelle), [RAPPORT_DEBUG.md](RAPPORT_DEBUG.md) (analyse des bugs).
>
> **Errata connus** :
> 1. Ce guide n'aborde jamais la question « quel bloc doit être l'esclave asynchrone » — et le diagnostic donné oralement pendant la session de debug (« les pins FS/SCK sont sur le bloc A ») était faux : ce sont les signaux `SAI2_FS_B` (D27/PG9) et `SAI2_SCK_B` (D28/PA2), donc **bloc B** (vérifié datasheet ST). C'est le bloc B qui doit être l'esclave asynchrone.
> 2. Il ne mentionne pas le bug bloquant n°1 (les deux blocs esclaves finissaient tous deux en `SAI_SYNCHRONOUS` ⇒ aucune horloge captée — voir RAPPORT_DEBUG §2), découvert après sa rédaction.
> 3. §7.6 : le frame sync y est décrit « pulse 1 BCLK, **active LOW** » — faux pour la Teensy, dont le FS est **actif HAUT** (cf. RAPPORT_DEBUG §5).
> 4. La marche à suivre §7 (étapes A-E) est obsolète : tout est implémenté et généralisé dans la refonte. Le projet tourne par ailleurs désormais à **48 kHz** (Teensy recompilée), pas 44,1.

> Note de passation pour Paul (stagiaire DSP).
> Cible : comprendre comment fonctionne l'audio multi-SAI dans ce repo, et savoir mettre la Daisy en **slave TDM sur SAI2** quand le maître d'horloge est un Teensy.
>
> Tous les liens pointent vers les fichiers du repo `daisy_seed_tdm_eurorack_pmod`.

---

## 1. De quoi on parle

Le STM32H7 de la Daisy Seed expose deux périphériques SAI indépendants :

- **SAI1** — utilisé pour le codec audio interne de la Daisy (WM8731, stéréo I²S).
- **SAI2** — utilisé ici pour un codec externe en TDM (AK4619 du module Eurorack-PMOD, 4 slots).

Chaque SAI a deux **blocs** matériels `A` et `B`, qui peuvent indépendamment être TX/RX et master/slave. Un bloc maître génère MCLK/SCK/FS ; un bloc esclave reçoit ces signaux.

Dans le repo, trois fichiers font le boulot :

| Fichier | Rôle |
|---|---|
| [src/heartware_pod_prototype.h](../src/heartware_pod_prototype.h) | Configuration applicative : init de SAI1 (interne) + SAI2 (TDM externe) + I²C + codec PMOD |
| [libDaisy/src/per/sai.cpp](../libDaisy/src/per/sai.cpp) | Couche bas niveau : HAL/DMA STM32, init pins, IRQs |
| [libDaisy/src/hid/audio.cpp](../libDaisy/src/hid/audio.cpp) | Couche haute : fusion des buffers SAI1+SAI2, désentrelacement, conversion int↔float, appel du `AudioCallback` utilisateur |

---

## 2. Configuration actuelle du repo

### SAI1 — codec interne (master)

Voir [heartware_pod_prototype.h:68-93](../src/heartware_pod_prototype.h#L68-L93).

| Champ | Valeur |
|---|---|
| `periph` | `SAI_1` |
| `sr` | 48 kHz |
| `bit_depth` | 24 bits |
| `a_sync` / `b_sync` | MASTER / SLAVE |
| `a_dir` / `b_dir` | TRANSMIT / RECEIVE |
| `tdm_slots` | 0 (mode stéréo I²S classique) |

→ Le STM32 fournit MCLK/SCK/FS au codec interne.

### SAI2 — TDM externe (master)

Voir [heartware_pod_prototype.h:96-114](../src/heartware_pod_prototype.h#L96-L114).

| Champ | Valeur |
|---|---|
| `periph` | `SAI_2` |
| `sr` | 48 kHz |
| `bit_depth` | 24 bits (côté API ; le transport est en 32 bits, voir §6) |
| `a_sync` / `b_sync` | SLAVE / **MASTER** |
| `a_dir` / `b_dir` | RECEIVE / TRANSMIT |
| `tdm_slots` | **4** |

→ Le STM32 est encore maître ici ; il génère MCLK pour l'AK4619. C'est précisément ce qu'on va **inverser** au §7.

---

## 3. Un seul callback pour les deux SAI

Point critique à comprendre. Dans [audio.cpp:174-190](../libDaisy/src/hid/audio.cpp#L174-L190) :

```cpp
AudioHandle::Result AudioHandle::Impl::Start(AudioHandle::AudioCallback callback)
{
    if(sai2_.IsInitialized())
    {
        // SAI2 démarre sans callback : il tourne en DMA circulaire en silence.
        sai2_.StartDma(buff_rx_[1], buff_tx_[1], config_.blocksize, nullptr);
    }
    // SEUL SAI1 déclenche le callback :
    sai1_.StartDma(buff_rx_[0], buff_tx_[0], config_.blocksize, audio_handle.InternalCallback);
    callback_ = (void*)callback;
    return Result::OK;
}
```

Les IRQs DMA sont définies dans [sai.cpp:639-665](../libDaisy/src/per/sai.cpp#L639-L665) :

```cpp
extern "C" void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef* hsai)
{
    if(hsai->Instance == SAI1_Block_A || hsai->Instance == SAI1_Block_B)
        sai_handles[0].InternalCallback(0);
    else if(hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B)
        sai_handles[1].InternalCallback(0);
}
```

Les deux SAI génèrent bien des interruptions half/full DMA, mais **seule celle de SAI1 a un callback enregistré** (`callback_` est `nullptr` côté SAI2). Conséquences :

- **Le `AudioCallback` utilisateur n'est appelé qu'une fois par demi-buffer SAI1.**
- À ce moment-là, on **lit/écrit un snapshot** du buffer DMA circulaire de SAI2, en utilisant `audio_handle.sai2_.GetOffset()` (qui vaut `0` ou `block_size/2`).
- Tant que SAI1 et SAI2 partagent la même horloge source (cas actuel : Daisy = master des deux), les half/full des deux DMA sont alignés → pas de glissement.
- Si SAI2 est piloté par une **horloge externe** (cas Teensy), les deux DMA dérivent → glitches. Voir §7 étape D.

---

## 4. Où sont les échantillons SAI1 vs SAI2 ?

### 4.1 Buffers DMA bruts

[audio.cpp:13-23](../libDaisy/src/hid/audio.cpp#L13-L23) :

```cpp
static const size_t kAudioMaxBufferSize = 1024;
static const size_t kAudioMaxChannels   = 6;

static int32_t DMA_BUFFER_MEM_SECTION
    dsy_audio_rx_buffer[kAudioMaxChannels / 2][kAudioMaxBufferSize]; // [3][1024]
static int32_t DMA_BUFFER_MEM_SECTION
    dsy_audio_tx_buffer[kAudioMaxChannels / 2][kAudioMaxBufferSize];
```

| Indice | Contenu |
|---|---|
| `buff_rx_[0]` / `buff_tx_[0]` | **SAI1** — 2 canaux entrelacés L/R |
| `buff_rx_[1]` / `buff_tx_[1]` | **SAI2** — N slots TDM entrelacés (`N = tdm_slots`) |
| `[2]` | Pas utilisé directement ; les slots TDM 3-4 « débordent » dans cette zone mémoire — c'est la raison du facteur `/2` dans le dimensionnement |

### 4.2 Vue côté `AudioCallback` utilisateur

Avec la config actuelle du repo (SAI1 stéréo + SAI2 4 slots TDM, soit 6 canaux totaux), [audio_processing.h:13-35](../src/audio_processing.h#L13-L35) :

```cpp
static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
```

| `in[ch]` / `out[ch]` | Source / destination physique |
|---|---|
| `in[0]`, `in[1]` | SAI1 — canal gauche / droit du codec interne |
| `in[2]`, `in[3]`, `in[4]`, `in[5]` | SAI2 — slots TDM 0, 1, 2, 3 du codec externe |

**Comment faire la différence ?** Par l'indice de canal, tout simplement. La règle gravée dans `InternalCallback` est :

> `ch 0..1` = SAI1, `ch 2..(2 + tdm_slots - 1)` = SAI2

### 4.3 Le désentrelacement dans `InternalCallback`

[audio.cpp:357-447](../libDaisy/src/hid/audio.cpp#L357-L447), branche `bit_depth = 24` (la nôtre) :

```cpp
size_t offset    = audio_handle.sai2_.GetOffset();
int32_t *in2     = audio_handle.buff_rx_[1] + offset;  // pointeur vers la moitié "fraîche" du buffer SAI2

for(size_t i = 0; i < block_size; i++)
{
    // SAI1 — données passées directement par l'IRQ
    fin[0][i] = s242f(in[i * 2 + 0]) * gain;
    fin[1][i] = s242f(in[i * 2 + 1]) * gain;

    if(chns > 4) // TDM (4 slots ici → chns == 6)
    {
        fin[2][i] = s322f(in2[i * 4 + 0]) * gain;
        fin[3][i] = s322f(in2[i * 4 + 1]) * gain;
        fin[4][i] = s322f(in2[i * 4 + 2]) * gain;
        fin[5][i] = s322f(in2[i * 4 + 3]) * gain;
    }
}
```

Trois choses à noter :

1. **SAI1** est lu via le pointeur `in` que l'IRQ HAL passe en paramètre.
2. **SAI2** est lu en allant chercher manuellement dans `buff_rx_[1] + offset`, avec `offset` qui vaut 0 ou la moitié du buffer selon qu'on est dans le half ou le full DMA.
3. Les samples TDM sont convertis avec `s322f` (32 bits), **même si `bit_depth = 24`** — parce que le protocole TDM custom (voir §6) transporte toujours en 32 bits.

---

## 5. Limites en dur de l'implémentation actuelle

À garder en tête avant toute extension :

| Limite | Localisation | Conséquence |
|---|---|---|
| `kAudioMaxChannels = 6` | [audio.cpp:14](../libDaisy/src/hid/audio.cpp#L14) | Plafonne SAI2 à 4 slots (6 - 2 SAI1) |
| `kAudioMaxBufferSize = 1024` | [audio.cpp:13](../libDaisy/src/hid/audio.cpp#L13) | Limite `block_size × 2 × tdm_slots ≤ 1024` |
| Désentrelacement hardcodé à 4 slots | [audio.cpp:387-394, 410-413, 430-433](../libDaisy/src/hid/audio.cpp#L387-L394) (et symétrique côté écriture) | Pas de support 6/8 slots out-of-the-box |
| Callback piloté par SAI1 | [audio.cpp:183-186](../libDaisy/src/hid/audio.cpp#L183-L186) | Si SAI1 et SAI2 ont des horloges sources différentes → glissement |
| TDM RX et TX partagent le même nombre de slots | nature du protocole + [sai.cpp:223-236](../libDaisy/src/per/sai.cpp#L223-L236) | On ne peut pas avoir 6 slots RX et 8 slots TX en parallèle |

---

## 6. Protocole TDM custom

Le `InitProtocolTDM` du fork ([sai.cpp:258-328](../libDaisy/src/per/sai.cpp#L258-L328)) impose un protocole TDM précis :

| Paramètre | Valeur |
|---|---|
| `Init.Protocol` | `SAI_FREE_PROTOCOL` |
| `Init.FirstBit` | MSB |
| `Init.DataSize` | **32 bits** (toujours, même si `bit_depth = 24`) |
| `FrameInit.FSDefinition` | `SAI_FS_STARTFRAME` |
| `FrameInit.FSPolarity` | **ACTIVE_LOW** |
| `FrameInit.FSOffset` | `BEFOREFIRSTBIT` |
| `FrameInit.ActiveFrameLength` | 1 BCLK (pulse court, style TDM) |
| `FrameInit.FrameLength` | `32 × nbslot` |
| `SlotInit.SlotSize` | 32 bits |
| `SlotInit.SlotActive` | `SAI_SLOTACTIVE_ALL` |
| `ClockStrobing` | TX = falling edge, RX = rising edge |

C'est important parce que **le maître TDM en face (Teensy)** doit générer une trame compatible avec ces réglages : pulse FS court de 1 BCLK, polarité active basse, alignement avant le premier bit.

Si la Teensy Audio Library génère un FS « long » (style I²S 50/50) à la place, il faudra adapter `FSDefinition` / `ActiveFrameLength` dans `InitProtocolTDM` côté Daisy.

---

## 7. Mise en mode SAI2 **slave TDM** (Teensy = master)

### 7.1 Cahier des charges

- Teensy = **TDM master** : génère BCLK + FS (+ MCLK si nécessaire pour le sample rate).
- Teensy → Daisy : **6 canaux** d'entrée vers la Daisy (hexaphonique).
- Daisy → Teensy : **8 canaux** de sortie depuis la Daisy.
- Daisy = **TDM slave** sur SAI2.

**Contrainte protocole** : une trame TDM a un seul nombre de slots, valable pour RX et TX en parallèle. On prend donc `tdm_slots = max(6, 8) = 8`. Côté RX, on n'utilisera que 6 slots utiles ; les 2 autres seront ignorés (ou silence).

### 7.2 Étape A — Reconfigurer SAI2 en slave

Dans [heartware_pod_prototype.h:96-114](../src/heartware_pod_prototype.h#L96-L114), remplacer la section SAI2 par :

```cpp
sai_config[1].periph    = SaiHandle::Config::Peripheral::SAI_2;
sai_config[1].sr        = SaiHandle::Config::SampleRate::SAI_48KHZ;
sai_config[1].bit_depth = SaiHandle::Config::BitDepth::SAI_32BIT;
sai_config[1].tdm_slots = 8;

// Daisy = slave : les DEUX blocs écoutent FS+SCK du Teensy
sai_config[1].a_sync = SaiHandle::Config::Sync::SLAVE;
sai_config[1].b_sync = SaiHandle::Config::Sync::SLAVE;

// 8 canaux TX (Daisy -> Teensy), 6 canaux RX utiles (Teensy -> Daisy)
sai_config[1].a_dir  = SaiHandle::Config::Direction::TRANSMIT; // SDO (Daisy out)
sai_config[1].b_dir  = SaiHandle::Config::Direction::RECEIVE;  // SDI (Daisy in)

// MCLK non utilisé en slave (pas câblé en alternate function) :
sai_config[1].pin_config.mclk = {DSY_GPIOX, 0};
sai_config[1].pin_config.sa   = seed::D26; // SDO -> Teensy SDI
sai_config[1].pin_config.sb   = seed::D25; // SDI <- Teensy SDO
sai_config[1].pin_config.fs   = seed::D27; // FS / LRCK <- Teensy
sai_config[1].pin_config.sck  = seed::D28; // SCK / BCLK <- Teensy
```

**Pourquoi ça marche sans MCLK** ? Voir [sai.cpp:506-516](../libDaisy/src/per/sai.cpp#L506-L516) :

```cpp
is_master = (config_.a_sync == Config::Sync::MASTER
             || config_.b_sync == Config::Sync::MASTER);
for(size_t i = 0; i < 5; i++)
{
    // Skip MCLK if not master.
    if(dsy_pin_cmp(&config_.pin_config.mclk, cfg[i]) && !is_master)
        continue;
    // ... mux le pin en alternate function ...
}
```

Comme les deux `_sync` sont `SLAVE`, `is_master = false`, et la pin MCLK n'est **pas** initialisée → aucune fuite, le Teensy peut tranquillement piloter BCLK/FS.

### 7.3 Étape B — Lever le plafond de canaux

[audio.cpp:14](../libDaisy/src/hid/audio.cpp#L14) — augmenter `kAudioMaxChannels` :

```cpp
static const size_t kAudioMaxChannels = 10; // 2 (SAI1) + 8 (SAI2 TDM)
```

(Ou bien `8` si tu décides de virer SAI1 complètement, voir §7.5.)

**Vérification de la taille de buffer DMA** :

```
block_size × 2 × tdm_slots ≤ kAudioMaxBufferSize
32        × 2 × 8          = 512        ≤ 1024  ✔
```

OK avec `block_size = 32`. Si tu pousses à `block_size = 64` ou plus, vérifie.

### 7.4 Étape C — Généraliser le désentrelacement TDM

[audio.cpp:357-517](../libDaisy/src/hid/audio.cpp#L357-L517) — la branche `chns > 4` est hardcodée à 4 slots avec `in2[i * 4 + 0..3]`. Il faut la rendre paramétrique. Récupère le nombre de slots avec :

```cpp
size_t tdm_slots = audio_handle.sai2_.GetConfig().tdm_slots;
```

Puis remplace les 4 lignes hardcodées par une boucle :

```cpp
if(tdm_slots > 0) // TDM
{
    for(size_t s = 0; s < tdm_slots; s++)
        fin[2 + s][i] = s322f(in2[i * tdm_slots + s]) * gain;
}
else if(chns > 2) // I²S stéréo sur SAI2
{
    fin[2][i] = s242f(in2[i * 2 + 0]) * gain;
    fin[3][i] = s242f(in2[i * 2 + 1]) * gain;
}
```

À répliquer pour les 3 bit-depths × 2 directions (RX/TX) = 6 endroits dans `InternalCallback`. Profite-en pour factoriser si tu veux (mais reste prudent : c'est de l'audio temps réel dans une IRQ).

### 7.5 Étape D — Qui pilote le callback ?

C'est **le point le plus délicat**. Le callback est aujourd'hui déclenché par SAI1 ([audio.cpp:183-186](../libDaisy/src/hid/audio.cpp#L183-L186)). Deux scénarios possibles :

#### Scénario 1 — Garder SAI1 (codec interne) ET SAI2 (slave Teensy)

- SAI1 tourne à 48 kHz sur l'horloge **STM32**.
- SAI2 tourne à 48 kHz sur l'horloge **Teensy**.
- Les deux horloges ne sont jamais exactement à la même fréquence (PPM près) → **dérive progressive**. Les fenêtres half/full DMA des deux SAI vont glisser l'une par rapport à l'autre. Au bout d'un moment, le callback SAI1 lira un buffer SAI2 en cours d'écriture → **glitches / pops audibles**.
- Pas recommandé sauf si tu mets en place un mécanisme de re-synchro / SRC.

#### Scénario 2 — N'utiliser que SAI2 (recommandé pour ton cas)

- Vire SAI1 dans `InitAudio()` : ne configure et n'initialise que `sai_config[1]`.
- Appelle `pod.seed.audio_handle.Init(cfg, sai_handle[1])` (la surcharge à un seul SAI, [audio.cpp:114-140](../libDaisy/src/hid/audio.cpp#L114-L140)).
- ⚠️ Cette surcharge place le SAI passé en argument dans `sai1_`, pas dans `sai2_`. Ça signifie que :
  - Le buffer sera dans `buff_rx_[0]` / `buff_tx_[0]`.
  - Le code de désentrelacement TDM (qui suppose le TDM en `buff_rx_[1]`) **ne marchera pas tel quel**.
- Donc soit :
  - **(a)** tu modifies `Impl::Init(config, sai)` pour qu'il accepte un SAI avec `tdm_slots > 0` et adapte le mapping `in[ch]` ; ou
  - **(b)** tu gardes l'appel à deux SAI mais tu passes un `SaiHandle` non-initialisé en premier argument, et tu adaptes `Start()` pour que le callback soit déclenché par `sai2_` quand `sai1_.IsInitialized() == false`.

L'option (b) est plus chirurgicale. En gros, dans `Impl::Start` :

```cpp
SaiHandle& clock_master_sai = sai1_.IsInitialized() ? sai1_ : sai2_;
SaiHandle& other_sai        = sai1_.IsInitialized() ? sai2_ : sai1_;

if(other_sai.IsInitialized())
    other_sai.StartDma(/*...*/, nullptr);

clock_master_sai.StartDma(/*...*/, audio_handle.InternalCallback);
```

Et il faut aussi adapter `InternalCallback` pour qu'il sache quel buffer est « passé en paramètre » (le SAI maître du callback) et quel est « lu via offset » (l'autre).

### 7.6 Étape E — Côté Teensy

Calcul d'horloges pour TDM 8 slots × 32 bits @ 48 kHz :

```
BCLK = 48000 × 8 × 32 = 12.288 MHz
FS   = 48 kHz, pulse 1 BCLK, active LOW, aligné avant le premier bit
```

À vérifier dans la doc de la Teensy Audio Library / `AudioOutputTDM` :

- Polarité et largeur du FS (style « TDM short pulse » vs « I²S long pulse »). Le mode TDM standard de la Teensy Audio est généralement compatible.
- Slot order MSB-first.
- Slot size 32 bits.

Si la Teensy génère un FS différent, tu as deux options : adapter `InitProtocolTDM` côté Daisy (changer `FSPolarity` / `ActiveFrameLength` / `FSOffset`), ou configurer la Teensy en mode SAI compatible.

---

## 8. Checklist de portage

Quand tu attaques le portage, suis cet ordre — chaque étape est testable indépendamment :

- [ ] **(1)** Augmenter `kAudioMaxChannels` à 10 dans [audio.cpp:14](../libDaisy/src/hid/audio.cpp#L14).
- [ ] **(2)** Rendre `InternalCallback` paramétrique en `tdm_slots` (boucles au lieu des 4 lignes hardcodées).
- [ ] **(3)** Modifier la config SAI2 dans [heartware_pod_prototype.h:96-114](../src/heartware_pod_prototype.h#L96-L114) : `a_sync=SLAVE`, `b_sync=SLAVE`, `tdm_slots=8`, `bit_depth=SAI_32BIT`, inverser `a_dir`/`b_dir` selon ton routage Teensy.
- [ ] **(4)** Décider du scénario (Scénario 1 vs 2 du §7.5) et l'implémenter.
- [ ] **(5)** Tester avec un signal connu côté Teensy (ex : sinusoïde 1 kHz sur le slot 0, silence partout ailleurs) et vérifier que tu le récupères proprement sur `in[2][...]` dans le callback Daisy.
- [ ] **(6)** Vérifier l'absence de drift sur plusieurs minutes (oscilloscope ou logging d'amplitude par slot).
- [ ] **(7)** Tester la sortie : envoyer un signal connu sur `out[2..9]` et vérifier que la Teensy le reçoit slot par slot.

---

## 9. Pièges à éviter

- **Ne pas confondre `bit_depth` et `DataSize` réel** : en TDM, la couche bas niveau force 32 bits sur le bus, peu importe `bit_depth`. La conversion 16/24/32 → float est faite en software dans `InternalCallback`.
- **Ne pas oublier `tdm_slots = 8`** : sans ça, `InitProtocolTDM` n'est pas appelé et tu retombes en I²S 2 canaux.
- **Ne pas câbler MCLK en slave** : en mode slave pur, la pin n'est pas muxée — c'est volontaire, ne la force pas, ça ferait conflit avec le Teensy si lui aussi la sortait.
- **L'ordre des slots dans la trame TDM** est entièrement déterminé par le maître (Teensy). Il faut absolument valider quel slot Teensy correspond à quel index `in[ch]` côté Daisy avec un signal de test.
- **`block_size` doit rester ≤ 256** avec 8 slots TDM et un buffer DMA de 1024 ; sinon il faut aussi augmenter `kAudioMaxBufferSize`.
- **Le pointeur `in2` est calculé à chaque entrée du callback** via `GetOffset()` — c'est ce qui permet de toujours lire la moitié « fraîche » du buffer circulaire de SAI2. Ne pas cacher ce pointeur.

---

## 10. Pour aller plus loin

- Datasheet AK4619 : [doc/ak4619vn-en-datasheet.pdf](ak4619vn-en-datasheet.pdf) — utile pour comprendre comment un slave TDM s'attend à recevoir une trame, par analogie.
- Eurorack-PMOD : [doc/eurorack-pmod.pdf](eurorack-pmod.pdf) et le repo apfelaudio sur GitHub.
- Présentation STM32H7 SAI (lien dans [sai.cpp:261](../libDaisy/src/per/sai.cpp#L261)) — référence officielle ST pour comprendre les bits SAI_xCR1/CR2/FRCR/SLOTR.
- Issue libDaisy #499 « Feature - TDM SAI/Codec Support » — discussion d'origine sur le support TDM.

---

Bon courage Paul, et n'hésite pas à griffer dans `InternalCallback` — c'est moche mais c'est là que se joue tout le mapping canal/buffer.
