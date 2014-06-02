#ifndef _LINUX_IPC_H
#define _LINUX_IPC_H

#include <linux/types.h>

#define IPC_PRIVATE ((__kernel_key_t) 0)  

/* Obsolete, used only for backwards compatibility and libc5 compiles */
struct ipc_perm
{
	__kernel_key_t	key;
	__kernel_uid_t	uid;
	__kernel_gid_t	gid;
	__kernel_uid_t	cuid;
	__kernel_gid_t	cgid;
	__kernel_mode_t	mode; 
	unsigned short	seq;
};

/* Include the definition of ipc64_perm */
#include <asm/ipcbuf.h>

/* resource get request flags */
#define IPC_CREAT  00001000   /* create if key is nonexistent */
#define IPC_EXCL   00002000   /* fail if key exists */
#define IPC_NOWAIT 00004000   /* return error on wait */

/* these fields are used by the DIPC package so the kernel as standard
   should avoid using them if possible */
   
#define IPC_DIPC 00010000  /* make it distributed */
#define IPC_OWN  00020000  /* this machine is the DIPC owner */

/* 
 * Control commands used with semctl, msgctl and shmctl 
 * see also specific commands in sem.h, msg.h and shm.h
 */
#define IPC_RMID 0     /* remove resource */
#define IPC_SET  1     /* set ipc_perm options */
#define IPC_STAT 2     /* get ipc_perm options */
#define IPC_INFO 3     /* see ipcs */

/*
 * Version flags for semctl, msgctl, and shmctl commands
 * These are passed as bitflags or-ed with the actual command
 */
#define IPC_OLD 0	/* Old version (no 32-bit UID support on many
			   architectures) */
#define IPC_64  0x0100  /* New version (support 32-bit UIDs, bigger
			   message sizes, etc. */

/*
 * These are used to wrap system calls.
 *
 * See architecture code for ugly details..
 */
struct ipc_kludge {
	struct msgbuf __user *msgp;
	long msgtyp;
};

#define SEMOP		 1
#define SEMGET		 2
#define SEMCTL		 3
#define SEMTIMEDOP	 4
#define MSGSND		11
#define MSGRCV		12
#define MSGGET		13
#define MSGCTL		14
#define SHMAT		21
#define SHMDT		22
#define SHMGET		23
#define SHMCTL		24

/* Used by the DIPC package, try and avoid reusing it */
#define DIPC            25

#define IPCCALL(version,op)	((version)<<16 | (op))

#ifdef __KERNEL__

#include <linux/kref.h>
#include <linux/spinlock.h>

#define IPCMNI 32768  /* <= MAX_INT limit for ipc arrays (including sysctl changes) */

/* used by in-kernel data structures */
/* 用户态ipc对象在内核中的表示 */
struct kern_ipc_perm
{
	spinlock_t	lock;
	int		deleted;
	/* ipc对象在内核中的ID */
	int		id;
	/* 用户态标识ipc对象的魔数 */
	key_t		key;
	/* 所有者的uid和gid */
	uid_t		uid;
	gid_t		gid;
	/* 产生该对象的uid和gid */
	uid_t		cuid;
	gid_t		cgid;
	/* 位掩码，用于指定所有者、所有者组、其他用户的权限 */
	mode_t		mode; 
	/* 序号，分配ipc对象时使用 */
	unsigned long	seq;
	void		*security;
};

struct ipc_ids;
/**
 * ipc命名空间
 */
struct ipc_namespace {
	struct kref	kref;
	/* 信号量、消息队列、共享内存 */
	struct ipc_ids	*ids[3];

	int		sem_ctls[4];
	int		used_sems;

	int		msg_ctlmax;
	int		msg_ctlmnb;
	int		msg_ctlmni;
	atomic_t	msg_bytes;
	atomic_t	msg_hdrs;

	size_t		shm_ctlmax;
	size_t		shm_ctlall;
	int		shm_ctlmni;
	int		shm_tot;
};

extern struct ipc_namespace init_ipc_ns;

#ifdef CONFIG_SYSVIPC
#define INIT_IPC_NS(ns)		.ns		= &init_ipc_ns,
extern void free_ipc_ns(struct kref *kref);
extern struct ipc_namespace *copy_ipcs(unsigned long flags,
						struct ipc_namespace *ns);
#else
#define INIT_IPC_NS(ns)
static inline struct ipc_namespace *copy_ipcs(unsigned long flags,
						struct ipc_namespace *ns)
{
	return ns;
}
#endif

static inline struct ipc_namespace *get_ipc_ns(struct ipc_namespace *ns)
{
#ifdef CONFIG_SYSVIPC
	if (ns)
		kref_get(&ns->kref);
#endif
	return ns;
}

static inline void put_ipc_ns(struct ipc_namespace *ns)
{
#ifdef CONFIG_SYSVIPC
	kref_put(&ns->kref, free_ipc_ns);
#endif
}

#endif /* __KERNEL__ */

#endif /* _LINUX_IPC_H */
