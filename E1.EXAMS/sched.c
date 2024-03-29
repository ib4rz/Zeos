/*
 * sched.c - initializes struct for task 0 anda task 1
 */

#include <types.h>
#include <hardware.h>
#include <sched.h>
#include <mm.h>
#include <io.h>
#include <utils.h>

union task_union task[NR_TASKS]
  __attribute__((__section__(".data.task")));

#if 1
struct task_struct *list_head_to_task_struct(struct list_head *l)
{
  return list_entry( l, struct task_struct, list);
}
#endif

extern struct list_head blocked;
struct list_head freequeue;
struct list_head readyqueue;
struct task_struct *idle_task;
void writeMSR();
void task_switch(union task_union*t);
void change_context(unsigned int *ker_esp1,unsigned int ker_esp2);

/* get_DIR - Returns the Page Directory address for task 't' */
page_table_entry * get_DIR (struct task_struct *t) 
{
	return t->dir_pages_baseAddr;
}

/* get_PT - Returns the Page Table address for task 't' */
page_table_entry * get_PT (struct task_struct *t) 
{
	return (page_table_entry *)(((unsigned int)(t->dir_pages_baseAddr->bits.pbase_addr))<<12);
}

int allocate_DIR(struct task_struct *t) 
{
	int pos;

	pos = ((int)t-(int)task)/sizeof(union task_union);

	t->dir_pages_baseAddr = (page_table_entry*) &dir_pages[pos]; 

	return 1;
}

void cpu_idle(void)
{
	__asm__ __volatile__("sti": : :"memory");

	while(1)
	{
	;
	}
}

void init_idle (void)
{
  struct list_head *first = list_first(&freequeue);
  struct task_struct *idle = list_head_to_task_struct(first);
  list_del(first);  // Delete from the freequeue
  idle -> PID  = 0;
  allocate_DIR(idle);
  union task_union *uidle = (union task_union*) idle;
  uidle -> stack[KERNEL_STACK_SIZE-1] = (unsigned long)cpu_idle;
  uidle -> stack[KERNEL_STACK_SIZE-2] = 0;  // fake ebp
  idle -> kernel_esp = (unsigned int)&(uidle -> stack[KERNEL_STACK_SIZE-2]);

  init_stats(idle);
  idle_task = idle;
}

void init_task1(void)
{
  struct list_head *first = list_first(&freequeue);
  struct task_struct *init = list_head_to_task_struct(first);
  list_del(first);  // Delete from the freequeue
  init-> PID = 1;
  allocate_DIR(init);
  set_user_pages(init);
  union task_union *uinit = (union task_union*) init;
  tss.esp0 = (DWord)&(uinit -> stack[KERNEL_STACK_SIZE]);
  writeMSR((unsigned long)&(uinit -> stack[KERNEL_STACK_SIZE]), 0x175);
  set_cr3(get_DIR(init));
  
  init -> quantum = QUANTUM;
  init_stats(init);
  init -> state = ST_RUN;
}

void init_stats(struct task_struct *t) 
{
  t->st.user_ticks = 0;       // Total ticks executing user code
  t->st.system_ticks = 0;     // Total ticks executing system code
  t->st.blocked_ticks = 0;    // Total ticks in the blocked state
  t->st.ready_ticks = 0;      // Total ticks in the ready state
  t->st.elapsed_total_ticks = get_ticks();  // Total ticks since the beginning
  t->st.total_trans = 0;      // Total transitions ready --> run
  t->st.remaining_ticks = t->quantum; // Remaining quantum
}

int get_quantum(struct task_struct *t)
{
  return t->quantum;
}

void set_quantum(struct task_struct *t, int new_quantum)
{
  t->quantum = new_quantum;
}

void update_sched_data_rr (void) 
{
  --quantum;
  current()->st.remaining_ticks = quantum;
}

int needs_sched_rr (void)
{
  return (quantum <= 0 && !list_empty(&readyqueue));
}

void update_process_state_rr (struct task_struct *t, struct list_head *dst_queue)
{
  if (t->state != ST_RUN) list_del(&(t->list));
  if (dst_queue != NULL) {
    if (t != idle_task) list_add_tail(&(t->list), dst_queue);
      if (dst_queue == &readyqueue) {
        t->state = ST_READY;
        current()->st.system_ticks += get_ticks() - current()->st.elapsed_total_ticks;
        t->st.elapsed_total_ticks = get_ticks();
      }
      else t->state = ST_BLOCKED;
  }
  else t->state = ST_RUN;
}

void sched_next_rr (void)
{
  struct task_struct *t;
  if (!list_empty(&readyqueue)) {
    struct list_head *first = list_first(&readyqueue);
    t = list_head_to_task_struct(first);
    update_process_state_rr(t, NULL);
    quantum = get_quantum(t);
    t->st.remaining_ticks = quantum;
  }
  else t = idle_task;

  t->st.ready_ticks += get_ticks() - t->st.elapsed_total_ticks;
  t->st.elapsed_total_ticks = get_ticks();
  t->st.total_trans++;

  task_switch((union task_union*)t);
}

void init_sched()
{
  quantum = QUANTUM;
  INIT_LIST_HEAD(&freequeue);
  for (int i = 0; i < NR_TASKS; ++i) {
    task[i].task.PID = -1;
    list_add_tail(&(task[i].task.list), &freequeue);
  }
  INIT_LIST_HEAD(&readyqueue);
}

void schedule() 
{
  update_sched_data_rr();
  if (needs_sched_rr()) {
    update_process_state_rr(current(), &readyqueue);
    sched_next_rr();
  }
}

void inner_task_switch(union task_union *t) {
  tss.esp0 = (DWord)&(t -> stack[KERNEL_STACK_SIZE]);
  writeMSR((unsigned long)&(t -> stack[KERNEL_STACK_SIZE]), 0x175);
  set_cr3(get_DIR(&t->task));
  change_context(&current()-> kernel_esp, t->task.kernel_esp);
}

struct task_struct* current()
{
  int ret_value;
  
  __asm__ __volatile__(
  	"movl %%esp, %0"
	: "=g" (ret_value)
  );
  return (struct task_struct*)(ret_value&0xfffff000);
}

