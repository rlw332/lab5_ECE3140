/*
 * process.h
 *
 * Nils Napp
 * Cornell University
 * All right reserved
 *
 * Jan 2024
 * Ithaca NY
 *
 * This file is part of the ECE3140/CS3420 offering for Spring 2024. If you are not part
 * of this class you should not have access to this file. Do not post, share, or otherwise
 * distribute this file. We will consider it an AI violation if you do.
 */

#ifndef PROCESS_H_
#define PROCESS_H_

#include "realtime.h"

struct process_state{
	unsigned int *sp;
	unsigned int *orig_sp;
	int n;
	int is_realtime;    //Flag to indicate if process is RT (1) or non-RT (0)
	realtime_t *arrival_time;  //Start timing
	realtime_t *deadline;  //Absolute deadline based on process_start()
	struct process_state * next;
};

typedef struct process_state process_t;


#endif /* PROCESS_H_ */
