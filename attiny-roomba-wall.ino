/*
 * attiny85.ino
 *
 * Copyright 2020 Victor Chew
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <limits.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <tiny_IRremote.h>

#define BUTTON_PIN        PB1
#define LED_PIN           PB3
#define VCC_THRESHOLD     2800  // Signal low battery when volage falls below this level (mV)
#define AUTO_OFF_INTERVAL 80    // Turns off automatically after runnig for this time in minutes

volatile bool button_clicked = false, check_vcc = false;
bool started = false, low_voltage = false;
int led_state, led_on_time = 1000, led_period = 2000;
long led_last_on = 0, ir_last_fired = 0, start_time = 0;
IRsend irsend; // Outputs on PB4

// Watchdog Timer ISR. Called to trigger request to check battery level.
ISR(WDT_vect) {
  check_vcc = true;
}

// Pin Change Interrupt ISR. Called to check if button is pressed
ISR(PCINT0_vect) {
  if (digitalRead(BUTTON_PIN) == LOW) button_clicked = true;
}

// Measure VCC using interal 1.1V bandgap as reference
void checkLowVoltage() {
  ADMUX = bit(MUX3) | bit(MUX2); 
  delay(1); // Wait for bandgap voltage to settle
  ADCSRA = bit(ADEN) | bit(ADSC) | bit(ADPS2) | bit(ADPS1) | bit(ADPS0);
  while(bit_is_set(ADCSRA, ADSC));
  uint16_t vcc = (1.1*1024*1000L) / ADC; // Compute VCC (in mV)
  ADCSRA = 0; // Turn off ADC to save power
  low_voltage = (vcc < VCC_THRESHOLD);
}

// WDT interrupts every 64s
void startWDT() {
  MCUSR &= ~bit(WDRF);
  WDTCR |= bit(WDCE) | bit(WDE);
  WDTCR = bit(WDIE) | bit(WDP1);
}

void stopWDT() {
  WDTCR |= bit(WDCE) | bit(WDE);
  WDTCR = ~bit(WDE) & ~bit(WDIE);
}

void setup()
{
  pinMode(PB0, OUTPUT);
  pinMode(PB2, OUTPUT);
    
  // Setup status LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Setup IR LED 
  irsend.enableIROut(38);

  // Setup on/off button and enable pin change interrupt
  pinMode(BUTTON_PIN, INPUT);
  PCMSK |= (1 << PCINT1);
  GIMSK |= bit(PCIE); 

  // Use sleep mode with lowest power consumption
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  ADCSRA = 0; // Disable ADC
  sleep_cpu();
}

void loop()
{
  // Handle button click with 5ms debounce
  if (button_clicked) {
    button_clicked = false;
    delay(5);
    if (digitalRead(BUTTON_PIN) == LOW) {
      while(digitalRead(BUTTON_PIN) == LOW);
      started = !started;
      if (started) {
        start_time = millis();
        startWDT();
      } else {
        stopWDT();
      }
    }
  }

  // Send IR signal: (500us on, 7500us off) x 3, every 132ms
  if (started && millis() - ir_last_fired >= 132) {
    for (int i=0; i<3; i++) {
      irsend.mark(500);
      irsend.space(7500);
    }
    ir_last_fired = millis();
  }

  // Check battery voltage after flag set by WDT ISR
  if (check_vcc) {
    check_vcc = false;
    if (started) {
      checkLowVoltage();
      led_on_time = low_voltage ? 100 : 1000;
      led_period = low_voltage ? 4000 : 2000;
    }
  }

  // Automatically turn off if we have been running for 70mins
  if (started && millis() - start_time >= AUTO_OFF_INTERVAL*60*1000L) {
    started = false;
  }

  // Handle status LED: blink every sec for normal operation, blink 0.5s every 3s if battery low
  if (!started) {
    led_state = LOW; 
  }
  else {
    if (led_state == LOW) {
      if (millis() - led_last_on >= led_period) {
        led_state = HIGH;
        led_last_on = millis();
      }
    } else {
      if (millis() - led_last_on >= led_on_time) {
        led_state = LOW;      
      }
    }
  }
  digitalWrite(PB3, led_state);

  // Deep sleep if we are not started to save power
  if (!started) {
    ADCSRA &= ~bit(ADEN); // disable the ADC
    sleep_cpu();
  }
}
