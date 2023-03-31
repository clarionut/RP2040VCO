/*
  Modified code for the RP2040 zero based VCO by Hagiwo (https://note.com/solder_state/n/nca6a1dec3921)

  Key changes are:
    Pin assignments modified to facilitate stripboard layout

    Waveform select button is debounced

    The RP2040's on-board Neopixel is used to indicate the selected waveform

    Only two analogue inputs are used. The frequency pot, V/Oct input
    and pitch CV inputs are mixed externally as are the modulation pot
    and modulation CV inputs. This requires an extra op-amp but allows
    both +ve and -ve CV inputs to be used. The combined voltages are
    clamped to 0 - 3.3V before going to the RP2040

    The initialisation of the voctpow[] array has been moved to a separate
    file (RP2040_VCO.h) to make editing the main code easier
*/

#include <hardware/pwm.h>
#include <pico/multicore.h>
#include "RP2040_VCO.h"
#include <Adafruit_NeoPixel.h>

int k = 0;
int slice_num = 0;
int Voct, freq_pot, oct_sw, mode, old_mode;
int waveform = 0;
int wavetable[256];       // 1024 bit resolution, 256 samples
int mod2_wavetable[256];  // 2nd modulated wavetable
float mod_wavetable[256]; // 1st modulated wavetable
float f0 = 30.;           // base osc frequency (was 35.) - needs to be below C1 to allow tuning
float f = 0;
float osc_freq = 0;
float mod = 0;
float calb = 1.08; // 1.018;       // calibration to match CV input buffer (orig. 1.165)
float freq_table[2048];
const float PIx2 = M_PI * 2;
bool push_sw = true, old_push_sw = true;
unsigned long push_sw_counter, debounce_counter = 0;
unsigned long timer;      // for AM sinewave

Adafruit_NeoPixel pixel(1, 16, NEO_GRB + NEO_KHZ800);
int colour[][3] = { {100,   0,   0}, // red
                    { 88,  12,   0}, // orange
                    { 65,  35,   0}, // yellow
                    {  0, 100,   0}, // green
                    {  0,  70,  20}, // cyan
                    {  0,   0, 100}, // blue
                    { 40,   0,  40}, // magenta
                    { 45,  40,  35}  // white
                  };

void setup() {
  pinMode(0, INPUT_PULLUP); // octave select
  pinMode(1, INPUT_PULLUP); // octave select
  pinMode(2, INPUT_PULLUP); // mode select
  pinMode(3, INPUT_PULLUP); // mode select
  pinMode(4, INPUT_PULLUP); // push sw

  //Serial.begin(115200);
  multicore_launch_core1(loop1);

  timer = micros();
  //push_sw_millis = millis();

  old_mode = -1;
  mode_select();
  wavetable_setup();

  //-------------------octave select-------------------------------
  for (int i = 0; i < 1230; i++) {
    // Covers 6 (=1230) octaves. > 1230 leads to unstable operation
    freq_table[i] = f0 * pow(2, voctpow[i]);
  }
  for (int i = 1230; i < 2048; i++) {
    freq_table[i] = 6;
  }

  //-------------------PWM setting-------------------------------
  gpio_set_function(6, GPIO_FUNC_PWM);  // set GP6 function PWM
  slice_num = pwm_gpio_to_slice_num(6); // GP6 PWM slice

  pwm_clear_irq(slice_num);
  pwm_set_irq_enabled(slice_num, true);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
  irq_set_enabled(PWM_IRQ_WRAP, true);

  // set PWM frequency
  pwm_set_clkdiv(slice_num, 1);     // = sysclock / ((resolution + 1) * frequency)
  pwm_set_wrap(slice_num, 1023);    // resolution
  pwm_set_enabled(slice_num, true); // PWM output enable

  pixel.begin();
  pixel.setPixelColor(0, pixel.Color(colour[0][0], colour[0][1], colour[0][2]));
  pixel.show();

}

void on_pwm_wrap() {
  pwm_clear_irq(slice_num);
  f = f + osc_freq;
  if (f > 255) {
    f = 0;
  }
  pwm_set_chan_level(slice_num, PWM_CHAN_A, mod2_wavetable[(int) f] + 511);
}

void loop() {
  ++debounce_counter;
  //-------------------octave select----------------------------
  switch ((digitalRead(1) << 1) + digitalRead(0)) {
    case 3:
      oct_sw = 2;
      break;
    case 2:
      oct_sw = 4;
      break;
    case 1:
      oct_sw = 1;
      break;
  }

  mode_select();

  //  -------------------frequency calculation------------------
  // pin 28 is combined frequency CV input
  Voct = analogRead(28) * calb;       // convert input voltage range
  Voct = (Voct > 1225) ? 1225 : Voct; // Covers 6 (=1220) octaves. > 1230 leads to unstable operation
  osc_freq = 256.0 * freq_table[Voct] / 122070.0 * (float) oct_sw;

  //  -------------------modulation parameter-------------------
  // pin 27 is combined modulation CV input
  int tmpmod = analogRead(27);
  analogRead(28); // extra read of the frequency CV input to stabilise the internal multiplexer
  tmpmod = (tmpmod > 1023) ? 1023 : tmpmod;
  switch (mode) {
    case 0:
      // wavefold (or PWM for square)
      if (2 != waveform) {
        mod = (float) tmpmod * 0.0036 + 0.90;
      } else {
        // scale for PWM
        mod = tmpmod >> 3;
      }
      break;
    case 1:
      // FM
      mod = tmpmod >> 3;
      break;
    case 2:
      // AM
      mod = 1023 - tmpmod;
      break;
  }

  //  -------------------push switch----------------------------
  push_sw = digitalRead(4);
  if ((push_sw != old_push_sw) && ((debounce_counter - push_sw_counter) > 200)) {
    // push switch has changed state outside the debounce period
    if (0 == push_sw) {
      // push switch is now pressed so change waveform
      waveform = (waveform < 7) ? waveform + 1 : 0;
      if (0 == mode || 2 == mode) {
        wavetable_setup();
      }
      //Serial.println(waveform);
      // set the neopixel colour
      pixel.clear();
      pixel.setPixelColor(0, pixel.Color(colour[waveform][0], colour[waveform][1], colour[waveform][2]));
      pixel.show();
    }
    push_sw_counter = debounce_counter;
    //push_sw_millis = millis();
    old_push_sw = push_sw;
  }
}

void loop1() {
  // modulation
  while (1) {
    if (0 == mode) { // wavefold
      if (2 == waveform) {
        // PWM rather than wavefolding for square wave
        for (int i = 0; i < 128 + mod; i++) {
          mod2_wavetable[i] = 511;
        }
        for (int i = 128 + mod; i < 256; i++) {
          mod2_wavetable[i] = -511;
        }
      } else {
        for (int i = 0; i < 256; i++) {
          mod_wavetable[i] = wavetable[i] * mod;
        }
        for (int i = 0; i < 256; i++) {
          // fold
          if (mod_wavetable[i] > 511 && mod_wavetable[i] < 1535) { // 1023 + 512
            mod2_wavetable[i] = 1024 - mod_wavetable[i];
  
          } else if (mod_wavetable[i] < -512 && mod_wavetable[i] > -1536) { // -1024 - 512
            mod2_wavetable[i] = -1023 - mod_wavetable[i];
  
          } else if (mod_wavetable[i] < -1535) { // -1024 - 511
            mod2_wavetable[i] = 2048 + mod_wavetable[i];
  
          } else if (mod_wavetable[i] > 1534) { // 1023 + 511
            mod2_wavetable[i] = mod_wavetable[i] - 2047;
  
          } else {
            mod2_wavetable[i] = mod_wavetable[i];
          }
        }
      }
    }

    else if (1 == mode) { // FM
      switch (waveform) {
        case 0:
          // FM1
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = (sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 3 * i / 256))) * 511;
          }
          break;
  
        case 1:
          // FM2
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = (sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 3 * i / 256 + mod / 128 * sin(PIx2 * 3 * i / 256)))) * 511;
          }
          break;
  
        case 2:
          // FM3
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = (sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 5 * i / 256 + mod / 128 * sin(PIx2 * 7 * i / 256)))) * 511;
          }
          break;
  
        case 3:
          // FM4
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = (sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 9 * i / 256 + mod / 128 * sin(PIx2 * 5 * i / 256)))) * 511;
          }
          break;
  
        case 4:
          // FM5
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = ((sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 19 * i / 256))) + (sin(PIx2 * 3 * i / 256 + mod / 128 * sin(PIx2 * 7 * i / 256)))) / 2 * 511;
          }
          break;
  
        case 5:
          // FM6
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = ((sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 7 * i / 124))) + (sin(PIx2 * 9 * i / 368 + mod / 128 * sin(PIx2 * 11 * i / 256)))) * 511;
          }
          break;
  
        case 6:
          // FM7
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = ((sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 13 * i / 256))) + (sin(PIx2 * 17 * i / 111 + mod / 128 * sin(PIx2 * 19 * i / 89)))) * 511;
          }
          break;
  
        case 7:
          // FM8
          for (int i = 0; i < 256; i++) {
            mod2_wavetable[i] = (sin(PIx2 * i / 256 + mod / 128 * sin(PIx2 * 11 * i / 124 + mod / 128 * sin(PIx2 * 7 * i / 333 + mod / 128 * sin(PIx2 * 9 * i / 56)))))  * 511;
          }
          break;
      }
    }
  
    else if (2 == mode) {
      // AM
      // N.B. the timing of this block is dependent on the execution time of the contained code
      //if (timer + mod <= micros()) {
      if (micros() - timer >= (long) mod) {
        k = (k < 63) ? k + 1 : 0;
        float sinVal = sin(PIx2 * k / 63);
        for (int i = 0; i < 255; i++) { // was 255?
          //mod2_wavetable[i] = wavetable[i] * sin(PIx2 * k / 63); // multiply by AM sine wave
          mod2_wavetable[i] = wavetable[i] * sinVal; // multiply by AM sine wave
        }
        timer = micros();
        //timer += mod;
      }
    }
  }
}

void wavetable_setup() {
  // make wavetable
  if (0 == mode) {
    // wavefold
    switch (waveform) {
      case 0:
        // saw
        for (int i = 0; i < 256; i++) {
          wavetable[i] = i * 4 - 512;
        }
        break;

      case 1:
        // sine
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256)) * 511;
        }
        break;

      case 2:
        // square
        for (int i = 0; i < 128; i++) {
          wavetable[i] = 511;
          wavetable[i + 128] = -511;
        }
        break;

      case 3:
        // triangle
        for (int i = 0; i < 128; i++) {
          wavetable[i] = i * 8 - 511;
          wavetable[i + 128] = 511 - i * 8;
        }
        break;

      case 4:
        // octave saw
        for (int i = 0; i < 128; i++) {
          wavetable[i] = i * 4 - 512 + i * 2;
          wavetable[i + 128] = i * 2 - 256 + i * 4;
        }
        break;

      case 5:
        // FM1
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 3 * i / 256))) * 511;
        }
        break;

      case 6:
        // FM2
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 7 * i / 256))) * 511;
        }
        break;

      case 7:
        // FM3
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 4 * i / 256 + sin(PIx2 * 11 * i / 256)))) * 511;
        }
        break;
    }
  }

  else if (2 == mode) {
    // AM
    switch (waveform) {
      case 0:
        // sine
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256)) * 511;
        }
        break;

      case 1:
        // FM1
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 3 * i / 256))) * 511;
        }
        break;

      case 2:
        // FM2
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 5 * i / 256))) * 511;
        }
        break;

      case 3:
        // FM3
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 4 * i / 256 + sin(PIx2 * 11 * i / 256)))) * 511;
        }
        break;

      case 4:
        // non-integer multiplets FM1
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 1.28 * i / 256))) * 511;
        }
        break;

      case 5:
        // non-integer multiplets FM2
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 3.19 * i / 256))) * 511;
        }
        break;

      case 6:
        // non-integer multiplets FM3
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 2.3 * i / 256 + sin(PIx2 * 7.3 * i / 256)))) * 511;
        }
        break;

      case 7:
        // non-integer multiplets FM4
        for (int i = 0; i < 256; i++) {
          wavetable[i] = (sin(PIx2 * i / 256 + sin(PIx2 * 6.3 * i / 256 + sin(PIx2 * 11.3 * i / 256)))) * 511;
        }
        break;
    }
  }
}

void mode_select(void) {
  //-------------------mode select------------------------------
  switch ((digitalRead(3) << 1) + digitalRead(2)) {
    case 3:
      // FM
      mode = 1;
      break;
    case 2:
      // AM
      mode = 2;
      break;
    case 1:
      // wavefolder
      mode = 0;
      break;
  }
  if (mode != old_mode) {
    if (push_sw) {
    // Push switch is NOT held down so change the waveform 
    // (when switching between wavefolder and AM)
      wavetable_setup();
    }
    old_mode = mode;
  }
}
