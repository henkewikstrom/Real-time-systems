#include <taskLib.h>
#include <stdio.h>
#include <kernelLib.h>
#include <semLib.h>
#include <intLib.h>
#include <iv.h>
#include <unistd.h>


#include <xlnx_zynq7k.h>


/*------------------- INCLUDES FOR THE SENDER PART ---------------*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
//#include "drv/timer/timerDev.h"

#include <inetLib.h>
#include <sockLib.h>

#define MAX_BUF 4
#define MESSAGE_MAX_LEN 20
/*------------------- END INCLUDES SENDER PART ---------------*/


/*------------------- VARIABLES SENDER PART ---------------*/
// char ip[16] = "192.168.202.189"; //ip of the slave
// int port = 8080;
/*------------------- END VARIABLES SENDER PART ---------------*/


#define REGISTER(base, offs) (*((volatile UINT32 *)((base) + (offs))))
#define BIT(i) ((1) << (i))


/* MOTOR macros */
// See section FPGA registers for more information.

#define PMOD1_BASE 0x43c20000
#define PMOD2_BASE 0x43c30000

#define MOTOR_BASE PMOD1_BASE

/* GPIO register definitions
// See Zynq-7000 Technical Reference Manual for more information
//     Section: 14.2.4 Interrupt Function (pg. 391, pg. 1348). */

/* Pin on GPIO selected for interrupt
// Note: Each bit in a register refers to one pin. Setting some bit to `1`
//       also means which pin is selected. */
#define MOTOR_IRQ_PIN BIT(2)

/* Setting a bit in DIRM to `1` makes the corresponding pin behave as an output,
// for `0` as input.
// Note: So setting this to `MOTOR_IRQ_PIN` means, that this pin is an output
//       (which it is not so do not do it!).
//       This is similar with other GPIO/INT registers. */
#define GPIO_DIRM         REGISTER(ZYNQ7K_GPIO_BASE, 0x00000284)

// Writing 1 to a bit enables IRQ from the corresponding pin.
#define GPIO_INT_ENABLE   REGISTER(ZYNQ7K_GPIO_BASE, 0x00000290)

// Writing 1 to a bit disables IRQ from the corresponding pin.
#define GPIO_INT_DISABLE  REGISTER(ZYNQ7K_GPIO_BASE, 0x00000294)

/* Bits read as `1` mean that the interrupt event has occurred on a corresponding pin.
// Writing `1` clears the bits, writing `0` leaves the bits intact. */
#define GPIO_INT_STATUS   REGISTER(ZYNQ7K_GPIO_BASE, 0x00000298)

// Setting TYPE to `0` makes interrupt level sensitive, `1` edge sensitive.
#define GPIO_INT_TYPE     REGISTER(ZYNQ7K_GPIO_BASE, 0x0000029c)

// Setting POLARITY to `0` makes interrupt active-low (falling edge),
//                     `1` active-high (raising edge).
#define GPIO_INT_POLARITY REGISTER(ZYNQ7K_GPIO_BASE, 0x000002a0)

// Setting ANY to `1` while TYPE is `1` makes interrupts act on both edges.
#define GPIO_INT_ANY      REGISTER(ZYNQ7K_GPIO_BASE, 0x000002a4)

// FPGA register definition
#define MOTOR_SR REGISTER(MOTOR_BASE, 0x4)
#define MOTOR_SR_IRC_A_MON 8
#define MOTOR_SR_IRC_B_MON 9



volatile signed irq_count;

volatile unsigned temp_a = 0;
volatile unsigned temp_b = 0;


void www();
void irc_isr(void)
{
        _Bool irc_a = (MOTOR_SR & BIT(MOTOR_SR_IRC_A_MON)) != 0;
        _Bool irc_b = (MOTOR_SR & BIT(MOTOR_SR_IRC_B_MON)) != 0;
        // ...
        
        if((irc_a != temp_a) || (irc_b != temp_b))
		{
        	if(irc_a == temp_b)
        	{
        		//clock wise
        		irq_count--;
        	}
        	else
        	{
        		//counter clock wise
        		irq_count++;
        	}
        	temp_a = irc_a;
        	temp_b = irc_b;
		}
        
        GPIO_INT_STATUS = MOTOR_IRQ_PIN; /* clear the interrupt */
}

void irc_init(void)
{
        GPIO_INT_STATUS = MOTOR_IRQ_PIN; /* reset status */
        GPIO_DIRM = 0x0;                 /* set as input */
        GPIO_INT_TYPE = MOTOR_IRQ_PIN;   /* interrupt on edge */
        GPIO_INT_POLARITY = 0x0;         /* rising edge */
        GPIO_INT_ANY = 0x0;              /* ignore falling edge */
        GPIO_INT_ENABLE = MOTOR_IRQ_PIN; /* enable interrupt on MOTOR_IRQ pin */

        intConnect(INUM_TO_IVEC(INT_LVL_GPIO), irc_isr, 0);
        intEnable(INT_LVL_GPIO);         /* enable all GPIO interrupts */
}

void irc_cleanup(void)
{
        GPIO_INT_DISABLE = MOTOR_IRQ_PIN;

        intDisable(INT_LVL_GPIO);
        intDisconnect(INUM_TO_IVEC(INT_LVL_GPIO), irc_isr, 0);
}

/* --------------------------MESSAGE SENDING PART-----------------------*/

//we can use atol to convert string to int long

int sender(char ip[], int port , char message[])/*should be 8080 */
{

  char buf[MESSAGE_MAX_LEN];	
  int sockd;
  struct sockaddr_in my_addr, srv_addr;
  int addrlen;
  
  sockd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockd == -1)
  {
    perror("Socket creation error");
    exit(1);
  }

  /* Configure client address */
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port = 0; 	/*0 */

  bind(sockd, (struct sockaddr*)&my_addr, sizeof(my_addr));
 
  /* Set server address */
  srv_addr.sin_family = AF_INET;
  inet_aton(ip, &srv_addr.sin_addr);
  srv_addr.sin_port = htons(port);
  addrlen = sizeof(srv_addr);

  strcpy(buf, message);

  sendto(sockd, buf, strlen(buf)+1, 0,(struct sockaddr*)&srv_addr, sizeof(srv_addr));

  //printf("Sent: %s\n",buf);
  
  close(sockd);
  return 0;
}

/*------------------------------------ MASTER MAIN -----------------------------------*/

int master()
{       
        char message[MESSAGE_MAX_LEN];
        
        irc_init();
        char key;       
        while (1) {
        	if (key == 'E')
        	{
        		return(1);
        	}
        	sprintf(message, "%d", irq_count);
        	sender(ip,port,message);
        }
        irc_cleanup();
}









