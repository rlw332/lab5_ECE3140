/*************************************************************************
 * Lab 5 Test Case 1 - Preemptive Processes
 *
 * pRT1: ^g(on)------- g(off) g(on) g(off) v
 * pRT2: ^___r r r v
 *
 *   The sequence in this test case should be as follows:
 *     - The real-time processes pRT1 and pRT2 should start one after the other (pRT2 starts 500 msec later)
 *       than pRT1 which starts at time zero. 
 *     - pRT1 should start and then toggle green on, during which pRT2 starts. Because the system is EDF,
 *       this means that pRT1 will be preempted and put on hold until pRT2 finishes in its entirety. After
 *       pRT2 finishes, pRT1 finishes in its entirety.   
 *
 *   pRT2 should miss its deadline of 1 millisecond
 *   pRT1 should have more than enough time to meet the deadline (60 seconds)
 *
 ************************************************************************/

#include "led.h"
#include "3140_concur.h"
#include "realtime.h"

#define RT_STACK  20

realtime_t t_startpoint0 = {0, 0};
realtime_t t_startpoint1 = {0, 500};   //Start time 1 is 500 msec later
realtime_t t_deadline_late = {60, 0};  //Make the deadline long so that it should make the deadline (1 minute (60 seconds))
realtime_t t_deadline_early = {0, 1};  //Make a really short deadline and will cause preemption of the ongoing process,
				       //allowing the process with the earlier deadline to run
                                
void pRT1(void) { //Long process: green on and off twice
	for (int i = 0; i < 2; i++) {
		green_on_frdm();
		delay(800);
		green_toggle_frdm();
		delay(800);
	}
}

void pRT2(void) { //Short process: red on and off three times
	for (int i = 0; i < 3; i++) {
		red_on_frdm();
		delay(200);
		red_toggle_frdm();
		delay(200);
	}
}


int main(void) {
	led_init();

	if (process_rt_create(pRT1, RT_STACK, &t_startpoint0, &t_deadline_late) < 0){ //Create green process with long deadline, startpoint 0
		return -1;
	}
	if (process_rt_create(pRT2, RT_STACK, &t_startpoint1, &t_deadline_early) < 0){ //Create red process with early deadline, startpoint 1
		return -1;
	}

	process_start();

	 while(process_deadline_miss>0) {  //Keep track of how many missed processes
			red_on_frdm();	   //(should be 1 blink (red sequence can't make deadline_early))
			green_on_frdm();
			delay(330);
			green_off_frdm();
			red_off_frdm();
			delay(330);
			process_deadline_miss--;
		}

	while (1);
	return 0;
}
