#include <taskLib.h>
#include <stdio.h>
#include <kernelLib.h>
#include <semLib.h>
#include <intLib.h>
#include <iv.h>
#include <unistd.h>


#include <xlnx_zynq7k.h>


/*------------------- INCLUDES FOR THE RECEIVER PART ---------------*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>

#include <inetLib.h>
#include <sockLib.h>

/*------------------- END INCLUDES FOR THE RECEIVER PART ---------------*/

/*------------------- INCLUDES FOR THE SERVER PART ---------------*/

#include <netdb.h>


#define SERVER_PORT     80 /* Port 80 is reserved for HTTP protocol */
#define SERVER_MAX_CONNECTIONS  20

/*------------------- END INCLUDES FOR THE SERVER PART ---------------*/

/*------------------- VARIABLES SENDER PART ---------------*/
//int port = 8080;
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


#define PWM_CONTROL		REGISTER(PMOD1_BASE, 0x0)
#define PWM_PERIOD_SR 	REGISTER(PMOD1_BASE, 0x0008)
#define PWM_DUTY_CR 	REGISTER(PMOD1_BASE, 0x000C)
#define PWM_SET_DUTY	

#define PWM_F BIT(30)
#define PWM_R BIT(31)

/* Base clock 100 MHz 
 * PWM period 100 MHz/20 kHz
 */

#define PWM_PERIOD 5000

#define PWM_MAX 20000 //268435455

/*--------------------- self-defined constants ----------------------*/

#define MESSAGE_MAX_LEN 20



/*-------------------------------------------------------------------*/
void www();
void log_readings();

volatile signed irq_count;   // Position of slave wheel
volatile signed irq_count_m; // Position of master wheel
volatile signed error;
volatile unsigned old_a = 0;
volatile unsigned old_b = 0;

volatile signed motor_speed = 0;		// 0 to 1073741823 (MAX)

volatile int current_duty_cycle = 0;
volatile int direction = 0;

int position_history[400] = {0};


SEM_ID lock;


void irc_isr2(void)
{
        _Bool irc_a = (MOTOR_SR & BIT(MOTOR_SR_IRC_A_MON)) != 0;
        _Bool irc_b = (MOTOR_SR & BIT(MOTOR_SR_IRC_B_MON)) != 0;
        // ...
        
        if((irc_a != old_a) || (irc_b != old_b))
		{
        	if(irc_a == old_b)
        	{
        		
        		irq_count--;	//clock wise
        	}
        	else
        	{

        		irq_count++;   //counter clock wise
        	}
        	old_a = irc_a;
        	old_b = irc_b;
		}
        
        GPIO_INT_STATUS = MOTOR_IRQ_PIN; /* clear the interrupt */
}

void irc_init2(void)
{
        GPIO_INT_STATUS = MOTOR_IRQ_PIN; /* reset status */
        GPIO_DIRM = 0x0;                 /* set as input */
        GPIO_INT_TYPE = MOTOR_IRQ_PIN;   /* interrupt on edge */
        GPIO_INT_POLARITY = 0x0;         /* rising edge */
        GPIO_INT_ANY = 0x0;              /* ignore falling edge */
        GPIO_INT_ENABLE = MOTOR_IRQ_PIN; /* enable interrupt on MOTOR_IRQ pin */

        intConnect(INUM_TO_IVEC(INT_LVL_GPIO), irc_isr2, 0);
        intEnable(INT_LVL_GPIO);         /* enable all GPIO interrupts */
}

void irc_cleanup2(void)
{
        GPIO_INT_DISABLE = MOTOR_IRQ_PIN;

        intDisable(INT_LVL_GPIO);
        intDisconnect(INUM_TO_IVEC(INT_LVL_GPIO), irc_isr2, 0);
}

void init_pwm(void)
{
	PWM_PERIOD_SR = PWM_PERIOD; 				// PWM period 20kHz
	PWM_CONTROL = PWM_CONTROL | 0b1000000; 		//PWM Generator enable, use PWM generator	
}

/* -----------------------MESSAGE RECEIVING PART--------------------------*/

#define MAX_BUF 1024


int receive() //Argument in int port
{
	
  int port = 8080;
  int sockd;
  struct sockaddr_in my_name, cli_name;
  char buf[MAX_BUF];
  int status;
  int addrlen;

  /* Create a UDP socket */
  sockd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockd == -1)
  {
    perror("Socket creation error");
    exit(1);
  }

  /* Configure server address */
  my_name.sin_family = AF_INET;
  my_name.sin_addr.s_addr = INADDR_ANY;
  my_name.sin_port = htons(port);

  status = bind(sockd, (struct sockaddr*)&my_name, sizeof(my_name));

  addrlen = sizeof(cli_name);
  while(1)
  {
  status = recvfrom(sockd, buf, MAX_BUF, 0,
      (struct sockaddr*)&cli_name, &addrlen);
  
  irq_count_m = atoi(buf);
  strcat(buf, "OK!\n");

  status = sendto(sockd, buf, strlen(buf)+1, 0, (struct sockaddr*)&cli_name, sizeof(cli_name));
  }
  close(sockd);
  
  //return (atoi(buf));
  return 1;
}

/*---------------------------- CONTROLLER ------------------------------*/

void controller() //Argument in int irq_count_m, int irq_count
{
	int kp = 75;
	int pwm = 0;
	
	//int iter = 0;
	
	while(1)
	{
		error = irq_count_m - irq_count;
		pwm = kp * error;
		if(pwm > 5000)
		{
			pwm = 5000;
		}
		if(error > 0)
		{
			direction = 1;
			PWM_DUTY_CR = pwm | PWM_F;
		}
		else if(error < 0)
		{
			direction = -1;
			pwm = pwm * -1;
			PWM_DUTY_CR = pwm | PWM_R;
		}
		else
		{
			PWM_DUTY_CR = 0; // before 0 | PWM_F
		}
		
		if (pwm > 20000){
			
			current_duty_cycle = 100 * direction;//changed without testing
		}
		else{
			float current_duty_cycle_f = ((float)pwm/PWM_MAX)*100;
			int temp = current_duty_cycle_f;
			current_duty_cycle = temp * direction;
		}
		/*if (iter == 100000){
			semTake(lock,WAIT_FOREVER);
			current_duty_cycle = (pwm/PWM_MAX)*100;
			semGive(lock);
			//printf("PWM duty cycle = %f\n", current_duty_cycle);
			printf("test");
		}*/
	}

}

/*-----------------------SLAVE MAIN----------------------------*/

void slave(void)
{
	printf("slave program running\n");
	//TASK_ID st;
	
	int iter = 0;
	int cdc = 0;
	
	int i = 0;
	
	lock = semOpen("/complock", SEM_TYPE_MUTEX, SEM_FULL, SEM_Q_FIFO, OM_CREATE, NULL);
	sem_position = semCCreate(SEM_Q_FIFO, 0);
	irc_init2();
	init_pwm();
	
	int id1 = taskSpawn("receiver", 200, 0, 4096, (FUNCPTR) receive, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	int id2 = taskSpawn("controller", 203, VX_FP_TASK, 4096, (FUNCPTR) controller, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	int idServer = taskSpawn("server", 202, 0, 4096, (FUNCPTR) www, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	int idLogger = taskSpawn("logger", 199, 0, 4096, (FUNCPTR) log_readings, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	
	
	//--------------------SLAVE MAIN : SERVER  INIT----------------------------
	//server();
	while(1)
	{
		
		iter ++;
		if (iter == 100000){
			semTake(lock,WAIT_FOREVER);
			//if (direction == -1){
				//current_duty_cycle = current_duty_cycle * direction;
				
			//}
			cdc = current_duty_cycle;			
			semGive(lock);
			//printf("PWM duty cycle = %d\n", cdc);
			iter = 0;
			
			//bargraph
		    printf("%d ", cdc);
		    
		    cdc = cdc/10;
		    //printf("%d ", cdc);
			if (cdc != 0){
			    
			    if (cdc < 0){
		    	    for (i = 0; i< (10+cdc); i++){
		    	        printf(".");
		    	    }

		    		for (i = 0; i< (-cdc); i++){

		    		    printf("=");
		    		}
		    	}else{
		    	    printf("..........");
		    	}
		    	printf("|");
		    	if (cdc > 0){
		    	    for (i = 0; i< cdc; i++){
		    	        printf("=");
		    	    }
		    		for (i = 0; i< 10-cdc; i++){
		    		    printf(".");
		    		}
		    	}else{
		    	    printf("..........");
		    	}
			}else{
			    printf("..........|..........");
			}
			printf("\r");
			
			 /*
			sleep(1);
			printf("Set position: %d, current position: %d\n", irq_count_m, irq_count);
			*/	
		}
		
	}
	printf("exiting the while\n");
	
	taskDelete(id1);
	taskDelete(id2);
	taskDelete(idServer);
	taskDelete(idLogger);
	irc_cleanup2();
	}



//#define SERVER_PORT     80 /* Port 80 is reserved for HTTP protocol */

void log_readings()
{
	int i;
	while(1)
	{
	for (i = 0; i < 400; i++)
	{
		position_history[i] = irq_count;
		//printf("Position of slave %d\n", irq_count);
		sleep(0.002);
	}
	semGive(sem_position);
	}
}
#define SERVER_MAX_CONNECTIONS  20

void www()
{
  int s;
  int newFd;
  struct sockaddr_in serverAddr;
  struct sockaddr_in clientAddr;
  int sockAddrSize;

  sockAddrSize = sizeof(struct sockaddr_in);
  bzero((char *) &serverAddr, sizeof(struct sockaddr_in));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(SERVER_PORT);
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  s=socket(AF_INET, SOCK_STREAM, 0);
  if (s<0)
  {
    printf("Error: www: socket(%d)\n", s);
    return;
  }


  if (bind(s, (struct sockaddr *) &serverAddr, sockAddrSize) == ERROR)
  {
    printf("Error: www: bind\n");
    return;
  }

  if (listen(s, SERVER_MAX_CONNECTIONS) == ERROR)
  {
    perror("www listen");
    close(s);
    return;
  }

  printf("www server running\n");

  while(1)
  {
    /* accept waits for somebody to connect and the returns a new file descriptor */
    if ((newFd = accept(s, (struct sockaddr *) &clientAddr, &sockAddrSize)) == ERROR)
    {
      perror("www accept");
      close(s);
      return;
    }
    
    /* The client connected from IP address inet_ntoa(clientAddr.sin_addr)
       and port ntohs(clientAddr.sin_port).

       Start a new task for each request. The task will parse the request
       and sends back the response.

       Don't forget to close newFd at the end */
		FILE *f = fdopen(newFd, "w");
		fprintf(f, "HTTP/1.0 200 OK\r\n\r\n");
		fprintf(f, "<head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1250\">"
		"<meta name=\"GENERATOR\" content=\"Microsoft FrontPage 4.0\">"
		"<title>Hlavní stránka Katedry řídicí techniky FEL ČVUT</title>"
		"</head>");
		fprintf(f, "<body onload=\"setTimeout(function(){location.reload()}, 100);\">");
		
		
		fprintf(f, "<svg version=\"1.2\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" class=\"graph\" aria-labelledby=\"title\" role=\"img\">"
		  "<title id=\"title\">A line chart showing some information</title>"
		"<g class=\"grid x-grid\" id=\"xGrid\">"
		 "<line x1=\"90\" x2=\"90\" y1=\"5\" y2=\"371\"></line>"
		"</g>"
		"<g class=\"grid y-grid\" id=\"yGrid\">"
		  "<line x1=\"90\" x2=\"705\" y1=\"370\" y2=\"370\"></line>"
		"</g>"
		  "<g class=\"labels x-labels\">"
		  "<text x=\"100\" y=\"400\">2008</text>"
		  "<text x=\"246\" y=\"400\">2009</text>"
		  "<text x=\"392\" y=\"400\">2010</text>"
		  "<text x=\"538\" y=\"400\">2011</text>"
		 "<text x=\"684\" y=\"400\">2012</text>"
		  "<text x=\"400\" y=\"440\" class=\"label-title\">Year</text>"
		"</g>"
		"<g class=\"labels y-labels\">"
		 "<text x=\"80\" y=\"15\">15</text>"
		  "<text x=\"80\" y=\"131\">10</text>"
		  "<text x=\"80\" y=\"248\">5</text>"
		  "<text x=\"80\" y=\"373\">0</text>"
		 " <text x=\"50\" y=\"200\" class=\"label-title\">Price</text>"
		"</g>"
		"</svg>");

		
		fprintf(f, "<svg width=\"1200\" height=\"600\" xmlns='http://www.w3.org/2000/svg'> "
		  "<g transform=\"translate(450,130) scale(1)\"> "
		    "<!-- Now Draw the main X and Y axis --> "
		    "<g style=\"stroke-width:2; stroke:black\"> "
		      "<!-- X Axis --> "
		      "<path d=\"M 0 0 L 400 0 Z\"/> ");
		fprintf(f, "<!-- Y Axis --> "
		      "<path d=\"M 0 100 L 0 100 Z\"/> "
		    "</g> "
		    "<g style=\"fill:none; stroke:#B0B0B0; stroke-width:1; stroke-dasharray:2 4;text-anchor:end; font-size:30\"> "
		    "<text style=\"fill:black; stroke:none\" x=\"-1\" y=\"100\" >%d</text> " 
		    "<text style=\"fill:black; stroke:none\" x=\"-1\" y=\"0\" >%d</text> "	
		    "<text style=\"fill:black; stroke:none\" x=\"-1\" y=\"-100\" >%d</text> "
		    "<g style=\"text-anchor:middle\"> "
			"<text style=\"fill:black; stroke:none\" x=\"100\" y=\"20\" >100</text> "
			"<text style=\"fill:black; stroke:none\" x=\"200\" y=\"20\" >200</text> "
			"<text style=\"fill:black; stroke:none\" x=\"300\" y=\"20\" >300</text> "
			"<text style=\"fill:black; stroke:none\" x=\"400\" y=\"20\" >400</text> "
		    "</g></g>", irq_count_m - 100, irq_count_m, irq_count_m + 100);
		      
		int i;
		fprintf(f, "<polyline points=\" ");
		for(i = 0; i < 400; i+=10)
		{
		fprintf(f, "%d,\t%d\n", i, position_history[i]); //position_history[i] * -1
		}
		fprintf(f, "\"\n ");
	    fprintf(f, "style=\"stroke:red; stroke-width: 1; fill : none;\"/>"
	    "</g>"
	     "</svg>");
	    fprintf(f, "\n</html>");
	     
		fclose(f);
		sleep(1);
  }
}
//fprintf(f,"<html\><head><meta http-equiv=\"Content-Type/\" content=\"text/html; charset=utf-8\"/><title>Motor Status Example</title></head><body onload=\"setTimeout(function(){location.reload()}, 100);\"><h1>Motor Status Example</h1><object type=\"image/svg+xml\" data=\"graph.svg\"></body> </html>");


