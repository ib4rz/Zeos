/*
 * sys.c - Syscalls implementation
 */
#include <devices.h>

#include <utils.h>

#include <io.h>

#include <mm.h>

#include <mm_address.h>

#include <sched.h>

#include <errno.h>

#define LECTURA 0
#define ESCRIPTURA 1
#define BUFF_SIZE 1024

int PID_GL = 1000;

int check_fd(int fd, int permissions)
{
  if (fd!=1) return -9; /*EBADF*/
  if (permissions!=ESCRIPTURA) return -13; /*EACCES*/
  return 0;
}

int sys_ni_syscall()
{
	return -38; /*ENOSYS*/
}

int sys_getpid()
{
	return current()->PID;
}

unsigned int get_ebp();

int ret_from_fork()
{
  return 0;
}

int sys_fork()
{
  // creates the child process
  if (list_empty(&freequeue)) return -ENOMEM;

  struct list_head *first = list_first(&freequeue);
  struct task_struct *child = list_head_to_task_struct(first);

  copy_data(current(), child, 4096);

  allocate_DIR(child);

  // Search physical pages
  unsigned int frames[NUM_PAG_DATA];
  for (int pag = 0; pag < NUM_PAG_DATA; ++pag) {
    frames[pag] = alloc_frame();
    if (frames[pag] == -1) {
      while(pag > 0) {
        --pag;
        free_frame(frames[pag]);
      }
      return -ENOMEM;
    }
  }
  list_del(first);

  page_table_entry *parent_pt = get_PT(current());
  page_table_entry *child_pt = get_PT(child);

  for (int pag = 0; pag < NUM_PAG_KERNEL; ++pag) {
    set_ss_pag(child_pt, pag, get_frame(parent_pt,pag));
  }

  for (int pag = 0; pag < NUM_PAG_DATA; ++pag) {
    set_ss_pag(child_pt, PAG_LOG_INIT_DATA+pag, frames[pag]);
  }

  for (int pag = PAG_LOG_INIT_CODE; pag < NUM_PAG_CODE + PAG_LOG_INIT_CODE; ++pag) {
    set_ss_pag(child_pt,pag,get_frame(parent_pt,pag));
  }

  int free_pag = NUM_PAG_KERNEL + NUM_PAG_CODE + NUM_PAG_DATA;
  for (int pag = 0; pag < NUM_PAG_DATA; ++pag) {
    set_ss_pag(parent_pt,free_pag+pag,frames[pag]);
    unsigned int address1 = (PAG_LOG_INIT_DATA+pag)*PAGE_SIZE;
    unsigned int address2 = (free_pag+pag)*PAGE_SIZE;
    copy_data((void *)address1, (void *)address2, PAGE_SIZE);
    del_ss_pag(parent_pt,free_pag+pag);
  }
  // Flush TLB
  set_cr3(get_DIR(current()));

  child->PID = ++PID_GL;

  unsigned int ebp = (unsigned int) get_ebp();
  ebp -= (unsigned int)current() + (unsigned int)child;
  *(unsigned int*)(ebp) = (unsigned int)ret_from_fork;
  *(unsigned int*)(ebp-4) = 0;
  child->kernel_esp = ebp-4;

  list_add_tail(&(child->list),&readyqueue);

  child->state = ST_READY;
  
  return child->PID;
}

void sys_exit()
{  
}

int sys_write(int fd, char * buffer, int size) 
{
  char bufk[size];
  int error = check_fd(fd,ESCRIPTURA);
  if (error != 0) return error;
  
  // Size must be positive
  if (size < 0) return -EINVAL;
  // Checks if a user space pointer is valid
  if (!access_ok(VERIFY_READ,buffer,size)) return -EFAULT;

  copy_from_user(buffer,bufk,size);
  return sys_write_console(bufk,size);
}

extern int zeos_ticks;
int sys_gettime() {
  return zeos_ticks;
}
