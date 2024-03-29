#include "semaphore.h"
#include "interrupt.h"
#include "debug.h"

/**************************
 * 函数名:sema_init()
 * psema:信号量指针
 * value:信号量初值
 * 功能:初始化信号量值和等待队列
 * 返回值:无
 */ 
void sema_init(semaphore* psema, int value)
{
    /* 初始化值 */
    psema->value = value;

    /* 初始化信号量等待队列 */
    list_init(&psema->waiters);
}

/**************************************
 * 函数名:sema_down()
 * psema:semaphore指针
 * 功能:p操作
 * 返回值:无
 */ 
void sema_down(semaphore* psema)
{
    /* 关中断实现原子操作 */
    intr_status old_status = intr_disable();

    while(psema->value <= 0)
    {
        /* 当前线程不应该在阻塞队列上 */
        ASSERT(!elem_find(&psema->waiters, &get_running_thread_pcb()->general_tag));
        
        /********************************************************************
         * 执行while循环，不会导致等待队列中出现重复线程标志，因为下面会阻塞
         * 当前正在执行的线程，被唤醒的时候，该线程会被从等待队列中拿掉。而添加
         * 到就绪队列当中。
         */ 
        list_push_back(&psema->waiters, &get_running_thread_pcb()->general_tag);
        
       // put_char('\n');
       // put_int(psema->value);
       // put_char('\n');    

        /***************************************************
         * 阻塞线程，直到被唤醒,唤醒之后是ready状态，需要等待调度才能上
         * cpu运行。假如存在3个请求资源值为1的信号量a、b、c。当a获得该资源
         * 时，b请求该资源被阻塞，c一次未运行。当a运行完后，b被唤醒成为就绪态，
         * 此时cpu调度c去运行，该资源值又变为0.c还未运行完的时候，b去运行，应
         * 该再做一次资源判断，因为此时资源仍然为0.
         * 信号量和中断的区别:
         *      当关闭中断后，在临界资源内不会发生调度，而使用信号量进行上锁，
         * 在访问临界资源的时候仍然会进行线程调度，只是不不会执行破坏临界资源
         * 的线程，因为会被阻塞掉。
         */
        thread_block(TASK_BLOCKED);
    }
    /**************************************************************************
     * 当被唤醒且此时临界资源可以访问时,这条语句也不能和while循环进行交换，
     * 如果value值先减少的话，会出现饥饿现象。比如两个线程同时进入while循环
     * 发生阻塞，此时value-=2,当一个线程被唤醒后，重新执行while循环判断条件会
     * 不满足。
    */
    psema->value--;
    
    set_intr_status(old_status);
}

/********************************************
 * 函数名:sema_up()
 * psema:semaphore
 * 功能:唤醒被阻塞的线程
 * 返回值:无
 */ 
void sema_up(semaphore* psema)
{
    intr_status old_status = intr_disable();
    
//    put_str("sema_up\n");

//    put_int(psema->value);
//    put_char('\n');

    /* 如果没有线程睡眠 */
    if(!list_empty(&psema->waiters))
    {
        /* 从阻塞队列中删除 */
        list_elem* tag = list_pop_head(&psema->waiters);
        task_struct* thread_blocked = 
            (uint32_t*)((uint32_t)tag & 0xfffff000);
        
  //      put_int(thread_blocked);

        /* 加入就绪队列，改为就绪态 */
        thread_unblock(thread_blocked);
    }
    psema->value++;

    set_intr_status(old_status);
}
