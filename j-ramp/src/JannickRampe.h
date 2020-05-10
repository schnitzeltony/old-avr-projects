#ifndef _JANNICK_RAMPE_H_
#define _JANNICK_RAMPE_H_ 1

#include <stdint.h>

// Hardware definitions CPU running @1MHz
#define CYCLE_TIME                 50 // 50Hz
#define ALL_LED_OUT_MASK           ((1<<PA0) | (1<<PA1) | (1<<PA2) | (1<<PA3) | (1<<PA4))
#define GATE_SERVO_PWM_OUT_MASK    (1<<PA5)
#define INNER_BUTTON_MASK          (1<<PA6)
#define INNER_LED_MASK             (1<<PA7)
#define GATE_ACTIVE_LED_MASK       (1<<PB0)
#define GATE_SERVO_SUPPLY_OUT_MASK (1<<PB1)
#define BUTTON_IN_MASK             (1<<PB2)
#define NOT_USED_PB                (1<<PB3) // TODO remove all

// Bit enumeration for gStatus
enum enGlobalStatusBits
{
	BitNoExtPBWasPressed = 0,
	BitNoBarrelLEDOn,
	BitNoGateServoActive,
	BitNoGateServoDirection,
	BitNoGateServoNeedsSwitchOff,
	BitNoGateServoOpenDirection, // detects the open position

};

// Open detection
#define GATE_OPEN_DETECTION_MASK ((1<<BitNoGateServoDirection)|(1<<BitNoGateServoOpenDirection))

// Inner LED state machine states definitions
enum enInnerLEDStates
{
	StateInnerLEDRising = 0,
	StateInnerLEDOn,
	StateInnerLEDFalling,
	StateInnerLEDIdle
};


// EEPROM parameter file definition
typedef struct
{
	// global status byte
	uint8_t ui8BinaryStatus;
	// Barrel LED
	uint8_t ui8BarrelLEDOnDuration;
	uint8_t ui8BarrelLEDPauseReload;
	// Inner LED
	uint8_t ui8InnerLEDStepRising;
	uint8_t ui8InnerLEDOnDuration;
	uint8_t ui8InnerLEDStepFalling;
	// Gate Servo
	uint16_t ui16GateEndposLow;
	uint16_t ui16GateEndposHigh;
	uint16_t ui16GateStepWidth;
	// Gate flashing LED
	uint8_t ui8GateFlashLEDReload;
} typEEPROMVars;


#endif		// #ifndef _JANNICK_RAMPE_H_
