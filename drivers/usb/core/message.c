/*
 * message.c - synchronous message handling
 */

#include <linux/pci.h>	/* for scatterlist macros */
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/scatterlist.h>
#include <linux/usb/quirks.h>
#include <asm/byteorder.h>

#include "hcd.h"	/* for usbcore internals */
#include "usb.h"

struct api_context {
	struct completion	done;
	int			status;
};

static void usb_api_blocking_completion(struct urb *urb)
{
	struct api_context *ctx = urb->context;

	ctx->status = urb->status;
	complete(&ctx->done);
}


/*
 * Starts urb and waits for completion or timeout. Note that this call
 * is NOT interruptible. Many device driver i/o requests should be
 * interruptible and therefore these drivers should implement their
 * own interruptible routines.
 */
/**
 * 等待URB完成。
 */
static int usb_start_wait_urb(struct urb *urb, int timeout, int *actual_length)
{ 
	struct api_context ctx;
	unsigned long expire;
	int retval;

	/**
	 * 定义并初始化一个完成原语。当URB完成时，调用complete唤醒本线程。
	 */
	init_completion(&done); 	
	urb->context = &done;
	urb->actual_length = 0;
	/**
	 * 提交一个URB请求。
	 */
	status = usb_submit_urb(urb, GFP_NOIO);
	if (unlikely(status))
		goto out;

	/**
	 * 等待时间是以ms为单位的，转换成jiffies。
	 */
	expire = timeout ? msecs_to_jiffies(timeout) : MAX_SCHEDULE_TIMEOUT;
	/**
	 * 等待URB执行结束。
	 */
	if (!wait_for_completion_timeout(&done, expire)) {

		dev_dbg(&urb->dev->dev,
			"%s timed out on ep%d%s len=%d/%d\n",
			current->comm,
			usb_endpoint_num(&urb->ep->desc),
			usb_urb_dir_in(urb) ? "in" : "out",
			urb->actual_length,
			urb->transfer_buffer_length);
	} else
		retval = ctx.status;
out:
	/**
	 * 向上层函数返回本次实际传递的数据长度。
	 */
	if (actual_length)
		*actual_length = urb->actual_length;

	/**
	 * 释放URB。
	 */
	usb_free_urb(urb);
	return retval;
}

/*-------------------------------------------------------------------*/
// returns status (negative) or length (positive)
/**
 * 向USB设备发送控制消息。
 */
static int usb_internal_control_msg(struct usb_device *usb_dev,
				    unsigned int pipe, 
				    struct usb_ctrlrequest *cmd,
				    void *data, int len, int timeout)
{
	struct urb *urb;
	int retv;
	int length;

	/**
	 * usb_alloc_urb函数，创建一个urb，struct urb结构体只能使用它来创建
	 */
	urb = usb_alloc_urb(0, GFP_NOIO);
	if (!urb)
		return -ENOMEM;
  
  	/**
  	 * 初始化一个控制urb，urb被创建之后，使用之前必须要正确的初始化。
  	 */
	usb_fill_control_urb(urb, usb_dev, pipe, (unsigned char *)cmd, data,
			     len, usb_api_blocking_completion, NULL);

	/**
	 * 将urb提交给咱们的usb core，以便分配给特定的主机控制器驱动进行处理，然后默默的等待处理结果，或者超时。
	 */
	retv = usb_start_wait_urb(urb, timeout, &length);
	if (retv < 0)
		return retv;
	else
		return length;
}

/**
 *	usb_control_msg - Builds a control urb, sends it off and waits for completion
 *	@dev: pointer to the usb device to send the message to
 *	@pipe: endpoint "pipe" to send the message to
 *	@request: USB message request value
 *	@requesttype: USB message request type value
 *	@value: USB message value
 *	@index: USB message index value
 *	@data: pointer to the data to send
 *	@size: length in bytes of the data to send
 *	@timeout: time in msecs to wait for the message to complete before
 *		timing out (if 0 the wait is forever)
 *	Context: !in_interrupt ()
 *
 *	This function sends a simple control message to a specified endpoint
 *	and waits for the message to complete, or timeout.
 *	
 *	If successful, it returns the number of bytes transferred, otherwise a negative error number.
 *
 *	Don't use this function from within an interrupt context, like a
 *	bottom half handler.  If you need an asynchronous message, or need to send
 *	a message from within interrupt context, use usb_submit_urb()
 *      If a thread in your driver uses this call, make sure your disconnect()
 *      method can wait for it to complete.  Since you don't have a handle on
 *      the URB used, you can't cancel the request.
 */
/**
 * 创建一个控制urb，并把它发送给usb设备，然后等待它完成。
 */
int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype,
			 __u16 value, __u16 index, void *data, __u16 size, int timeout)
{
	/**
	 * 为一个struct usb_ctrlrequest 结构体申请内存，这个结构体描述了主机通过控制传输发送给设备的请求
	 */
	struct usb_ctrlrequest *dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_NOIO);
	int ret;
	
	if (!dr)
		return -ENOMEM;

	/**
	 * 使用传递过来的参数填充请求包。
	 */
	dr->bRequestType= requesttype;
	/**
	 * 对于SET_ADDRESS 来说，bRequest 的值就是USB_REQ_SET_ADDRESS，标准请求之一.
	 */
	dr->bRequest = request;
	dr->wValue = cpu_to_le16p(&value);
	dr->wIndex = cpu_to_le16p(&index);
	dr->wLength = cpu_to_le16p(&size);

	//dbg("usb_control_msg");	

	/**
	 * 发送控制消息。
	 */
	ret = usb_internal_control_msg(dev, pipe, dr, data, size, timeout);

	kfree(dr);

	return ret;
}


/**
 * usb_interrupt_msg - Builds an interrupt urb, sends it off and waits for completion
 * @usb_dev: pointer to the usb device to send the message to
 * @pipe: endpoint "pipe" to send the message to
 * @data: pointer to the data to send
 * @len: length in bytes of the data to send
 * @actual_length: pointer to a location to put the actual length transferred in bytes
 * @timeout: time in msecs to wait for the message to complete before
 *	timing out (if 0 the wait is forever)
 * Context: !in_interrupt ()
 *
 * This function sends a simple interrupt message to a specified endpoint and
 * waits for the message to complete, or timeout.
 *
 * If successful, it returns 0, otherwise a negative error number.  The number
 * of actual bytes transferred will be stored in the actual_length paramater.
 *
 * Don't use this function from within an interrupt context, like a bottom half
 * handler.  If you need an asynchronous message, or need to send a message
 * from within interrupt context, use usb_submit_urb() If a thread in your
 * driver uses this call, make sure your disconnect() method can wait for it to
 * complete.  Since you don't have a handle on the URB used, you can't cancel
 * the request.
 */
/**
 * 创建并传输一个中断消息。
 */
int usb_interrupt_msg(struct usb_device *usb_dev, unsigned int pipe,
		      void *data, int len, int *actual_length, int timeout)
{
	return usb_bulk_msg(usb_dev, pipe, data, len, actual_length, timeout);
}
EXPORT_SYMBOL_GPL(usb_interrupt_msg);

/**
 *	usb_bulk_msg - Builds a bulk urb, sends it off and waits for completion
 *	@usb_dev: pointer to the usb device to send the message to
 *	@pipe: endpoint "pipe" to send the message to
 *	@data: pointer to the data to send
 *	@len: length in bytes of the data to send
 *	@actual_length: pointer to a location to put the actual length transferred in bytes
 *	@timeout: time in msecs to wait for the message to complete before
 *		timing out (if 0 the wait is forever)
 *	Context: !in_interrupt ()
 *
 *	This function sends a simple bulk message to a specified endpoint
 *	and waits for the message to complete, or timeout.
 *	
 *	If successful, it returns 0, otherwise a negative error number.
 *	The number of actual bytes transferred will be stored in the 
 *	actual_length paramater.
 *
 *	Don't use this function from within an interrupt context, like a
 *	bottom half handler.  If you need an asynchronous message, or need to
 *	send a message from within interrupt context, use usb_submit_urb()
 *      If a thread in your driver uses this call, make sure your disconnect()
 *      method can wait for it to complete.  Since you don't have a handle on
 *      the URB used, you can't cancel the request.
 *
 *	Because there is no usb_interrupt_msg() and no USBDEVFS_INTERRUPT
 *	ioctl, users are forced to abuse this routine by using it to submit
 *	URBs for interrupt endpoints.  We will take the liberty of creating
 *	an interrupt URB (with the default interval) if the target is an
 *	interrupt endpoint.
 */
/**
 * 创建并提交一个批量消息。
 */
int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, 
			void *data, int len, int *actual_length, int timeout)
{
	struct urb *urb;
	struct usb_host_endpoint *ep;

	/**
	 * 根据管道类型和端点号，获得端点结构。
	 */
	ep = (usb_pipein(pipe) ? usb_dev->ep_in : usb_dev->ep_out)
			[usb_pipeendpoint(pipe)];
	if (!ep || len < 0)
		return -EINVAL;

	/**
	 * 分配并初始化一个URB。
	 */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	/**
	 * 根据端点描述符来确定是中断传输还是批量传输
	 */
	if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_INT) {
		/**
		 * 如果是中断传输，需要修改管道类型。
		 */
		pipe = (pipe & ~(3 << 30)) | (PIPE_INTERRUPT << 30);
		usb_fill_int_urb(urb, usb_dev, pipe, data, len,
				usb_api_blocking_completion, NULL,
				ep->desc.bInterval);
	} else
		usb_fill_bulk_urb(urb, usb_dev, pipe, data, len,
				usb_api_blocking_completion, NULL);

	/**
	 * 等待URB返回。
	 */
	return usb_start_wait_urb(urb, timeout, actual_length);
}

/*-------------------------------------------------------------------*/

static void sg_clean (struct usb_sg_request *io)
{
	if (io->urbs) {
		while (io->entries--)
			usb_free_urb (io->urbs [io->entries]);
		kfree (io->urbs);
		io->urbs = NULL;
	}
	if (io->dev->dev.dma_mask != NULL)
		usb_buffer_unmap_sg (io->dev, usb_pipein(io->pipe),
				io->sg, io->nents);
	io->dev = NULL;
}

static void sg_complete (struct urb *urb)
{
	struct usb_sg_request	*io = urb->context;
	int status = urb->status;

	spin_lock (&io->lock);

	/* In 2.5 we require hcds' endpoint queues not to progress after fault
	 * reports, until the completion callback (this!) returns.  That lets
	 * device driver code (like this routine) unlink queued urbs first,
	 * if it needs to, since the HC won't work on them at all.  So it's
	 * not possible for page N+1 to overwrite page N, and so on.
	 *
	 * That's only for "hard" faults; "soft" faults (unlinks) sometimes
	 * complete before the HCD can get requests away from hardware,
	 * though never during cleanup after a hard fault.
	 */
	if (io->status
			&& (io->status != -ECONNRESET
				|| status != -ECONNRESET)
			&& urb->actual_length) {
		dev_err (io->dev->bus->controller,
			"dev %s ep%d%s scatterlist error %d/%d\n",
			io->dev->devpath,
			usb_endpoint_num(&urb->ep->desc),
			usb_urb_dir_in(urb) ? "in" : "out",
			status, io->status);
		// BUG ();
	}

	if (io->status == 0 && status && status != -ECONNRESET) {
		int i, found, retval;

		io->status = status;

		/* the previous urbs, and this one, completed already.
		 * unlink pending urbs so they won't rx/tx bad data.
		 * careful: unlink can sometimes be synchronous...
		 */
		spin_unlock (&io->lock);
		for (i = 0, found = 0; i < io->entries; i++) {
			if (!io->urbs [i] || !io->urbs [i]->dev)
				continue;
			if (found) {
				retval = usb_unlink_urb (io->urbs [i]);
				if (retval != -EINPROGRESS &&
				    retval != -ENODEV &&
				    retval != -EBUSY)
					dev_err (&io->dev->dev,
						"%s, unlink --> %d\n",
						__FUNCTION__, retval);
			} else if (urb == io->urbs [i])
				found = 1;
		}
		spin_lock (&io->lock);
	}
	urb->dev = NULL;

	/* on the last completion, signal usb_sg_wait() */
	io->bytes += urb->actual_length;
	io->count--;
	if (!io->count)
		complete (&io->complete);

	spin_unlock (&io->lock);
}


/**
 * usb_sg_init - initializes scatterlist-based bulk/interrupt I/O request
 * @io: request block being initialized.  until usb_sg_wait() returns,
 *	treat this as a pointer to an opaque block of memory,
 * @dev: the usb device that will send or receive the data
 * @pipe: endpoint "pipe" used to transfer the data
 * @period: polling rate for interrupt endpoints, in frames or
 * 	(for high speed endpoints) microframes; ignored for bulk
 * @sg: scatterlist entries
 * @nents: how many entries in the scatterlist
 * @length: how many bytes to send from the scatterlist, or zero to
 * 	send every byte identified in the list.
 * @mem_flags: SLAB_* flags affecting memory allocations in this call
 *
 * Returns zero for success, else a negative errno value.  This initializes a
 * scatter/gather request, allocating resources such as I/O mappings and urb
 * memory (except maybe memory used by USB controller drivers).
 *
 * The request must be issued using usb_sg_wait(), which waits for the I/O to
 * complete (or to be canceled) and then cleans up all resources allocated by
 * usb_sg_init().
 *
 * The request may be canceled with usb_sg_cancel(), either before or after
 * usb_sg_wait() is called.
 */
int usb_sg_init (
	struct usb_sg_request	*io,
	struct usb_device	*dev,
	unsigned		pipe, 
	unsigned		period,
	struct scatterlist	*sg,
	int			nents,
	size_t			length,
	gfp_t			mem_flags
)
{
	int			i;
	int			urb_flags;
	int			dma;

	if (!io || !dev || !sg
			|| usb_pipecontrol (pipe)
			|| usb_pipeisoc (pipe)
			|| nents <= 0)
		return -EINVAL;

	spin_lock_init (&io->lock);
	io->dev = dev;
	io->pipe = pipe;
	io->sg = sg;
	io->nents = nents;

	/* not all host controllers use DMA (like the mainstream pci ones);
	 * they can use PIO (sl811) or be software over another transport.
	 */
	dma = (dev->dev.dma_mask != NULL);
	if (dma)
		io->entries = usb_buffer_map_sg(dev, usb_pipein(pipe),
				sg, nents);
	else
		io->entries = nents;

	/* initialize all the urbs we'll use */
	if (io->entries <= 0)
		return io->entries;

	io->count = io->entries;
	io->urbs = kmalloc (io->entries * sizeof *io->urbs, mem_flags);
	if (!io->urbs)
		goto nomem;

	urb_flags = URB_NO_TRANSFER_DMA_MAP | URB_NO_INTERRUPT;
	if (usb_pipein (pipe))
		urb_flags |= URB_SHORT_NOT_OK;

	for (i = 0; i < io->entries; i++) {
		unsigned		len;

		io->urbs [i] = usb_alloc_urb (0, mem_flags);
		if (!io->urbs [i]) {
			io->entries = i;
			goto nomem;
		}

		io->urbs [i]->dev = NULL;
		io->urbs [i]->pipe = pipe;
		io->urbs [i]->interval = period;
		io->urbs [i]->transfer_flags = urb_flags;

		io->urbs [i]->complete = sg_complete;
		io->urbs [i]->context = io;

		/*
		 * Some systems need to revert to PIO when DMA is temporarily
		 * unavailable.  For their sakes, both transfer_buffer and
		 * transfer_dma are set when possible.  However this can only
		 * work on systems without:
		 *
		 *  - HIGHMEM, since DMA buffers located in high memory are
		 *    not directly addressable by the CPU for PIO;
		 *
		 *  - IOMMU, since dma_map_sg() is allowed to use an IOMMU to
		 *    make virtually discontiguous buffers be "dma-contiguous"
		 *    so that PIO and DMA need diferent numbers of URBs.
		 *
		 * So when HIGHMEM or IOMMU are in use, transfer_buffer is NULL
		 * to prevent stale pointers and to help spot bugs.
		 */
		if (dma) {
			io->urbs [i]->transfer_dma = sg_dma_address (sg + i);
			len = sg_dma_len (sg + i);
#if defined(CONFIG_HIGHMEM) || defined(CONFIG_GART_IOMMU)
			io->urbs[i]->transfer_buffer = NULL;
#else
			io->urbs[i]->transfer_buffer = sg_virt(&sg[i]);
#endif
		} else {
			/* hc may use _only_ transfer_buffer */
			io->urbs [i]->transfer_buffer = sg_virt(&sg[i]);
			len = sg [i].length;
		}

		if (length) {
			len = min_t (unsigned, len, length);
			length -= len;
			if (length == 0)
				io->entries = i + 1;
		}
		io->urbs [i]->transfer_buffer_length = len;
	}
	io->urbs [--i]->transfer_flags &= ~URB_NO_INTERRUPT;

	/* transaction state */
	io->status = 0;
	io->bytes = 0;
	init_completion (&io->complete);
	return 0;

nomem:
	sg_clean (io);
	return -ENOMEM;
}


/**
 * usb_sg_wait - synchronously execute scatter/gather request
 * @io: request block handle, as initialized with usb_sg_init().
 * 	some fields become accessible when this call returns.
 * Context: !in_interrupt ()
 *
 * This function blocks until the specified I/O operation completes.  It
 * leverages the grouping of the related I/O requests to get good transfer
 * rates, by queueing the requests.  At higher speeds, such queuing can
 * significantly improve USB throughput.
 *
 * There are three kinds of completion for this function.
 * (1) success, where io->status is zero.  The number of io->bytes
 *     transferred is as requested.
 * (2) error, where io->status is a negative errno value.  The number
 *     of io->bytes transferred before the error is usually less
 *     than requested, and can be nonzero.
 * (3) cancellation, a type of error with status -ECONNRESET that
 *     is initiated by usb_sg_cancel().
 *
 * When this function returns, all memory allocated through usb_sg_init() or
 * this call will have been freed.  The request block parameter may still be
 * passed to usb_sg_cancel(), or it may be freed.  It could also be
 * reinitialized and then reused.
 *
 * Data Transfer Rates:
 *
 * Bulk transfers are valid for full or high speed endpoints.
 * The best full speed data rate is 19 packets of 64 bytes each
 * per frame, or 1216 bytes per millisecond.
 * The best high speed data rate is 13 packets of 512 bytes each
 * per microframe, or 52 KBytes per millisecond.
 *
 * The reason to use interrupt transfers through this API would most likely
 * be to reserve high speed bandwidth, where up to 24 KBytes per millisecond
 * could be transferred.  That capability is less useful for low or full
 * speed interrupt endpoints, which allow at most one packet per millisecond,
 * of at most 8 or 64 bytes (respectively).
 */
void usb_sg_wait (struct usb_sg_request *io)
{
	int		i, entries = io->entries;

	/* queue the urbs.  */
	spin_lock_irq (&io->lock);
	i = 0;
	while (i < entries && !io->status) {
		int	retval;

		io->urbs [i]->dev = io->dev;
		retval = usb_submit_urb (io->urbs [i], GFP_ATOMIC);

		/* after we submit, let completions or cancelations fire;
		 * we handshake using io->status.
		 */
		spin_unlock_irq (&io->lock);
		switch (retval) {
			/* maybe we retrying will recover */
		case -ENXIO:	// hc didn't queue this one
		case -EAGAIN:
		case -ENOMEM:
			io->urbs[i]->dev = NULL;
			retval = 0;
			yield ();
			break;

			/* no error? continue immediately.
			 *
			 * NOTE: to work better with UHCI (4K I/O buffer may
			 * need 3K of TDs) it may be good to limit how many
			 * URBs are queued at once; N milliseconds?
			 */
		case 0:
			++i;
			cpu_relax ();
			break;

			/* fail any uncompleted urbs */
		default:
			io->urbs [i]->dev = NULL;
			io->urbs [i]->status = retval;
			dev_dbg (&io->dev->dev, "%s, submit --> %d\n",
				__FUNCTION__, retval);
			usb_sg_cancel (io);
		}
		spin_lock_irq (&io->lock);
		if (retval && (io->status == 0 || io->status == -ECONNRESET))
			io->status = retval;
	}
	io->count -= entries - i;
	if (io->count == 0)
		complete (&io->complete);
	spin_unlock_irq (&io->lock);

	/* OK, yes, this could be packaged as non-blocking.
	 * So could the submit loop above ... but it's easier to
	 * solve neither problem than to solve both!
	 */
	wait_for_completion (&io->complete);

	sg_clean (io);
}

/**
 * usb_sg_cancel - stop scatter/gather i/o issued by usb_sg_wait()
 * @io: request block, initialized with usb_sg_init()
 *
 * This stops a request after it has been started by usb_sg_wait().
 * It can also prevents one initialized by usb_sg_init() from starting,
 * so that call just frees resources allocated to the request.
 */
void usb_sg_cancel (struct usb_sg_request *io)
{
	unsigned long	flags;

	spin_lock_irqsave (&io->lock, flags);

	/* shut everything down, if it didn't already */
	if (!io->status) {
		int	i;

		io->status = -ECONNRESET;
		spin_unlock (&io->lock);
		for (i = 0; i < io->entries; i++) {
			int	retval;

			if (!io->urbs [i]->dev)
				continue;
			retval = usb_unlink_urb (io->urbs [i]);
			if (retval != -EINPROGRESS && retval != -EBUSY)
				dev_warn (&io->dev->dev, "%s, unlink --> %d\n",
					__FUNCTION__, retval);
		}
		spin_lock (&io->lock);
	}
	spin_unlock_irqrestore (&io->lock, flags);
}

/*-------------------------------------------------------------------*/

/**
 * usb_get_descriptor - issues a generic GET_DESCRIPTOR request
 * @dev: the device whose descriptor is being retrieved
 * @type: the descriptor type (USB_DT_*)
 * @index: the number of the descriptor
 * @buf: where to put the descriptor
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Gets a USB descriptor.  Convenience functions exist to simplify
 * getting some types of descriptors.  Use
 * usb_get_string() or usb_string() for USB_DT_STRING.
 * Device (USB_DT_DEVICE) and configuration descriptors (USB_DT_CONFIG)
 * are part of the device structure.
 * In addition to a number of USB-standard descriptors, some
 * devices also use class-specific or vendor-specific descriptors.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
/**
 * 获得设备描述符。
 *		type:		要获得的描述符类型:设备描述符，配置描述符和字符串描述符
 *		index:		对于配置描述符，可能有多个，这里指定描述符的序号。
 *		buf,size:	用于存放返回结果。
 */
int usb_get_descriptor(struct usb_device *dev, unsigned char type, unsigned char index, void *buf, int size)
{
	int i;
	int result;
	
	memset(buf,0,size);	// Make sure we parse really received data

	/**
	 * 某些设备实现上有问题，一次读取可能不成功，这里多试几次。
	 */
	for (i = 0; i < 3; ++i) {
		/* retry on length 0 or error; some devices are flakey */
		/**
		 * 通过向设备发送一个USB_REQ_GET_DESCRIPTOR控制消息来获取设备描述符。
		 */
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				(type << 8) + index, 0, buf, size,
				USB_CTRL_GET_TIMEOUT);/* USB_CTRL_GET_TIMEOUT表示超时时间为5S */
		/**
		 * 控制消息发送不成功，需要重试。
		 */
		if (result == 0 || result == -EPIPE)
			continue;
		/**
		 * 返回的描述符类型与请求的不一致。
		 */
		if (result > 1 && ((u8 *)buf)[1] != type) {
			result = -EPROTO;
			continue;
		}
		break;
	}
	return result;
}

/**
 * usb_get_string - gets a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @langid: code for language chosen (from string descriptor zero)
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Retrieves a string, encoded using UTF-16LE (Unicode, 16 bits per character,
 * in little-endian byte order).
 * The usb_string() function will often be a convenient way to turn
 * these strings into kernel-printable form.
 *
 * Strings may be referenced in device, configuration, interface, or other
 * descriptors, and could also be used in vendor-specific ways.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
static int usb_get_string(struct usb_device *dev, unsigned short langid,
			  unsigned char index, void *buf, int size)
{
	int i;
	int result;

	/**
	 * 某些有问题的设备需要读取多次。
	 */
	for (i = 0; i < 3; ++i) {
		/* retry on length 0 or stall; some devices are flakey */
		/**
		 * 向设备发送控制消息，读取其描述符。
		 */
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
			(USB_DT_STRING << 8) + index, langid, buf, size,
			USB_CTRL_GET_TIMEOUT);
		if (!(result == 0 || result == -EPIPE))
			break;
	}
	return result;
}

static void usb_try_string_workarounds(unsigned char *buf, int *length)
{
	int newlength, oldlength = *length;

	/**
	 * 遍历处理unicode编码。得到字符数。
	 */
	for (newlength = 2; newlength + 1 < oldlength; newlength += 2)
		if (!isprint(buf[newlength]) || buf[newlength + 1])
			break;

	if (newlength > 2) {
		buf[0] = newlength;
		*length = newlength;
	}
}

/**
 * 获取字符串描述符。
 */
static int usb_string_sub(struct usb_device *dev, unsigned int langid,
		unsigned int index, unsigned char *buf)
{
	int rc;

	/* Try to read the string descriptor by asking for the maximum
	 * possible number of bytes */
	/**
	 * 设备在读取字符串描述符时会有问题。
	 */
	if (dev->quirks & USB_QUIRK_STRING_FETCH_255)
		rc = -EIO;
	else
		/**
		 * usb_get_string真正从设备读取字符串描述符。
		 */
		rc = usb_get_string(dev, langid, index, buf, 255);

	/* If that failed try to read the descriptor length, then
	 * ask for just that many bytes */
	if (rc < 2) {/* 读取失败了 */
		/**
		 * 先读取描述符长度。
		 */
		rc = usb_get_string(dev, langid, index, buf, 2);
		/**
		 * 根据长度读取描述符。
		 */
		if (rc == 2)
			rc = usb_get_string(dev, langid, index, buf, buf[0]);
	}

	if (rc >= 2) {/* 除去长度和类型字段，还读到了字符串数据。 */
		if (!buf[0] && !buf[1])/* 前两个字节中有一个是0，需要通过usb_try_string_workarounds计算有效的数据长度。 */
			usb_try_string_workarounds(buf, &rc);

		/* There might be extra junk at the end of the descriptor */
		/**
		 * 返回结果中有一些垃圾数据。
		 */
		if (buf[0] < rc)
			rc = buf[0];

		/**
		 * unicode字符是两个字节对齐的，强制进行对齐处理。
		 */
		rc = rc - (rc & 1); /* force a multiple of two */
	}

	if (rc < 2)
		rc = (rc < 0 ? rc : -EINVAL);

	return rc;
}

/**
 * usb_string - returns ISO 8859-1 version of a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 * 
 * This converts the UTF-16LE encoded strings returned by devices, from
 * usb_get_string_descriptor(), to null-terminated ISO-8859-1 encoded ones
 * that are more usable in most kernel contexts.  Note that all characters
 * in the chosen descriptor that can't be encoded using ISO-8859-1
 * are converted to the question mark ("?") character, and this function
 * chooses strings in the first language supported by the device.
 *
 * The ASCII (or, redundantly, "US-ASCII") character set is the seven-bit
 * subset of ISO 8859-1. ISO-8859-1 is the eight-bit subset of Unicode,
 * and is appropriate for use many uses of English and several other
 * Western European languages.  (But it doesn't include the "Euro" symbol.)
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns length of the string (>= 0) or usb_control_msg status (< 0).
 */
int usb_string(struct usb_device *dev, int index, char *buf, size_t size)
{
	unsigned char *tbuf;
	int err;
	unsigned int u, idx;

	/**
	 * 设备不能是挂起状态。
	 */
	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;
	if (size <= 0 || !buf || !index)
		return -EINVAL;
	buf[0] = 0;
	tbuf = kmalloc(256, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	/* get langid for strings if it's not yet known */
	if (!dev->have_langid) {/* 设备没有指定语言ID。 */
		/**
		 * 索引号为0，是为了获得设备所支持的所有语言ID。
		 */
		err = usb_string_sub(dev, 0, 0, tbuf);
		if (err < 0) {/* 获取语言ID失败。 */
			dev_err (&dev->dev,
				"string descriptor 0 read error: %d\n",
				err);
			goto errout;
		} else if (err < 4) {/* 最小长度也应该是4，失败了。 */
			dev_err (&dev->dev, "string descriptor 0 too short\n");
			err = -EINVAL;
			goto errout;
		} else {
			dev->have_langid = 1;
			/**
			 * 第三、四字节是可用ID。
			 */
			dev->string_langid = tbuf[2] | (tbuf[3]<< 8);
				/* always use the first langid listed */
			dev_dbg (&dev->dev, "default language 0x%04x\n",
				dev->string_langid);
		}
	}
	
	/**
	 * 使用语言ID去获取字符串描述符。
	 */
	err = usb_string_sub(dev, dev->string_langid, index, tbuf);
	if (err < 0)
		goto errout;

	size--;		/* leave room for trailing NULL char in output buffer */
	/**
	 * 将unicode转换为ISO8859－1
	 */
	for (idx = 0, u = 2; u < err; u += 2) {
		if (idx >= size)
			break;
		if (tbuf[u+1])			/* high byte */
			buf[idx++] = '?';  /* non ISO-8859-1 character */
		else
			buf[idx++] = tbuf[u];
	}
	buf[idx] = 0;
	err = idx;

	if (tbuf[1] != USB_DT_STRING)
		dev_dbg(&dev->dev, "wrong descriptor type %02x for string %d (\"%s\")\n", tbuf[1], index, buf);

 errout:
	kfree(tbuf);
	return err;
}

/**
 * usb_cache_string - read a string descriptor and cache it for later use
 * @udev: the device whose string descriptor is being read
 * @index: the descriptor index
 *
 * Returns a pointer to a kmalloc'ed buffer containing the descriptor string,
 * or NULL if the index is 0 or the string could not be read.
 */
/**
 * 获得接口的字符串描述符。
 */
char *usb_cache_string(struct usb_device *udev, int index)
{
	char *buf;
	char *smallbuf = NULL;
	int len;

	/**
	 * 索引号不能为0，这是规范里面规定了的。
	 */
	if (index > 0 && (buf = kmalloc(256, GFP_KERNEL)) != NULL) {
		/**
		 * usb_string完成实际的工作。
		 */
		if ((len = usb_string(udev, index, buf, 256)) > 0) {
			if ((smallbuf = kmalloc(++len, GFP_KERNEL)) == NULL)
				return buf;
			memcpy(smallbuf, buf, len);
		}
		kfree(buf);
	}
	return smallbuf;
}

/*
 * usb_get_device_descriptor - (re)reads the device descriptor (usbcore)
 * @dev: the device whose device descriptor is being updated
 * @size: how much of the descriptor to read
 * Context: !in_interrupt ()
 *
 * Updates the copy of the device descriptor stored in the device structure,
 * which dedicates space for this purpose.
 *
 * Not exported, only for use by the core.  If drivers really want to read
 * the device descriptor directly, they can call usb_get_descriptor() with
 * type = USB_DT_DEVICE and index = 0.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
/**
 * 获得设备的描述符。
 */
int usb_get_device_descriptor(struct usb_device *dev, unsigned int size)
{
	struct usb_device_descriptor *desc;
	int ret;

	if (size > sizeof(*desc))
		return -EINVAL;
	/**
	 * 准备一个设备描述符结构。
	 */
	desc = kmalloc(sizeof(*desc), GFP_NOIO);
	if (!desc)
		return -ENOMEM;

	/**
	 * 调用usb_get_descriptor()获得设备描述符
	 */
	ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, desc, size);
	/**
	 * 将获得的设备描述符复制到设备结构中。
	 */
	if (ret >= 0) 
		memcpy(&dev->descriptor, desc, size);
	kfree(desc);
	return ret;
}

/**
 * usb_get_status - issues a GET_STATUS call
 * @dev: the device whose status is being checked
 * @type: USB_RECIP_*; for device, interface, or endpoint
 * @target: zero (for device), else interface or endpoint number
 * @data: pointer to two bytes of bitmap data
 * Context: !in_interrupt ()
 *
 * Returns device, interface, or endpoint status.  Normally only of
 * interest to see if the device is self powered, or has enabled the
 * remote wakeup facility; or whether a bulk or interrupt endpoint
 * is halted ("stalled").
 *
 * Bits in these status bitmaps are set using the SET_FEATURE request,
 * and cleared using the CLEAR_FEATURE request.  The usb_clear_halt()
 * function should be used to clear halt ("stall") status.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_status(struct usb_device *dev, int type, int target, void *data)
{
	int ret;
	u16 *status = kmalloc(sizeof(*status), GFP_KERNEL);

	if (!status)
		return -ENOMEM;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | type, 0, target, status,
		sizeof(*status), USB_CTRL_GET_TIMEOUT);

	*(u16 *)data = *status;
	kfree(status);
	return ret;
}

/**
 * usb_clear_halt - tells device to clear endpoint halt/stall condition
 * @dev: device whose endpoint is halted
 * @pipe: endpoint "pipe" being cleared
 * Context: !in_interrupt ()
 *
 * This is used to clear halt conditions for bulk and interrupt endpoints,
 * as reported by URB completion status.  Endpoints that are halted are
 * sometimes referred to as being "stalled".  Such endpoints are unable
 * to transmit or receive data until the halt status is cleared.  Any URBs
 * queued for such an endpoint should normally be unlinked by the driver
 * before clearing the halt condition, as described in sections 5.7.5
 * and 5.8.5 of the USB 2.0 spec.
 *
 * Note that control and isochronous endpoints don't halt, although control
 * endpoints report "protocol stall" (for unsupported requests) using the
 * same status code used to report a true stall.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_clear_halt(struct usb_device *dev, int pipe)
{
	int result;
	int endp = usb_pipeendpoint(pipe);
	
	if (usb_pipein (pipe))
		endp |= USB_DIR_IN;

	/* we don't care if it wasn't halted first. in fact some devices
	 * (like some ibmcam model 1 units) seem to expect hosts to make
	 * this request for iso endpoints, which can't halt!
	 */
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT,
		USB_ENDPOINT_HALT, endp, NULL, 0,
		USB_CTRL_SET_TIMEOUT);

	/* don't un-halt or force to DATA0 except on success */
	if (result < 0)
		return result;

	/* NOTE:  seems like Microsoft and Apple don't bother verifying
	 * the clear "took", so some devices could lock up if you check...
	 * such as the Hagiwara FlashGate DUAL.  So we won't bother.
	 *
	 * NOTE:  make sure the logic here doesn't diverge much from
	 * the copy in usb-storage, for as long as we need two copies.
	 */

	/* toggle was reset by the clear */
	usb_settoggle(dev, usb_pipeendpoint(pipe), usb_pipeout(pipe), 0);

	return 0;
}

/**
 * usb_disable_endpoint -- Disable an endpoint by address
 * @dev: the device whose endpoint is being disabled
 * @epaddr: the endpoint's address.  Endpoint number for output,
 *	endpoint number + USB_DIR_IN for input
 *
 * Deallocates hcd/hardware state for this endpoint ... and nukes all
 * pending urbs.
 *
 * If the HCD hasn't registered a disable() function, this sets the
 * endpoint's maxpacket size to 0 to prevent further submissions.
 */
/**
 * 禁止端点。
 */
void usb_disable_endpoint(struct usb_device *dev, unsigned int epaddr)
{
	unsigned int epnum = epaddr & USB_ENDPOINT_NUMBER_MASK;
	struct usb_host_endpoint *ep;

	if (!dev)
		return;

	/**
	 * 根据端点的方向取得端点描述符。
	 */
	if (usb_endpoint_out(epaddr)) {
		ep = dev->ep_out[epnum];
		dev->ep_out[epnum] = NULL;
	} else {
		ep = dev->ep_in[epnum];
		dev->ep_in[epnum] = NULL;
	}
	/**
	 * 禁止端点。
	 */
	if (ep && dev->bus)
		usb_hcd_endpoint_disable(dev, ep);
}

/**
 * usb_disable_interface -- Disable all endpoints for an interface
 * @dev: the device whose interface is being disabled
 * @intf: pointer to the interface descriptor
 *
 * Disables all the endpoints for the interface's current altsetting.
 */
void usb_disable_interface(struct usb_device *dev, struct usb_interface *intf)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	int i;

	for (i = 0; i < alt->desc.bNumEndpoints; ++i) {
		usb_disable_endpoint(dev,
				alt->endpoint[i].desc.bEndpointAddress);
	}
}

/*
 * usb_disable_device - Disable all the endpoints for a USB device
 * @dev: the device whose endpoints are being disabled
 * @skip_ep0: 0 to disable endpoint 0, 1 to skip it.
 *
 * Disables all the device's endpoints, potentially including endpoint 0.
 * Deallocates hcd/hardware state for the endpoints (nuking all or most
 * pending urbs) and usbcore state for the interfaces, so that usbcore
 * must usb_set_configuration() before any interfaces could be used.
 */
/**
 * 禁止USB设备，使其回到USB_STATE_ADDRESS状态。
 */
void usb_disable_device(struct usb_device *dev, int skip_ep0)
{
	int i;

	dev_dbg(&dev->dev, "%s nuking %s URBs\n", __FUNCTION__,
			skip_ep0 ? "non-ep0" : "all");
	/**
	 * 首先禁止掉所有端点。
	 */
	for (i = skip_ep0; i < 16; ++i) {
		usb_disable_endpoint(dev, i);
		usb_disable_endpoint(dev, i + USB_DIR_IN);
	}
	dev->toggle[0] = dev->toggle[1] = 0;

	/* getting rid of interfaces will disconnect
	 * any drivers bound to them (a key side effect)
	 */
	/**
	 * 设备当前激活了配置。
	 */
	if (dev->actconfig) {
		/**
		 * 将所有接口从设备模型中删除掉。
		 */
		for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
			struct usb_interface	*interface;

			/* remove this interface if it has been registered */
			interface = dev->actconfig->interface[i];
			if (!device_is_registered(&interface->dev))
				continue;
			dev_dbg (&dev->dev, "unregistering interface %s\n",
				interface->dev.bus_id);
			usb_remove_sysfs_intf_files(interface);
			device_del (&interface->dev);
		}

		/* Now that the interfaces are unbound, nobody should
		 * try to access them.
		 */
		/**
		 * 将活动配置的接口数组清空。
		 */
		for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
			put_device (&dev->actconfig->interface[i]->dev);
			dev->actconfig->interface[i] = NULL;
		}
		dev->actconfig = NULL;
		/**
		 * 如果当前是配置状态，则回到USB_STATE_ADDRESS状态。
		 */
		if (dev->state == USB_STATE_CONFIGURED)
			usb_set_device_state(dev, USB_STATE_ADDRESS);
	}
}


/*
 * usb_enable_endpoint - Enable an endpoint for USB communications
 * @dev: the device whose interface is being enabled
 * @ep: the endpoint
 *
 * Resets the endpoint toggle, and sets dev->ep_{in,out} pointers.
 * For control endpoints, both the input and output sides are handled.
 */
/**
 * 激活端点。
 */
static void
usb_enable_endpoint(struct usb_device *dev, struct usb_host_endpoint *ep)
{
	int epnum = usb_endpoint_num(&ep->desc);
	int is_out = usb_endpoint_dir_out(&ep->desc);
	int is_control = usb_endpoint_xfer_control(&ep->desc);

	if (is_out || is_control) {
		usb_settoggle(dev, epnum, 1, 0);
		dev->ep_out[epnum] = ep;
	}
	if (!is_out || is_control) {
		usb_settoggle(dev, epnum, 0, 0);
		dev->ep_in[epnum] = ep;
	}
	ep->enabled = 1;
}

/*
 * usb_enable_interface - Enable all the endpoints for an interface
 * @dev: the device whose interface is being enabled
 * @intf: pointer to the interface descriptor
 *
 * Enables all the endpoints for the interface's current altsetting.
 */
/**
 * 激活接口设置。
 */
static void usb_enable_interface(struct usb_device *dev,
				 struct usb_interface *intf)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	int i;

	/**
	 * 轮询接口的每个端点并激活它。
	 */
	for (i = 0; i < alt->desc.bNumEndpoints; ++i)
		usb_enable_endpoint(dev, &alt->endpoint[i]);
}

/**
 * usb_set_interface - Makes a particular alternate setting be current
 * @dev: the device whose interface is being updated
 * @interface: the interface being updated
 * @alternate: the setting being chosen.
 * Context: !in_interrupt ()
 *
 * This is used to enable data transfers on interfaces that may not
 * be enabled by default.  Not all devices support such configurability.
 * Only the driver bound to an interface may change its setting.
 *
 * Within any given configuration, each interface may have several
 * alternative settings.  These are often used to control levels of
 * bandwidth consumption.  For example, the default setting for a high
 * speed interrupt endpoint may not send more than 64 bytes per microframe,
 * while interrupt transfers of up to 3KBytes per microframe are legal.
 * Also, isochronous endpoints may never be part of an
 * interface's default setting.  To access such bandwidth, alternate
 * interface settings must be made current.
 *
 * Note that in the Linux USB subsystem, bandwidth associated with
 * an endpoint in a given alternate setting is not reserved until an URB
 * is submitted that needs that bandwidth.  Some other operating systems
 * allocate bandwidth early, when a configuration is chosen.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 * Also, drivers must not change altsettings while urbs are scheduled for
 * endpoints in that interface; all such urbs must first be completed
 * (perhaps forced by unlinking).
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_set_interface(struct usb_device *dev, int interface, int alternate)
{
	struct usb_interface *iface;
	struct usb_host_interface *alt;
	int ret;
	int manual = 0;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;

	iface = usb_ifnum_to_if(dev, interface);
	if (!iface) {
		dev_dbg(&dev->dev, "selecting invalid interface %d\n",
			interface);
		return -EINVAL;
	}

	alt = usb_altnum_to_altsetting(iface, alternate);
	if (!alt) {
		warn("selecting invalid altsetting %d", alternate);
		return -EINVAL;
	}

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				   USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE,
				   alternate, interface, NULL, 0, 5000);

	/* 9.4.10 says devices don't need this and are free to STALL the
	 * request if the interface only has one alternate setting.
	 */
	if (ret == -EPIPE && iface->num_altsetting == 1) {
		dev_dbg(&dev->dev,
			"manual set_interface for iface %d, alt %d\n",
			interface, alternate);
		manual = 1;
	} else if (ret < 0)
		return ret;

	/* FIXME drivers shouldn't need to replicate/bugfix the logic here
	 * when they implement async or easily-killable versions of this or
	 * other "should-be-internal" functions (like clear_halt).
	 * should hcd+usbcore postprocess control requests?
	 */

	/* prevent submissions using previous endpoint settings */
	if (iface->cur_altsetting != alt && device_is_registered(&iface->dev))
		usb_remove_sysfs_intf_files(iface);
	usb_disable_interface(dev, iface);

	iface->cur_altsetting = alt;

	/* If the interface only has one altsetting and the device didn't
	 * accept the request, we attempt to carry out the equivalent action
	 * by manually clearing the HALT feature for each endpoint in the
	 * new altsetting.
	 */
	if (manual) {
		int i;

		for (i = 0; i < alt->desc.bNumEndpoints; i++) {
			unsigned int epaddr =
				alt->endpoint[i].desc.bEndpointAddress;
			unsigned int pipe =
	__create_pipe(dev, USB_ENDPOINT_NUMBER_MASK & epaddr)
	| (usb_endpoint_out(epaddr) ? USB_DIR_OUT : USB_DIR_IN);

			usb_clear_halt(dev, pipe);
		}
	}

	/* 9.1.1.5: reset toggles for all endpoints in the new altsetting
	 *
	 * Note:
	 * Despite EP0 is always present in all interfaces/AS, the list of
	 * endpoints from the descriptor does not contain EP0. Due to its
	 * omnipresence one might expect EP0 being considered "affected" by
	 * any SetInterface request and hence assume toggles need to be reset.
	 * However, EP0 toggles are re-synced for every individual transfer
	 * during the SETUP stage - hence EP0 toggles are "don't care" here.
	 * (Likewise, EP0 never "halts" on well designed devices.)
	 */
	usb_enable_interface(dev, iface);
	if (device_is_registered(&iface->dev))
		usb_create_sysfs_intf_files(iface);

	return 0;
}

/**
 * usb_reset_configuration - lightweight device reset
 * @dev: the device whose configuration is being reset
 *
 * This issues a standard SET_CONFIGURATION request to the device using
 * the current configuration.  The effect is to reset most USB-related
 * state in the device, including interface altsettings (reset to zero),
 * endpoint halts (cleared), and data toggle (only for bulk and interrupt
 * endpoints).  Other usbcore state is unchanged, including bindings of
 * usb device drivers to interfaces.
 *
 * Because this affects multiple interfaces, avoid using this with composite
 * (multi-interface) devices.  Instead, the driver for each interface may
 * use usb_set_interface() on the interfaces it claims.  Be careful though;
 * some devices don't support the SET_INTERFACE request, and others won't
 * reset all the interface state (notably data toggles).  Resetting the whole
 * configuration would affect other drivers' interfaces.
 *
 * The caller must own the device lock.
 *
 * Returns zero on success, else a negative error code.
 */
int usb_reset_configuration(struct usb_device *dev)
{
	int			i, retval;
	struct usb_host_config	*config;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;

	/* caller must have locked the device and must own
	 * the usb bus readlock (so driver bindings are stable);
	 * calls during probe() are fine
	 */

	for (i = 1; i < 16; ++i) {
		usb_disable_endpoint(dev, i);
		usb_disable_endpoint(dev, i + USB_DIR_IN);
	}

	config = dev->actconfig;
	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			config->desc.bConfigurationValue, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (retval < 0)
		return retval;

	dev->toggle[0] = dev->toggle[1] = 0;

	/* re-init hc/hcd interface/endpoint state */
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = config->interface[i];
		struct usb_host_interface *alt;

		if (device_is_registered(&intf->dev))
			usb_remove_sysfs_intf_files(intf);
		alt = usb_altnum_to_altsetting(intf, 0);

		/* No altsetting 0?  We'll assume the first altsetting.
		 * We could use a GetInterface call, but if a device is
		 * so non-compliant that it doesn't have altsetting 0
		 * then I wouldn't trust its reply anyway.
		 */
		if (!alt)
			alt = &intf->altsetting[0];

		intf->cur_altsetting = alt;
		usb_enable_interface(dev, intf);
		if (device_is_registered(&intf->dev))
			usb_create_sysfs_intf_files(intf);
	}
	return 0;
}

static void usb_release_interface(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_interface_cache *intfc =
			altsetting_to_usb_interface_cache(intf->altsetting);

	kref_put(&intfc->ref, usb_release_interface_cache);
	kfree(intf);
}

#ifdef	CONFIG_HOTPLUG
static int usb_if_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct usb_device *usb_dev;
	struct usb_interface *intf;
	struct usb_host_interface *alt;

	intf = to_usb_interface(dev);
	usb_dev = interface_to_usbdev(intf);
	alt = intf->cur_altsetting;

	if (add_uevent_var(env, "INTERFACE=%d/%d/%d",
		   alt->desc.bInterfaceClass,
		   alt->desc.bInterfaceSubClass,
		   alt->desc.bInterfaceProtocol))
		return -ENOMEM;

	if (add_uevent_var(env,
		   "MODALIAS=usb:v%04Xp%04Xd%04Xdc%02Xdsc%02Xdp%02Xic%02Xisc%02Xip%02X",
		   le16_to_cpu(usb_dev->descriptor.idVendor),
		   le16_to_cpu(usb_dev->descriptor.idProduct),
		   le16_to_cpu(usb_dev->descriptor.bcdDevice),
		   usb_dev->descriptor.bDeviceClass,
		   usb_dev->descriptor.bDeviceSubClass,
		   usb_dev->descriptor.bDeviceProtocol,
		   alt->desc.bInterfaceClass,
		   alt->desc.bInterfaceSubClass,
		   alt->desc.bInterfaceProtocol))
		return -ENOMEM;

	return 0;
}

#else

static int usb_if_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return -ENODEV;
}
#endif	/* CONFIG_HOTPLUG */

struct device_type usb_if_device_type = {
	.name =		"usb_interface",
	.release =	usb_release_interface,
	.uevent =	usb_if_uevent,
};

static struct usb_interface_assoc_descriptor *find_iad(struct usb_device *dev,
						       struct usb_host_config *config,
						       u8 inum)
{
	struct usb_interface_assoc_descriptor *retval = NULL;
	struct usb_interface_assoc_descriptor *intf_assoc;
	int first_intf;
	int last_intf;
	int i;

	for (i = 0; (i < USB_MAXIADS && config->intf_assoc[i]); i++) {
		intf_assoc = config->intf_assoc[i];
		if (intf_assoc->bInterfaceCount == 0)
			continue;

		first_intf = intf_assoc->bFirstInterface;
		last_intf = first_intf + (intf_assoc->bInterfaceCount - 1);
		if (inum >= first_intf && inum <= last_intf) {
			if (!retval)
				retval = intf_assoc;
			else
				dev_err(&dev->dev, "Interface #%d referenced"
					" by multiple IADs\n", inum);
		}
	}

	return retval;
}


/*
 * usb_set_configuration - Makes a particular device setting be current
 * @dev: the device whose configuration is being updated
 * @configuration: the configuration being chosen.
 * Context: !in_interrupt(), caller owns the device lock
 *
 * This is used to enable non-default device modes.  Not all devices
 * use this kind of configurability; many devices only have one
 * configuration.
 *
 * @configuration is the value of the configuration to be installed.
 * According to the USB spec (e.g. section 9.1.1.5), configuration values
 * must be non-zero; a value of zero indicates that the device in
 * unconfigured.  However some devices erroneously use 0 as one of their
 * configuration values.  To help manage such devices, this routine will
 * accept @configuration = -1 as indicating the device should be put in
 * an unconfigured state.
 *
 * USB device configurations may affect Linux interoperability,
 * power consumption and the functionality available.  For example,
 * the default configuration is limited to using 100mA of bus power,
 * so that when certain device functionality requires more power,
 * and the device is bus powered, that functionality should be in some
 * non-default device configuration.  Other device modes may also be
 * reflected as configuration options, such as whether two ISDN
 * channels are available independently; and choosing between open
 * standard device protocols (like CDC) or proprietary ones.
 *
 * Note that a non-authorized device (dev->authorized == 0) will only
 * be put in unconfigured mode.
 *
 * Note that USB has an additional level of device configurability,
 * associated with interfaces.  That configurability is accessed using
 * usb_set_interface().
 *
 * This call is synchronous. The calling context must be able to sleep,
 * must own the device lock, and must not hold the driver model's USB
 * bus mutex; usb device driver probe() methods cannot use this routine.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying call that failed.  On successful completion, each interface
 * in the original device configuration has been destroyed, and each one
 * in the new configuration has been probed by all relevant usb device
 * drivers currently known to the kernel.
 */
/**
 * 将配置设置为设备的当前配置。
 */
int usb_set_configuration(struct usb_device *dev, int configuration)
{
	int i, ret;
	struct usb_host_config *cp = NULL;
	struct usb_interface **new_interfaces = NULL;
	int n, nintf;

	/**
	 * 如果没有找到合适的配置，choose_configuration会返回－1，则将配置号设置为0.这样SET_CONFIGURATION配置请求不会使设备进入配置状态。
	 * 而是保持在Address状态。表示没有合适的配置。这里不直接返回，是因为有些奇怪的设备允许传配置0.
	 */
	if (configuration == -1)
		configuration = 0;
	else {
		/**
		 * 从配置数组里面找到对应的配置结构体。
		 */
		for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
			if (dev->config[i].desc.bConfigurationValue ==
					configuration) {
				cp = &dev->config[i];
				break;
			}
		}
	}
	/**
	 * 如果没有合适的配置，那么配置号就必须为0.
	 */
	if ((!cp && configuration != 0))
		return -EINVAL;

	/* The USB spec says configuration 0 means unconfigured.
	 * But if a device includes a configuration numbered 0,
	 * we will accept it as a correctly configured state.
	 * Use -1 if you really want to unconfigure the device.
	 */
	/**
	 * 如果传了configuration为0，同时找到了对应的配置描述符，则打印警告。这种设备比较奇怪。
	 */
	if (cp && configuration == 0)
		dev_warn(&dev->dev, "config 0 descriptor??\n");

	/* Allocate memory for new interfaces before doing anything else,
	 * so that if we run out then nothing will have changed. */
	n = nintf = 0;
	if (cp) {
		/**
		 * 根据配置的接口数目，为其分配指针数组。
		 */
		nintf = cp->desc.bNumInterfaces;
		new_interfaces = kmalloc(nintf * sizeof(*new_interfaces),
				GFP_KERNEL);
		if (!new_interfaces) {
			dev_err(&dev->dev, "Out of memory\n");
			return -ENOMEM;
		}

		/**
		 * 再为每一个接口分配内存。
		 */
		for (; n < nintf; ++n) {
			new_interfaces[n] = kzalloc(
					sizeof(struct usb_interface),
					GFP_KERNEL);
			if (!new_interfaces[n]) {
				dev_err(&dev->dev, "Out of memory\n");
				ret = -ENOMEM;
free_interfaces:
				while (--n >= 0)
					kfree(new_interfaces[n]);
				kfree(new_interfaces);
				return ret;
			}
		}

		/**
		 * 配置需要的电流较大，警告一下。
		 */
		i = dev->bus_mA - cp->desc.bMaxPower * 2;
		if (i < 0)
			dev_warn(&dev->dev, "new config #%d exceeds power "
					"limit by %dmA\n",
					configuration, -i);
	}

	/* Wake up the device so we can send it the Set-Config request */
	ret = usb_autoresume_device(dev);
	if (ret)
		goto free_interfaces;

	/* if it's already configured, clear out old state first.
	 * getting rid of old interfaces means unbinding their drivers.
	 */
	/**
	 * 大多数情况下是从USB_STATE_ADDRESS状态进入配置状态的。如果不是，说明设备可能已经配置过了。
	 * 调用usb_disable_device使其回到USB_STATE_ADDRESS状态。
	 */
	if (dev->state != USB_STATE_ADDRESS)
		/**
		 * 不用禁止掉端点0.在两种状态都需要用到它。
		 */
		usb_disable_device (dev, 1);	// Skip ep0

	/**
	 * 向设备发送USB_REQ_SET_CONFIGURATION消息，使其进入配置状态。
	 */
	if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0, configuration, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT)) < 0) {

		/* All the old state is gone, so what else can we do?
		 * The device is probably useless now anyway.
		 */
		cp = NULL;
	}

	/**
	 * 将激活的配置赋给actconfig
	 */
	dev->actconfig = cp;
	/**
	 * 配置为空，说明配置参数为－1，或者配置参数为0，但是配置数组里面第0项就是为空，或者说是设备进入配置状态失败。
	 */
	if (!cp) {
		/**
		 * 进入配置状态失败，返回到USB_STATE_ADDRESS状态，退出函数。
		 */
		usb_set_device_state(dev, USB_STATE_ADDRESS);
		usb_autosuspend_device(dev);
		goto free_interfaces;
	}
	/**
	 * 设备已经正常进入配置状态。
	 */
	usb_set_device_state(dev, USB_STATE_CONFIGURED);

	/* Initialize the new interface structures and the
	 * hc/hcd/usbcore interface/endpoint state.
	 */
	/**
	 * 对配置里面的所有接口进行处理。
	 */
	for (i = 0; i < nintf; ++i) {
		struct usb_interface_cache *intfc;
		struct usb_interface *intf;
		struct usb_host_interface *alt;

		cp->interface[i] = intf = new_interfaces[i];
		intfc = cp->intf_cache[i];
		intf->altsetting = intfc->altsetting;
		intf->num_altsetting = intfc->num_altsetting;
		intf->intf_assoc = find_iad(dev, cp, i);
		kref_get(&intfc->ref);

		/**
		 * 获得接口的第1个设置。这个设置是接口的默认设置。
		 */
		alt = usb_altnum_to_altsetting(intf, 0);

		/* No altsetting 0?  We'll assume the first altsetting.
		 * We could use a GetInterface call, but if a device is
		 * so non-compliant that it doesn't have altsetting 0
		 * then I wouldn't trust its reply anyway.
		 */
		/**
		 * 没有第一个设置，用数组中的第一个设置。
		 */
		if (!alt)
			alt = &intf->altsetting[0];

		/**
		 * 使用默认设置作为当前设置。
		 */
		intf->cur_altsetting = alt;
		usb_enable_interface(dev, intf);
		intf->dev.parent = &dev->dev;
		intf->dev.driver = NULL;
		intf->dev.bus = &usb_bus_type;
		intf->dev.type = &usb_if_device_type;
		intf->dev.dma_mask = dev->dev.dma_mask;
		/**
		 * 初始化接口的设备模型，准备加入到设备模型中去。
		 */
		device_initialize (&intf->dev);
		/**
		 * is_active 标志初始化为0，表示它还没有与任何驱动绑定。
		 */
		mark_quiesced(intf);
		sprintf (&intf->dev.bus_id[0], "%d-%s:%d.%d",
			 dev->bus->busnum, dev->devpath,
			 configuration, alt->desc.bInterfaceNumber);
	}
	kfree(new_interfaces);

	/**
	 * 获得设备字符串描述符。
	 */
	if (cp->string == NULL)
		cp->string = usb_cache_string(dev, cp->desc.iConfiguration);

	/* Now that all the interfaces are set up, register them
	 * to trigger binding of drivers to interfaces.  probe()
	 * routines may install different altsettings and may
	 * claim() any interfaces not yet bound.  Many class drivers
	 * need that: CDC, audio, video, etc.
	 */
	/**
	 * 将所有接口与设备模型绑定起来，并为其查找驱动。
	 */
	for (i = 0; i < nintf; ++i) {
		struct usb_interface *intf = cp->interface[i];

		dev_dbg (&dev->dev,
			"adding %s (config #%d, interface %d)\n",
			intf->dev.bus_id, configuration,
			intf->cur_altsetting->desc.bInterfaceNumber);
		ret = device_add (&intf->dev);
		if (ret != 0) {
			dev_err(&dev->dev, "device_add(%s) --> %d\n",
				intf->dev.bus_id, ret);
			continue;
		}
		usb_create_sysfs_intf_files(intf);
	}

	usb_autosuspend_device(dev);
	return 0;
}

struct set_config_request {
	struct usb_device	*udev;
	int			config;
	struct work_struct	work;
};

/* Worker routine for usb_driver_set_configuration() */
static void driver_set_config_work(struct work_struct *work)
{
	struct set_config_request *req =
		container_of(work, struct set_config_request, work);

	usb_lock_device(req->udev);
	usb_set_configuration(req->udev, req->config);
	usb_unlock_device(req->udev);
	usb_put_dev(req->udev);
	kfree(req);
}

/**
 * usb_driver_set_configuration - Provide a way for drivers to change device configurations
 * @udev: the device whose configuration is being updated
 * @config: the configuration being chosen.
 * Context: In process context, must be able to sleep
 *
 * Device interface drivers are not allowed to change device configurations.
 * This is because changing configurations will destroy the interface the
 * driver is bound to and create new ones; it would be like a floppy-disk
 * driver telling the computer to replace the floppy-disk drive with a
 * tape drive!
 *
 * Still, in certain specialized circumstances the need may arise.  This
 * routine gets around the normal restrictions by using a work thread to
 * submit the change-config request.
 *
 * Returns 0 if the request was succesfully queued, error code otherwise.
 * The caller has no way to know whether the queued request will eventually
 * succeed.
 */
int usb_driver_set_configuration(struct usb_device *udev, int config)
{
	struct set_config_request *req;

	req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	req->udev = udev;
	req->config = config;
	INIT_WORK(&req->work, driver_set_config_work);

	usb_get_dev(udev);
	schedule_work(&req->work);
	return 0;
}
EXPORT_SYMBOL_GPL(usb_driver_set_configuration);

// synchronous request completion model
EXPORT_SYMBOL(usb_control_msg);
EXPORT_SYMBOL(usb_bulk_msg);

EXPORT_SYMBOL(usb_sg_init);
EXPORT_SYMBOL(usb_sg_cancel);
EXPORT_SYMBOL(usb_sg_wait);

// synchronous control message convenience routines
EXPORT_SYMBOL(usb_get_descriptor);
EXPORT_SYMBOL(usb_get_status);
EXPORT_SYMBOL(usb_string);

// synchronous calls that also maintain usbcore state
EXPORT_SYMBOL(usb_clear_halt);
EXPORT_SYMBOL(usb_reset_configuration);
EXPORT_SYMBOL(usb_set_interface);

