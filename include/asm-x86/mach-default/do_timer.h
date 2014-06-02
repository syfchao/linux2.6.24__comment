/* defines for inline arch setup functions */
#include <linux/clockchips.h>

#include <asm/i8259.h>
#include <asm/i8253.h>

/**
 * do_timer_interrupt_hook - hook into timer tick
 *
 * Call the pit clock event handler. see asm/i8253.h
 **/

/**
 * 全局时钟处理
 */
static inline void do_timer_interrupt_hook(void)
{
	/* 调用do_timer和update_process_times处理全局事务 */
	global_clock_event->event_handler(global_clock_event);
}
