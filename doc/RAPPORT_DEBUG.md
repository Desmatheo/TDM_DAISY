# Rapport de débogage — Daisy Seed esclave TDM sur SAI2 (master Teensy)

> **Équipe** : Yannick + Paul (stage DSP) · **Date** : 2026-06-12
> **Symptômes initiaux** : le `AudioCallback` utilisateur ne se déclenche jamais (compteur à 0, aucun print), alors que « quelque chose » semble sortir sur la broche SDO de SAI2, mais sans rapport avec ce que la Teensy envoie.
>
> **Verdict** : trois causes racines indépendantes, plus un bug latent transitoire et deux pièges détectés pendant la correction. Toutes les affirmations registre/protocole ci-dessous ont été vérifiées contre le **RM0433 Rev 8** (chapitre 51 SAI), le **HAL ST local** (`libDaisy/Drivers/STM32H7xx_HAL_Driver/`), les **sources Teensy réelles** (`PaulStoffregen/Audio`, `output_tdm.cpp`/`input_tdm.cpp`, chemin i.MX RT1062) et les données de brochage officielles ST (`STM32H750IBKx.xml`).

---

## 1. Contexte

Objectif : utiliser la Daisy Seed comme **esclave TDM** sur SAI2. La Teensy 4.x est **master TDM** (génère BCLK + FS), envoie 6 canaux vers la Daisy (hexaphonique) et reçoit 8 canaux en retour.

Configuration au moment du bug (commit `e747c40`) :

```cpp
sai_config[1].tdm_slots = 8;
sai_config[1].a_sync = SLAVE;   // les deux blocs esclaves
sai_config[1].b_sync = SLAVE;
sai_config[1].a_dir  = TRANSMIT; // D26 -> Teensy
sai_config[1].b_dir  = RECEIVE;  // D25 <- Teensy
seed.audio_handle.Init(cfg, sai_handle[1]); // SAI2 seul, plus de SAI1
```

Cette configuration est **conceptuellement correcte**. Elle échouait à cause de bugs dans la couche libDaisy en dessous.

---

## 2. Cause racine n°1 — Les deux blocs en `SAI_SYNCHRONOUS` : personne n'écoute les pins

**C'est le bug qui explique « le callback ne se déclenche jamais ».**

### Le code fautif

`libDaisy/src/per/sai.cpp`, fonction `Init` (avant correction) :

```cpp
if(config.a_sync == Config::Sync::MASTER)
    sai_a_handle_.Init.Synchro = SAI_ASYNCHRONOUS;
else
    sai_a_handle_.Init.Synchro = SAI_SYNCHRONOUS;   // <- SLAVE => SYNCHRONOUS
// ... identique pour le bloc B
```

Avec `a_sync = b_sync = SLAVE`, **les deux blocs** se retrouvent en `SAI_SYNCHRONOUS` (SYNCEN = 01).

### Pourquoi c'est fatal

Sur STM32H7, chaque bloc SAI a un champ `SYNCEN` (SAI_xCR1, bits 11:10) :

- **`00` (asynchrone)** — RM0433 §51.4.3 : *« If the SAI subblock is configured in asynchronous mode, then SCK_x and FS_x pins are configured as inputs. »* Le bloc esclave asynchrone échantillonne FS/SCK **sur ses propres broches externes**.
- **`01` (synchrone)** — le bloc prend FS/SCK **en interne, depuis l'autre bloc du même SAI**, et — point crucial — *« sees its own SCK_x, FS_x, and MCLK_x pins released back as GPIOs »* (§51.4.4) : **ses broches externes sont ignorées**.

Le RM0433 §51.4.4 décrit explicitement la seule configuration valide à deux esclaves :

> *« One of the two audio blocks can be configured as a master and the other as slave, **or both as slaves with one asynchronous block (SYNCEN=00) and one synchronous block (SYNCEN=01)**. »*

Avec les deux blocs en SYNCEN=01 : chaque bloc attend ses horloges du bus de synchro interne, que **seul un bloc asynchrone peut piloter** — et il n'y en a aucun. Les BCLK/FS envoyés par la Teensy sur D27/D28 arrivent sur des broches que **plus aucun bloc ne regarde**.

### Pourquoi le silence est total (pas même une erreur)

Aucun front d'horloge n'atteint les registres à décalage → rien n'est jamais shifté → le FIFO RX ne se remplit jamais → **aucune requête DMA** → les IRQ `HAL_SAI_RxCpltCallback`/`RxHalfCpltCallback` ne tirent jamais → `InternalCallback` n'est jamais appelé → le `AudioCallback` utilisateur non plus. Et les détecteurs d'erreur de trame (AFSDET/LFSDET) ne se déclenchent pas non plus : **ils ont eux-mêmes besoin de fronts SCK pour fonctionner**. L'interface est silencieusement morte — exactement le symptôme observé.

### Le correctif

`sai.cpp` détecte maintenant le cas « deux esclaves » et met en `SAI_ASYNCHRONOUS` le bloc qui possède physiquement les broches FS/SCK, sélectionné par le nouveau champ de config `ext_clock_block` :

```cpp
const bool both_slave = a_sync == SLAVE && b_sync == SLAVE;
// bloc A : SLAVE => SYNCHRONOUS, sauf si c'est lui qui a les pins
sai_a_handle_.Init.Synchro = (both_slave && ext_clock_block == BLOCK_A)
                                 ? SAI_ASYNCHRONOUS : SAI_SYNCHRONOUS;
// bloc B : symétrique
```

---

## 3. Quel bloc possède FS/SCK ? — Bloc **B** (et erratum)

Pour appliquer le correctif n°1, il faut savoir quel bloc est câblé aux broches exposées du Seed. Vérifié dans `libDaisy/src/daisy_seed.h` (table des pins) croisé avec les données officielles ST (`STM32_open_pin_data`, `STM32H750IBKx.xml`) :

| Pin Seed | GPIO | Signal silicium | AF | Bloc |
|---|---|---|---|---|
| D24 | PA1 | SAI2_MCLK_B | AF10 | B |
| D25 | PA0 | SAI2_SD_B | AF10 | B |
| D26 | PD11 | SAI2_SD_A | AF10 | **A** |
| D27 | PG9 | **SAI2_FS_B** | AF10 | **B** |
| D28 | PA2 | **SAI2_SCK_B** | AF8 | **B** |

**FS et SCK appartiennent au bloc B.** C'est donc le bloc B qui doit être l'esclave asynchrone (d'où le défaut `ExtClockBlock::BLOCK_B`). Un bloc A asynchrone est physiquement impossible sur ces pins : les broches FS_A/SCK_A de SAI2 (PD12/PD13…) ne sont pas routées sur le Seed.

> **Erratum** : le premier diagnostic donné en cours de session (« les pins FS/SCK sont mappées sur le bloc A ») était **faux**. À noter aussi : `daisy_patch.cpp` nomme sa constante locale `PIN_SAI2_FS_A = D27` — c'est un **misnomer** dans libDaisy, le signal silicium est bien `SAI2_FS_B`. Ne pas s'y fier.
>
> Cohérence a posteriori : la Daisy Patch configure SAI2 avec `b_sync = MASTER` — c'est-à-dire bloc B asynchrone, le bloc qui possède les pins. CQFD.

---

## 4. Cause racine n°2 — La couche audio traitait « un seul SAI » comme « stéréo 2 canaux »

Même avec les horloges réparées, les données auraient été fausses. L'application passe son unique SAI TDM par `audio_handle.Init(cfg, sai)` (surcharge à un seul SAI). Or dans l'ancien `audio.cpp` :

1. **`GetChannels()` ignorait `tdm_slots` pour le slot logique `sai1_`** : `if(sai1_.IsInitialized()) ch += 2;` — toujours 2, seul `sai2_` regardait `tdm_slots`. Résultat : `chns = 2` au lieu de 8.
2. Le désentrelacement traitait alors le buffer DMA — qui contient 8 slots entrelacés — comme de la stéréo : `in[0]` recevait les slots 0,2,4,6 mélangés, `in[1]` les slots 1,3,5,7, avec une taille de bloc fausse (`size/2` = 4× trop grande). **Données chaotiques garanties**, ce qui correspond au second symptôme (« données non conformes »).
3. Le chemin TDM existant était de toute façon **codé en dur pour 4 slots** (`in2[i*4 + 0..3]`, branches `chns > 4`) et uniquement pour un TDM placé en *second* SAI.

Correctif : `audio.cpp` réécrit — chaque SAI contribue `tdm_slots` (ou 2) canaux, le désentrelacement est générique (boucles sur le nombre de slots réel), la taille de bloc est calculée à partir du nombre de canaux du SAI pilote. Détails dans [RAPPORT_IMPLEMENTATION.md](RAPPORT_IMPLEMENTATION.md).

---

## 5. Cause racine n°3 — Polarité du frame sync inversée par rapport à la Teensy

`InitProtocolTDM` codait en dur `FSPolarity = SAI_FS_ACTIVE_LOW` (hérité de la configuration AK4619 d'origine du fork).

Or la trame TDM générée par la Teensy 4.x (vérifiée dans `output_tdm.cpp`, registres i.MX RT1062) est :

- FS **actif HAUT** (bit FSP non positionné dans TCR4),
- large d'**exactement 1 BCLK** (SYWD=0),
- asserté **un bit avant** le premier bit de données (FSE — *early frame sync*),
- 8 slots × 32 bits = 256 BCLK/trame, MSB first (MF, FBT=31),
- BCLK = 256 × Fs, données émises sur front descendant, échantillonnées sur front montant (BCP=1),
- Fs = 44 100 Hz **par défaut**, exactement, sur Teensy 4 (PLL fractionnaire ; ce n'est PAS le 44 117,6 Hz des Teensy 3). Le projet impose **48 kHz** via `AUDIO_SAMPLE_RATE_EXACT=48000.0f` (cf. guide §3.0), soit BCLK = 12,288 MHz — la structure de trame, les polarités et l'alignement restent strictement identiques.

En mode esclave, le démarrage de trame est **sensible au front** défini par FSPOL (RM0433 §51.4.6). Avec ACTIVE_LOW, la Daisy aurait calé ses trames sur le front descendant du pulse FS → décalage d'alignement de slots, et détections d'erreur AFSDET/LFSDET en pagaille.

Correctif : nouveau champ `SaiHandle::Config::tdm_fs_polarity` (`ACTIVE_HIGH` pour la Teensy, défaut `ACTIVE_LOW` pour conserver la compatibilité AK4619). `FSOffset = SAI_FS_BEFOREFIRSTBIT` correspond déjà exactement au FSE de l'i.MX — RM0433 : *« FS is asserted one bit before the first bit of the slot 0 »*.

---

## 6. Bug latent transitoire — `SaiHandle` non initialisé passé à l'audio (commits `4c2750d`/`f99aca7`)

Entre les commits `4c2750d` et `f99aca7`, le code appelait encore :

```cpp
seed.audio_handle.Init(cfg, sai_handle[0], sai_handle[1]);
```

alors que `sai_handle[0].Init(...)` venait d'être commenté. Un `SaiHandle` **jamais initialisé** (pimpl nul) était donc utilisé comme SAI pilote : `Start()` appelait `StartDma()` dessus → déréférencement de pointeur nul. Sur Cortex-M, l'adresse 0 est lisible (alias flash/ITCM) : pas de crash franc, mais un **comportement indéterminé** — configuration DMA/SAI fantôme à partir de données lues à l'adresse 0.

C'est l'explication la plus plausible des « échantillons non conformes » observés sur la broche SDO pendant cette phase : de l'activité électrique réelle mais issue d'un état indéterminé (ou, après `e747c40`, simplement la ligne SDO au repos avec pull-up — `InitPins` configure `GPIO_PULLUP` — vue comme un niveau fixe au scope). À noter qu'après `e747c40`, le FIFO TX était bien préchargé par `HAL_SAI_Transmit_DMA`, mais sans horloge il ne se vidait jamais.

Paul a corrigé ce bug dans `e747c40` en passant à `Init(cfg, sai_handle[1])` — c'était le bon réflexe ; la nouvelle couche audio ajoute en plus des gardes (`Start()` retourne `ERR` si le SAI pilote n'est pas initialisé, et l'ancien `GetOffset()` sur handle nul a disparu du chemin mono-SAI).

---

## 7. Piège évité pendant la correction — `ClockStrobing` est interprété selon la direction du bloc

Pendant la refonte, première tentative : « uniformiser » `ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE` pour les deux blocs, en lisant la définition du bit CKSTR dans le RM0433 (CKSTR=1 : *« signals generated change on falling edge, signals received are sampled on rising edge »* — ce qu'on veut partout).

**C'était faux.** Vérification dans `stm32h7xx_hal_sai.c` (lignes ~591-601) : le HAL traduit la constante `ClockStrobing` en bit CKSTR **différemment selon que le bloc est TX ou RX** :

| Bloc | Constante HAL | CKSTR résultant |
|---|---|---|
| TX | `FALLINGEDGE` | **1** ✔ |
| TX | `RISINGEDGE` | 0 |
| RX | `RISINGEDGE` | **1** ✔ |
| RX | `FALLINGEDGE` | 0 ✗ |

Pour obtenir CKSTR=1 partout (convention I²S/TDM standard, et celle de la Teensy avec BCP=1), il faut donc `FALLINGEDGE` côté TX et `RISINGEDGE` côté RX — **ce que le code original du fork faisait déjà correctement**. La version finale restaure ce comportement avec un commentaire explicite. C'est le même mapping que `SAI_InitI2S` du HAL ST lui-même.

Moralité : sur ce périphérique, ne jamais raisonner sur les constantes HAL sans vérifier la traduction registre.

---

## 8. Anti-patterns relevés dans le code de debug (sans lien avec la panne, mais à bannir)

1. **`PrintLine` dans le callback audio** (`audio_processing.h`, commit `e747c40`) : le callback s'exécute en contexte d'interruption DMA ; un printf USB-CDC peut bloquer plusieurs ms, voire indéfiniment → dropouts ou gel complet de l'audio. Ici il ne s'est rien passé… uniquement parce que le callback ne tournait pas. Le nouveau code n'imprime que depuis la boucle principale, le callback ne fait qu'incrémenter des compteurs `volatile`.
2. **Définitions de variables globales dans un header** (`volatile float debug_in_level` etc.) : fonctionne tant qu'une seule unité de compilation inclut le header ; erreur de link à la première inclusion supplémentaire. Remplacé par une `struct AudioDiagnostics` instanciée en `static`.
3. **`using namespace daisy;` placé avant les `#include`** dans un header partagé : pollution d'espace de noms pour tout includeur, ordre d'inclusion fragile.

---

## 9. Récapitulatif

| # | Problème | Localisation | Effet | Statut |
|---|---|---|---|---|
| 1 | Deux blocs esclaves ⇒ deux `SAI_SYNCHRONOUS`, aucun bloc ne lit FS/SCK | `sai.cpp` `Init` | Callback jamais déclenché, interface silencieusement morte | **Corrigé** (`ext_clock_block`, bloc B asynchrone) |
| 2 | Couche audio : 1 SAI = 2 canaux codé en dur, TDM 4 slots codé en dur | `audio.cpp` | Données mélangées/chaotiques même avec horloges OK | **Corrigé** (réécriture générique) |
| 3 | FS `ACTIVE_LOW` codé en dur vs Teensy actif HAUT | `sai.cpp` `InitProtocolTDM` | Désalignement de trame chez l'esclave | **Corrigé** (`tdm_fs_polarity`) |
| 4 | `SaiHandle` non initialisé passé à `audio_handle.Init` | app (commits intermédiaires) | UB, sorties SDO erratiques | **Corrigé par Paul** (`e747c40`) + gardes ajoutées |
| 5 | Mapping HAL direction-dépendant de CKSTR | `sai.cpp` | Piège (régression évitée pendant la refonte) | **Documenté + code explicite** |
| 6 | Erratum bloc A/bloc B du diagnostic initial | conversation (session de debug) | Aurait fait corriger le mauvais bloc | **Corrigé** (vérifié datasheet) |
| 7 | PrintLine dans l'IRQ audio, globals dans headers | app | Risque de gel/dropouts une fois le callback vivant | **Corrigé** (refonte src/) |

### Correspondance symptômes ↔ causes

- *« La fonction de callback n'est jamais déclenchée »* → cause n°1 (aucune horloge capturée ⇒ aucune IRQ DMA).
- *« Des échantillons sortent sur la broche SAI2 mais non conformes »* → cause n°4 pendant les commits intermédiaires (UB) ; après `e747c40`, ligne au repos/pull-up + FIFO préchargé jamais vidé.
- *« Un callback interne semble tourner »* → non : **rien** ne tournait côté SAI2. Les prints `--- TEST --- Appels du Callback : 0/seconde` (compteur jamais incrémenté) venaient de la boucle principale, pas d'un callback.

---

## 10. Validation restant à faire sur matériel

Le code compile et la configuration registre est conforme aux specs des deux côtés, mais la validation finale exige le banc réel :

1. Brancher selon le câblage du [GUIDE_UTILISATION.md](GUIDE_UTILISATION.md), flasher, ouvrir le moniteur série.
2. Vérifier `callbacks/s: 1500` (48000/32). `0` ⇒ revoir câblage BCLK/FS ou sketch Teensy pas lancé ; `~1378` ⇒ Teensy restée à 44,1 kHz.
3. Vérifier les 6 peaks d'entrée avec les 6 sinus de test du sketch Teensy.
4. Vérifier côté Teensy la réception du LA 440 Hz généré par la Daisy sur le slot 6 (port pair 12).
5. Laisser tourner ≥ 30 min : aucune dérive attendue (la Daisy est esclave de l'unique horloge Teensy).
