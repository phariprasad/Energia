/*
 ************************************************************************
 *	HardwareSerial.cpp
 *
 *	Arduino core files for MSP430
 *		Copyright (c) 2012 Robert Wessels. All right reserved.
 *
 *
 ***********************************************************************

 2013-12-23 Limited size for RX and TX buffers, by spirilis

 
  Derived from:
  HardwareSerial.cpp - Hardware serial library for Wiring
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Modified 23 November 2006 by David A. Mellis
  Modified 28 September 2010 by Mark Sproul
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "wiring_private.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_uart.h"
#include "driverlib/gpio.h"
#include "driverlib/debug.h"
#include "driverlib/interrupt.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/arcm.h"
#include "driverlib/uart.h"
#include "driverlib/systick.h"
#include "HardwareSerial.h"

#define TX_BUFFER_EMPTY    (txReadIndex == txWriteIndex)
#define TX_BUFFER_FULL     (((txWriteIndex + 1) % txBufferSize) == txReadIndex)

#define RX_BUFFER_EMPTY    (rxReadIndex == rxWriteIndex)
#define RX_BUFFER_FULL     (((rxWriteIndex + 1) % rxBufferSize) == rxReadIndex)

#define UART_BASE g_ulUARTBase[uartModule]

static const unsigned long g_ulUARTBase[2] =
{
	UARTA0_BASE, UARTA1_BASE
};

//*****************************************************************************
//
// The list of possible interrupts for the console UART.
//
//*****************************************************************************
static const unsigned long g_ulUARTInt[2] =
{
	INT_UARTA0, INT_UARTA1
};

//*****************************************************************************
//
// The list of UART peripherals.
//
//*****************************************************************************
static const ArcmPeripherals_t  g_ulUARTPeriph[2] =
{
	ARCM_UARTA0, ARCM_UARTA1
};
//*****************************************************************************
//
// The list of UART GPIO configurations.
//
//*****************************************************************************
static const unsigned long g_ulUARTConfig[2][2] =
{
	{PIN_57, PIN_55}, {PIN_2, PIN_1}
};

void (*g_UARTIntHandlers[2])(void) =
{
	UARTIntHandler, UARTIntHandler1
};

// Constructors ////////////////////////////////////////////////////////////////
HardwareSerial::HardwareSerial(void)
{
	txWriteIndex = 0;
	txReadIndex = 0;
	rxWriteIndex = 0;
	rxReadIndex = 0;
	uartModule = 0;

//	txBuffer = (unsigned char *) 0xFFFFFFFF;
//	rxBuffer = (unsigned char *) 0xFFFFFFFF;
	txBufferSize = SERIAL_BUFFER_SIZE;
	rxBufferSize = SERIAL_BUFFER_SIZE;
}

HardwareSerial::HardwareSerial(unsigned long module) 
{
	txWriteIndex = 0;
	txReadIndex = 0;
	rxWriteIndex = 0;
	rxReadIndex = 0;
	uartModule = module;

	//txBuffer = (unsigned char *) 0xFFFFFFFF;
	//rxBuffer = (unsigned char *) 0xFFFFFFFF;
	txBufferSize = SERIAL_BUFFER_SIZE;
	rxBufferSize = SERIAL_BUFFER_SIZE;
}

// Private Methods //////////////////////////////////////////////////////////////
void
HardwareSerial::flushAll(void)
{
	/* wait for transmission of outgoing data */
	while(!TX_BUFFER_EMPTY){}

	txReadIndex = 0;
	txWriteIndex = 0;

	/* Flush the receive buffer. */
	rxReadIndex = 0;
	rxWriteIndex = 0;
}

void
HardwareSerial::primeTransmit(unsigned long ulBase)
{
	/* Do we have any data to transmit? */
	if(!TX_BUFFER_EMPTY) {
		/* Disable the UART interrupt. If we don't do this there is a race
		 * condition which can cause the read index to be corrupted. */
		MAP_IntDisable(g_ulUARTInt[uartModule]);
		//
		// Yes - take some characters out of the transmit buffer and feed
		// them to the UART transmit FIFO.
		//
		while(!TX_BUFFER_EMPTY) {
			while(MAP_UARTSpaceAvail(ulBase) && !TX_BUFFER_EMPTY){
				MAP_UARTCharPutNonBlocking(ulBase, txBuffer[txReadIndex]);

				txReadIndex = (txReadIndex + 1) % txBufferSize;
			}
		}

		/* Reenable the UART interrupt */
		MAP_IntEnable(g_ulUARTInt[uartModule]);
	}
}

// Public Methods //////////////////////////////////////////////////////////////

void HardwareSerial::begin(unsigned long baud)
{
	baudRate = baud;

	/* Initialize the UART. */
	UARTClockSourceSet(UART_BASE, UART_CLOCK_SYSTEM);
	MAP_ArcmPeripheralEnable(g_ulUARTPeriph[uartModule]);

	MAP_PinTypeUART(g_ulUARTConfig[uartModule][0], PIN_MODE_3);
	MAP_PinTypeUART(g_ulUARTConfig[uartModule][1], PIN_MODE_3);

	MAP_UARTConfigSetExpClk(UART_BASE, MAP_ArcmPeripheralClkGet(g_ulUARTPeriph[uartModule]), baudRate,
                            (UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE |
                             UART_CONFIG_WLEN_8));

	/* Set the UART to interrupt whenever the TX FIFO is almost empty or
	 * when any character is received. */
	MAP_UARTFIFOLevelSet(UART_BASE, UART_FIFO_TX1_8, UART_FIFO_RX1_8);

	flushAll();
	MAP_UARTIntEnable(UART_BASE, UART_INT_RX | UART_INT_TX);
	MAP_IntEnable(g_ulUARTInt[uartModule]);

	/* Enable the UART operation. */
	MAP_UARTEnable(UART_BASE);

	/* The UART sends garbage if the SysTick is enabled before the UART.
	 * Reenabling the SysTick interrupt seems to work around the issue. */
	//SysTickIntEnable();
}

void HardwareSerial::setModule(unsigned long module)
{
	MAP_UARTIntDisable(UART_BASE, UART_INT_RX | UART_INT_TX);
	MAP_UARTIntUnregister(UART_BASE);
	uartModule = module;
	begin(baudRate);
}

void HardwareSerial::end()
{
	unsigned long ulInt = MAP_IntMasterDisable();

	flushAll();

	/* If interrupts were enabled when we turned them off, 
	 * turn them back on again. */
	if(!ulInt) {
		MAP_IntMasterEnable();
	}

	MAP_UARTIntDisable(UART_BASE, UART_INT_RX | UART_INT_TX);
	MAP_UARTIntUnregister(UART_BASE);
}

int HardwareSerial::available(void)
{
	return((rxWriteIndex >= rxReadIndex) ?
		(rxWriteIndex - rxReadIndex) : rxBufferSize - (rxReadIndex - rxWriteIndex));
}

int HardwareSerial::peek(void)
{
	unsigned char cChar = 0;

	/* Wait for a character to be received. */
	while(RX_BUFFER_EMPTY) {
		/* Block waiting for a character to be received (if the buffer is
		 * currently empty). */
	}

	/* Read a character from the buffer. */
	cChar = rxBuffer[rxReadIndex];

	/* Return the character to the caller. */
	return(cChar);
}

int HardwareSerial::read(void)
{
	unsigned char cChar = peek();

	rxReadIndex = ((rxReadIndex) + 1) % rxBufferSize;
	return(cChar);
}

void HardwareSerial::flush()
{
	while(!TX_BUFFER_EMPTY);
}

size_t HardwareSerial::write(uint8_t c)
{
	unsigned int numTransmit = 0;

	/* Check for valid arguments. */
	ASSERT(c != 0);

	/* Send the character to the UART output. */
	while (TX_BUFFER_FULL);

	txBuffer[txWriteIndex] = c;
	txWriteIndex = (txWriteIndex + 1) % txBufferSize;
	numTransmit ++;

	/* If we have anything in the buffer, make sure that the UART is set
	 * up to transmit it. */
	if(!TX_BUFFER_EMPTY) {
		primeTransmit(UART_BASE);
		MAP_UARTIntEnable(UART_BASE, UART_INT_TX);
	}

	/* Return the number of characters written. */
	return(numTransmit);
}

void HardwareSerial::UARTIntHandler(void){
	unsigned long ulInts;
	long lChar;

	/* Get and clear the current interrupt source(s) */
	ulInts = MAP_UARTIntStatus(UART_BASE, true);
	MAP_UARTIntClear(UART_BASE, ulInts);

	/* Are we being interrupted because the TX FIFO has space available? */
	if(ulInts & UART_INT_TX) {
		/* Move as many bytes as we can into the transmit FIFO. */
		primeTransmit(UART_BASE);

		/* If the output buffer is empty, turn off the transmit interrupt. */
		if(TX_BUFFER_EMPTY) {
			MAP_UARTIntDisable(UART_BASE, UART_INT_TX);
		}
	}

	if(ulInts & (UART_INT_RX | UART_INT_TX)) {
		while(MAP_UARTCharsAvail(UART_BASE)) {
			/* Read a character */
			lChar = MAP_UARTCharGetNonBlocking(UART_BASE);

			/* If there is space in the receive buffer, put the character
			 * there, otherwise throw it away. */
			uint8_t volatile full = RX_BUFFER_FULL;
			if(full) break;

			rxBuffer[rxWriteIndex] = (unsigned char)(lChar & 0xFF);
			rxWriteIndex = ((rxWriteIndex) + 1) % rxBufferSize;

			/* If we wrote anything to the transmit buffer, make sure it actually
			 * gets transmitted. */
		}

		primeTransmit(UART_BASE);
		MAP_UARTIntEnable(UART_BASE, UART_INT_TX);
	}
}

void UARTIntHandler(void)
{
	Serial.UARTIntHandler();
}

void UARTIntHandler1(void)
{
	Serial1.UARTIntHandler();
}

void serialEvent() __attribute__((weak));
void serialEvent() {}
void serialEvent1() __attribute__((weak));
void serialEvent1() {}

void serialEventRun(void)
{
	if (Serial.available()) serialEvent();
	if (Serial1.available()) serialEvent1();
}

HardwareSerial Serial;
HardwareSerial Serial1(1);
