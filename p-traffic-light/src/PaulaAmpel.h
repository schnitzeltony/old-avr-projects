#ifndef PAULAAMPEL_H_
#define PAULAAMPEL_H_

// Hardware definitions
#define PB_MASK		(uint8_t) (1<<PB1)
#define LED_RED		(uint8_t) (1<<PB2)
#define LED_YELLOW	(uint8_t) (1<<PB3)
#define LED_GREEN	(uint8_t) (1<<PB0)
#define PINS_UNUSED	(uint8_t) (1<<PB4) | (1<<PB5)


#define	T0_OVR_FRQ			18				// CPU running @1,2 Mhz prescaler 256 Overlow ~9Hz

// Timings for behaviour
#define RED_TIME			15*T0_OVR_FRQ	// ~ 15s
#define YELLOW_TIME 		2*T0_OVR_FRQ	// ~ 2s
#define GREEN_TIME 			15*T0_OVR_FRQ	// ~ 15s
#define YELLOW_FLASH_TIME 	0.5*T0_OVR_FRQ	// ~ 0.5s
#define SLEEP_TIME 			180*T0_OVR_FRQ	// ~ 180s


// Bit enumeration for gStatus
enum enGlobalStatusBits
{
	BitNoPBIsCurrentlyPressed = 0,
	BitNoPBWasPressed,
	BitNoPBWasPressedLong,
	BitNoDeepSleepRequired
};

enum enStates
{
	// Active configuration
	stateRed = 0,
	stateRedYellow,
	stateGreen, 
	stateYello,
	stateLastRYG,
	// Yellow flashing
	stateYellowOn,
	stateYellowOff,
	stateYellowLastOnOff
};


#endif /*PAULAAMPEL_H_*/
