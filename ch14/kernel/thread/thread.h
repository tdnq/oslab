#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "global.h"
#include "list.h"
#include "memory.h"

/* 每个进程可以打开的最大文件数 */
#define MAX_FILES_OPEN_PER_PROC 8


typedef int16_t pid_t;

/* 定义通用函数类型，它将在很多线程函数中作为形参参数 */
typedef void thread_func(void*);

/* 进程或者线程的状态 */
typedef enum task_status
{
    /* 运行 */
    TASK_RUNNING,

    /* 就绪 */
    TASK_READY,

    /* 阻塞 */
    TASK_BLOCKED,

    TASK_WAITING,


    TASK_HANGING,

    /* 暂停 */
    TASK_DIED
}task_status;

/********** 中断栈 ******************
 * 此结构用于中断发生时保护程序的上下文环境进程或
 * 线程被外部或者软中断打断时，会按照此结构压入上下文
 * 寄存器，intr_exit中的出栈操作是此结构的逆操作，此
 * 栈在线程自己的内核栈中位置固定，位于所在页的最顶端
 */
typedef struct intr_stack
{
    /* 中断号 */
    uint32_t vec_no;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    
    /* pushad把esp压入，但popad忽略 */
    uint32_t esp_dummy;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    uint32_t err_code;
    void (*eip)(void);
    uint32_t cs;
    uint32_t eflags;

    /* 以下由cpu从低特权级进入高特权级时压入 */
    void* esp;  

    /* ss压入的是4个字节 */
    uint32_t ss;
}intr_stack;

/**************** 线程栈 *********************
 * 线程自己的栈，用于存储执行线程中的环境
 */
typedef struct thread_stack
{
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    /**************************************************
     * 线程第一次执行时，eip指向调用的函数kernel_thread
     * 其他时候，eip指向switch_to的返回地址
     * ret指令执行后进入eip指向的函数，栈中的eip被弹出
     */
    void (*eip)(thread_func* func, void* func_arg);

    /* 以下仅供第一次被调度上cpu时使用 */

    /* 为了模拟call指令，call执行后，进入被调用函数，栈顶元素是
    *  返回地址。
    */
    void (*unused_retaddr);

    /* 由kernel_thread所调用的函数名  */
    thread_func* function;

    /* 由kernel_thread所调用的函数所需的参数 */
    void* func_arg;
}thread_stack;

/* 进程或线程的pcb,程序控制块 */
typedef struct task_struct
{
    /* 线程内核栈的栈顶 */
    uint32_t* self_kstack;
   
    pid_t pid;

    /* 线程状态 */
    task_status status;

    /* 线程优先级，数字越大执行时间越长 */
    uint8_t priority;

    /* 线程名字 */
    char name[14];

    /* 每次在cpu上运行的时间数 */
    uint8_t ticks;

    /* 记录任务在cpu上运行的总时间数 */
    uint32_t elapsed_ticks;

    /* 文件描述符数组  */
    int32_t fd_table[MAX_FILES_OPEN_PER_PROC]; 

    /* 用于把线程加入一般队列(如就绪队列)等 */
    list_elem general_tag;

    /* 用于把线程加入所all_thread队列 */
    list_elem all_list_tag;
    
    /* 用户进程的虚拟地址  */
    virtual_addr userprog_vaddr;

    /* 进程的页表虚拟地址,如果是线程则为NULL */
    uint32_t* pgdir_vaddr;

    /* 用户进程内存块描述符 */
    mem_block_desc u_block_desc[DESC_CNT];

    /* 栈的边界标记，用于检测栈的溢出*/
    uint32_t stack_magic;    
}task_struct;


/************************************************
 * 函数名:get_running_thread_pcb()
 * 功能:获取正在运行线程的pcb
 * 返回值:指向该线程pcb的指针 
 */
task_struct* get_running_thread_pcb();

/***********************************************
 * 函数名:thread_start()
 * name:线程名字
 * prio:线程优先级
 * function:执行的函数
 * func_arg:function的参数
 * 功能:通过该函数创建线程并执行
 * 返回值:线程pcb
 */
task_struct* thread_start(char* name,
                          int prio,
                          thread_func* function,
                          void* func_arg);

/*********************************************
 * 函数名:thread_init()
 * 功能:初始化线程
 * 返回值:无
 */
void thread_init(void);

/**********************************************************
 * 函数名:thread_block()
 * stat:线程状态
 * 功能:修改当前线程为阻塞态，换下处理器，当苏醒后恢复阻塞前的中断状态
 * 返回值:无
 */ 
void thread_block(const task_status stat);

/*************************************************************
 * 函数名:thread_unblock()
 * pthread:被唤醒的线程，由当前线程调用
 * 功能:把pthread唤醒，加入就绪队列，状态设置为ready
 * 返回值:无
 */ 
void thread_unblock(task_struct* pthread);

/* 主动让出cpu */
void thread_yield(void);

#endif
