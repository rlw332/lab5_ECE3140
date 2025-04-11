

#include <stdlib.h>
#include "3140_concur.h"
#include "realtime.h"
#include "process.h"

extern process_queue_t process_queue;  // Global non‐RT process queue
process_queue_t ready_rt = {NULL};       // Ready real‐time process queue
process_queue_t not_ready_rt = {NULL};     // RT processes not yet ready

int process_deadline_met = 0;
int process_deadline_miss = 0;

volatile realtime_t current_time = {0, 0};

static void process_free(process_t *proc) {
    if (proc->is_realtime) {
        free(proc->arrival_time);
        free(proc->deadline);
    }
    process_stack_free(proc->orig_sp, proc->n);
    free(proc);
}

/* Starts up the concurrent execution */
void process_start (void) {
	SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
	PIT->MCR = 0;
	PIT->CHANNEL[0].LDVAL = 150000;
	NVIC_EnableIRQ(PIT_IRQn);
	// Don't enable the timer yet. The scheduler will do so itself

	if(is_empty(&process_queue)) return;
	//bail out fast if no processes were ever created

	process_begin();
}

/* Create a new process */
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

int cmp_time(volatile realtime_t *proc_one, volatile realtime_t *proc_two) {
    if ((proc_one->sec < proc_two->sec) ||
        (proc_one->sec == proc_two->sec && proc_one->msec <= proc_two->msec)) {
        return 1;
    }
    return 0;
}

void add_sorted_arrival(process_t *proc, process_queue_t *queue) {
	proc->next = NULL;
	if(queue->head == NULL) {
		queue->head = proc;
		return;
	}
    if (cmp_time(proc->arrival_time, queue->head->arrival_time)) {
        proc->next = queue->head;
        queue->head = proc;
        return;
    }

    process_t *cur = queue->head;
    while (!(cur->next == NULL) && cmp_time(cur->next->arrival_time, proc->arrival_time)) {
        cur = cur->next;
    }
    proc->next = cur->next;
    cur->next = proc;
}


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

void add_sorted_deadline(process_t *proc, process_queue_t *queue) {

	proc->next = NULL;
    realtime_t abs_deadline_proc = compute_abs_deadline(proc);
    if (queue->head == NULL) {
        queue->head = proc;
        return;
    }
    realtime_t abs_deadline_head = compute_abs_deadline(queue->head);
    if (cmp_time(&abs_deadline_proc, &abs_deadline_head)) {
        proc->next = queue->head;
        queue->head = proc;
        return;
    }

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
        if ((current_process_p->is_realtime)) {
        	// For real-time processes, re-enqueue into
        	// real-time queue in case another high priority process ready now
			//add_sorted_deadline(current_process_p, &ready_rt);
        	return current_process_p -> sp;
        } else {
        	// For non-real-time processes, requeue for later execution.
        	enqueue(current_process_p, &process_queue);
        }
    } else if(current_process_p){
        // Process has terminated.
        if (current_process_p->is_realtime) {
            realtime_t deadline_abs = compute_abs_deadline(current_process_p);
            if (cmp_time(&current_time, &deadline_abs)) {
                process_deadline_miss++;
            } else {
                process_deadline_met++;
            }
        }

        process_free(current_process_p);
    }


    if(is_empty(&ready_rt) && is_empty(&process_queue) && is_empty(&not_ready_rt)) {
    	return NULL;
    }
    // Select the next process:
    // Give priority to ready real-time processes.
    while(!is_empty(&ready_rt) || !is_empty(&process_queue) || !is_empty(&not_ready_rt)) {
		if (!is_empty(&ready_rt)) {
			current_process_p = dequeue(&ready_rt);
			return current_process_p->sp;
		} else if (!is_empty(&process_queue)) {
			current_process_p = dequeue(&process_queue);
			return current_process_p->sp;
		}
    }


    return current_process_p->sp;
}


void PIT1_Service(void) {
    current_time.msec++;
    if (current_time.msec >= 1000) {
        current_time.sec += current_time.msec/1000;
        current_time.msec %= 1000;
    }

    while (!is_empty(&not_ready_rt) &&
           cmp_time(not_ready_rt.head->arrival_time, &current_time)) {
        process_t *proc = dequeue(&not_ready_rt);
        add_sorted_deadline(proc, &ready_rt);
    }

    PIT->CHANNEL[1].TFLG = PIT_TFLG_TIF_MASK;
}
