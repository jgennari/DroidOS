# DroidOS

![Board](https://github.com/jgennari/DroidOS/blob/master/hardware/Board.png?raw=true "DroidOS Board")

DroidOS is a Arduino based operating system for community built R2 units. It uses an SBUS-based RC receiver and an Arduino microcontroller to operate any number of extra functions on your droid.

## Bring your droid to life!

The DroidOS got us started in the right direction, however this project is just the starting point for us. It allows us to control lights and sound from the RC controller, but you can build anything on top of it. It's based on the Teensy LC development kit.

## Startup Sequence:  
The startup lives in the Arduino `setup` procedure. It follows this procedure:

* Initiate USB serial for debugging
* Initialize MP3 Player
* Initialize SBUS
* Confirm SBUS is communicating
* Check Mode Switch

During the setup, the status LED on the Teensy will be solid.

Any error will be annunciated via the status LED on the board. Short bursts indicate the failure mode, followed by no light for 1.5 seconds, repeated 3 times.

1. MP3 player initaliztion failed
2. Card online failed
3. Failed to count MP3 files
4. SBUS init failed, no serial control

Upon failure and notification the system will reset and re-enter the startup sequence.

## SBUS:
The SBUS library was sourced & slightly modified from [zendes](https://github.com/zendes/SBUS). We are using an FrSky X4R & X8R and are using inverted serial setup in Arduino. Once SBUS is connected and the main loop is running, any change to the SBUS first 6 channels will trigger a brief flash of the Teensy status LED.

## Questions/suggestions
If you have any questions or suggestions please feel free to reach out via GitHub or submit and issue.
