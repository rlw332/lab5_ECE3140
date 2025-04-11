#include "led.h"
#include "3140_concur.h"
#include "realtime.h"

#define RT_STACK  20

realtime_t t_start0 = {0, 0};
realtime_t t_start1 = {0, 500};     // 0.5s later
realtime_t t_deadline_long = {2, 0};
realtime_t t_deadline_short = {0, 1};

void pRT1(void) {
	for (int i = 0; i < 2; i++) {
		green_on_frdm();
		delay(800);  // long task
		green_toggle_frdm();
		delay(800);
	}
}

void pRT2(void) {
	for (int i = 0; i < 3; i++) {
		red_on_frdm();
		delay(200);
		red_toggle_frdm();
		delay(200);
	}
}

int main(void) {
	led_init();

	if (process_rt_create(pRT1, RT_STACK, &t_start0, &t_deadline_long) < 0){
		return -1;
	}
	if (process_rt_create(pRT2, RT_STACK, &t_start1, &t_deadline_short) < 0){
		return -1;
	}

	process_start();

	while (1);
	return 0;
}

