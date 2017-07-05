# DroidOS

DroidOS is a Arduino based operating system for community built R2 units. It uses an SBUS-based RC receiver and an Arduino microcontroller to operate any number of extra functions on your droid.

## Bring your droid to life!

The DroidOS got us started in the right direction, however this project is just the starting point for us. It allows us to control lights and sound from the RC controller, but you can build anything on top of it. It's based on the Particle.io Electron cellular-connected Arduino-compatible development kit.

## Startup Sequence:  
The startup lives in the Arduino `setup` procedure. It follows this procedure:

* Initiate USB serial for debugging
* Initialize MP3 Player
* Initialize SBUS
* Confirm SBUS is communicating
* Check Mode Switch

If any step fails (except the first two), an error will be annunciated via the MP3 player.

## Mode Switch:
The mode switch allows for arming and disarming of the main program loop to allow for debugging. When the 3-position mode switch (as defined in the sketch) is in `up` position, the droid is disarmed. When in the `middle` position, the droid is armed and the main loop will respond to input. When the switch is in the `down` position, the droid will initiate it's communication loop and connect to the Particle.io cloud.

## SBUS:
The SBUS library was sourced from someone on GitHub. We are using an FrSky X4R and pulling the inverted SBUS signal off of one of the through-hole pads on the board. 

## Questions/suggestions
If you have any questions or suggestions please feel free to reach out via GitHub or submit and issue.
