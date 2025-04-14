/* process.c
 *
 * Nils Napp
 * Cornell University
 * All rights reserved
 *
 * Jan 2024
 * Ithaca NY
 *
 * This file is part of the ECE3140/CS3420 offering for Spring 2024. If you
 * are not part of this class you should not have access to this file. Do not
 * post, share, or otherwise distribute this file. We will consider it an AI
 * violation if you do. If you somehow get this code and you are NOT enrolled
 * the Spring 2024 version of ECE3140 please contact the course staff
 * immediately and describe how you found it.
 */



#include <stdlib.h>
#include "3140_concur.h"
#include "realtime.h"
//#include "led.h"(included when testing where/if the code got to certain points

extern process_queue_t process_queue;  // Global non‐RT process queue
process_queue_t ready_rt = {NULL};       // Ready real‐time process queue
process_queue_t not_ready_rt = {NULL};     // RT processes not yet ready

int process_deadline_met = 0; // number of processes that meet deadline
int process_deadline_miss = 0; // processes that miss deadline

volatile realtime_t current_time = {0, 0};

process_t *idle_process = NULL; // will be used for stalling purposes when no processes are ready

/* free process proc */
static void process_free(process_t *proc) {
    if (proc->is_realtime == 1) {
        free(proc->arrival_time);
        free(proc->deadline);
    }
    process_stack_free(proc->orig_sp, proc->n);
    free(proc);
}

/* function for idle process that does nothing */
void idle(void) {
    while (1) {
    }
}
/* Starts up the concurrent execution */
void process_start (void) {
	unsigned int *sp = process_stack_init(idle, 20); // initialize idle process that may or may not be used

	idle_process = (process_t*)malloc(sizeof(process_t));
	idle_process->sp = idle_process->orig_sp = sp;
	idle_process->n = 20;
	idle_process->is_realtime = -1;

	SIM->SCGC6 |= SIM_SCGC6_PIT_MASK; // all the normal stuff
	PIT->MCR = 0;
	PIT->CHANNEL[0].LDVAL = 150000;
	PIT->CHANNEL[1].LDVAL = 15000;
	PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TIE_MASK | PIT_TCTRL_TEN_MASK; // timer needs to be enabled for PIT1 because scheduler does not do itself

	NVIC_EnableIRQ(PIT_IRQn);
	// Don't enable the timer yet. The scheduler will do so itself

	if(is_empty(&process_queue) && is_empty(&ready_rt) && is_empty(&not_ready_rt)) return;
	//bail out fast if no processes were ever created

	process_begin();
}

/* Create a new non-realtime process */
int process_create(void (*f)(void), int n) {
    unsigned int *sp = process_stack_init(f, n);
    if (!sp) return -2;

    process_t *proc = (process_t*)malloc(sizeof(process_t));
    if (!proc) {
        process_stack_free(sp, n);
        return -1;
    }

    proc->sp = proc->orig_sp = sp;
    proc->n = n;
    proc->is_realtime = 0;
    proc->arrival_time = NULL;
    proc->deadline = NULL;
    proc->next = NULL;

    enqueue(proc, &process_queue);
    return 0;
}

/* compare method (similar to normal sorting), returns 1 if time proc_one is earlier than time proc_two */
int cmp_time(volatile realtime_t *proc_one, volatile realtime_t *proc_two) {
    if ((proc_one->sec < proc_two->sec) ||
        (proc_one->sec == proc_two->sec && proc_one->msec <= proc_two->msec)) {
        return 1;
    }
    return 0;
}

/* add to queue a process proc, maintaining that the queue is sorted by earliest arrival first */
void add_sorted_arrival(process_t *proc, process_queue_t *queue) {
    proc->next = NULL;

    // if queue empty, set as head
    if(queue->head == NULL) {
	queue->head = proc;
	return;
    }

    // if proc earliest arrival, add before head
    if (cmp_time(proc->arrival_time, queue->head->arrival_time)) {
        proc->next = queue->head;
        queue->head = proc;
        return;
    }

    // loop through queue until at where proc should be, when arrival time of proc is 'sandwiched' between neighboring processes
    process_t *cur = queue->head;
    while (!(cur->next == NULL) && cmp_time(cur->next->arrival_time, proc->arrival_time)) {
        cur = cur->next;
    }
    proc->next = cur->next;
    cur->next = proc;
}

/* compute absolute deadline for a process by taking arrival + relative deadline */
realtime_t compute_abs_deadline(process_t *proc) {
    realtime_t abs_deadline;
    abs_deadline.sec = proc->arrival_time->sec + proc->deadline->sec;
    abs_deadline.msec = proc->arrival_time->msec + proc->deadline->msec;
    if (abs_deadline.msec >= 1000) {
        abs_deadline.sec += abs_deadline.msec / 1000;
        abs_deadline.msec %= 1000;
    }
    return abs_deadline;
}

/* add to queue a process proc, maintaining that the queue is sorted by earliest deadline first*/
void add_sorted_deadline(process_t *proc, process_queue_t *queue) {
    proc->next = NULL;
    realtime_t abs_deadline_proc = compute_abs_deadline(proc);

    // if no elements, set this as the head
    if (queue->head == NULL) {
        queue->head = proc;
        return;
    }

    realtime_t abs_deadline_head = compute_abs_deadline(queue->head);
    // if proc is earlier than earliest deadline, insert at beginning
    if (cmp_time(&abs_deadline_proc, &abs_deadline_head)) {
        proc->next = queue->head;
        queue->head = proc;
        return;
    }

    // loop through rest until get to place where proc should be (time is in between 2 neighboring processes)
    process_t *cur = queue->head;
    while (!(cur->next == NULL)) {
        realtime_t abs_deadline_next = compute_abs_deadline(cur->next);
        if (cmp_time(&abs_deadline_proc, &abs_deadline_next)) {
            break;
        }
        cur = cur->next;
    }
    proc->next = cur->next;
    cur->next = proc;
}

/* create realtime process */
int process_rt_create(void (*f)(void), int n, realtime_t *start, realtime_t *deadline) {
    unsigned int *sp = process_stack_init(f, n);
    if (!sp) return -2;

    process_t *proc = (process_t*)malloc(sizeof(process_t));
    if (!proc) {
        process_stack_free(sp, n);
        return -1;
    }

    realtime_t *start_copy = malloc(sizeof(realtime_t));
    realtime_t *deadline_copy = malloc(sizeof(realtime_t));
    if (!start_copy || !deadline_copy) {
        process_stack_free(sp, n);
        free(proc);
        free(start_copy);
        free(deadline_copy);
        return -1;
    }
    *start_copy = *start;
    *deadline_copy = *deadline;

    proc->sp = proc->orig_sp = sp;
    proc->n = n;
    proc->is_realtime = 1;
    proc->arrival_time = start_copy;
    proc->deadline = deadline_copy;
    proc->next = NULL;

    // checks whether the process is currently ready or not to determine which queue to add to
    if (cmp_time(start_copy, &current_time)) {
        add_sorted_deadline(proc, &ready_rt);
    } else {
        add_sorted_arrival(proc, &not_ready_rt);
    }
    return 0;
}

/* Called by the runtime system to select another process.
   "cursp" = the stack pointer for the currently running process
*/
unsigned int *process_select(unsigned int *cursp) {
    if (cursp && current_process_p) {
        // Update the saved stack pointer for the currently running process.
        current_process_p->sp = cursp;
        if (current_process_p->is_realtime == 1) {
        	// For real-time processes, re-enqueue into
        	// real-time queue in case another high priority process ready now
			add_sorted_deadline(current_process_p, &ready_rt);
        } else if (current_process_p->is_realtime == 0) {
        	// For non-real-time processes, requeue for later execution.
        	enqueue(current_process_p, &process_queue);
        }
    } else if(current_process_p){
        // Process has terminated.
        if (current_process_p->is_realtime == 1) {
            realtime_t deadline_abs = compute_abs_deadline(current_process_p);
            if (cmp_time(&deadline_abs, &current_time)) {
                process_deadline_miss++;
            } else {
                process_deadline_met++;
            }
        }

        process_free(current_process_p);
    }

    // updates the ready queue to include all processes previously not ready but now ready
    while (!is_empty(&not_ready_rt) &&
               cmp_time(not_ready_rt.head->arrival_time, &current_time)) {
		process_t *proc = dequeue(&not_ready_rt);
		add_sorted_deadline(proc, &ready_rt);
	}
    // Select the next process:
    // Give priority to ready real-time processes.
    if (!is_empty(&ready_rt)) {
		current_process_p = dequeue(&ready_rt);
		return current_process_p->sp;
	} else if (!is_empty(&process_queue)) {
		current_process_p = dequeue(&process_queue);
		return current_process_p->sp;
	} else if (!is_empty(&not_ready_rt)) {
		current_process_p = idle_process;
		return current_process_p->sp;
	} else {
	    	process_free(idle_process);
		return NULL;
	}


}

// called every millisecond and updates the current time
void PIT1_Service(void) {
	__disable_irq();
    current_time.msec++;
    if (current_time.msec >= 1000) {
        current_time.sec += current_time.msec/1000;
        current_time.msec %= 1000;
    }

    PIT->CHANNEL[1].TFLG = PIT_TFLG_TIF_MASK;
    __enable_irq();
}
