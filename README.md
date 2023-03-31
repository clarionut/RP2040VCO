# RP2040VCO
Modified version of the RP2040zero based VCO design by Hagiwo

This repository contains my modified code and schematics for the RP2040zero based VCO 
designed by Hagiwo - https://note.com/solder_state/n/nca6a1dec3921

Key differences from the original hardware are the use of op-amp mixers on the CV and
modulation inputs so only 2 analogue ports are used on the RP2040 board (which means
an original Raspberry Pi Pico could be used) and a DC coupled output circuit using an
additional op-amp stage to mix in the required DC offset voltage.

The code is modified to:
- compile with the Seeed XIAO RP2040 or Arduino Mbed OS RP2040 cores in the Arduino IDE, instead of the earlephilhower/arduino-pico core
- be more manageable by moving the large voctpow[] array definition into a separate header file
- run using just two analogue inputs and ensure that there is no bleed-through of modulation input into the VCO frequency due to a lack of MUX settling time
- use the on-board neopixel LED to indicate which waveform is selected
- apply PWM modulation to square wave output rather than attempting wavefolding
- attempt to debounce the waveform select button in software (not yet 100% reliable!)

The schematics folder contains the modified circuit diagram, the stripboard layout I 
used to build my version of the VCO and a Zip archive containing a Kicad project of 
the circuit diagram and a proposed PCB layout. The schematic shows the input sockets
and potentiometers but not the switches or output socket. I like to have an accessible
reset switch for microcontroller-based modules but there is no reset line broken out
on the RP2040zero board I used. The PCB design and stripboard layout allow for a 
flying lead from the main board to the reset button on the RP2040zero.

Note that I have NOT built the VCO using the Kicad PCB layout, so be sure to check it
thoroughly before use!

