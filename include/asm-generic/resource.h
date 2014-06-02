#ifndef _ASM_GENERIC_RESOURCE_H
#define _ASM_GENERIC_RESOURCE_H

/*
 * Resource limit IDs
 *
 * ( Compatibility detail: there are architectures that have
 *   a different rlimit ID order in the 5-9 range and want
 *   to keep that order for binary compatibility. The reasons
 *   are historic and all new rlimits are identical across all
 *   arches. If an arch has such special order for some rlimits
 *   then it defines them prior including asm-generic/resource.h. )
 */

/* 按毫秒计算的最大CPU时间 */
#define RLIMIT_CPU		0	/* CPU time in sec */
/* 允许的最大文件长度 */
#define RLIMIT_FSIZE		1	/* Maximum filesize */
/* 数据段的最大长度 */
#define RLIMIT_DATA		2	/* max data size */
/* 用户栈的最大长度 */
#define RLIMIT_STACK		3	/* max stack size */
/* 内存转储文件的最大长度 */
#define RLIMIT_CORE		4	/* max core file size */

#ifndef RLIMIT_RSS
/* 最大常驻内存长度，目前未用 */
# define RLIMIT_RSS		5	/* max resident set size */
#endif

#ifndef RLIMIT_NPROC
/* 用户可以拥有的最大进程数目，默认为max_threads/2。max_threads值由可用内存决定。 */
# define RLIMIT_NPROC		6	/* max number of processes */
#endif

#ifndef RLIMIT_NOFILE
/* 打开文件数量限制，默认为1024 */
# define RLIMIT_NOFILE		7	/* max number of open files */
#endif

#ifndef RLIMIT_MEMLOCK
/* 不可换出页的最大数目 */
# define RLIMIT_MEMLOCK		8	/* max locked-in-memory address space */
#endif

#ifndef RLIMIT_AS
/* 进程地址空间限制 */
# define RLIMIT_AS		9	/* address space limit */
#endif

/* 文件锁的最大数目 */
#define RLIMIT_LOCKS		10	/* maximum file locks held */
/* 未决信号的最大数量 */
#define RLIMIT_SIGPENDING	11	/* max number of pending signals */
/* POSIX消息队列的最大数量，以字节为单位 */
#define RLIMIT_MSGQUEUE		12	/* maximum bytes in POSIX mqueues */
/* 非实时进程的最大优先级 */
#define RLIMIT_NICE		13	/* max nice prio allowed to raise to
					   0-39 for nice level 19 .. -20 */
/* 实时进程的最大优先级 */
#define RLIMIT_RTPRIO		14	/* maximum realtime priority */

#define RLIM_NLIMITS		15

/*
 * SuS says limits have to be unsigned.
 * Which makes a ton more sense anyway.
 *
 * Some architectures override this (for compatibility reasons):
 */
#ifndef RLIM_INFINITY
/* 相应的资源未作限制 */
# define RLIM_INFINITY		(~0UL)
#endif

/*
 * RLIMIT_STACK default maximum - some architectures override it:
 */
#ifndef _STK_LIM_MAX
# define _STK_LIM_MAX		RLIM_INFINITY
#endif

#ifdef __KERNEL__

/*
 * boot-time rlimit defaults for the init task:
 */
/**
 * INIT进程的资源限制
 */
#define INIT_RLIMITS							\
{									\
	[RLIMIT_CPU]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_FSIZE]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_DATA]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_STACK]		= {       _STK_LIM,   _STK_LIM_MAX },	\
	[RLIMIT_CORE]		= {              0,  RLIM_INFINITY },	\
	[RLIMIT_RSS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_NPROC]		= {              0,              0 },	\
	[RLIMIT_NOFILE]		= {       INR_OPEN,       INR_OPEN },	\
	[RLIMIT_MEMLOCK]	= {    MLOCK_LIMIT,    MLOCK_LIMIT },	\
	[RLIMIT_AS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_LOCKS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_SIGPENDING]	= { 		0,	       0 },	\
	[RLIMIT_MSGQUEUE]	= {   MQ_BYTES_MAX,   MQ_BYTES_MAX },	\
	[RLIMIT_NICE]		= { 0, 0 },				\
	[RLIMIT_RTPRIO]		= { 0, 0 },				\
}

#endif	/* __KERNEL__ */

#endif
