# 🎛️ Guide Clé en Main — Projet Hexaphonique Teensy/Daisy sur Mac

> **Pour qui ?** Une personne qui repart de zéro sur un Mac vierge.  
> **Résultat final :** Un système audio hexaphonique complet fonctionnel :  
> `PC (Reaper) ↔ USB ↔ Teensy 4.1 ↔ TDM ↔ Daisy Seed ↔ TDM ↔ Teensy ↔ USB ↔ PC`  
> avec une GUI Python pour contrôler les effets en temps réel via MIDI.

> ⚠️ **Note importante :** Le firmware à flasher sur la Teensy est le projet **TeensyPass**. Le projet `TDM_TEENSY` est un projet distinct et n'est **pas** utilisé ici.

---

## 📋 Matériel requis et modes d'utilisation

| Composant | Description |
|---|---|
| **Teensy 4.1** | La carte principale qui fait l'interface audio USB ↔ TDM |
| **Daisy Seed** | Le processeur d'effets DSP |
| **Hub USB-C** | Pour brancher les deux cartes au Mac en même temps |
| **Câble micro-USB × 2** | ⚠️ Câbles **données** (pas seulement recharge) |
| **Câble TDM** | 5 fils : MCLK, BCLK, FS, Data In, Data Out + GND |

### 🔀 Deux modes d'utilisation possibles

Le montage Teensy/Daisy peut être utilisé de deux façons. **Le câblage TDM2 entre la Teensy et la Daisy reste identique dans les deux cas, tout comme le code.**

**Mode 1 — Breadboard (développement / test)**  
La Teensy et la Daisy sont montées à nu sur une breadboard et reliées directement par les fils TDM2 décrits ci-dessous. Le son provient alors de l'USB (fichier `.wav` envoyé via Reaper depuis le Mac).

**Mode 2 — PCB avec codec hexaphonique (utilisation scénique)**  
La Teensy est montée sur un PCB dédié qui intègre :
- Des **ports DIN 13** pour recevoir le signal hexaphonique de la guitare. La guitare peut y être connectée de deux façons :
  - Directement si elle dispose d'une sortie hexaphonique DIN 13 (ex. Roland GK)
  - Via un **boîtier adaptateur** si elle a des pickups submarine : 6 câbles jacks mono → conversion en DIN 13
- Des **sorties jack** pour renvoyer le son traité vers un ampli

> Dans ce mode, la **Teensy utilise son TDM1 (SAI1)** pour communiquer avec le codec audio du PCB (entrée/sortie guitare). Son **TDM2 (SAI2)** reste dédié à la liaison Teensy ↔ Daisy, comme en mode breadboard.

---

### Câblage TDM2 Teensy ↔ Daisy (identique dans les deux modes)

> Le firmware TeensyPass utilise **TDM2** (bus SAI2 de la Teensy), pas TDM1. Les pins sont différentes de la documentation générique Teensy.

```
Teensy 4.1  ──────────────  Daisy Seed
  Pin 33  →  D28  (MCLK)
  Pin 4   →  D27  (BCLK / TX_BCLK)
  Pin 3   →  D29  (FS / TX_SYNC)
  Pin 2   →  D25  (DATA OUT  Teensy→Daisy)
  Pin 5   ←  D26  (DATA IN   Daisy→Teensy)
  GND     ↔  GND
```

> ⚠️ **Ne pas utiliser les pins 21/20/7/8** — elles correspondent à TDM1 (SAI1) qui n'est pas utilisé par TeensyPass pour la liaison Teensy↔Daisy.

---

## 🚀 PARTIE 1 — Outils système à installer

### 1.1 — Homebrew (gestionnaire de paquets Mac)

Ouvre le **Terminal** (`Cmd + Espace` → "Terminal") et colle :

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

> Suivre les instructions à l'écran. À la fin, il te demandera d'ajouter Homebrew au PATH — **copie et colle les deux lignes qu'il te donne** dans le terminal.

### 1.2 — Git

```bash
brew install git
```

### 1.3 — Python 3 (via Homebrew, avec support Tkinter)

> ⚠️ **Important :** Le Python installé par défaut sur Mac **ne supporte pas Tkinter** (nécessaire pour la GUI). Il faut installer via Homebrew.

```bash
brew install python-tk@3.13
```

Vérifie :
```bash
python3 --version   # doit afficher Python 3.13.x
```

### 1.4 — VS Code + PlatformIO (pour la Teensy)

1. Télécharge **VS Code** : https://code.visualstudio.com/
2. Ouvre VS Code → Extensions (`Cmd + Shift + X`) → cherche **"PlatformIO IDE"** → Installe

> PlatformIO télécharge automatiquement les outils Teensy au premier build.

### 1.5 — Outils de compilation Daisy (ARM toolchain + make)

```bash
brew install --cask gcc-arm-embedded
brew install make
```

Vérifie :
```bash
arm-none-eabi-gcc --version   # doit afficher la version
make --version
```

### 1.6 — dfu-util (pour flasher la Daisy)

```bash
brew install dfu-util
```

---

## 🗂️ PARTIE 2 — Cloner les projets

Crée un dossier de travail, par exemple dans `~/Workspace` :

```bash
mkdir -p ~/Workspace
cd ~/Workspace
```

### 2.1 — Le firmware de la Teensy (TeensyPass)

```bash
git clone --recursive https://github.com/Desmatheo/TeensyPass.git
```

> **TeensyPass** est le firmware qui permet à la Teensy de faire l'interface audio USB ↔ TDM ↔ Daisy, et de transmettre le signal hexaphonique dans les deux sens.

### 2.2 — Le firmware de la Daisy (TDM_DAISY)

```bash
git clone --recursive https://github.com/Desmatheo/TDM_DAISY.git
```

> Le flag `--recursive` est **essentiel** — il télécharge aussi les sous-modules (`libDaisy`, `DaisySP`, `Q`, `gcem`, `infra`). Sans lui, la compilation échoue.

### 2.3 — La GUI Python (interface de contrôle MIDI)

```bash
git clone https://github.com/Desmatheo/GUI.git
```



---

## 🔧 PARTIE 3 — Patch 6 canaux pour la Teensy (OBLIGATOIRE)

> **Pourquoi ?** Par défaut, le framework Teensy ne supporte que 2 canaux audio USB (stéréo). Pour avoir les 6 canaux hexaphoniques, il faut patcher les fichiers internes.

### 3.1 — Faire un premier build pour forcer le téléchargement du framework

Dans VS Code, ouvre le dossier `TeensyPass`. Attends que PlatformIO finisse d'initialiser, puis clique sur **Build** (icône ✓ en bas). Il va télécharger le framework Teensy (ça peut prendre quelques minutes).

### 3.2 — Sauvegarder les fichiers d'origine

```bash
cp -r ~/.platformio/packages/framework-arduinoteensy/cores/teensy4 \
      ~/.platformio/packages/framework-arduinoteensy/cores/teensy4_backup
```

### 3.3 — Appliquer le patch 6 canaux

```bash
# Télécharger le patch communautaire
cd /tmp
git clone https://github.com/alex6679/teensy-4-usbAudio.git
cd teensy-4-usbAudio

# Copier les fichiers patchés dans le framework
cp usb_audio.cpp usb_audio.h usb_audio_interface.cpp usb_audio_interface.h \
   usb_desc.c usb_desc.h \
   ~/.platformio/packages/framework-arduinoteensy/cores/teensy4/
```

> ✅ Ce patch fait lire le flag `-D AUDIO_USB_CHANNEL_COUNT=6` qui est déjà dans le `platformio.ini` du projet.

### 3.4 — Nettoyer le cache PlatformIO

Dans VS Code avec le projet `TeensyPass` ouvert :
- Cliquer sur l'icône **PlatformIO** (maison) dans la barre latérale
- Aller dans **Project Tasks → teensy41 → General → Clean**

Ou en ligne de commande :
```bash
cd ~/Workspace/TeensyPass
~/.platformio/penv/bin/pio run -t clean
```

---

## ⚡ PARTIE 4 — Compiler et flasher la Teensy

### 4.1 — Compiler

Dans VS Code avec `TeensyPass` ouvert, cliquer sur **Build** (✓).  
Ou en ligne de commande :

```bash
cd ~/Workspace/TeensyPass
~/.platformio/penv/bin/pio run
```

### 4.2 — Flasher la Teensy

1. Brancher la Teensy en USB au Mac
2. Appuyer sur le **bouton physique** de la Teensy (elle entre en mode bootloader)
3. Dans VS Code, cliquer sur **Upload** (→) ou :

```bash
cd ~/Workspace/TeensyPass
~/.platformio/penv/bin/pio run -t upload
```

### 4.3 — Vérifier les 6 canaux

1. Ouvre l'app Mac **"Configuration audio et MIDI"** (`Cmd + Espace` → cherche "Configuration audio et MIDI")
2. Tu dois voir **"Teensy Audio"** dans la liste avec **6 entrées / 6 sorties @ 44100 Hz**

> 🔴 **Si tu ne vois que 2 canaux :** refaire l'étape 3 (patch). Vérifie bien que les fichiers ont été copiés dans le bon dossier. Si PlatformIO a été réinstallé, il faut refaire le patch.

---

## ⚡ PARTIE 5 — Compiler et flasher la Daisy

### 5.1 — Compiler les bibliothèques et le projet

Il faut compiler les bibliothèques **dans l'ordre**, en se plaçant dans chaque dossier :

```bash
# 1. Se placer dans le dossier racine du projet
cd ~/Workspace/TDM_DAISY

# 2. Compiler libDaisy (se place dans lib/libDaisy)
cd lib/libDaisy
make clean && make -j8

# 3. Compiler DaisySP (se place dans lib/DaisySP)
cd ../DaisySP
make clean && make -j8

# 4. Revenir à la racine et compiler le projet principal
cd ~/Workspace/TDM_DAISY
make clean && make -j8
```

> ⚠️ Si tu as des erreurs de compilation, voir la **section Dépannage**.

### 5.2 — Flasher la Daisy (mode BOOT_QSPI)

> **Note :** Le Makefile utilise `APP_TYPE = BOOT_QSPI`. Cela signifie que le firmware est flashé en mémoire QSPI externe via le **bootloader Daisy** (déjà présent en usine sur la puce). Ce n'est pas un flash DFU natif STM32 — c'est le bootloader Daisy qui reçoit le fichier.

**Première installation (ou si le bootloader a été effacé) :**
1. Brancher la Daisy au Mac via son câble USB
2. **Entrer en mode bootloader DFU** :
   - Appuie sur **BOOT** et **RESET** en même temps
   - Relâche **RESET** en premier, puis relâche **BOOT**
   - La Daisy apparaît alors comme périphérique `DFU` dans `dfu-util -l`
3. Flasher :

```bash
cd ~/Workspace/TDM_DAISY
make program-dfu
```

**Après la première installation — mises à jour suivantes (mode QSPI) :**

Une fois le bootloader Daisy actif, plus besoin de la manipulation BOOT+RESET. Il suffit d'appuyer sur **RESET seul** pour que la carte redémarre en mode bootloader et attende le nouveau firmware :

```bash
# Appuyer sur RESET sur la carte, puis immédiatement :
cd ~/Workspace/TDM_DAISY
make program-dfu
```

> ✅ **Comment vérifier que la carte est en mode bootloader ?**  
> Exécute `dfu-util -l` dans le terminal. Tu dois voir une ligne mentionnant `Found DFU: [0483:df11]`. Si cette ligne apparaît, la carte est prête à recevoir le firmware.

> ✅ Si le flash réussit, la Daisy redémarre automatiquement et une LED s'allume de façon stable.  
> 🔴 **Si `make program-dfu` échoue :** vérifie que `dfu-util` est installé (`dfu-util --version`) et que la carte est bien détectée (`dfu-util -l`).

---

## 🎵 PARTIE 6 — Installer et lancer la GUI Python

### 6.1 — Créer un environnement virtuel Python

```bash
cd ~/Workspace/GUI/Pedal

# Créer le venv avec Python 3.13 (avec Tkinter)
python3 -m venv venv_mac

# Activer le venv
source venv_mac/bin/activate
```

### 6.2 — Installer les dépendances

```bash
pip install customtkinter mido python-rtmidi
```

### 6.3 — Lancer la GUI

```bash
# (Depuis le dossier GUI/Pedal avec le venv activé)
python main_merge.py
```

> ✅ La fenêtre "Pédale Hexa - Contrôleur MIDI" doit s'ouvrir.  
> La GUI détecte automatiquement la Teensy via CoreMIDI (pas besoin de configurer quoi que ce soit).

### 6.4 — Pour les prochains lancements — éviter de réactiver le venv à chaque fois

Il n'est pas possible d'activer un venv «définitivement» (c'est par conception isolé au terminal courant), mais on peut créer un **alias ou un script de lancement** pour le faire en une seule commande.

**Option A — Alias permanent dans le terminal (recommandé)**

Ajouter cette ligne dans le fichier de configuration de ton shell :
```bash
echo 'alias gui="cd ~/Workspace/GUI/Pedal && source venv_mac/bin/activate && python main_merge.py"' >> ~/.zshrc
source ~/.zshrc
```

Ensuite, à chaque fois, il suffit de taper dans n'importe quel terminal :
```bash
gui
```

**Option B — Script de lancement (.command, double-cliquable depuis le Finder)**

```bash
cat > ~/Desktop/LancerGUI.command << 'EOF'
#!/bin/zsh
cd ~/Workspace/GUI/Pedal
source venv_mac/bin/activate
python main_merge.py
EOF
chmod +x ~/Desktop/LancerGUI.command
```

Ensuite, double-cliquer sur **LancerGUI.command** sur le Bureau suffit à lancer la GUI directement, sans ouvrir de terminal.

---

## 🎚️ PARTIE 7 — Configurer Reaper pour le routage hexaphonique

### 7.1 — Installer Reaper

Télécharger sur : https://www.reaper.fm/download.php  
(Version d'évaluation gratuite 60 jours, puis ~60€ pour une licence personnelle)

### 7.2 — Créer un "Périphérique Agrégé" macOS

> ⚠️ **Obligatoire !** Cela permet à Reaper d'utiliser **à la fois** la Teensy (6 canaux audio) et les haut-parleurs du Mac (2 canaux) en même temps.

1. Ouvrir **Configuration audio et MIDI**
2. Cliquer sur **`+`** en bas à gauche → **"Créer un périphérique agrégé"**
3. Cocher **Teensy Audio** puis **Haut-parleurs du MacBook** (dans cet ordre)
4. Nommer le périphérique : `Teensy + HP`
5. Activer **"Correction de dérive"** sur la Teensy Audio

> 📌 L'ordre compte : la Teensy aura les canaux 1-6, les haut-parleurs les canaux 7-8.

### 7.3 — Configurer Reaper

1. Ouvrir Reaper → Préférences (`Cmd + ,`) → **Audio → Device**
2. Décocher **"Allow use of different input and output devices"**
3. Dans **"Audio Device"**, choisir ton **périphérique agrégé** (Teensy + HP)
4. **Cocher "Request sample rate"** et mettre **44100**
5. Mettre **First: 1** / **Last: 8** pour les entrées et sorties
6. Appliquer

### 7.4 — Créer le routage dans Reaper

**Piste 1 — Envoi du .wav hexaphonique vers la Teensy :**
1. Glisser ton fichier `.wav` hexaphonique (6 canaux, 44100 Hz) sur une piste
2. Cliquer sur **ROUTE** de cette piste
3. **Décocher "Master send"**
4. Dans **"Hardware Outputs"** → ajouter → choisir canaux **1 à 6** (Teensy)
5. Source: 1/2, 3/4, 5/6 → Destination: 1/2, 3/4, 5/6

**Pistes 2 à 7 — Réception du son traité par la Daisy :**
1. Créer 6 pistes vides
2. Pour chaque piste, mettre l'**entrée sur Mono Input 1, 2, 3, 4, 5, 6** (un canal par piste)
3. Activer le bouton rouge **Record Arm** sur chaque piste
4. Activer le **Record Monitoring** (bouton haut-parleur) sur chaque piste

**Piste MASTER :**
1. Cliquer sur **ROUTE** du Master
2. Dans **"Hardware Outputs"**, choisir les canaux **7/8** (haut-parleurs du Mac)

### 7.5 — Tester

1. Vérifier que la Teensy et la Daisy sont bien branchées et flashées
2. Vérifier le câblage TDM
3. Appuyer sur **Play** dans Reaper
4. Les VU-mètres de la piste `.wav` doivent bouger
5. Les VU-mètres des pistes de retour (2-7) doivent bouger si la Daisy traite le son

---

## 🔴 DÉPANNAGE — Problèmes fréquents et solutions

### ❌ "python : command not found"

```bash
# Sur Mac, c'est python3, pas python
python3 main_merge.py

# Ou créer un alias permanent
echo 'alias python=python3' >> ~/.zshrc && source ~/.zshrc
```

### ❌ "source: no such file or directory: venv/bin/activate"

Le venv s'appelle `venv_mac`, pas `venv` :
```bash
source venv_mac/bin/activate
```

### ❌ Tkinter ne s'importe pas / GUI ne s'ouvre pas

Le Python d'App Store ou python.org n'inclut pas Tkinter. Utiliser celui de Homebrew :
```bash
deactivate             # désactiver le venv si actif
rm -rf venv_mac        # supprimer l'ancien venv

brew install python-tk@3.13

/opt/homebrew/opt/python@3.13/bin/python3 -m venv venv_mac
source venv_mac/bin/activate
pip install customtkinter mido python-rtmidi
python main_merge.py
```

### ❌ La Teensy n'apparaît qu'avec 2 canaux dans macOS

Le patch 6 canaux n'a pas été appliqué correctement. Refaire l'étape 3 :
1. Vérifier que les fichiers patchés sont dans `~/.platformio/packages/framework-arduinoteensy/cores/teensy4/`
2. Ouvrir le projet `TeensyPass` dans VS Code, faire un **Clean** puis un **Build** complet
3. **Reflasher** la Teensy

### ❌ Erreur "make program-dfu" pour la Daisy

```bash
# Vérifier que la Daisy est bien en mode bootloader
dfu-util -l   # doit lister la Daisy STM32

# Essayer avec dfu-util directement
dfu-util -a 0 -s 0x08000000:leave -D build/daisy_tdm_hexa.bin
```

> ⚠️ Pour le mode bootloader : tenir **BOOT + RESET**, relâcher **RESET** d'abord, puis **BOOT**.

### ❌ Larsen / feedback dans Reaper

Le micro du Mac est activé et crée une boucle. Solution :
1. Ouvrir **Configuration audio et MIDI**
2. Cliquer sur ton **Périphérique Agrégé**
3. S'assurer que le **microphone du Mac n'est PAS coché** dans les entrées
4. Seule la **Teensy Audio** doit être en entrée

### ❌ Les VU-mètres des pistes de retour ne bougent pas

Vérifier dans l'ordre :
1. **Câble TDM** bien branché (les 4 fils + GND)
2. **Daisy bien flashée** avec le bon firmware (`make program-dfu` réussi)
3. **Sample rate** : dans "Configuration audio et MIDI", la Teensy doit être à **44100 Hz**
4. **Record Monitoring** activé sur les pistes de retour (bouton haut-parleur)
5. Redémarrer le Mac (souvent la solution magique quand CoreAudio est capricieux)

### ❌ La GUI ne trouve pas de port MIDI

```bash
# Vérifier que la Teensy est reconnue comme device MIDI
python3 -c "import mido; print(mido.get_output_names())"
# Doit lister "Teensy MIDI" ou similaire
```

Si rien n'apparaît :
- Vérifier que le firmware Teensy est flashé avec l'option `USB_MIDI_AUDIO_SERIAL`
- Débrancher/rebrancher la Teensy

### ❌ Erreurs de compilation Daisy (sous-modules manquants)

```bash
cd ~/Workspace/TDM_DAISY
git submodule update --init --recursive
```

### ❌ PlatformIO réinstallé (patch 6 canaux perdu)

Si tu réinstalle PlatformIO ou VS Code, le patch 6 canaux est perdu. Refaire l'étape 3.  
Le dossier `teensy4_backup` sert de référence si besoin de revenir à l'original.

---

## ✅ Checklist finale — "Tout est prêt ?"

- [ ] Homebrew installé
- [ ] Python 3.13 + Tkinter installé via Homebrew
- [ ] VS Code + PlatformIO installés
- [ ] arm-none-eabi-gcc + make + dfu-util installés
- [ ] Les 4 repos clonés avec `--recursive`
- [ ] Patch 6 canaux appliqué dans le framework PlatformIO
- [ ] **Teensy flashée** → apparaît en 6 canaux dans "Configuration audio et MIDI"
- [ ] **Daisy flashée** → LED stable
- [ ] Câblage TDM OK (5 fils)
- [ ] Périphérique Agrégé créé dans macOS (Teensy + HP, 44100 Hz)
- [ ] Reaper configuré avec le périphérique agrégé et les bonnes pistes
- [ ] GUI Python lancée, port MIDI Teensy détecté
- [ ] **Test final** : Play dans Reaper → VU-mètres sur toutes les pistes → Son dans les haut-parleurs

---

## 📁 Liens des dépôts

| Projet | Rôle | GitHub |
|---|---|---|
| **TeensyPass** | ✅ Firmware Teensy (interface USB ↔ TDM) | https://github.com/Desmatheo/TeensyPass |
| **TDM_DAISY** | ✅ Firmware Daisy (effets DSP) | https://github.com/Desmatheo/TDM_DAISY |
| **GUI** | ✅ Interface Python MIDI | https://github.com/Desmatheo/GUI |
| **TDM_TEENSY** | ℹ️ Projet séparé, non utilisé ici | https://github.com/Desmatheo/TDM_TEENSY |
| **Patch 6 canaux Teensy** | ✅ Obligatoire pour avoir 6 canaux USB | https://github.com/alex6679/teensy-4-usbAudio |

---

*Guide rédigé le 14 août 2026 — basé sur les sessions de mise en place et de dépannage réelles sur Mac.*
