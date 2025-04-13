/*************************************************************************
 * Lab 5 Test Case 2 - Sorting and Idling
 *
 * pRT1: _r r r r
 * pRT2: _________g g g g
 * pRT3: _________________r r r
 * pRT4: _______________________g g g
 *
 *   The sequence in this test case should be as follows:
 *     - The program should first pause for half a second (idling) when all processes are not ready, 
 *       then go into process 1, 2, 3, 4 in that order due to their respective deadlines
 *     - pRT4 is added to the queue before pRT3, and should be sorted correctly in the not ready queue
 *       and the ready queue (when things become ready) when they are added to those queues.
 *
 *   All four processes should meet their deadlines (1000 seconds past start time)
 *
 ************************************************************************/

#include "led.h"
#include "3140_concur.h"
#include "realtime.h"

/*--------------------------*/
/* Parameters for test case */
/*--------------------------*/



/* Stack space for processes */
#define RT_STACK  20



/*--------------------------------------*/
/* Time structs for real-time processes */
/*--------------------------------------*/

/* Constants used for 'work' and 'deadline's, these really can all share variable
    but it is split into 4 here just for clarity */
realtime_t t_1msec = {1000, 0};
realtime_t t_2msec = {1000, 0};
realtime_t t_3msec = {1000, 0};
realtime_t t_4msec = {1000, 0};


/* Process start times */
realtime_t t_pRT1 = {0, 500};

realtime_t t_pRT2 = {1, 0};

realtime_t t_pRT3 = {1, 500};

realtime_t t_pRT4 = {2, 0};



/*-------------------
 * 4 real-time processes
 *-------------------*/
void pRT1(void) {
	int i;
	for (i=0; i<4;i++){
	red_on_frdm();
	delay(330);
	red_toggle_frdm();
	delay(330);
	}

}


void pRT2(void) {
	int i;
	for (i=0; i<4;i++){
	green_on_frdm();
	delay(330);
	green_toggle_frdm();
	delay(330);
	}
}

void pRT3(void) {
	int i;
	for (i=0; i<3;i++){
	red_on_frdm();
	delay(660);
	red_toggle_frdm();
	delay(660);
	}
}

void pRT4(void) {
	int i;
	for (i=0; i<3;i++){
	green_on_frdm();
	delay(660);
	green_toggle_frdm();
	delay(660);
	}
}

/*--------------------------------------------*/
/* Main function - start concurrent execution */
/*--------------------------------------------*/
int main(void) {

	led_init();
	
	/* Create processes */
	if (process_rt_create(pRT1, RT_STACK, &t_pRT1, &t_1msec) < 0) { return -1; }
	if (process_rt_create(pRT2, RT_STACK, &t_pRT2, &t_2msec) < 0) { return -1; }
	if (process_rt_create(pRT4, RT_STACK, &t_pRT4, &t_4msec) < 0) { return -1; }
	if (process_rt_create(pRT3, RT_STACK, &t_pRT3, &t_3msec) < 0) { return -1; }
	/* Launch concurrent execution */
	process_start();
	
	
	green_off_frdm();
	red_off_frdm();

	// should blink 4 times because all 4 deadlines are met
	while(process_deadline_met>0) {
		red_on_frdm();
		green_on_frdm();
		delay(330);
		green_off_frdm();
		red_off_frdm();
		delay(330);
		process_deadline_met--;
	}
	
	/* Hang out in infinite loop (so we can inspect variables if we want) */
	while (1);
	return 0;
}

