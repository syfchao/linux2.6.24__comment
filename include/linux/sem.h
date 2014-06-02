#ifndef _LINUX_SEM_H
#define _LINUX_SEM_H

#include <linux/ipc.h>

/* semop flags */
#define SEM_UNDO        0x1000  /* undo the operation on exit */

/* semctl Command Definitions. */
#define GETPID  11       /* get sempid */
#define GETVAL  12       /* get semval */
#define GETALL  13       /* get all semval's */
#define GETNCNT 14       /* get semncnt */
#define GETZCNT 15       /* get semzcnt */
#define SETVAL  16       /* set semval */
#define SETALL  17       /* set all semval's */

/* ipcs ctl cmds */
#define SEM_STAT 18
#define SEM_INFO 19

/* Obsolete, used only for backwards compatibility and libc5 compiles */
struct semid_ds {
	struct ipc_perm	sem_perm;		/* permissions .. see ipc.h */
	__kernel_time_t	sem_otime;		/* last semop time */
	__kernel_time_t	sem_ctime;		/* last change time */
	struct sem	*sem_base;		/* ptr to first semaphore in array */
	struct sem_queue *sem_pending;		/* pending operations to be processed */
	struct sem_queue **sem_pending_last;	/* last pending operation */
	struct sem_undo	*undo;			/* undo requests on this array */
	unsigned short	sem_nsems;		/* no. of semaphores in array */
};

/* Include the definition of semid64_ds */
#include <asm/sembuf.h>

/* semop system calls takes an array of these. */
/* 对信号量的操作 */
struct sembuf {
	/* 信号量在数组中的索引 */
	unsigned short  sem_num;	/* semaphore index in array */
	/* 要进行的操作 */
	short		sem_op;		/* semaphore operation */
	/* 操作标志 */
	short		sem_flg;	/* operation flags */
};

/* arg for semctl system calls. */
union semun {
	int val;			/* value for SETVAL */
	struct semid_ds __user *buf;	/* buffer for IPC_STAT & IPC_SET */
	unsigned short __user *array;	/* array for GETALL & SETALL */
	struct seminfo __user *__buf;	/* buffer for IPC_INFO */
	void __user *__pad;
};

struct  seminfo {
	int semmap;
	int semmni;
	int semmns;
	int semmnu;
	int semmsl;
	int semopm;
	int semume;
	int semusz;
	int semvmx;
	int semaem;
};

#define SEMMNI  128             /* <= IPCMNI  max # of semaphore identifiers */
#define SEMMSL  250             /* <= 8 000 max num of semaphores per id */
#define SEMMNS  (SEMMNI*SEMMSL) /* <= INT_MAX max # of semaphores in system */
#define SEMOPM  32	        /* <= 1 000 max num of ops per semop call */
#define SEMVMX  32767           /* <= 32767 semaphore maximum value */
#define SEMAEM  SEMVMX          /* adjust on exit max value */

/* unused */
#define SEMUME  SEMOPM          /* max num of undo entries per process */
#define SEMMNU  SEMMNS          /* num of undo structures system wide */
#define SEMMAP  SEMMNS          /* # of entries in semaphore map */
#define SEMUSZ  20		/* sizeof struct sem_undo */

#ifdef __KERNEL__
#include <asm/atomic.h>

struct task_struct;

/* One semaphore structure for each semaphore in the system. */
struct sem {
	int	semval;		/* current value */
	int	sempid;		/* pid of last operation */
};

/* One sem_array data structure for each set of semaphores in the system. */
/* 信号量集合 */
struct sem_array {
	/* 信号量访问权限，必须位于结构起始处 */
	struct kern_ipc_perm	sem_perm;	/* permissions .. see ipc.h */
	/* 上次访问信号量的时间 */
	time_t			sem_otime;	/* last semop time */
	/* 上次修改信号量的时间 */
	time_t			sem_ctime;	/* last change time */
	/* 指向数组中的第一个信号量 */
	struct sem		*sem_base;	/* ptr to first semaphore in array */
	/* 待决信号量操作链表。 */
	struct sem_queue	*sem_pending;	/* pending operations to be processed */
	/* 最后一个待决信号量 */
	struct sem_queue	**sem_pending_last; /* last pending operation */
	struct sem_undo		*undo;		/* undo requests on this array */
	/* 数组中的信号量数目 */
	unsigned long		sem_nsems;	/* no. of semaphores in array */
};

/* One queue for each sleeping process in the system. */
/* ipc信号量队列 */
struct sem_queue {
	/* 通过这两个字段将等待任务加入队列中 */
	struct sem_queue *	next;	 /* next entry in the queue */
	struct sem_queue **	prev;	 /* previous entry in the queue, *(q->prev) == q */
	/* 等待信号量的进程 */
	struct task_struct*	sleeper; /* this process */
	struct sem_undo *	undo;	 /* undo structure */
	/* 等待进程的pid */
	int    			pid;	 /* process id of requesting process */
	int    			status;	 /* completion status of operation */
	/* 操作的信号量数组 */
	struct sem_array *	sma;	 /* semaphore array for operations */
	/* 信号量内部id */
	int			id;	 /* internal sem id */
	/* 挂起的信号量操作数组 */
	struct sembuf *		sops;	 /* array of pending operations */
	int			nsops;	 /* number of operations */
	/* 操作是否修改信号量数据结构 */
	int			alter;   /* does the operation alter the array? */
};

/* Each task has a list of undo requests. They are executed automatically
 * when the process exits.
 */
struct sem_undo {
	struct sem_undo *	proc_next;	/* next entry on this process */
	struct sem_undo *	id_next;	/* next entry on this semaphore set */
	int			semid;		/* semaphore set identifier */
	short *			semadj;		/* array of adjustments, one per semaphore */
};

/* sem_undo_list controls shared access to the list of sem_undo structures
 * that may be shared among all a CLONE_SYSVSEM task group.
 */ 
struct sem_undo_list {
	atomic_t	refcnt;
	spinlock_t	lock;
	struct sem_undo	*proc_list;
};

struct sysv_sem {
	struct sem_undo_list *undo_list;
};

#ifdef CONFIG_SYSVIPC

extern int copy_semundo(unsigned long clone_flags, struct task_struct *tsk);
extern void exit_sem(struct task_struct *tsk);

#else
static inline int copy_semundo(unsigned long clone_flags, struct task_struct *tsk)
{
	return 0;
}

static inline void exit_sem(struct task_struct *tsk)
{
	return;
}
#endif

#endif /* __KERNEL__ */

#endif /* _LINUX_SEM_H */
