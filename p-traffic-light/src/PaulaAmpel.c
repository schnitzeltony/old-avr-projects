// Includes
#include <avr/io.h>
#include <stdint.h>
#include <avr/interrupt.h>	// sei(), cli() and ISR():
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <stdbool.h>
#include "PaulaAmpel.h"

// Fuses & Lock bits
FUSES = 
{
	.low = (FUSE_CKSEL0 & FUSE_SUT0 & FUSE_CKDIV8),
	.high = FUSE_RSTDISBL
};

LOCKBITS = LB_MODE_3;	// prevent everything

// Global register status byte
volatile uint8_t gStatus;

// Interrupt handlers
// Timer0 overflow interrupt (just for wake up)
EMPTY_INTERRUPT(TIM0_OVF_vect)

// INT0
EMPTY_INTERRUPT(INT0_vect)

// Main entry
int main(void)
{
	// Init registers
	// Set outputs on PORT B
	DDRB = LED_RED | LED_YELLOW | LED_GREEN | PINS_UNUSED;
	// Set pullup for PB
	PORTB = PB_MASK;


	// setup timer 0 (prescaler 256 -> 4687,5 Hz -> Overflow Interrupt ~18,31 Hz
	TCCR0B = (1<<CS02)|(0<<CS01)|(0<<CS00); 
	// enable Timer 0 Interrupt
	TIMSK0 = (1<<TOIE0);
	
	// Interrupt 0 on negative level
	MCUCR = (0<<ISC01)|(0<<ISC00);

	// Init Working globals
	gStatus = 0;
	// State
	uint8_t ui8CurrState = stateGreen;
	uint16_t ui16StateChangeTime = 0;
	// Timers
	uint16_t ui16Timer18Hz = 0;
	uint16_t ui16SleepTimer18Hz = 0;

	// Pushbutton
	uint8_t ui8PBPressStartTime = 0;

	// enable interrupts
	sei();

	for(;;) {
		// Increment timer
		ui16Timer18Hz++;

		// 1. Handle Pushbutton
		uint8_t ui8PBPressedDuration18Hz = ui16Timer18Hz - ui8PBPressStartTime;
		// Button pressed?
		if(!(PINB & PB_MASK)) {
			// Was not yet pressed
			if((gStatus & (1<<BitNoPBIsCurrentlyPressed)) == 0) {
				// keep press start time
				ui8PBPressStartTime = ui16Timer18Hz;
				gStatus |= (1<<BitNoPBIsCurrentlyPressed);
			}
			// Button was already pressed
			else {
				// Prevent Overflow
				if(ui8PBPressedDuration18Hz > 250)
					ui8PBPressedDuration18Hz = 250;
			}
			// reset Sleep timer
			ui16SleepTimer18Hz = 0;
		}
		// Button is not pressed
		else
		{
			// Was button pressed and debouncing
			if((gStatus & (1<<BitNoPBIsCurrentlyPressed)) && 
				ui8PBPressedDuration18Hz > 2) {
				// reset button pressed status
				gStatus &= ~(1<<BitNoPBIsCurrentlyPressed);
				// Keep information that button was pressed
				gStatus |= (1<<BitNoPBWasPressed);
				// button pressed for longer than 1s
				if(ui8PBPressedDuration18Hz > T0_OVR_FRQ)
					gStatus |= (1<<BitNoPBWasPressedLong);
			}
			ui16SleepTimer18Hz++;
		}
		
		// 2. Get the current state ON duration
		uint16_t ui16CompareTime = 0;
		// Normal operation without button pressed
		if(!(gStatus & (1<<BitNoPBWasPressed)))
		{
			switch(ui8CurrState) {
				case stateRedYellow:
				case stateYello:
					ui16CompareTime = YELLOW_TIME; 
					break;
				case stateRed:
					ui16CompareTime = RED_TIME; 
					break;
				case stateGreen:
					ui16CompareTime = GREEN_TIME; 
					break;
				default:
				/*case stateYellowOn:
				case stateYellowOff:*/
					ui16CompareTime = YELLOW_FLASH_TIME;
					break;
			}
		}
		// pressed long
		else if(gStatus & (1<<BitNoPBWasPressedLong)) {
			// Change to yellow flashing (starting off)
			if(ui8CurrState < stateLastRYG) {
				ui8CurrState = stateYellowOff-1;
			}
			// Change to normal operation (starting green)
			else {
				ui8CurrState = stateGreen-1;
			}
		}
		// pressed short
		else {
			switch(ui8CurrState) {
				// Yellow is not to be jittered
				// All others leave ui16CompareTime = 0 to ensure that
				// next state is initiated
				case stateRedYellow:
				case stateYello:
					ui16CompareTime = YELLOW_TIME; 
					break;
				case stateYellowOn:
				case stateYellowOff:
					ui16CompareTime = YELLOW_FLASH_TIME;
					break;

			}
		}
		// All button actions handled: reset status
		gStatus &= ~( (1<<BitNoPBWasPressed) | (1<<BitNoPBWasPressedLong) );

		// 3. State machine / Sleep handling
		if(ui16Timer18Hz-ui16StateChangeTime >= ui16CompareTime) {
			ui8CurrState++;
			if(ui8CurrState == stateLastRYG) {
				ui8CurrState = stateRed;
			}
			else if(ui8CurrState == stateYellowLastOnOff) {
				ui8CurrState = stateYellowOn;
			}
			ui16StateChangeTime = ui16Timer18Hz;
			// Check for smooth sleeping
			if(	ui16SleepTimer18Hz > SLEEP_TIME &&
				(ui8CurrState == stateYello || ui8CurrState == stateYellowOff) ) {
				gStatus |= (1<<BitNoDeepSleepRequired);
			}
		}

		// 4a. Output LED and weak sleep
		if(!(gStatus & (1<<BitNoDeepSleepRequired))) {
			switch(ui8CurrState) {
				case stateRed:
					PORTB = LED_RED | PB_MASK; 
					break;
				case stateRedYellow:
					PORTB = LED_RED | LED_YELLOW | PB_MASK; 
					break;
				case stateGreen:
					PORTB = LED_GREEN | PB_MASK; 
					break;
				case stateYellowOff:
					PORTB = PB_MASK;
					break;
				default:
				/*case stateYello:
				case stateYellowOn:*/
					PORTB = LED_YELLOW | PB_MASK; 
					break;
			}
			// prepare weak sleep
			set_sleep_mode(SLEEP_MODE_IDLE);
			// sleep to save power wake up on next timer or button
			sleep_mode();
		}
		// 4b. switch off LEDs and deep sleep
		else {
			// switch off LEDs
			PORTB = PB_MASK;
			// enable ext. int for wakeup
			GIMSK = (1<<INT0);

			// prepare deep sleep
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);
			// sleep to save power wake up on next timer or button
			sleep_mode();

			// disable ext. int
			GIMSK = 0;

			// reset sleep status
			gStatus &= ~(1<<BitNoDeepSleepRequired);
			// wait until button is released
			while(!(PINB & PB_MASK));
			// prepare weak sleep
			set_sleep_mode(SLEEP_MODE_IDLE);
			// weak sleep for debouncing
			sleep_mode();
			sleep_mode();

			// reset Sleep timer
			ui16SleepTimer18Hz = 0;
			// restart start with green
			ui8CurrState = stateGreen;
		}
	}
}

