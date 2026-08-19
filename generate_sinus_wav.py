import wave
import struct
import math

# Paramètres du fichier audio
duration_seconds = 60  # Durée du test : 1 minute
sample_rate = 44100    # Taux d'échantillonnage standard
num_channels = 6       # 6 cordes (Hexaphonique)

# Fréquences pour chaque corde (simili accord de guitare, mais une octave plus aiguë !)
frequencies = [164.82, 220.00, 293.66, 392.00, 493.88, 659.26]
amplitude = 16000  # Amplitude (volume), max est 32767 pour 16-bit

output_filename = "hexa_aigu_test.wav"

print(f"Génération de {output_filename} ({duration_seconds} secondes, {num_channels} canaux)...")

# Ouverture du fichier WAV en écriture
with wave.open(output_filename, 'w') as wav_file:
    # Paramètres : nchannels, sampwidth (en octets), framerate, nframes, comptype, compname
    wav_file.setparams((num_channels, 2, sample_rate, 0, 'NONE', 'not compressed'))
    
    total_frames = duration_seconds * sample_rate
    
    # Génération frame par frame
    for i in range(total_frames):
        frame_data = b''
        t = float(i) / sample_rate
        
        for ch in range(num_channels):
            # Calcul de l'onde sinusoïdale pure
            value = int(amplitude * math.sin(2.0 * math.pi * frequencies[ch] * t))
            # Ajout de la valeur 16-bit (short) au frame (little endian)
            frame_data += struct.pack('<h', value)
            
        wav_file.writeframes(frame_data)

print("Terminé ! Tu peux maintenant utiliser ce fichier pour tes tests.")
