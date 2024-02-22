// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.
// https://github.com/SpenceKonde/ATTinyCore/blob/master/avr/extras/tinyNeoPixel.md
// https://github.com/SpenceKonde/ATTinyCore/blob/master/avr/libraries/tinyNeoPixel_Static/examples/simple/simple.ino
// https://github.com/SpenceKonde/ATTinyCore/blob/master/avr/libraries/tinyNeoPixel_Static/examples/buttoncycler/buttoncycler.ino
// https://forum.arduino.cc/t/how-to-clone-multidimensional-array-to-another-array-arduino/642747/5

#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <tinyNeoPixel_Static.h>

// ========================= Constants =========================

// Digital IO pin connected to the button. This will be
// driven with a pull-up resistor so the switch should
// pull the pin to ground momentarily. On a high -> low
// transition the button press logic will execute.
// Pin 5 doesn't work, as it is also a RESETn pin.
#define BUTTON_PIN 3
#define PIXEL_PIN 0

const byte debounce_delay = 20;
// TODO: add modes with different transition rates
const byte fade_delay = 20;				// duration of each fading step, ms
const byte fade_step_count = 32;		// 20*32 = 640 ms for each color transition
const byte dim = 4;						// global brightness reduction divider
const byte mode_count = 3;				// [0..2]: off, LGBT flag, trans flag

// TODO: put color tables in PROGMEM; maybe, also merge them into one array (or better, a struct)
const byte EmptyColors[1][3] = {{0, 0, 0}};
const byte LgbtFlagColors[7][3] = {{228, 3, 3},
								   {255, 140, 0},
								   {255, 237, 0},
								   {0, 128, 38},
								   {0, 77, 255},
								   {117, 7, 135},
								   {0, 0, 0}};
const byte TransFlagColors[6][3] = {{0x5B, 0xCE, 0xFA},
									{0xF5, 0xA9, 0xB8},
									{0xFF, 0xFF, 0xFF},
									{0xF5, 0xA9, 0xB8},
									{0x5B, 0xCE, 0xFA},
									{0, 0, 0}};
/*
// Test the whiteness uniformity across brightness levels
// TODO: move to a separate sketch without crossfades
const byte WhiteColors[5][3] = {//{16, 16, 16},
								//{32, 32, 32},
								{64, 64, 64},
								{128, 128, 128},
								{192, 192, 192},
								{255, 255, 255},
								{0, 0, 0}};
*/
extern const uint8_t gamma8[];			// gamma correction table is at the botton of the sketch

// ========================= Variables =========================

byte color_count;						// number of colors in the current flag
const byte (*pcolors)[3] = nullptr;		// pointer to the 1st element of current flag, https://stackoverflow.com/q/1052818/4773752
byte subpxls_prev[] = {0, 0, 0};		// RGB components of the previous (fading out) color
byte subpxls[] = {0, 0, 0};				// RGB components of the current (fading in) color

// Init required by tinyNeoPixel_Static
tinyNeoPixel leds = tinyNeoPixel(1, PIXEL_PIN, NEO_GRB, subpxls);

bool btn_state_prev = HIGH;
bool btn_state = HIGH;

byte mode = 0;
bool was_mode_changed = false;

// ========================= Setup =========================

// Configure an interrupt service routine (ISR)
EMPTY_INTERRUPT(PCINT0_vect);

void setup()
{
	pinMode(BUTTON_PIN, INPUT_PULLUP);
	pinMode(PIXEL_PIN, OUTPUT);
	ADCSRA = 0;				// disable ADC
	// General Interrupt Mask Register,
	// Bit 5 - PCIE: Pin Change Interrupt Enable
	// When the PCIE bit is set (one) and the I-bit in the Status Register (SREG) is set (one), pin change interrupt is enabled.
	// Any change on any enabled PCINT[5:0] pin will cause an interrupt.
	// The corresponding interrupt of Pin Change Interrupt Request is executed from the PCI Interrupt Vector.
	// PCINT[5:0] pins are enabled individually by the PCMSK0 Register.
	//GIMSK = 0b00100000;
	//PCMSK = 0b00000111;		// Pin-change interrupt for PB0, PB1, PB2
	setMode(mode);
	leds.show();			// init pixel to 'off'
}

// ========================= Loop =========================

void loop()
{
	for (byte i = 0; i < color_count && !was_mode_changed; i++) {
		for (byte j = 1; j <= fade_step_count && !was_mode_changed; j++) {
			leds.setPixelColor(0, leds.Color(calcCrossfadedColor(subpxls_prev[0], pcolors[i][0], j, fade_step_count, dim),
											 calcCrossfadedColor(subpxls_prev[1], pcolors[i][1], j, fade_step_count, dim),
											 calcCrossfadedColor(subpxls_prev[2], pcolors[i][2], j, fade_step_count, dim)));
			//subpxls[1] = calcCrossfadedColor(subpxls_prev[0], pcolors[i][0], j, fade_step_count, dim);
			//subpxls[0] = calcCrossfadedColor(subpxls_prev[1], pcolors[i][1], j, fade_step_count, dim);
			//subpxls[2] = calcCrossfadedColor(subpxls_prev[2], pcolors[i][2], j, fade_step_count, dim);
			leds.show();

			btn_state = digitalRead(BUTTON_PIN);
			if (btn_state == LOW && btn_state_prev == HIGH) {
				delay(debounce_delay);					// short delay to debounce button
				btn_state = digitalRead(BUTTON_PIN);	// check if button is still low after debounce
				if (btn_state == LOW) {
					mode++;
					if (mode >= mode_count)
						mode = 0;
					setMode(mode);
					was_mode_changed = true;			// break out of for loops, to cleanly start a new mode
				}
			}
			btn_state_prev = btn_state;
			delay(fade_delay);
		}
		memcpy(subpxls_prev, pcolors[i], 3);			// subpxls_prev = pcolors[i]
	}
	was_mode_changed = false;
}

// ========================= Functions =========================

// Output a crossfaded color out of two input colors & their relative intensity, then reduce brightness
byte calcCrossfadedColor(byte shade_prev, byte shade_cur, byte fade_step_cur, byte fade_step_total, byte dim)
{
	return byte((float(shade_prev) / fade_step_total * (fade_step_total - fade_step_cur) +
				 float(shade_cur)  / fade_step_total * fade_step_cur) /
				dim);
}

// Gamma-correct a single color using the lookup table (bottom of the sketch)
byte calcGammaCorrectedColor(byte color)
{
	return pgm_read_byte(&gamma8[color]);
}

// TODO: maybe merge all colors into a single array & iterate over it (or better, a struct)
void setMode(byte i)
{
	switch(i) {
		case 0:	pcolors = EmptyColors;
				color_count = sizeof(EmptyColors) / 3;
				break;
		case 1:	pcolors = LgbtFlagColors;
				color_count = sizeof(LgbtFlagColors) / 3;
				break;
		case 2:	pcolors = TransFlagColors;
				color_count = sizeof(TransFlagColors) / 3;
				break;
	}
	//color_count = sizeof(pcolors[0]);
}

// ========================= Gamma correction table =========================

const uint8_t PROGMEM gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };
