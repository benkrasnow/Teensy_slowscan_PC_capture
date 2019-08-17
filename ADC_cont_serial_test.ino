/* Analog sampler for Teensy LC microcontroller
 * Copyright (c) 2015 Ben Krasnow  ben.krasnow@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
// This program samples a slow-scan analog video signal and sync signal with the ADC, and sends the data over USB serial


#include <ADC.h>

#define DEBUG_PINS  // Comment this to disable hardware debug pins

// Pin I/O
const int PIN_SEM_VIDEO = A9; // Pin connected to video signal from SEM
const int PIN_SEM_SYNC = 15; // Pin connected to sync signal from SEM.  HIGH = beam blanked during retrace

#ifdef DEBUG_PINS
  const int PIN_DEBUG1 = 2;  //HIGH during ADC ISR
  const int PIN_DEBUG2 = 4; //HIGH during main loop
  const int PIN_DEBUG3 = 5; //HIGH during sync ISR
#endif


//SEM video signal parameters
const int SYNC_DUR_THRESH = 300;  // Duration in microseconds that differentiates between a horizontal and vertical sync pulse.


//USB data parameters
const byte HSYNC_VAL = 0;  //Value sent via USB that indicates a horizontal sync pulse
const byte VSYNC_VAL = 1;  //Value sent via USB that indicates a vertical sync pulse
//Values 2-255 are image data


//Internal tuning
const int MAIN_BUF_LENGTH = 128; // How large is each ping-pong buffer
const int BUF_TX_TRIG = 100;  // When the buffer has this many bytes in it, switch to the other buffer, and transmit now.


//Global variables

byte bufferA[MAIN_BUF_LENGTH];
byte bufferB[MAIN_BUF_LENGTH];
int bufptrA, bufptrB, usebufA; // usebufA is used as a boolean

elapsedMicros syncPulseDur;  // This is a special Teensy variable type that increments in time automatically

ADC *adc = new ADC(); // adc object

void setup()
{
    int i;
    usebufA = 1;
    bufptrA = 0;
    bufptrB = 0;
  
    for (i=0; i < NVIC_NUM_INTERRUPTS; i++) NVIC_SET_PRIORITY(i, 128);  //The Teensy LC only uses interrupt priorities of 0, 64, 128, 192, 255
    NVIC_SET_PRIORITY(IRQ_ADC0,64);  // The ADC ISR must have a higher priority (lower number) than the USB ISR
    NVIC_SET_PRIORITY(IRQ_PORTCD, 0);  // The sync pulse ISR needs to have the highest priority of all
 
    Serial.begin(9600); // USB is always 12 Mbit/sec

    pinMode(PIN_SEM_VIDEO, INPUT);
    pinMode(PIN_SEM_SYNC, INPUT);
   
   // Debug pins
    #ifdef DEBUG_PINS
      pinMode(PIN_DEBUG1, OUTPUT);
      pinMode(PIN_DEBUG2, OUTPUT);
      pinMode(PIN_DEBUG3, OUTPUT);
    #endif
    
    attachInterrupt(PIN_SEM_SYNC, syncfallingISR, FALLING);

    ///// ADC0 ////
    // reference can be ADC_REF_3V3, ADC_REF_1V2 (not for Teensy LC) or ADC_REF_EXT.
    adc->setReference(ADC_REF_3V3, ADC_0); // change all 3.3 to 1.2 if you change the reference to 1V2

    adc->setAveraging(4); // set number of averages
    adc->setResolution(8); // set bits of resolution

    // it can be ADC_VERY_LOW_SPEED, ADC_LOW_SPEED, ADC_MED_SPEED, ADC_HIGH_SPEED_16BITS, ADC_HIGH_SPEED or ADC_VERY_HIGH_SPEED
    // see the documentation for more information
    // additionally the conversion speed can also be ADC_ADACK_2_4, ADC_ADACK_4_0, ADC_ADACK_5_2 and ADC_ADACK_6_2,
    // where the numbers are the frequency of the ADC clock in MHz and are independent on the bus speed.
    adc->setConversionSpeed(ADC_HIGH_SPEED);
    
    // it can be ADC_VERY_LOW_SPEED, ADC_LOW_SPEED, ADC_MED_SPEED, ADC_HIGH_SPEED or ADC_VERY_HIGH_SPEED
    adc->setSamplingSpeed(ADC_HIGH_SPEED); // change the sampling speed

    // always call the compare functions after changing the resolution!
    //adc->enableCompare(1.0/3.3*adc->getMaxValue(ADC_0), 0, ADC_0); // measurement will be ready if value < 1.0V
    //adc->enableCompareRange(1.0*adc->getMaxValue(ADC_0)/3.3, 2.0*adc->getMaxValue(ADC_0)/3.3, 0, 1, ADC_0); // ready if value lies out of [1.0,2.0] V
   //  In this case, we only want the raw byte vales from the ADC.  No need for scaling.
   
    adc->startContinuous(PIN_SEM_VIDEO, ADC_0);
    adc->enableInterrupts(ADC_0);
}


void loop()
{
  #ifdef DEBUG_PINS
    digitalWriteFast(PIN_DEBUG2,HIGH);
  #endif
  
  if(bufptrA > BUF_TX_TRIG && usebufA == 1)
       {
            __disable_irq();
            usebufA = 0;
            bufptrB = 0;
            __enable_irq();
            Serial.write(bufferA, bufptrA);
        }
   else if(bufptrB > BUF_TX_TRIG && usebufA == 0)
        {
            __disable_irq();
            usebufA = 1;
            bufptrA = 0;
            __enable_irq()
            Serial.write(bufferB, bufptrB);
        }     
   #ifdef DEBUG_PINS
     digitalWriteFast(PIN_DEBUG2,LOW);
   #endif
}


void adc0_isr(void) {
  int tempval;
    __disable_irq();
    #ifdef DEBUG_PINS
      digitalWriteFast(PIN_DEBUG1,HIGH);
    #endif
    if(digitalRead(PIN_SEM_SYNC) == HIGH)
      {
        syncPulseDur = 0;  //We detected the start of a sync pulse.  Set the auto-incrementing value to 0 so we can measure its length.
        adc->stopContinuous(); // Stop the ADC so that we can start it exactly at the end of the sync pulse
      }
    else
      {
        if(usebufA == 1 && bufptrA < MAIN_BUF_LENGTH)
          {
              tempval = adc->analogReadContinuous(ADC_0) + 2;  // Values 0 and 1 are used for sync, so 2-255 are data
              bufferA[bufptrA++] = (tempval < 255) ? tempval : 255;
          }  
        if(usebufA == 0 && bufptrB < MAIN_BUF_LENGTH)
          {  
            tempval = adc->analogReadContinuous(ADC_0) + 2;
            bufferB[bufptrB++] = (tempval < 255) ? tempval : 255;
          }
      }
     #ifdef DEBUG_PINS 
       digitalWriteFast(PIN_DEBUG1,LOW);
     #endif
     __enable_irq();
}

void syncfallingISR(void) {
  __disable_irq();
  #ifdef DEBUG_PINS
    digitalWriteFast(PIN_DEBUG3,HIGH);
  #endif
  if(usebufA == 1 && bufptrA < MAIN_BUF_LENGTH)
          {
            if(syncPulseDur < SYNC_DUR_THRESH)
              {
                bufferA[bufptrA++] = HSYNC_VAL;
              }
            else
              {
                bufferA[bufptrA++] = VSYNC_VAL;
              }
              
          }  
   if(usebufA == 0 && bufptrB < MAIN_BUF_LENGTH)
          {  
              if(syncPulseDur < SYNC_DUR_THRESH)
              {
                bufferB[bufptrB++] = HSYNC_VAL;
              }
            else
              {
                bufferB[bufptrB++] = VSYNC_VAL;
              }
          }
  adc->startContinuous(PIN_SEM_VIDEO, ADC_0);
  #ifdef DEBUG_PINS
    digitalWriteFast(PIN_DEBUG3, LOW);
  #endif
  __enable_irq();
}


