#pragma once 

// /!\ Attention, si on utilise le MIDI, l'usb servira qu'a ça. Faudra un adaptateur si on veut faire du serial logging.
#define ENABLE_SERIAL_LOGGING 1

#if ENABLE_SERIAL_LOGGING
#define SerialPeaking 0
#define CPU_METER 1
#define USE_MIDI_USB 1
#else
#endif
