PROJECT = servo-tester
MCU = attiny25
TARGET = servo-tester
SOURCES = \
    servo-tester-tiny25.S

# toolchain
CC = avr-gcc
LD = avr-ld
AS = avr-as
SIZE = avr-size
OBJCOPY = avr-objcopy

# avr-tools
PROGRAMMER ?= dragon_isp
#PROGRAMMER ?= dragon_dw

# from here the code should be common and reusable
OBJECTS=$(SOURCES:.S=.o)
CFLAGS+=-x assembler-with-cpp -c
LDFLAGS=

all: hex eeprom size

hex: $(TARGET).hex

eeprom: $(TARGET).eep

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -j .data -j .text $(TARGET).elf $(TARGET).hex

$(TARGET).eep: $(TARGET).elf
	$(OBJCOPY) -O ihex -j .eeprom --change-section-lma .eeprom=1 $(TARGET).elf $(TARGET).eep

$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -mmcu=$(MCU) $(OBJECTS) -o $(TARGET).elf

.S.o:
	$(CC) $(CFLAGS) -mmcu=$(MCU) $< -o $@

size: $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU) $(TARGET).elf

program:
	avrdude -p$(MCU) -c$(PROGRAMMER) -Uflash:w:$(TARGET).hex:a

clean:
	rm -rf *.o
	rm -rf *.elf
	rm -rf *.eep
	rm -rf *.hex

.PHONY: size program clean 
