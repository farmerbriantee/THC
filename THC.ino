/*
 I am using code from SWOLEBRO THC design located here: 

https://github.com/swolebro/swolebro-youtube

His youtube page is located here:

https://www.youtube.com/channel/UCRMLI3S0AFukV1tzX6Cl2Cw

I have adjusted his original code to add a 20x4 LCD using the liquid crystal I2C display. 
*/

//     URL: https://github.com/RobTillaart/I2C_LCD

#include "I2C_LCD.h"

//LD2004A Display
I2C_LCD lcd(39);

#include <NewTone.h>
//NewTone(TONE_PIN, freq); 
//noNewTone(TONE_PIN);
//NewTone(TONE_PIN, melody[thisNote], noteDuration);

// Setting the scale for the converting analogRead values to volts.
// 4.450 AREF voltage * 50 built-in voltage divider / 1023 resolution = 0.21749755 ADC counts per volt
// As far as I can tell, the arithmetic below *does* get optimized out by the compiler.
#define SCALE (5.00*50/1023)

#define THC_ON 1
#define THC_OFF 0

// Adjustment range for the knob.
#define MINSET 110
#define MAXSET 150

// Naming other pins.
#define ADJUST A1
#define PLASMA A0

    //Stepper driver input
#define DIR 10
#define PULSE 8

#define ARC_GOOD_PIN 4
#define THC_PIN 6

#define BUFSIZE 512  // Would technically let us do running averages up to BUFSIZE samples. In testing, shorter averages seemed better.
#define SAMP 16  // Use this many samples in the average; must be a power of 2 and no larger than BUFSIZE.
#define DISP 2048 // The number of samples to use in calculating a slower average for the display. Must also be a power of 2.

unsigned int shift = 0;

unsigned int values[BUFSIZE] = {0}; // buffer for ADC reads
unsigned long total = 0L; // for keeping a rolling total of the buffer
unsigned long disp = 0L;  // for separately tracking ADC reads for the display
long target = 0L; // voltage target, in ADC counts

// for tracking when to set opto pins
long diff = 0L;
long mean = 0L;
int mode = -1;

//stepper speed
int pulseFreq = 10;

// generic temp vars
unsigned long tmp = 0;
float ftmp = 0;
float ftmp2 = 0;

// generic looping vars
int i = 0;
int j = 0;

// for the startup adjustment period
unsigned long timelimit = 0;
unsigned long ms = 0;

// Threshold in ADC counts for when we say the torch is out of range.
// Multiply by SCALE for the threshold in volts.
//**** original 5 *****
#define THRESHOLD 20
int threshold = THRESHOLD;

const char blank_line_20[] = "                    ";

void setup()
{
    // ADC Inputs
    pinMode(ADJUST, INPUT);
    pinMode(PLASMA, INPUT);

    pinMode(DIR, OUTPUT);
    pinMode(PULSE, OUTPUT);

    //Switch the THC controlled Z axis
    pinMode(THC_PIN, OUTPUT);

    //Read if Arc Good to begin THC
    pinMode(ARC_GOOD_PIN, INPUT);
    pinMode(ARC_GOOD_PIN, INPUT_PULLUP);

    digitalWrite(THC_PIN, THC_OFF);

    // Set the reference voltage to the external linear regulator
    // Do a few throwaway reads so the ADC stabilizes, as recommended by the docs.
    analogReference(EXTERNAL);
    analogRead(PLASMA); analogRead(PLASMA); analogRead(PLASMA); analogRead(PLASMA); analogRead(PLASMA);

    // We need to calculate how big the shift must be, for a given sample size.
    // Since we are using bitshifting instead of division, I'm using a != here,
    // so your shit will be totally broke if you don't set SAMP to a power of 2.
    while ((1 << shift) != SAMP)
        shift++;

    // Setup the LCD's columns and rows 
    Wire.begin();
    lcd.begin(20, 4);
    lcd.clear();

    //  display fixed text once
    lcd.setCursor(0, 0);
    lcd.print("Set Volts: ");

    lcd.setCursor(0, 1);
    lcd.print("Plasma: Cal");

    // Now enter the period where you can set the voltage via the potentiomenter.
    // Default 5s period, plus an extension 2s as long as you keep adjusting it.
    // By fixing this after boot, we save cycles from needing to do two ADC reads per loop(),
    // avoid any nonsense from potentiometer drift, and don't need to think about the
    // capacitance of the ADC muxer.
    i = 0;
    ms = millis();
    timelimit = ms + 5000;

    while (ms < timelimit) {
        tmp = analogRead(ADJUST);

        // Keep a rotating total, buffer, and average.  Since this value only moves
        // a small amount due to noise in the AREF voltage and the physical
        // potentiometer itself, 10 samples is fine.
        total = total + tmp - values[i];
        values[i] = tmp;
        target = total / 10;

        // Calculate the setpoint, based on min/max, and chop it to one decimal point.
        ftmp2 = MINSET + ((MAXSET - MINSET) * (target / 1023.0));
        ftmp2 = ((int)(ftmp2 * 10)) / 10.0;

        if (ftmp != ftmp2) {
            ftmp = ftmp2;
            timelimit = max(timelimit, ms + 2000);

            // Print the set number Pin A1
            lcd.setCursor(11, 0);
            lcd.print(ftmp, 1);
        }

        i = (i + 1) % 10;
        ms = millis();
    }

    // Convert the voltage target back into an int, for ADC comparison, with the scale the plasma pin uses.
    target = ftmp / SCALE;

    // Before carrying on, we now reset some of those variables.
    for (i = 0; i < BUFSIZE; i++)
        values[i] = 0;

    total = 0;
    i = 0;
    j = 1; // Keeps display from triggering until we've done BUFSIZE samples.

}

void loop()
{
    tmp = analogRead(PLASMA);
    disp += tmp; // non-rolling tally for the lower sample rate display

    // Rolling window for a smaller sample
    total = total + tmp - values[i];
    values[i] = tmp;

    // This mean truncates downwards. Oh well. At least it's fast.
    mean = total >> shift;
    diff = mean - target;

    // If the mean is very low, then the plasma is turned off - it's just ADC
    // noise you're seeing and it and should be ignored.
    // This effectively checks if it's less than 2^4, ie. 16 counts, or ~3V with my scale factor.

    if (mode) threshold /= 2;

    if (!(mean >> 4)) 
    {
        mode = 0;
        noNewTone(PULSE);
    }

    // Otherwise, set pins as per reading.
    // Set 0's first to turn off one direction before turning on reverse.
    // We should never have both the UP and DOWN pins set to 1 - that would be nonsense.
    // Checking for current setting before flipping saves a few cycles.
    else if (diff > threshold) 
    {
        if (mode != 2) {
            mode = 2;
            digitalWrite(DIR, 1);
            NewTone(PULSE, pulseFreq);
        }
    }

    else if (diff < -threshold)
    {
        if (mode != 1) 
        {
            mode = 1;
            digitalWrite(DIR, 0);
            NewTone(PULSE, pulseFreq);
        }
    }

    else 
    {
        mode = 0;
        noNewTone(PULSE);
    }

    //Print the voltage it currently reads from plasma pin A0
    if (!j) 
    {
        lcd.setCursor(8, 1);
        lcd.print((float)((disp / DISP) * SCALE), 1);
        lcd.setCursor(8, 2);
        lcd.print("     ");
        lcd.setCursor(8, 2);
        lcd.print(diff);
        lcd.setCursor(8, 3);
        if (mode == 0) lcd.print("Hold");
        else if (mode == 1) lcd.print(" Up ");
        else if (mode == 2) lcd.print("Down");
        disp = 1;

        if (!digitalRead(ARC_GOOD_PIN)) digitalWrite(THC_PIN, THC_ON);
        else digitalWrite(THC_PIN, THC_OFF);
    }

    // Faster than modular arithmetic, by far. Doing that drops us down to ~3kS/sec.
    i = (i + 1) & (SAMP - 1);
    j = (j + 1) & (DISP - 1);

    threshold = THRESHOLD;
}
