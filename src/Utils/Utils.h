#pragma once 

// /!\ Attention, si on utilise le MIDI, l'usb servira qu'a ça. Faudra un adaptateur si on veut faire du seriamessaginggneial
#define SerialMessagingGenial 1

#if SerialMessagingGenial
#define SerialPeaking 0
#define CPU_METER 1
#define MIDI_USB_DE_LA_MORT 1
#else
#endif


static inline float clampf(float value, float min, float max){
    return (value < min) ? min : (value > max) ? max : value;
}