# RP2040VCO
Modified version of the RP2040 based VCO design by Hagiwo

This repository contains my modified code and schematics for the RP2040zero based VCO
esigned by Hagiwo - https://note.com/solder_state/n/nca6a1dec3921

Key differences from the original hardware are the use of op-amp mixers on the CV and
modulation inputs so only 2 analogue inputs are used on the RP2040 board (which means
an original Raspberry Pi Pico could be used) and a DC coupled output circuit using an
additional op-amp stage to mix in the required DC offset voltage.

The code is modified to:
- compile with the Seeed XIAO RP2040 or Arduino Mbed OS RP2040 cores in the Arduino IDE, instead of the earlephilhower/arduino-pico core
- run using just two analogue inputs and ensure that there is no bleed-through of modulation into the frequency input
- use the on-board neopixel LED to indicate which waveform is selected
- apply PWM modulation to square wave output rather than attempting wavefolding
- attempt to debounce the waveform select button in software (not yet 100% reliable!)
