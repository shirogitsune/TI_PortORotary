/*
 * main.c
 *
 *  Rotary RedCell - A revision of the Sparkfun Electronics Port-O-Rotary using
 *  the Texas Instruments MSP430 value line of microcontrollers on the LaunchPad platform
 *
 *  Created on: Aug 7, 2011
 *  Author: Justin Pearce (whitefox@guardianfox.net)
 *  Website: http://www.guardianfox.net
 */
/* Includes */
#include <io.h>
#include <stdbool.h>
#include <string.h>
/*/Includes */

/*
 * MSP430 Pin Address Space for P1 (Port 1)
 * The address space for the GPIO pins along Port 1 for the 14 pin MSP430G2231 is masked as a
 * single byte, using 1 for the flag in the byte for the pin position. In the C code, they
 * use a 2 digit hexadecimal number to represent either half of the byte
 * Hex Mask: 0x80 0x40 0x20 0x10 0x08 0x04 0x02 0x01
 * Bin Pos:  128  64   32   16   8    4    2    1
 * Pin #:    1.7  1.6  1.5  1.4  1.3  1.2  1.1  1.0
 *
 * Reference chart:
 *  0x   0   0
 *     0000|0001 = 0x01 = Pin 1.0 = HOOK: Rotary Phone Hook switch
 *     0000|0010 = 0x02 = Pin 1.1 = CELL_UART_TXD: TXD to ADH8066 RX
 *     0000|0100 = 0x04 = Pin 1.2 = CELL_UART_RXD: RXD from ADH8066 TX
 *     0000|1000 = 0x08 = Pin 1.3 = ROTARY: Rotary Phone Dial Increment Switch
 *     0001|0000 = 0x10 = Pin 1.4 = ROTARY_END: Rotary Dial Rest Position Switch
 *     0010|0000 = 0x20 = Pin 1.5 = Not Used
 *     0100|0000 = 0x40 = Pin 1.6 = Not Used
 *     1000|0000 = 0x80 = Pin 1.7 = Not Used
 */

/* DEFINES */
#define HOOK			0x01 // 1.0 Rotary Phone Hook switch
#define CELL_UART_TXD   	0x02 // 1.1->RX TXD to ADH8066 RX
#define	CELL_UART_RXD	        0x04 // 1.2<-TX RXD from ADH8066 TX
#define ROTARY			0x08 // 1.3 Rotary Phone Dial Increment Switch
#define ROTARY_END		0x10 // 1.4 Rotary Dial Rest Position Switch
/*------------------------------------------------------------------------------
 * Conditions for 9600 Baud SW UART, SMCLK = 1MHz
 *------------------------------------------------------------------------------
 */
#define UART_TBIT_DIV_2     (1000000 / (9600 * 2))
#define UART_TBIT           (1000000 / 9600)

/*------------------------------------------------------------------------------
 * Global Variables
 *------------------------------------------------------------------------------ 
 */
int CURR_DIGIT;
char *CURR_PHONE;
int OFFHOOK;

/* Function: main
 * Obviously the main function of the program. We're gonna use interrupts to save power
 * @Args: None
 * @Returns: None
 */
void main(){

	WDTCTL = WDTPW + WDTHOLD; //Shut off Watchdog Timer
	DCOCTL = 0x00;  // Set DCOCLK to 1MHz
	BCSCTL1 = CALBC1_1MHZ;
	DCOCTL = CALDCO_1MHZ;
	
	P1OUT = 0x00; // Initialize GPIO
	P1SEL = CELL_UART_TXD + CELL_UART_RXD;  // Timer function for TXD/RXD pins

	/* Setup Timer A: We'll use this for finding if we need to start dialing */
	TACCR0 = 62500 - 1;  // a period of 62,500 cycles is 0 to 62,499.
	TACCTL0 = CCIE;      // Enable interrupts for CCR0.
	TACTL = TASSEL_2 + ID_3 + MC_0 + TACLR + TAIE;
	
	/* Setup Timer B: We'll use this with the serial transmission */
	TBCCR1 = 62500 - 1;  // a period of 62,500 cycles is 0 to 62,499.
	TBCCTL1 = CCIE;      // Enable interrupts for CCR1.
	TBCTL = TBSSEL_2 + ID_3 + MC_0 + TBCLR + TBIE;
	
	/* Set the following pins to input */
	P1DIR ^= HOOK;
	P1DIR ^= CELL_UART_RXD;
	P1DIR ^= ROTARY;
	P1DIR ^= ROTARY_END;
	/* Set the following pins to output */
	P1DIR |= CELL_UART_TXD;

	/* Set Interrupts for relevant ports  */
	P1IE |= HOOK;
	P1IE |= CELL_UART_RXD;
	P1IE |= ROTARY;
	P1IE |= ROTARY_END;
	
	/* Set Hi/Lo Direction */
	P1IES ^= HOOK; 			// Lo -> Hi
	P1IES ^= CELL_UART_RXD;		// Lo -> Hi
	P1IES ^= ROTARY;		// Lo -> Hi
	P1IES |= ROTARY_END;		// Hi -> Lo
	
	/* Kill Interrupts for relevant ports so they don't trigger right away */
	P1IFG &= ~HOOK;
	P1IFG &= ~CELL_UART_RXD;
	P1IFG &= ~ROTARY;
	P1IFG &= ~ROTARY_END;

	/* Drop into low power mode and listen for interrupts */
	_BIS_SR(LPM4_bits + GIE);
}

#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void){
  
  switch(P1IFG){
    case HOOK:
      // Hook switch trigger
      if((P1IES & HOOK) != 0){
	//Off Hook
	OFFHOOK = 1;
	P1IES |= HOOK; // Hi->Lo
	P1IFG &= ~HOOK;
      }else{
	//On Hook
	char *cmd = "ATH;\r\0";
	//Clear phone number
	CURR_PHONE = '\0';
	CURR_DIGIT = 0;
	OFFHOOK = 0;
	//Stop/Clear Timer
	TACTL &= MC_0 + TACLR;
	//Reset Pin/Interrupt
	P1IES ^= HOOK; // Lo->Hi
	P1IFG &= ~HOOK;
      }
    break;
    case CELL_UART_RXD:
      // Cell Serial Receive trigger
    break;
    case ROTARY:
      // Rotary counter trigger
      if(OFFHOOK == 1){
	++CURR_DIGIT;
	//Reset Interrupt
	P1IFG &= ~ROTARY;
      }
    break;
    case ROTARY_END:
      // Rotary Dial End trigger
     if(OFFHOOK==1){
	if((P1IFG & ROTARY_END) != 0){
	  //Start/Clear Timer
	  TACTL &= MC_1 + TACLR;
	  //Append digit to phone number
	  CURR_PHONE = strcat(CURR_PHONE, (char)CURR_DIGIT);
	  //Reset Pin/Interrupt
	  P1IES |= ROTARY_END;
	  P1IFG &= ~ROTARY_END;
	}else{
	  // Stop/Clear Timer
	  TACTL &= MC_0 + TACLR;
	  //Reset Pin/Interrupt
	  P1IES ^= ROTARY_END;
	  P1IFG &= ~ROTARY_END;
	}
      }
    break;
    default:
      // Obviously there was a weird interrupt trigger....so we'll just clear it.
      P1IFG=0;
    break;
  }
  
}
#pragma vector = TIMERA0_VECTOR
__interrupt void CCR0_ISR(void) {
    // no flag clearing necessary; CCR0 has only one source, so it's automatic.
    char *cmd = strcat("ATD\0", strcat(CURR_PHONE, ";\r\0"));
    //Reset timer
    TACTL &= MC_0 + TACLR;
} // CCR0_ISR

#pragma vector = TIMERB0_VECTOR
__interrupt void CCR1_ISR(void){
  //Timer B interrupt stuff goes here...still have to figure out what this will be.
  
  TBCTL &= MC_0 + TBCLR;
}