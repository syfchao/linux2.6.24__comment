#ifndef _LINUX_MSG_H
#define _LINUX_MSG_H

#include <linux/ipc.h>

/* ipcs ctl commands */
#define MSG_STAT 11
#define MSG_INFO 12

/* msgrcv options */
#define MSG_NOERROR     010000  /* no error if message is too big */
#define MSG_EXCEPT      020000  /* recv any msg except of specified type.*/

/* Obsolete, used only for backwards compatibility and libc5 compiles */
struct msqid_ds {
	struct ipc_perm msg_perm;
	struct msg *msg_first;		/* first message on queue,unused  */
	struct msg *msg_last;		/* last message in queue,unused */
	__kernel_time_t msg_stime;	/* last msgsnd time */
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	__kernel_time_t msg_ctime;	/* last change time */
	unsigned long  msg_lcbytes;	/* Reuse junk fields for 32 bit */
	unsigned long  msg_lqbytes;	/* ditto */
	unsigned short msg_cbytes;	/* current number of bytes on queue */
	unsigned short msg_qnum;	/* number of messages in queue */
	unsigned short msg_qbytes;	/* max number of bytes on queue */
	__kernel_ipc_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_ipc_pid_t msg_lrpid;	/* last receive pid */
};

/* Include the definition of msqid64_ds */
#include <asm/msgbuf.h>

/* message buffer for msgsnd and msgrcv calls */
struct msgbuf {
	long mtype;         /* type of message */
	char mtext[1];      /* message text */
};

/* buffer for msgctl calls IPC_INFO, MSG_INFO */
struct msginfo {
	int msgpool;
	int msgmap; 
	int msgmax; 
	int msgmnb; 
	int msgmni; 
	int msgssz; 
	int msgtql; 
	unsigned short  msgseg; 
};

#define MSGMNI    16   /* <= IPCMNI */     /* max # of msg queue identifiers */
#define MSGMAX  8192   /* <= INT_MAX */   /* max size of message (bytes) */
#define MSGMNB 16384   /* <= INT_MAX */   /* default max size of a message queue */

/* unused */
#define MSGPOOL (MSGMNI*MSGMNB/1024)  /* size in kilobytes of message pool */
#define MSGTQL  MSGMNB            /* number of system message headers */
#define MSGMAP  MSGMNB            /* number of entries in message map */
#define MSGSSZ  16                /* message segment size */
#define __MSGSEG ((MSGPOOL*1024)/ MSGSSZ) /* max no. of segments */
#define MSGSEG (__MSGSEG <= 0xffff ? __MSGSEG : 0xffff)

#ifdef __KERNEL__
#include <linux/list.h>

/* one msg_msg structure for each message */
/* 消息队列中的消息，每一个消息都用一个页来保存，页前部为此结构 */
struct msg_msg {
	/* 通过此字段将消息链接到链表中 */
	struct list_head m_list; 
	/* 消息类型 */
	long  m_type;          
	/* 消息正文长度，以字节计算 */
	int m_ts;           /* message text size */
	/* 如果消息超过一个内存页，则使用此指针指向下一页 */
	struct msg_msgseg* next;
	void *security;
	/* the actual message follows immediately */
};

/* one msq_queue structure for each present queue on the system */
/* 消息队列数据结构 */
struct msg_queue {
	struct kern_ipc_perm q_perm;
	/* 上一次发送、接收、修改的时间 */
	time_t q_stime;			/* last msgsnd time */
	time_t q_rtime;			/* last msgrcv time */
	time_t q_ctime;			/* last change time */
	/* 队列中当前消息数量，以字节表示 */
	unsigned long q_cbytes;		/* current number of bytes on queue */
	/* 当前队列中的消息数量 */
	unsigned long q_qnum;		/* number of messages in queue */
	/* 队列中最大的消息数量 */
	unsigned long q_qbytes;		/* max number of bytes on queue */
	/* 最后一次发送和接收消息的进程id */
	pid_t q_lspid;			/* pid of last msgsnd */
	pid_t q_lrpid;			/* last receive pid */

	/* 三个链表，表示正在睡眠的发送者、接收者和消息本身 */
	struct list_head q_messages;
	struct list_head q_receivers;
	struct list_head q_senders;
};

/* Helper routines for sys_msgsnd and sys_msgrcv */
extern long do_msgsnd(int msqid, long mtype, void __user *mtext,
			size_t msgsz, int msgflg);
extern long do_msgrcv(int msqid, long *pmtype, void __user *mtext,
			size_t msgsz, long msgtyp, int msgflg);

#endif /* __KERNEL__ */

#endif /* _LINUX_MSG_H */
