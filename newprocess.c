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

// So far this turns the green led on but then never turns it off. I've tried debugging it but no matter what I do,
// it doesn't seem to want to do anything else. I've looked over it and I think the helpers, global variables, and proc_start
// are all ok. ChatGPT similarly thinks the issue lies somewhere in the process_select() logic but has been unsuccessful in
// pinpointing it.

// The correct sequence of LEDS (all on FRDM board) for test0 are GREEN on and off 3 times (RT process) and then
// RED on and off 4 times (non-RT process) before going through the main function and toggling both GREEN and RED
// on and off once. A video can be found on Ed if this is confusing under the Prelab/Lab 5 QnA pinned post. Good luck

#include <stdlib.h>
#include "3140_concur.h"
#include "realtime.h"
#include "process.h"

volatile realtime_t current_time = {0, 0};
int process_deadline_met = 0;  // Counter for jobs that meet deadline
int process_deadline_miss = 0; // Counter for jobs that miss deadline

extern process_queue_t process_queue;  // Global non‐RT process queue
process_queue_t ready_rt = {NULL};       // Ready real‐time process queue
process_queue_t not_ready_rt = {NULL};     // RT processes not yet ready

static void process_free(process_t *proc) {
    if (proc->is_realtime) {
        free(proc->arrival_time);
        free(proc->deadline);
    }
    process_stack_free(proc->orig_sp, proc->n);
    free(proc);
}

static int cmp_time(volatile realtime_t *proc_one, volatile realtime_t *proc_two) {
    if ((proc_one->sec < proc_two->sec) ||
        (proc_one->sec == proc_two->sec && proc_one->msec <= proc_two->msec)) {
        return 1;
    }
    return 0;
}

static void add_sorted_arrival(process_t *proc, process_queue_t *queue) {
	if(queue->head == NULL) {
		queue->head = proc;
	}
    if (cmp_time(proc->arrival_time, queue->head->arrival_time)) {
        proc->next = queue->head;
        queue->head = proc;
        return;
    }

    process_t *cur = queue->head;
    while (!(cur->next == NULL) && !cmp_time(proc->arrival_time, cur->next->arrival_time)) {
        cur = cur->next;
    }
    proc->next = cur->next;
    cur->next = proc;
}

static realtime_t compute_abs_deadline(process_t *proc) {
    realtime_t abs_deadline;
    abs_deadline.sec = proc->arrival_time->sec + proc->deadline->sec;
    abs_deadline.msec = proc->arrival_time->msec + proc->deadline->msec;
    if (abs_deadline.msec >= 1000) {
        abs_deadline.sec += abs_deadline.msec / 1000;
        abs_deadline.msec %= 1000;
    }
    return abs_deadline;
}

static void add_sorted_deadline(process_t *proc, process_queue_t *queue) {
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

void PIT1_Service(void) {
	__disable_irq();
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
    __enable_irq();
}

int process_create(void (*f)(void), int n) {
    unsigned int *sp = process_stack_init(f, n);
    if (!sp) return -2;

    process_t *proc = malloc(sizeof(process_t));
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

int process_rt_create(void (*f)(void), int n, realtime_t *start, realtime_t *deadline) {
    unsigned int *sp = process_stack_init(f, n);
    if (!sp) return -2;

    process_t *proc = malloc(sizeof(process_t));
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

    if (cmp_time(&current_time, start_copy)) {
        add_sorted_deadline(proc, &ready_rt);
    } else {
        add_sorted_arrival(proc, &not_ready_rt);
    }
    return 0;
}


unsigned int *process_select(unsigned int *cursp) {
    if (cursp && current_process_p) {
        // Update the saved stack pointer for the currently running process.
        current_process_p->sp = cursp;
        if (!current_process_p->is_realtime) {
            // For non-real-time processes, requeue for later execution.
            enqueue(current_process_p, &process_queue);
        } else {
            // For real-time processes, do not requeue.
            // Simply return the updated context so that execution resumes
            // from the point of preemption.
            return current_process_p->sp;
        }
    } else if (current_process_p) {
        // Process has terminated.
        if (current_process_p->is_realtime) {
            realtime_t deadline_abs = compute_abs_deadline(current_process_p);
            if (cmp_time(&current_time, &deadline_abs)) {
                process_deadline_met++;
            } else {
                process_deadline_miss++;
            }
        }
        process_free(current_process_p);
    }

    // Select the next process:
    // Give priority to ready real-time processes.
    if (!is_empty(&ready_rt)) {
        current_process_p = dequeue(&ready_rt);
    } else if (!is_empty(&process_queue)) {
        current_process_p = dequeue(&process_queue);
    } else if (!is_empty(&not_ready_rt)) {
    	return process_select(cursp);
    } else {
        current_process_p = NULL;
    }

    return current_process_p ? current_process_p->sp : NULL;
}

void process_start(void) {
    SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
    PIT->MCR = 0;

    PIT->CHANNEL[0].LDVAL = 150000; // 10 milliseconds
    PIT->CHANNEL[0].TCTRL = PIT_TCTRL_TIE_MASK | PIT_TCTRL_TEN_MASK;

    PIT->CHANNEL[1].LDVAL = 15000;  // 1 millisecond
    PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TIE_MASK | PIT_TCTRL_TEN_MASK;

    NVIC_EnableIRQ(PIT_IRQn);

    current_time.sec = 0;
    current_time.msec = 0;

    if (!is_empty(&process_queue) || !is_empty(&ready_rt)) {
        process_begin();
    }
}
