#include <stdio.h>
#include <stdlib.h>
#include "hardware.h"
#include "drivers.h"
#include "kernel.h"


/* You may use the definitions below, if you are so inclined, to
   define the process table entries. Feel free to use your own
   definitions, though. */

typedef enum { RUNNING, READY, BLOCKED , UNINITIALIZED } PROCESS_STATE;

typedef struct process_table_entry {
  PROCESS_STATE state;
  int total_CPU_time_used;
} PROCESS_TABLE_ENTRY;

PROCESS_TABLE_ENTRY process_table[MAX_NUMBER_OF_PROCESSES];

int idle_process_running; //check to see if the processor is currently idle

void round_robin(); //this is the scheduler

/* Since you will have a ready queue as well as a queue for each semaphore,
   where each queue contains PIDs, here are two structure definitions you can
   use, if you like, to implement a single queue.  
   In this case, a queue is a linked list with a head pointer 
   and a tail pointer. */

typedef struct PID_queue_elt {
  struct PID_queue_elt *next;
  PID_type pid;
} PID_QUEUE_ELT;


typedef struct {
  PID_QUEUE_ELT *head;
  PID_QUEUE_ELT *tail;
} PID_QUEUE;

//ready queue for round robin
PID_QUEUE rq;

//sempahore struct
typedef struct semaphore {
  int value;
  PID_QUEUE *q;
} SEMAPHORE;

/* This constant defines the number of semaphores that your code should
   support */

#define NUMBER_OF_SEMAPHORES 16

/* This is the initial integer value for each semaphore (remember that
   each semaphore should have a queue associated with it, too). */

#define INITIAL_SEMAPHORE_VALUE 1

//array of semaphores
SEMAPHORE semaphores[NUMBER_OF_SEMAPHORES];


/* A quantum is 40 ms */

#define QUANTUM 40


/* This variable can be used to store the current value of the clock
   when a process starts its quantum. Later on, when an interrupt
   (of any kind) occurs, if the difference between the current time
   and the quantum start time is greater or equal to QUANTUM (40), 
   then the current process has used up its quantum. */

int current_quantum_start_time;

//count the number of outstanding requests and processes
int outstanding_requests;
int num_processes;


//enqueue and dequeue a PID_QUEUE
void enqueue(PID_type pid, PID_QUEUE *q)
{
  PID_QUEUE_ELT *new_tail = (PID_QUEUE_ELT*)malloc(sizeof(PID_QUEUE_ELT));
  new_tail->pid = pid;
  new_tail->next = NULL;
  if(q->head == NULL)
  {
    q->head = new_tail;
    q->tail = new_tail;
  }
  else
  {
    q->tail->next = new_tail;
    q->tail = q->tail->next;
  }
}

int dequeue(PID_QUEUE *q)
{
  if(q->head == NULL)
  {
    return IDLE_PROCESS;
  }
  int pid = q->head->pid;
  free(q->head);
  q->head = q->head->next;
  return pid;
}


//up and down procedures for a semaphore
void up(int s)
{
  if(semaphores[s].value == 0)
  {
    if(semaphores[s].q->head == NULL)
    {
      semaphores[s].value++;
    }
    else
    {
      int pid = dequeue(semaphores[s].q);
      process_table[pid].state = READY;
      enqueue(pid, &rq);
    }
  }
  else
  {
    semaphores[s].value++;
  }
}

void down(int s)
{
  if(semaphores[s].value == 0)
  {
    process_table[current_pid].state = BLOCKED;
    process_table[current_pid].total_CPU_time_used += (clock - current_quantum_start_time);
    enqueue(current_pid, semaphores[s].q);
    round_robin();
  }
  else
  {
    semaphores[s].value--;
  }
}

void round_robin()
{
  current_pid = dequeue(&rq);
  if(current_pid == IDLE_PROCESS)
  {
    if(outstanding_requests == 0)
    {
      printf("deadlock\n");
      exit(1);
    }
    else
    {
      if(idle_process_running == FALSE)
        printf("Time %d: Processor is idle\n", clock);
    }
    idle_process_running = TRUE;
  }
  else
  {
    printf("Time %d: Process %d runs\n", clock, current_pid);
    idle_process_running = FALSE;
    process_table[current_pid].state = RUNNING;
    current_quantum_start_time = clock;
  }
}

//handle all of the interrupts

void handle_clock_interrupt()
{
  if((clock - current_quantum_start_time) >= QUANTUM)
  {
    process_table[current_pid].total_CPU_time_used += (clock - current_quantum_start_time);
    process_table[current_pid].state = READY;
    enqueue(current_pid, &rq);
    round_robin();
  }
}

void handle_disk_interrupt()
{
  outstanding_requests--;
  printf("Time %d: Handled DISK_INTERRUPT for pid %d\n", clock, R1);
  process_table[R1].state = READY;
  enqueue(R1, &rq);
  if(current_pid == IDLE_PROCESS)
  {
    round_robin();
  }
}

void handle_keyboard_interrupt()
{
  outstanding_requests--;
  printf("Time %d: Handled KEYBOARD_INTERRUPT for pid %d\n", clock, R1);
  process_table[R1].state = READY;
  enqueue(R1, &rq);
  if(current_pid == IDLE_PROCESS)
  {
    round_robin();
  }
}

void handle_trap()
{
  switch(R1)
  {
    case DISK_READ:
      outstanding_requests++;
      printf("Time %d: Process %d issues disk read request\n", clock, current_pid);
      disk_read_req(current_pid, R2);
      process_table[current_pid].state = BLOCKED;
      process_table[current_pid].total_CPU_time_used += (clock - current_quantum_start_time);
      round_robin();
      break;
    case DISK_WRITE:
      printf("Time %d: Process %d issues disk write request\n", clock, current_pid);
      disk_write_req(current_pid);
      break;
    case KEYBOARD_READ:
      outstanding_requests++;
      printf("Time %d: Process %d issues keyboard read request\n", clock, current_pid);
      keyboard_read_req(current_pid);
      process_table[current_pid].state = BLOCKED;
      process_table[current_pid].total_CPU_time_used += (clock - current_quantum_start_time);
      current_quantum_start_time = clock;
      round_robin();
      break;
    case FORK_PROGRAM:
      num_processes++;
      printf("Time %d: Creating process entry for pid %d\n", clock, R2);
      process_table[R2].state = READY;
      enqueue(R2, &rq);
      break;
    case END_PROGRAM:
      num_processes--;
      process_table[current_pid].total_CPU_time_used += (clock - current_quantum_start_time);
      current_quantum_start_time = clock;
      printf("Time %d: Process %d exits.  Total CPU time = %d\n", 
        clock, current_pid, process_table[current_pid].total_CPU_time_used);
      if(num_processes == 0)
      {
        printf("-- No more processes to execute --\n");
        exit(0);
      }
      process_table[current_pid].state = UNINITIALIZED;
      round_robin();
      break;
    case SEMAPHORE_OP:
      if(R3)
      {
        printf("Time %d: Process %d issues UP operation on semaphore %d\n", clock, current_pid, R2);
        up(R2);
      }
      else
      {
        printf("Time %d: Process %d issues DOWN operation on semaphore %d\n", clock, current_pid, R2);
        down(R2);
      }
      break;
  }
}

/* This procedure is automatically called when the 
   (simulated) machine boots up */

void initialize_kernel()
{
  // Put any initialization code you want here.
  // Remember, the process 0 will automatically be
  // executed after initialization (and current_pid
  // will automatically be set to 0), 
  // so the your process table should initially reflect 
  // that fact.

  //initialize the process table
  int i;
  for(i = 0; i < MAX_NUMBER_OF_PROCESSES; i++)
  {
    process_table[i].state = UNINITIALIZED;
    process_table[i].total_CPU_time_used = 0;
  }

  //set process 0 to running
  process_table[current_pid].state = RUNNING;
  idle_process_running = FALSE;

  // Don't forget to populate the interrupt table
  // (see hardware.h) with the interrupt handlers
  // that you are writing in this file.

  INTERRUPT_TABLE[TRAP] = handle_trap;
  INTERRUPT_TABLE[CLOCK_INTERRUPT] = handle_clock_interrupt;
  INTERRUPT_TABLE[DISK_INTERRUPT] = handle_disk_interrupt;
  INTERRUPT_TABLE[KEYBOARD_INTERRUPT] = handle_keyboard_interrupt;

  // Also, be sure to initialize the semaphores as well
  // as the current_quantum_start_time variable.

  current_quantum_start_time = clock;
  for(i = 0; i < NUMBER_OF_SEMAPHORES; i++)
  {
    semaphores[i].value = INITIAL_SEMAPHORE_VALUE;
    semaphores[i].q = (PID_QUEUE *)malloc(sizeof(PID_QUEUE));
    semaphores[i].q->head = NULL;
    semaphores[i].q->tail = NULL;
  }

  //initialize ready queue
  rq.head = NULL;
  rq.tail = NULL;

  //initialize outstanding requests to 0 and the number of processes to 1 (process 0)
  outstanding_requests = 0;
  num_processes = 1;
}

