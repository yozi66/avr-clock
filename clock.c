#ifndef F_CPU
#define F_CPU 12000000UL // or whatever may be your frequency
#endif

#include <avr/interrupt.h>
#include <avr/io.h>

/*
 * Hardware mapping - wires and codes
 */

#define DIGIT0 (1<<PD0)
#define DIGIT1 (1<<PD1)
#define DIGIT2 (1<<PD2)
#define DIGIT3 (1<<PD6)
#define BUZZER (1<<PD3)
#define BUTTON1 (1<<PD4)
#define BUTTON2 (1<<PD5)

// display bits: 1 = off, 0 = on
//
//  a
// f b
//  g
// e c
//  d dp
//
// a - 0x80
// b - 0x20
// c - 0x04
// d - 0x08
// e - 0x10
// f - 0x40
// g - 0x02
// dp - 0x01 (colon - digit-1 only)
#define SEGMENT_DP (1<<PB0)

const __flash char decode[] = {
	0x03, // 0
	0xdb, // 1
	0x45, // 2
	0x51, // 3
	0x99, // 4
	0x31, // 5
	0x21, // 6
	0x5b, // 7
	0x01, // 8
	0x11, // 9
	/* more character codes - not used for decoding
	0x09, // A
	0xa1, // b
	0xdb, // C
	0x3f, // d
	0x25, // E
	0x2d, // F
	0x89, // H
	0xa7, // L
	0xe9, // n
	0xe1, // o
	0xfd // -
	*/
};

#define BLANK 0xFF

/*
 * Timing definitions and display driver code
 */

// run the main loop every 10ms (100 interrupts)
#define MAIN_LOOP_TICKS 100

// interrupt counter - the main loop waits for this variable
volatile char ticks = 0;

// display one digit for 5 ms (50 x 100us = 50 interrupts)
#define DIGIT_DURATION 50
#define DIGIT_COUNT 4

volatile char display[DIGIT_COUNT];
const __flash char digits[DIGIT_COUNT] = {DIGIT0,DIGIT1,DIGIT2,DIGIT3};
unsigned char digit_time;
unsigned char current_digit = 0;
volatile unsigned char brightness = 19; // 1:min, 50:max

// logarithmic brightness steps give linear feeling
#define BRIGHTSTEP_COUNT 9
const __flash unsigned char brightsteps[BRIGHTSTEP_COUNT] = {1,2,3,5,7,12,19,31,50};

// https://www.nongnu.org/avr-libc/user-manual/group__avr__interrupts.html
// timer0 compare A interrupt
ISR(TIMER0_COMPA_vect) {
	
	if (++digit_time >= brightness) {
		DDRD &= ~(digits[current_digit]); // switch current digit off
	}
	
	if (digit_time == DIGIT_DURATION) {
		digit_time = 0;
		if (++current_digit == DIGIT_COUNT) {
			current_digit = 0;
		}
		DDRB = display[current_digit]; // show next digit
		DDRD |= digits[current_digit]; // switch next digit on
	}

	if (++ticks == MAIN_LOOP_TICKS) {
		ticks = 0;
	}

}

/*
 * button & event handling
 */

// debouncing - 50ms (5 x 10)
#define DEBOUNCE_TIME 5

// long press - 700ms (70 x 10)
#define LONG_PRESS_TIME 70

// auto repeat - 250ms (25 x 10)
#define AUTO_REPEAT_INTERVAL 25

// events
#define E_BUTTON1_SHORT  1
#define E_BUTTON1_LONG   2
#define E_BUTTON1_REPEAT 4
#define E_BUTTON2_SHORT  0x10
#define E_BUTTON2_LONG   0x20
#define E_BUTTON2_REPEAT 0x40

char getButtonEvent(unsigned char * counter, char mask) {
	char event = 0;
	if (PIND & mask) {
		// button is up
		if (*counter >= DEBOUNCE_TIME && *counter < LONG_PRESS_TIME) {
			event = E_BUTTON1_SHORT;
		}
		*counter = 0;
	} else {
		// button is down
		if (++(*counter) == LONG_PRESS_TIME) {
			event = E_BUTTON1_LONG;
		} else if (*counter == LONG_PRESS_TIME + AUTO_REPEAT_INTERVAL) {
			event = E_BUTTON1_REPEAT;
			*counter -= AUTO_REPEAT_INTERVAL;
		}
	}
	return event;
}

/*
 * states & modes - high nibble: what to display, low nibble: digits to blink
 */
#define M_TIME			0x00
#define M_TIME_SET_HOURS 	0x0c
#define M_TIME_SET_TENS		0x02
#define M_TIME_SET_MINUTES 	0x01
#define M_SECONDS		0x10
#define M_SECONDS_SET 		0x13
#define M_BRIGHTNESS 	0x20
#define M_BRIGHTNESS_SET 	0x21
#define M_ALARM_BEEP	0x70
#define M_ALARM 		0x80
#define M_ALARM_SET 		0x83
#define M_ALARM_TIME	0x90
#define M_ALARM_SET_HOURS 	0x9c
#define M_ALARM_SET_TENS 	0x92
#define M_ALARM_SET_MINUTES 0x91

// #define M_DEBUG 0xF0

int main(void)
{
	unsigned char hundredths = 0;
	
	unsigned char seconds = 0;
	unsigned char minutes = 0;
	unsigned char hours = 12;
	
	char display_1 = display[1];
	
	unsigned char brightstep = 6;
	
	unsigned char mode = M_TIME;
	// mode = M_DEBUG;
	char event; // button events
	unsigned char button1_pressed = 0; // button pressed time in hundredths
	unsigned char button2_pressed = 0; // button pressed time in hundredths
	
	int ten_hour;
	
	char alarm_enabled = 1;
	unsigned char alarm_hours = 12;
	unsigned char alarm_minutes = 1;

	// set PortB to LOW
	// PORTB = 0; this is the power-on default value
	// set PortB to input
	// DDRB = 0x00; this is the power-on default value
	
	PORTD |= BUTTON1 | BUTTON2; // enable pullups for the buttons
	
	// set Timer0 to 100 usec @ 12MHz clock
	// interrupt for every 1200 clock cycle
	// enable interrupt on TOP: OCF0A
	
	TCCR0A = 0x02;  // CTC mode (count from 0 to OCR0A, no PWM output)
	TCCR0B = 0x02;	// prescaler: 8
	OCR0A = 149;    // count from 0 to 149 (freq.div=150)
	TIMSK = 0x01;   // enable Timer0 output compare match A interrupt
	
	sei(); // enable interrupts

    while(1) {
    	// run every 10 ms - wait for ticks 0->1 transition
    	while(ticks != 0); // wait for ticks == 0;
    	while(ticks == 0); // wait for ticks != 0;

    	// timekeeping
    	if (++hundredths == 100) {
    		hundredths = 0;
    		if (++seconds == 60) {
    			seconds = 0;
    			if (++minutes == 60) {
    				minutes = 0;
    				if (++hours == 24) {
    					hours = 0;
    				}
    			}
				if (alarm_enabled && hours == alarm_hours && minutes == alarm_minutes) {
					// start the alarm
					mode = M_ALARM_BEEP;
				} else if (mode == M_ALARM_BEEP) {
					// switch off alarm after one minute
					mode = M_TIME;
				}
    		}
    	}
    	
    	// generate button events
    	event = getButtonEvent(&button1_pressed, BUTTON1) |
    			getButtonEvent(&button2_pressed, BUTTON2) << 4 ;

    	// process the events
    	if (event) {
    		switch (mode) {
    		case M_TIME:
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_ALARM_SET;
    			}
    			if (event & E_BUTTON1_LONG) {
    				mode = M_TIME_SET_HOURS;
    			}
    			if (event & E_BUTTON2_SHORT) {
    				mode = M_SECONDS;
    			}
    			break;
    		case M_TIME_SET_HOURS:
    			if (event & E_BUTTON1_LONG) {
    				mode = M_TIME;
    			}
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_TIME_SET_TENS;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				if (++hours == 24) {
    					hours = 0;
    				}
    			}
    			break;
    		case M_TIME_SET_TENS:
    			if (event & E_BUTTON1_LONG) {
    				mode = M_TIME;
    			}
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_TIME_SET_MINUTES;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				minutes += 10;
    				if (minutes > 59) {
    					minutes -= 60;
    				}
    			}
    			break;
    		case M_TIME_SET_MINUTES:
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_TIME;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				if (++minutes % 10 == 0) {
    					minutes -= 10;
    				}
    			}
    			break;
    		case M_SECONDS:
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_BRIGHTNESS_SET;
    			}
    			if (event & E_BUTTON2_SHORT) {
    				mode = M_TIME;
    			}
    			if (event & E_BUTTON1_LONG) {
    				mode = M_SECONDS_SET;
    			}
    			break;
    		case M_SECONDS_SET:
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_TIME;
    			}
    			if (event & E_BUTTON2_SHORT) {
    				if (seconds > 30) {
    					if (++minutes == 60) {
    						minutes = 0;
    						if (++hours == 24) {
    							hours = 0;
    						}
    					}
    				}
    				seconds = 0;
    				hundredths = 25;
    				mode = M_TIME;
    			}
    			break;
    		case M_ALARM_SET:
    			if (event & E_BUTTON1_SHORT) {
    				mode = alarm_enabled ? M_ALARM_SET_HOURS : M_TIME;
    			}
    			if (event & E_BUTTON1_LONG) {
    				mode = M_ALARM_SET_HOURS;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				alarm_enabled = ! alarm_enabled;
    			}
    			break;
    		case M_ALARM_SET_HOURS:
    			if (event & E_BUTTON1_LONG) {
    				mode = M_TIME;
    			}
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_ALARM_SET_TENS;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				if (++alarm_hours == 24) {
    					alarm_hours = 0;
    				}
    			}
    			break;
    		case M_ALARM_SET_TENS:
    			if (event & E_BUTTON1_LONG) {
    				mode = M_TIME;
    			}
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_ALARM_SET_MINUTES;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				alarm_minutes += 10;
    				if (alarm_minutes > 59) {
    					alarm_minutes -= 60;
    				}
    			}
    			break;
    		case M_ALARM_SET_MINUTES:
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_TIME;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				if (++alarm_minutes % 10 == 0) {
    					alarm_minutes -= 10;
    				}
    			}
    			break;
    		case M_BRIGHTNESS_SET:
    			if (event & E_BUTTON1_SHORT) {
    				mode = M_TIME;
    			}
    			if (event & (E_BUTTON2_SHORT | E_BUTTON2_REPEAT)) {
    				if (++brightstep >= BRIGHTSTEP_COUNT) {
    					brightstep = 0;
    				}
    				brightness = brightsteps[brightstep];
    			}
    			break;
    		case M_ALARM_BEEP:
    			if (event & (E_BUTTON1_SHORT | E_BUTTON2_SHORT)) {
    				mode = M_TIME;
    			}
    			break;
    		}
    	}
    	
    	// update the display according to the mode
    	switch (mode & 0xF0) {
    	case M_ALARM_BEEP:
    	case M_TIME:
    		ten_hour = hours / 10;
    		display[0] = ten_hour ? decode[ten_hour] : BLANK; // hide leading zero
    		display_1  = decode[hours % 10];
    		display[2] = decode[minutes / 10];
    		display[3] = decode[minutes % 10];
    		break;
    	case M_SECONDS:
    		display[0] = BLANK;
    		display_1  = BLANK;
    		display[2] = decode[seconds / 10];
    		display[3] = decode[seconds % 10];
    		break;
    	case M_ALARM:
    		display[0] = 0x09; // 'A'
    		display_1  = 0xA7; // 'L'
    		display[2] = alarm_enabled ? 0xe1 : 0xfd; // o:-
    		display[3] = alarm_enabled ? 0xe9 : 0xfd; // n:-
    		break;
    	case M_ALARM_TIME:
    		ten_hour = alarm_hours / 10;
    		display[0] = ten_hour ? decode[ten_hour] : BLANK; // hide leading zero
    		display_1  = decode[alarm_hours % 10];
    		display[2] = decode[alarm_minutes / 10];
    		display[3] = decode[alarm_minutes % 10];
    		break;
    	case M_BRIGHTNESS:
    		display[0] = BLANK;
    		display_1  = BLANK;
    		display[2] = 0xa1; // 'b'
    		display[3] = decode[brightstep+1];
    		break;
    		/*
    	case M_DEBUG:
    		if (event) {
				display[0] = decode[event / 16];
				display[1] = decode[event % 16];
    		}
    		display[2] = decode[button2_pressed / 10 % 10];
    		display[3] = decode[button2_pressed % 10];
			if (event & E_BUTTON1_SHORT) {
				DDRD |= BUZZER;
			}
			if (event & E_BUTTON2_SHORT) {
				DDRD &= ~BUZZER;
			}
    		break;
    		*/
    	}
    	
    	// flash & beep with 3 Hz
		if ((hundredths >= 17 && hundredths < 34) || 
			(hundredths >= 50 && hundredths < 67) ||
		    (hundredths >= 83)) {
			if (button2_pressed < LONG_PRESS_TIME) {
				// flash the selected digits
				if (mode & 0x08) {
					display[0] = BLANK;
				}
				if (mode & 0x04) {
					display_1 = BLANK;
				}
				if (mode & 0x02) {
					display[2] = BLANK;
				}
				if (mode & 0x01) {
					display[3] = BLANK;
				}
			}
			if (mode == M_ALARM_BEEP) {
				// short beep
				DDRD |= BUZZER;
			}
		} else {
			// silence
			DDRD &= ~BUZZER;
		}
		if (hundredths < 50 || (mode & M_ALARM)) {
			// switch colon on (=LOW=input)
			display[1] = display_1 & ~SEGMENT_DP;
		} else {
			// switch colon off
			display[1] = display_1 | SEGMENT_DP;
		}
    	    	
    }
}
