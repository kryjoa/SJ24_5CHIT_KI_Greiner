#define F_CPU 16000000UL

#include <avr/io.h>
#include <stdio.h>
#include <avr/interrupt.h>
#include "lcd.h"
#include "lcd_definitions.h"
#include "dht.h"
#include "uart.h"
#include <util/delay.h>
#include <stdbool.h>
#include <avr/eeprom.h>

#define STX '\x02'
#define ETX '\x03'
#define BAUD_RATE 9600
#define DHT_TIMEOUT 1000

#define OCR1A_1S 15624
#define OCR1A_4S 62496
#define ACK '\x06'

volatile int8_t currentTemp = 0;
volatile int8_t currentHumi = 0;
volatile int8_t errorStatus = 0;
volatile uint8_t useInterval = 1;

volatile uint8_t sequenceNumber = 1;
#define MAX_SEQUENCE_NUMBER 255
volatile bool sendingAborted = false;
volatile bool fanrunning = false;
volatile uint8_t messagesSentWithoutAck = 0;
volatile uint8_t statusFAN = false;

volatile bool resumeNormalSend = true;

typedef struct {
	int8_t temp;
	int8_t hum;
} measurement_t;

EEMEM measurement_t eeprom_measurements[10];

volatile uint8_t measurementIndex = 0;


void timer1_init(void) {
	TCCR1B |= (1 << WGM12);
	TCCR1B |= (1 << CS12) | (1 << CS10); 
	OCR1A = OCR1A_4S;
	TIMSK1 |= (1 << OCIE1A);
	sei();
}

void button_init(void) {
	DDRB &= ~(1 << PINB1);
	PORTB |= (1 << PINB1);
	PCICR |= (1 << PCIE0);
	PCMSK0 |= (1 << PCINT1);
}

void led_init(void) {
	DDRB |= (1 << DDB0);
	PORTB &= ~(1<<PORTB0);
}

void led_init2(){
	DDRB |= (1<<DDB2);
	PORTB &= ~(1<<PORTB2);
}


void send_message(void)
{
	if(sendingAborted)
	{
		return;
	}
	
	uart_puts("\r\n");
	char uartBuffer[21];
	uart_putc(STX);
	sprintf(uartBuffer, "Temp%02d|Hum%d|SN%d", currentTemp, currentHumi, sequenceNumber);
	uart_puts(uartBuffer);
	uart_putc(ETX);
	
	sequenceNumber = (sequenceNumber < MAX_SEQUENCE_NUMBER) ? (sequenceNumber + 1) : 0;
	
	messagesSentWithoutAck++;
}


void check_ack()
{
	char received = uart_getc();
	if(received == ACK) {
		messagesSentWithoutAck = 0;
	}
	else if(received == 'r') {
		sendingAborted = false;
		messagesSentWithoutAck = 0;
		PORTB &= ~(1 << PORTB0);
	}
	else if(received == 'd') {
		sendingAborted = false;
		PORTB &= ~(1 << PORTB0);
	}
	else if(received == 'q') {
		sendingAborted = true;
		lcd_puts("ME gestoppt");
	}
	else if(received == 'e')
	{
		statusFAN = true;
		//Lüfter ein
		PORTB |= (1<<PORTB2);
		lcd_gotoxy(0,2);
		lcd_puts("Lüfter ein");
	}
	else if(received == 'a')
	{
		statusFAN = false;
		PORTB &= ~(1<<PORTB2);
		lcd_gotoxy(0,2);
		lcd_puts("Lüfter aus");
	}
	else if(received == 's')
	{
		uart_puts("\r\n");
		if(statusFAN == false)
		{
			uart_putc('f2');
		}
		else if(statusFAN == true)
		{
			uart_putc('f1');
		}
	}
	
}


ISR(TIMER1_COMPA_vect) {
	check_ack();
	
	if (!sendingAborted && resumeNormalSend) {
		if (messagesSentWithoutAck < 3)
		{
			send_message();
		}
		else
		{
			PORTB |= (1 << PORTB0);
			sendingAborted = true;
		}
	}
}



ISR(PCINT0_vect) {
	if (!(PINB & (1 << PINB1))) {
		useInterval = !useInterval;
		if (useInterval) {
			OCR1A = OCR1A_1S;
			} else {
			OCR1A = OCR1A_4S;
		}
	}
}

volatile uint8_t sendStoredFlag = 0;
void sendStoredMeasurements(void) {
	char msg[32];
	uart_puts("\r\nStored Measurements:\r\n");
	for (uint8_t i = 0; i < 10; i++) {
		// Berechne den Index so, dass der älteste Wert zuerst gesendet wird:
		uint8_t index = (measurementIndex + i) % 10;
		measurement_t meas;
		eeprom_read_block((void *)&meas, (const void *)&eeprom_measurements[index], sizeof(measurement_t));
		uart_puts("\r\n");
		uart_putc(STX);
		sprintf(msg, "Temp%02d|Hum%d|SN%d", meas.temp, meas.hum, sequenceNumber);
		uart_puts(msg);
		uart_putc(ETX);
		sequenceNumber = (sequenceNumber < MAX_SEQUENCE_NUMBER) ? (sequenceNumber + 1) : 0;
	}
}

int main(void) {
	char buffer[16];
	char buffer2[16];
	uart_init(UART_BAUD_SELECT(BAUD_RATE, F_CPU));
	lcd_init(LCD_DISP_ON);
	timer1_init();
	button_init();
	led_init();
	led_init2();
	sei();
	while (1) {
		
		
		if (sendingAborted) {
			lcd_clrscr();
			lcd_puts("ME gestoppt");
			_delay_ms(500);
			resumeNormalSend = false;
			while (sendingAborted) {
				_delay_ms(100);
			}
			
			sendStoredMeasurements();
			_delay_ms(500);
			resumeNormalSend = true;
		}
		
		errorStatus = dht_gettemperaturehumidity((int8_t *)&currentTemp, (int8_t *)&currentHumi);
		_delay_ms(500);
		if (errorStatus == 0) {
			sprintf(buffer, "Temp:%dC Hum:%d%%", currentTemp, currentHumi);
			lcd_clrscr();
			lcd_puts(buffer);
			
			// gibt den Messwert in eine Struktur
			measurement_t meas;
			meas.temp = currentTemp;
			meas.hum  = currentHumi;
			
			eeprom_write_block((const void *)&meas, (void *)&eeprom_measurements[measurementIndex], sizeof(measurement_t));
			
			measurementIndex = (measurementIndex + 1) % 10;
			
			} else {
			sprintf(buffer2, "ERROR: %d", errorStatus);
			lcd_clrscr();
			lcd_puts(buffer2);
		}
		_delay_ms(DHT_TIMEOUT);
		
	}
}