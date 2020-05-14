
;************************************************************************************
;	register definitions for ATTiny13
;************************************************************************************
.include "tn13def.inc"

;************************************************************************************
;	local register definitions
;************************************************************************************
.def	ADC_REF_L	= R0	; reference adc (=mid position) value low
.def	ADC_REF_H	= R1	; reference adc (=mid position) value high
.def	SREG_BACK0	= R2	; SREG backup during interrupt



.def	WORK0		= R16	; temporary register
.def	WORK1		= R17	; temporary register
.def	PARAM0		= R18	; parameter register

.def	ADC_L		= R20	; adc result low
.def	ADC_H		= R21	; adc result high
.def	TIMER_UPPER	= R22	; upper timer 0 value

;************************************************************************************
;	port definitions
;************************************************************************************
.set	PUSHBUTTON	= 0	; input for reference pushbutton
.set	POTENTIOMETER	= 2	; analog control input
.set	RC_OUT		= 4	; RC pulse output


;************************************************************************************
;	Resetvector
;************************************************************************************
	.cseg

	.org	0x0000			; Reset vector
	rjmp	reset_entry		; reset entry

;************************************************************************************
;	Interruptvectors
;************************************************************************************

	.org	0x0001			; External Interrupt Request 0
	reti				; not handled

	.org	0x0002			; Pin Change Interrupt Request 0
	reti				; not handled

	.org	0x0003			; Timer/Counter0 Overflow
	reti				; not handled

	.org	0x0004			; EEPROM ready
	reti				; not handled

	.org	0x0005			; Analog comparator
	reti				; not handled

	.org	0x0006			; Timer0 Compare Match A
	in	SREG_BACK0,SREG		; save SREG
	inc	TIMER_UPPER		; increment upper timer value
	out	SREG,SREG_BACK0		; restore SREG
	reti				; over and out

	;.org	0x0007			; Timer0 Compare Match B
	;reti				; not handled

	;.org	0x0008			; Watchdog Timeout
	;reti				; not handled

	;.org	0x0009			; ADC conversion complete
	;reti				; not handled



;************************************************************************************
;	Reset entry
;************************************************************************************
reset_entry:
	; prepare port b
	sbi	PORTB,PUSHBUTTON	; enable internal pullups for Pushbutton
	ldi	WORK0,(1<<RC_OUT)	; RC pin as output
	out	DDRB,WORK0		; setup port b direction
	; setup ADC
	ldi	WORK0,(1<<ADC1D)	; Disable ADC pin as digital input
	out	DIDR0,WORK0		; set
	ldi	WORK0,1<<MUX0		; ADC1
	out	ADMUX,WORK0		; Setup multiplexer
	; Enable ACD / Prescaler 128 (62,5kHz -> samling frq >= 2500Hz)
	ldi	WORK0,(1<<ADEN)+(1<<ADPS2)+(1<<ADPS1)+(1<<ADPS0)
	out	ADCSRA,WORK0		; prepare ADC
	
	; Wait for 0.5s to start up
	ldi	PARAM0,50		; 50*10ms
	rcall	wait_n10ms		; do wait

	; Get inital state of the adc (reference position)
	rcall	adc_conv		; get adc
	movw	ADC_REF_H:ADC_REF_L,\
		ADC_H:ADC_L		; store reference position

	; setup Timer0
	ldi	TIMER_UPPER,0		; init upper timer value
	ldi	WORK0,(1<<WGM01)	; mode: clear on compare match
	out	TCCR0A,WORK0		; set mode of operation
	ldi	WORK0,150-1		; compare match every 125µs @9.6Mhz
	out	OCR0A,WORK0		; set compare match value
	ldi	WORK0,(1<<OCIE0A)	; Timer0 compare match interrupt
	out	TIMSK0,WORK0		; Enable interrupt
	sei				; global enable
	ldi	WORK0,(1<<CS01)		; Clk/8 -> 1.2Mhz
	out	TCCR0B,WORK0		; Start Timer0 with 1.2Mhz

main_loop:
	; in the first millisecond get next adc and button
	sbi	PORTB,RC_OUT		; set RC out
	rcall	adc_conv		; get adc
	sbis	PINB,PUSHBUTTON		; check if button is pressed
	movw	ADC_REF_H:ADC_REF_L, \
		ADC_H:ADC_L		; if button pressed store reference position
	; calculate pulse width
	sub	ADC_L,ADC_REF_L		; calc difference low
	sbc	ADC_H,ADC_REF_H		; calc difference high
;	ldi	WORK0,1			; shift loop count
;zz_shift:
;	asr	ADC_H
;	ror	ADC_L
;	dec	WORK0
;	brne	zz_shift
;	inc	ADC_H

	ldi	WORK0,68		; set offset low
	add	ADC_L,WORK0		; add offset low
	ldi	WORK0,9			; set offset high
	adc	ADC_H,WORK0		; add offset high

zz_wait_075ms:
	cpi	TIMER_UPPER,6		; 6*125µs = 0.75ms
	brne	zz_wait_075ms		; wait for first 0.75ms to finish
	; wait for pulse to finish
zz_finish_loop:
	subi	ADC_L,1
	brcc	zz_finish_loop
	subi	ADC_H,1
	brpl	zz_finish_loop

	cbi	PORTB,RC_OUT		; reset RC out

zz_wait_20ms:				
	cpi	TIMER_UPPER,160		; 160*125µs = 20ms
	brne	zz_wait_20ms		; wait for first 20ms to finish
	ldi	TIMER_UPPER,0		; reset upper counter

	rjmp	main_loop		; continue with next loop



;------------------------------------------------------------------------------------
; wait_n10ms: 			Waits for PARAM0*10ms
;				Registers changed:	PARAM0,WORK1,WORK0
;------------------------------------------------------------------------------------
wait_n10ms:
	ldi	WORK0,200		; Loop 2
zz_wait_loop2:
	ldi	WORK1,159		; Loop 1
zz_wait_loop1:
	dec	WORK1			; decrement loop count 1
	brne	zz_wait_loop1		; repeat loop 1
	dec	WORK0			; decrement loop count 2
	brne	zz_wait_loop2		; repeat loop 2
	dec	PARAM0			; decrement parameter loop count
	brne	wait_n10ms		; repeat parameter loop
	ret


;------------------------------------------------------------------------------------
;	adc_conv: 	Multiple conversion of adc. 
;			The result is found in ADC_H:ADC_L
;			WORK0 is changed.
;------------------------------------------------------------------------------------
adc_conv:
	; setup adc sum & loop count
	ldi	WORK1,4			; setup adc loop
	ldi	ADC_L,0			; init adc sum low
	ldi	ADC_H,0			; init adc sum high
zz_adc_loop:
	sbi	ADCSRA,ADSC		; start conversion
zz_adc_wait:
	sbic	ADCSRA,ADSC		; check for adc to complete
	rjmp	zz_adc_wait		; not yet finshed wait more
	in	WORK0,ADCL		; get adc result low
	add	ADC_L,WORK0		; add to sum low
	in	WORK0,ADCH		; get adc result high
	adc	ADC_H,WORK0		; add to sum high
	dec	WORK1			; decrement loop count
	brne	zz_adc_loop		; one more adc to go
	ret				; over and out


