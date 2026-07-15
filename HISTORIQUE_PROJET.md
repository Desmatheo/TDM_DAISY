# Historique des Travaux - Projet TDM Daisy

Ce document retrace les différentes étapes de recherche, d'expérimentation et de développement réalisées sur le projet. Il sert de mémoire technique pour ne pas perdre le contexte des décisions passées.

## Début Juillet 2026 : Phase d'Expérimentation et de Diagnostic

### 1. Validation Audio et Protocole MIDI
- **Contexte :** Mise en communication matérielle et logicielle de la Teensy (`TDM2`) et de la Daisy Seed (`TDM`).
- **Travaux réalisés :** 
  - Résolution d'un bug dans le code de la pédale (GUI) où le protocole MIDI était désactivé par inadvertance à cause de macros.
  - Validation de l'envoi de signaux de contrôle depuis l'interface Python vers la Daisy via le câble USB principal (MIDI).
  - Tests d'enregistrements audio (`test1.wav`, `test2.wav`) issus du capteur hexaphonique.
- **Conclusions :** Identification physique du phénomène de diaphonie ("bleeding") entre les cordes. L'architecture de base (Audio + MIDI via USB) est validée et fonctionnelle.

### 2. Tentative de Monitoring des Performances (Double USB / UART)
- **Problème :** Apparition d'artéfacts sonores importants (saturation, bruits numériques) lors de l'activation d'effets lourds. Suspicion d'une surcharge processeur (CPU) de la Daisy.
- **Travaux réalisés :**
  - Essai de mise en place d'un second canal de communication vers le PC pour monitorer la charge CPU en temps réel, sans couper le flux MIDI.
  - Tests d'implémentation d'un port USB virtuel secondaire (USB 2). Cela a généré de nombreux conflits, des erreurs de flashage (`make program-dfu`, `dfu-util`) et une instabilité globale du système.
  - Étude théorique de l'utilisation d'un adaptateur externe USB-UART (FT232RL) branché sur la breadboard.
- **Conclusions :** La piste du double USB logiciel a été **définitivement abandonnée** car trop instable. Le code a été nettoyé de tous ces essais ("rollback") pour conserver un socle 100% MIDI très stable.

### 3. Diagnostic des Crashs DSP et Qualité des Effets
- **Problème :** Plantage total de la carte Daisy lorsque l'effet Octaveur (`Earth`) est réglé sur le mode "-2 octaves".
- **Travaux réalisés :**
  - Analyse approfondie du code DSP de l'effet.
- **Conclusions & Diagnostics :** 
  - **Crash Earth :** Une erreur mathématique (division par zéro générant un `NaN`) a été identifiée dans `OctaveGenerator.h`. Le code essayait de calculer l'octave -2 sans avoir préalablement calculé et initialisé l'octave -1.
  - **Amélioration Delay :** Il a été constaté que le Delay n'utilisait pas correctement sa variable de mixage (`dryMix`), et qu'un filtre de type analogique (`Tone`) prévu pour réchauffer les répétitions n'était jamais appelé dans la boucle de calcul.

---

## Prochaines Étapes (À partir du 9 Juillet 2026)
1. **Corriger le bug mathématique** dans `OctaveGenerator.h` pour réparer l'octaveur.
2. **Améliorer la qualité du Delay** en activant son filtre passe-bas et en corrigeant son mixage.
3. **Continuer l'optimisation DSP** pour soulager le CPU et éliminer les artéfacts audio résiduels.
