// Includes
#include <avr/io.h>
#include <stdint.h>
#include <avr/interrupt.h>	// sei(), cli() and ISR():
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <stdbool.h>
#include "JannickRampe.h"

// Fuses & Lock bits
FUSES = 
{
	.low = (uint8_t)LFUSE_DEFAULT, //CKDIV8,
	.high = (uint8_t)HFUSE_DEFAULT, //RSTDISBL,
	.extended = (uint8_t)EFUSE_DEFAULT,
};

LOCKBITS = LB_MODE_1;

// EEPROM file implementation
// Some calculating definitions here
#define GATE_SERVO_MIN_POS    750            // (Close in the current conifig)
#define GATE_SERVO_MAX_POS    2200           // (Open in the current config)
#define GATE_SERVO_STEPS      (4*CYCLE_TIME) // 4s
#define GATE_SERVO_STEP_WIDTH (GATE_SERVO_MAX_POS-GATE_SERVO_MIN_POS)/GATE_SERVO_STEPS

typEEPROMVars EEMEM eep_gstrctEEPROMVars =
{
	// Inital global status byte
	(1<<BitNoGateServoOpenDirection), // ui8BinaryStatus
	// Barrel LED
	12,                               // ui8BarrelLEDOnDuration: ~5s*12->60s
	(0.25*CYCLE_TIME),                // ui8BarrelLEDPauseReload: 0.25s
	// Inner LED
	(256/CYCLE_TIME/5),               // ui8InnerLEDStepRising: 5s
	2,                                // ui8InnerLEDOnDuration ~5s*2->10s
	(256/CYCLE_TIME/2),               // ui8InnerLEDStepFalling: 2s
	// Gate Servo
	GATE_SERVO_MIN_POS,               // ui16GateEndposLow
	GATE_SERVO_MAX_POS,               // ui16GateEndposHigh
	GATE_SERVO_STEP_WIDTH,            // ui16GateStepWidth
	// Gate flashing LED
	(CYCLE_TIME/8)                    // ui8GateFlashLEDReload 1/8s
};

// Global register status byte
volatile uint8_t gStatus;

// Interrupt handlers
// compare match interrupt (just for wake up)
EMPTY_INTERRUPT(TIM1_COMPA_vect)

// external interrupt 0 (just for wake up)
EMPTY_INTERRUPT(INT0_vect)


// Main entry
int main(void)
{
	// Init Vars from EEPROM
	typEEPROMVars strctEEPROMVars;
	eeprom_read_block(&strctEEPROMVars, &eep_gstrctEEPROMVars, sizeof(typEEPROMVars));
	gStatus = strctEEPROMVars.ui8BinaryStatus;

	// Init registers
	// Set outputs on PORT A
	DDRA = ALL_LED_OUT_MASK | GATE_SERVO_PWM_OUT_MASK | INNER_LED_MASK;
	// Set outputs on PORT B
	DDRB = GATE_SERVO_SUPPLY_OUT_MASK | GATE_ACTIVE_LED_MASK | NOT_USED_PB;
	// Button pullup / gate servo supply off (neg. logic)
	PORTB = BUTTON_IN_MASK | GATE_SERVO_SUPPLY_OUT_MASK;
	// Inner Button pullup
	PORTA = INNER_BUTTON_MASK;
	// Disable analog comparator for power saving
	ACSR = (1<<ACD);
	// Timer 1: setup FastPWM TOP (similar to CTC) 50Hz
	OCR1A = 1000000 / 50;
	// Timer 1: init servo position
	OCR1B = strctEEPROMVars.ui16GateEndposLow;
	// Timer 1: FastPWM TOP=OCR1A / prescaler -> 1Mhz / OCR1B Pin normal port operation
	TCCR1A = (1<<WGM10)|(1<<WGM11)|(0<<COM1B1)|(0<<COM1B0);
	TCCR1B = (1<<WGM13)|(1<<WGM12)|(0<<CS12)|(0<<CS11)|(1<<CS10);
	// Timer 0 initial PWM value
	OCR0B = 0;
	// Timer 0: FastPWM TOP=0xFF / prescaler -> 1MHz/PWM ~3.9kHz / ORC0B Pin normal port operation
	TCCR0A = (1<<WGM01)|(1<<WGM00)|(0<<COM0B1)|(0<<COM0B0);
	TCCR0B = (0<<WGM02)|(0<<CS02)|(0<<CS01)|(1<<CS00);
	// enable Timer 1 Interrupt
	TIMSK1 = (1<<OCIE1A);
	// Interrupt 0 on falling edge
	MCUCR = (1<<ISC01)|(0<<ISC01);
	// enable external Interrupt 0
	GIMSK = (1<<INT0);
	// enable interrupts
	sei();

	// Init Working globals
	// Timers
	uint8_t ui8Timer50Hz = 0;
	uint8_t ui8Timer5Seconds = 0;
	// Pushbutton
	uint8_t ui8PBPressedTime = 0;
	// barrel LEDs
	uint8_t ui8BarrelLEDOnStartTime = 0;
	uint8_t ui8BarrelLEDOutPattern = (1<<PA0);
	uint8_t ui8BarrelLEDPauseCount = strctEEPROMVars.ui8BarrelLEDPauseReload;
	// inner LED
	uint8_t ui8InnerLEDState = StateInnerLEDIdle;
	uint8_t ui8InnerLEDStartOnTime = 0;
	// gate flashing LED
	uint8_t ui8GateFlashLED = 0;

	for(;;)	{
		ui8Timer50Hz++;
		if(ui8Timer50Hz==0) {
			ui8Timer5Seconds++;
		}

		// Handle Pushbuttons
		// Internal button overrides external
		if((PINA & INNER_BUTTON_MASK) == 0) {
			if(ui8InnerLEDState != StateInnerLEDOn) {
				ui8InnerLEDState = StateInnerLEDRising;
			}
			// Restart Inner LED Timing
			else {
				ui8InnerLEDStartOnTime = ui8Timer5Seconds;
			}
			// reset external pressed state to ensure internal overwrites external
			gStatus &= ~(1<<BitNoExtPBWasPressed);
		}
		else
		{
			uint8_t iPBPressedDuration = ui8Timer50Hz-ui8PBPressedTime;
			// Is external button currently pressed
			if((PINB & BUTTON_IN_MASK) == 0) {
				// Was not yet pressed
				if((gStatus & (1<<BitNoExtPBWasPressed)) == 0) {
					// keep press start time
					ui8PBPressedTime = ui8Timer50Hz;
					gStatus |= (1<<BitNoExtPBWasPressed);
				}
				else {
					// Check for very long pressed (>5s)
					if(iPBPressedDuration > 250) {
						iPBPressedDuration = 250;
					}
				}
			}
			// external button released
			else {
				// Was button pressed and debouncing
				if((gStatus & (1<<BitNoExtPBWasPressed)) && 
						iPBPressedDuration > 5) {
					// reset button pressed status
					gStatus &= ~(1<<BitNoExtPBWasPressed);
					// button pressed for less than 1s -> toggle LED barrel
					if(iPBPressedDuration < 50) {
						// barrel was on -> off
						if(gStatus & (1<<BitNoBarrelLEDOn)) {
							gStatus &= ~(1<<BitNoBarrelLEDOn);
							// Switch LEDs off
							uint8_t ui8PAOut = PORTA;
							ui8PAOut &= ~ALL_LED_OUT_MASK;
							PORTA = ui8PAOut;
						}
						// barrel was off -> on
						else {
							// Set LED barrel on
							gStatus |= (1<<BitNoBarrelLEDOn);
							// Keep start time
							ui8BarrelLEDOnStartTime = ui8Timer5Seconds;
							// init LED pattern
							ui8BarrelLEDOutPattern = 0;
						}
					}
					// button pressed for more than 1s -> gate: on / toggle direction / ensure movement
					else {
						// Power up (neg. logic)
						PORTB &= ~GATE_SERVO_SUPPLY_OUT_MASK;
						// setup OCR1B Pin to create neg edge on compare match
						TCCR1A = (1<<WGM10)|(1<<WGM11)|(1<<COM1B1)|(0<<COM1B0);
						// invert direction
						gStatus ^= (1<<BitNoGateServoDirection);
						// Set gate active
						gStatus |= (1<<BitNoGateServoActive);
						// Reset flashing gate LED count (in case it is not active)
						if(ui8GateFlashLED == 0) {
							ui8GateFlashLED = strctEEPROMVars.ui8GateFlashLEDReload;
						}
						// Start Inner LED (only when opening gate)
						if(gStatus & (1<<BitNoGateServoDirection)) {
							if(ui8InnerLEDState != StateInnerLEDOn) {
								ui8InnerLEDState = StateInnerLEDRising;
							}
							// Restart Inner LED Timing
							else {
								ui8InnerLEDStartOnTime = ui8Timer5Seconds;
							}
						}
						// In case inner LED is still on fade to off
						else if(ui8InnerLEDState == StateInnerLEDRising ||
										ui8InnerLEDState == StateInnerLEDOn) {
							ui8InnerLEDState = StateInnerLEDFalling;
						}
					}
				}
				// reset external pressed state
				gStatus &= ~(1<<BitNoExtPBWasPressed);
			}
		}

		// Handle gate (servo) movement
		// Switch off required?
		if(gStatus & (1<<BitNoGateServoNeedsSwitchOff)) {
			// Apply gate inactive
			gStatus &= ~((1<<BitNoGateServoActive)|(1<<BitNoGateServoNeedsSwitchOff));
			// Do power down (negative logic)
			PORTB |= GATE_SERVO_SUPPLY_OUT_MASK;
			// OCR1B Pin back to normal port operation
			TCCR1A = (1<<WGM10)|(1<<WGM11)|(0<<COM1B1)|(0<<COM1B0);
		}
		// Gate active
		if(gStatus & (1<<BitNoGateServoActive)) {
			uint16_t ui16OCR1B = OCR1B;
			// Increasing direction
			if(gStatus & (1<<BitNoGateServoDirection)) {
				ui16OCR1B += strctEEPROMVars.ui16GateStepWidth;
				if(ui16OCR1B > strctEEPROMVars.ui16GateEndposHigh) {
					ui16OCR1B = strctEEPROMVars.ui16GateEndposHigh;
					gStatus |= (1<<BitNoGateServoNeedsSwitchOff);
				}
			}
			// Decreasing direction
			else {
				ui16OCR1B -= strctEEPROMVars.ui16GateStepWidth;
				if(ui16OCR1B < strctEEPROMVars.ui16GateEndposLow) {
					ui16OCR1B = strctEEPROMVars.ui16GateEndposLow;
					gStatus |= (1<<BitNoGateServoNeedsSwitchOff);
				}
			}
			OCR1B = ui16OCR1B;
		}

		// Handle barrel LED output
		if(gStatus & (1<<BitNoBarrelLEDOn)) {
			uint8_t ui8PAOut = PORTA;
			ui8BarrelLEDPauseCount--;
			// LED output to be handled
			if(ui8BarrelLEDPauseCount == 0) {
				ui8BarrelLEDPauseCount = strctEEPROMVars.ui8BarrelLEDPauseReload;
				ui8BarrelLEDOutPattern = ui8BarrelLEDOutPattern << 1;
				// overflow (sequence finished)?
				if(ui8BarrelLEDOutPattern > (1<<PA4)) {
					// Timeout reached: switch off
					uint8_t ui8DiffTime = ui8Timer5Seconds-ui8BarrelLEDOnStartTime;
					if(ui8DiffTime >= strctEEPROMVars.ui8BarrelLEDOnDuration) {
						// Switch LEDs off
						ui8BarrelLEDOutPattern = 0;
						gStatus &= ~(1<<BitNoBarrelLEDOn);
					}
					// Timeout not reached first LED
					else {
						ui8BarrelLEDOutPattern = (1<<PA0);
					}
				}
				// First?
				else if(ui8BarrelLEDOutPattern == 0) {
					ui8BarrelLEDOutPattern = 1;
				}
				ui8PAOut &= ~ALL_LED_OUT_MASK;
				ui8PAOut |= ui8BarrelLEDOutPattern;
			}
			PORTA = ui8PAOut;
		}

		// Handle inner LED output state machine
		switch(ui8InnerLEDState)
		{
			case StateInnerLEDRising:
			case StateInnerLEDFalling: {
				// OCR0B Pin PWM
				TCCR0A = (1<<WGM01) | (1<<WGM00) | (1<<COM0B1) | (0<<COM0B0);
				uint8_t ui8NewPWMValue = OCR0B;
				bool bGotoNextState = false;
				if(ui8InnerLEDState == StateInnerLEDRising) {
					uint8_t ui8AddValue;
					ui8AddValue = strctEEPROMVars.ui8InnerLEDStepRising;
					ui8NewPWMValue += ui8AddValue;
					// Wrap around: end position
					if(ui8NewPWMValue < ui8AddValue) {
						ui8NewPWMValue = 255;
						ui8InnerLEDStartOnTime = ui8Timer5Seconds;
						bGotoNextState = true;
					}
				}
				else {
					uint8_t ui8SubValue;
					ui8SubValue = strctEEPROMVars.ui8InnerLEDStepFalling;
					uint8_t ui8OldPWMValue = ui8NewPWMValue;
					ui8NewPWMValue -= ui8SubValue;
					// Wrap around: end position
					if(ui8NewPWMValue > ui8OldPWMValue) {
						ui8NewPWMValue = 0;
						bGotoNextState = true;
					}
				}
				OCR0B = ui8NewPWMValue;
				if(bGotoNextState)
					ui8InnerLEDState++;
				break;
			}
			case StateInnerLEDOn:
			default:
				// OCR0B Pin normal port operation
				TCCR0A = (1<<WGM01) | (1<<WGM00) | (0<<COM0B1) | (0<<COM0B0);
				if(ui8InnerLEDState == StateInnerLEDOn) {
					PORTA |= INNER_LED_MASK;	// On
					// Check for next state
					uint8_t ui8MaxOnDuration;
					ui8MaxOnDuration = strctEEPROMVars.ui8InnerLEDOnDuration;
					uint8_t ui8OnDuration = ui8Timer5Seconds - ui8InnerLEDStartOnTime;
					// next state
					if(ui8OnDuration >= ui8MaxOnDuration)
						ui8InnerLEDState++;
				}
				else {
					PORTA &= ~INNER_LED_MASK;	// Off
				}
				break;
		}

		// Handle flashing gate LED
		if(ui8GateFlashLED != 0) {
			ui8GateFlashLED--;
			if(ui8GateFlashLED==0) {
				uint8_t ui8NewPORTB = (PORTB^GATE_ACTIVE_LED_MASK);
				// gate movement still active: reload timer & out gate LED
				if(gStatus & (1<<BitNoGateServoActive)) {
					ui8GateFlashLED = strctEEPROMVars.ui8GateFlashLEDReload;
					PORTB = ui8NewPORTB;
				}
				// ensure that LED is switched off
				if((ui8NewPORTB & GATE_ACTIVE_LED_MASK) == 0) {
					PORTB = ui8NewPORTB;
				}
			}
		}

		// End loop with power saving
		uint8_t ui8CheckForPower = ((1<<BitNoBarrelLEDOn) | (1<<BitNoGateServoActive) | (1<<BitNoExtPBWasPressed));
		if((gStatus & ui8CheckForPower) == 0 && ui8InnerLEDState == StateInnerLEDIdle) {
			// Interrupt 0 on negative level
			MCUCR = (0<<ISC01)|(0<<ISC01);
			// deep sleep
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);
			// sleep to save power wake up on next timer or button
			sleep_mode();
			// return to Interrupt 0 on falling edge
			MCUCR = (1<<ISC01)|(0<<ISC01);
		}
		else {
			// weak sleep
			set_sleep_mode(SLEEP_MODE_IDLE);
			// sleep to save power wake up on next timer or button
			sleep_mode();
		}
	}
}
