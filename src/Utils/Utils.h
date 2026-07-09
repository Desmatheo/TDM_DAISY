#pragma once 

// /!\ Attention, si on utilise le MIDI, l'usb servira qu'a ça. Faudra un adaptateur si on veut faire du seriamessaginggneial
#define SerialMessagingGenial 0

#if SerialMessagingGenial
#define SerialPeaking 0
#define CPU_METER 1
#else
#define MIDI_USB_DE_LA_MORT 0
#endif


