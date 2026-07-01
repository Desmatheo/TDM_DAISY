# daisy-tdm-hexa

Daisy Seed en **esclave TDM sur SAI2**, pilotée par une **Teensy 4.x master** (Audio Library, `AudioOutputTDM`/`AudioInputTDM`) :

- **6 canaux** Teensy → Daisy (traitement hexaphonique)
- **8 canaux** Daisy → Teensy
- Trame : 8 slots × 32 bits @ **48 kHz** (BCLK 12,288 MHz), la Teensy génère toutes les horloges (lib Audio recompilée à 48 kHz, voir guide §3.0).

## Documentation

| Document | Contenu |
|---|---|
| [doc/GUIDE_UTILISATION.md](doc/GUIDE_UTILISATION.md) | **Commencer ici** : câblage, sketch Teensy, build/flash, où écrire le DSP, dépannage |
| [doc/RAPPORT_IMPLEMENTATION.md](doc/RAPPORT_IMPLEMENTATION.md) | Architecture, modifications libDaisy (SAI esclave, audio multi-slots), choix de conception |
| [doc/RAPPORT_DEBUG.md](doc/RAPPORT_DEBUG.md) | Analyse des bugs d'origine (SYNCEN, canaux codés en dur, polarité FS…) vérifiée contre RM0433/HAL/sources Teensy |
| [doc/SAI_TDM_guide_Paul.md](doc/SAI_TDM_guide_Paul.md) | *Historique* — onboarding SAI/TDM d'avant la refonte (contient un erratum, voir bandeau) |

## Démarrage rapide

```bash
git clone --recursive git@github.com:sgtpepper335/daisy-tdm-hexa.git
cd daisy-tdm-hexa
make -C libDaisy -j8 && make -C DaisySP -j8 && make -j8
make program-dfu      # Daisy en mode bootloader (BOOT+RESET)
```

Câblage minimal : Teensy 21→D28 (BCLK), 20→D27 (FS), 7→D25 (data in), 8←D26 (data out), GND↔GND. Détails et sketch Teensy de test dans le [guide](doc/GUIDE_UTILISATION.md).

Le code applicatif tient dans trois fichiers sous [src/](src/) : `daisy_tdm_slave.h` (configuration matérielle), `audio_processing.h` (le callback DSP — c'est là qu'on travaille), `main.cpp` (boucle de statut).

## Origine

Fork de [heartwerktech/daisy_seed_tdm_eurorack_pmod](https://github.com/heartwerktech/daisy_seed_tdm_eurorack_pmod) (exemple TDM AK4619/Eurorack-PMOD, master Daisy), reconverti en esclave TDM pour master Teensy. Utilise un fork de [libDaisy](https://github.com/electro-smith/libDaisy) étendu : support TDM esclave intégral (deux blocs SLAVE), polarité FS configurable, moteur audio générique multi-slots. Voir l'issue libDaisy [#499](https://github.com/electro-smith/libDaisy/issues/499) pour le contexte amont.
