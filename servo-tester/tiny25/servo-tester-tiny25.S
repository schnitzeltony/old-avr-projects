#include <avr/io.h>

;*******************************************************************************
;    local register definitions
;*******************************************************************************
ADC_REF_L     = 0                         ; reference adc (=mid position) value low
ADC_REF_H     = 1                         ; reference adc (=mid position) value high
SREG_BACK0    = 2                         ; SREG backup during interrupt

WORK0         = 16                        ; temporary register
WORK1         = 17                        ; temporary register
PARAM0        = 18                        ; parameter register

ADC_L         = 20                        ; adc result low
ADC_H         = 21                        ; adc result high
TIMER_UPPER   = 22                        ; upper timer 0 value

;*******************************************************************************
;   port definitions
;*******************************************************************************
PUSHBUTTON    = 0                         ; input for reference pushbutton
POTENTIOMETER = 2                         ; analog control input
RC_OUT        = 4                         ; RC pulse output


;*******************************************************************************
;   Resetvector
;*******************************************************************************
    .section .text

;*******************************************************************************
;   Interruptvector
;*******************************************************************************
.global TIM0_COMPA_vect
TIM0_COMPA_vect:                          ; Timer/Counter0 Compare Match A
    in     SREG_BACK0,AVR_STATUS_ADDR     ; save SREG
    inc    TIMER_UPPER                    ; increment upper timer value
    out    AVR_STATUS_ADDR,SREG_BACK0     ; restore SREG
    reti                                  ; over and out

;************************************************************************************
;    Main
;************************************************************************************
.global main
main:
    ; prepare port b
    sbi    _SFR_IO_ADDR(PORTB),PUSHBUTTON ; enable internal pullups for Pushbutton
    ldi    WORK0,(1<<RC_OUT)              ; RC pin as output
    out    DDRB,WORK0                     ; setup port B direction
    ; setup ADC
    ldi    WORK0,(1<<ADC1D)               ; Disable ADC pin as digital input
    out    DIDR0,WORK0                    ; set
    ldi    WORK0,(1<<MUX0)                ; ADC1
    out    ADMUX,WORK0                    ; Setup multiplexer
    ; Enable ACD / Prescaler 128 (62,5kHz -> samling frq >= 2500Hz)
    ldi    WORK0,(1<<ADEN)+(1<<ADPS2)+(1<<ADPS1)+(1<<ADPS0)
    out    ADCSRA,WORK0                   ; prepare ADC
    
    ; Wait for 0.5s to start up
    ldi    PARAM0,50                      ; 100*10ms
    rcall  wait_n10ms                     ; do wait

    ; Get inital state of the adc (reference position)
    rcall  adc_conv                       ; get adc
    movw   ADC_REF_L, ADC_L               ; store reference position

    ; setup Timer0
    ldi    TIMER_UPPER,0                  ; init upper timer value
    ldi    WORK0,(1<<WGM01)               ; mode: clear on compare match
    out    _SFR_IO_ADDR(TCCR0A),WORK0     ; set mode of operation
    ldi    WORK0,250-1                    ; compare match every 250탎
    out    _SFR_IO_ADDR(OCR0A),WORK0      ; set compare match value
    ldi    WORK0,(1<<OCIE0A)              ; Timer0 compare match interrupt
    out    _SFR_IO_ADDR(TIMSK),WORK0      ; Enable interrupt
    sei                                   ; global enable
    ldi    WORK0,(1<<CS01)                ; Clk/8 -> 1Mhz
    out    _SFR_IO_ADDR(TCCR0B),WORK0     ; Start Timer0 with 1Mhz

main_loop:
    ; in the first millisecond get next adc and button
    sbi    _SFR_IO_ADDR(PORTB),RC_OUT     ; set RC out
    rcall  adc_conv                       ; get adc
    sbis   _SFR_IO_ADDR(PINB),PUSHBUTTON  ; check if button is pressed
    movw   ADC_REF_L, ADC_L               ; if button pressed store reference position
    ; TODO calculate pulse width
    sub    ADC_L,ADC_REF_L                ; calc difference low
    sbc    ADC_H,ADC_REF_H                ; calc difference high
    ldi    WORK0,1                        ; shift loop count
zz_shift:
    asr    ADC_H
    ror    ADC_L
    dec    WORK0
    brne   zz_shift
    inc    ADC_H

    ldi    WORK0,41                       ; set offset low
    add    ADC_L,WORK0                    ; add offset low
    ldi    WORK0,4                        ; set offset high
    adc    ADC_H,WORK0                    ; add offset high

zz_wait_1ms:
    cpi    TIMER_UPPER,4                  ; 4*250탎 = 1ms
    brne   zz_wait_1ms                    ; wait for first ms to finish
    ; wait for pulse to finish
zz_finish_loop:
    subi   ADC_L,1
    brcc   zz_finish_loop
    subi   ADC_H,1
    brpl   zz_finish_loop

    cbi    _SFR_IO_ADDR(PORTB),RC_OUT     ; reset RC out

zz_wait_20ms:                
    cpi    TIMER_UPPER,80                 ; 80*250탎 = 20ms
    brne   zz_wait_20ms                   ; wait for first 20ms to finish
    ldi    TIMER_UPPER,0                  ; reset upper counter

    rjmp   main_loop                      ; continue with next loop



;------------------------------------------------------------------------------------
; wait_n10ms:             Waits for PARAM0*10ms
;                Registers changed:    PARAM0,WORK1,WORK0
;------------------------------------------------------------------------------------
wait_n10ms:
    ldi    WORK0,199                      ; Loop 2 -> 10ms
zz_wait_loop2:
    ldi    WORK1,133                      ; Loop 1 -> 500탎
zz_wait_loop1:
    dec    WORK1                          ; decrement loop count 1
    brne   zz_wait_loop1                  ; repeat loop 1
    dec    WORK0                          ; decrement loop count 2
    brne   zz_wait_loop2                  ; repeat loop 2
    dec    PARAM0                         ; decrement parameter loop count
    brne   wait_n10ms                     ; repeat parameter loop
    ret


;------------------------------------------------------------------------------------
;    adc_conv:     Multiple conversion of adc. 
;            The result is found in ADC_H:ADC_L
;            WORK0 is changed.
;------------------------------------------------------------------------------------
adc_conv:
    ; setup adc sum & loop count
    ldi    WORK1,4                        ; setup adc loop
    ldi    ADC_L,0                        ; init adc sum low
    ldi    ADC_H,0                        ; init adc sum high
zz_adc_loop:
    sbi    _SFR_IO_ADDR(ADCSRA),ADSC      ; start conversion
zz_adc_wait:
    sbic   _SFR_IO_ADDR(ADCSRA),ADSC      ; check for adc to complete
    rjmp   zz_adc_wait                    ; not yet finshed wait more
    in     WORK0,ADCL                     ; get adc result low
    add    ADC_L,WORK0                    ; add to sum low
    in     WORK0,ADCH                     ; get adc result high
    adc    ADC_H,WORK0                    ; add to sum high
    dec    WORK1                          ; decrement loop count
    brne   zz_adc_loop                    ; one more adc to go
    ret                                   ; over and out
