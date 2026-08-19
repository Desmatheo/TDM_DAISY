import wave
import struct
import math

# Paramètres du fichier audio
duration_seconds = 15  # Durée du sweep (15 secondes suffit largement)
sample_rate = 44100    # Taux d'échantillonnage standard
num_channels = 6       # 6 cordes (Hexaphonique)

# Plage du balayage (Sweep)
freq_start = 20.0      # Départ dans les graves
freq_end = 20000.0     # Fin dans les extrêmes aigus
amplitude = 16000      # Amplitude (volume), max est 32767 pour 16-bit

output_filename = "hexa_sweep_test.wav"

print(f"Génération du Sine Sweep {output_filename} ({duration_seconds} secondes, {num_channels} canaux)...")
print(f"Balayage de {freq_start} Hz à {freq_end} Hz.")

# Ouverture du fichier WAV en écriture
with wave.open(output_filename, 'w') as wav_file:
    # Paramètres : nchannels, sampwidth (en octets), framerate, nframes, comptype, compname
    wav_file.setparams((num_channels, 2, sample_rate, 0, 'NONE', 'not compressed'))
    
    total_frames = duration_seconds * sample_rate
    
    # Génération frame par frame
    for i in range(total_frames):
        frame_data = b''
        t = float(i) / sample_rate
        
        # Formule mathématique de la phase pour un sweep linéaire parfait :
        # phi(t) = 2 * pi * (f_start * t + (f_end - f_start) / (2 * Duree) * t^2)
        phase = 2.0 * math.pi * (freq_start * t + ((freq_end - freq_start) / (2.0 * duration_seconds)) * t * t)
        
        # Calcul de la valeur
        value = int(amplitude * math.sin(phase))
        
        # On injecte le MÊME sweep sur les 6 canaux pour être sûr que ça marche sur toutes les cordes
        for ch in range(num_channels):
            frame_data += struct.pack('<h', value)
            
        wav_file.writeframes(frame_data)

print("Terminé ! Tu peux maintenant utiliser ce fichier pour visualiser l'aliasing dans Reaper.")
