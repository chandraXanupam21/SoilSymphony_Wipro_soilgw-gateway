// SPDX-License-Identifier: GPL-2.0
/*
 * soilgw - Soil-moisture multi-node gateway character driver.
 *
 * Raw bytes from the node link (UART/RS-485 RX path, or a simulator via
 * write()) enter through parse_byte(), which hunts the SOF byte, validates
 * CRC-8 and node id, and stores good 8-byte frames in a ring buffer.
 * User space reads whole frames with blocking / non-blocking read() or poll().
 *
 * /dev/soilgw : read frames, write raw bytes (loopback / simulator path)
 * /proc/soilgw: statistics
 * ioctl       : stats, clear, node mask
 *
 * Target: Linux 5.x/6.x (version #ifdefs handle API changes).
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/version.h>
#include "../include/soilgw_proto.h"

#define RING_FRAMES 256
#define MAX_BURST   16

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("Smart agriculture soil-moisture multi-node gateway driver");

struct soilgw_dev {
	struct cdev cdev;
	dev_t devt;
	struct class *cls;
	struct proc_dir_entry *proc;

	spinlock_t lock;               /* protects everything below */
	wait_queue_head_t rq;
	u8 ring[RING_FRAMES][SOILGW_FRAME_LEN];
	unsigned int head, tail, count;
	u8 pbuf[SOILGW_FRAME_LEN];     /* frame under assembly */
	unsigned int plen;
	u32 mask;                      /* bit (n-1) set => accept node n */
	struct soilgw_stats st;
};

static struct soilgw_dev sg;

/* ---- ring buffer (call with sg.lock held) ---- */
static void ring_push(const u8 *frame)
{
	if (sg.count == RING_FRAMES) {          /* overrun: drop oldest */
		sg.tail = (sg.tail + 1) % RING_FRAMES;
		sg.count--;
		sg.st.overruns++;
	}
	memcpy(sg.ring[sg.head], frame, SOILGW_FRAME_LEN);
	sg.head = (sg.head + 1) % RING_FRAMES;
	sg.count++;
	wake_up_interruptible(&sg.rq);
}

/* ---- framing state machine (call with sg.lock held) ---- */
static void frame_complete(void)
{
	u8 node = sg.pbuf[1];

	if (node < 1 || node > SOILGW_MAX_NODES) {
		sg.st.frames_bad++;
		return;
	}
	if (!(sg.mask & (1U << (node - 1)))) {
		sg.st.frames_filtered++;
		return;
	}
	sg.st.frames_ok++;
	ring_push(sg.pbuf);
}

static void parse_byte(u8 b)
{
	unsigned int i;

	if (sg.plen == 0 && b != SOILGW_SOF) {
		sg.st.resyncs++;
		return;
	}
	sg.pbuf[sg.plen++] = b;
	if (sg.plen < SOILGW_FRAME_LEN)
		return;

	if (soilgw_crc8(&sg.pbuf[1], 6) == sg.pbuf[7]) {
		frame_complete();
		sg.plen = 0;
		return;
	}
	/* bad CRC: keep any bytes after the next SOF and continue */
	sg.st.frames_bad++;
	for (i = 1; i < SOILGW_FRAME_LEN; i++)
		if (sg.pbuf[i] == SOILGW_SOF)
			break;
	if (i < SOILGW_FRAME_LEN) {
		memmove(sg.pbuf, sg.pbuf + i, SOILGW_FRAME_LEN - i);
		sg.plen = SOILGW_FRAME_LEN - i;
	} else {
		sg.plen = 0;
	}
}

/* ---- file operations ---- */
static int sg_open(struct inode *inode, struct file *filp)
{
	return nonseekable_open(inode, filp);
}

static int sg_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t sg_read(struct file *filp, char __user *ubuf, size_t len,
		       loff_t *off)
{
	u8 tmp[MAX_BURST * SOILGW_FRAME_LEN];
	unsigned long flags;
	size_t n, i;

	if (len < SOILGW_FRAME_LEN)
		return -EINVAL;

	for (;;) {
		spin_lock_irqsave(&sg.lock, flags);
		if (sg.count)
			break;
		spin_unlock_irqrestore(&sg.lock, flags);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(sg.rq, sg.count > 0))
			return -ERESTARTSYS;
	}

	n = len / SOILGW_FRAME_LEN;
	if (n > sg.count)
		n = sg.count;
	if (n > MAX_BURST)
		n = MAX_BURST;
	for (i = 0; i < n; i++) {
		memcpy(tmp + i * SOILGW_FRAME_LEN, sg.ring[sg.tail],
		       SOILGW_FRAME_LEN);
		sg.tail = (sg.tail + 1) % RING_FRAMES;
	}
	sg.count -= n;
	spin_unlock_irqrestore(&sg.lock, flags);

	if (copy_to_user(ubuf, tmp, n * SOILGW_FRAME_LEN))
		return -EFAULT;
	return n * SOILGW_FRAME_LEN;
}

/* write() injects raw link bytes (simulator / loopback test path) */
static ssize_t sg_write(struct file *filp, const char __user *ubuf,
			size_t len, loff_t *off)
{
	u8 tmp[64];
	size_t done = 0, n, i;
	unsigned long flags;

	while (done < len) {
		n = min(len - done, sizeof(tmp));
		if (copy_from_user(tmp, ubuf + done, n))
			return done ? (ssize_t)done : -EFAULT;
		spin_lock_irqsave(&sg.lock, flags);
		for (i = 0; i < n; i++) {
			sg.st.bytes_in++;
			parse_byte(tmp[i]);
		}
		spin_unlock_irqrestore(&sg.lock, flags);
		done += n;
	}
	return len;
}

static __poll_t sg_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;
	unsigned long flags;

	poll_wait(filp, &sg.rq, wait);
	spin_lock_irqsave(&sg.lock, flags);
	if (sg.count)
		mask |= EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&sg.lock, flags);
	return mask;
}

static long sg_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct soilgw_stats st;
	unsigned long flags;
	__u32 mask;

	if (_IOC_TYPE(cmd) != SOILGW_IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case SOILGW_IOC_GET_STATS:
		spin_lock_irqsave(&sg.lock, flags);
		st = sg.st;
		st.queued = sg.count;
		spin_unlock_irqrestore(&sg.lock, flags);
		return copy_to_user((void __user *)arg, &st, sizeof(st)) ?
		       -EFAULT : 0;
	case SOILGW_IOC_CLEAR:
		spin_lock_irqsave(&sg.lock, flags);
		sg.head = sg.tail = sg.count = sg.plen = 0;
		memset(&sg.st, 0, sizeof(sg.st));
		spin_unlock_irqrestore(&sg.lock, flags);
		return 0;
	case SOILGW_IOC_SET_MASK:
		if (get_user(mask, (__u32 __user *)arg))
			return -EFAULT;
		spin_lock_irqsave(&sg.lock, flags);
		sg.mask = mask;
		spin_unlock_irqrestore(&sg.lock, flags);
		return 0;
	case SOILGW_IOC_GET_MASK:
		spin_lock_irqsave(&sg.lock, flags);
		mask = sg.mask;
		spin_unlock_irqrestore(&sg.lock, flags);
		return put_user(mask, (__u32 __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations sg_fops = {
	.owner          = THIS_MODULE,
	.open           = sg_open,
	.release        = sg_release,
	.read           = sg_read,
	.write          = sg_write,
	.poll           = sg_poll,
	.unlocked_ioctl = sg_ioctl,
	.llseek         = no_llseek,
};

/* ---- /proc/soilgw ---- */
static int sg_proc_show(struct seq_file *m, void *v)
{
	struct soilgw_stats st;
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&sg.lock, flags);
	st = sg.st;
	st.queued = sg.count;
	mask = sg.mask;
	spin_unlock_irqrestore(&sg.lock, flags);

	seq_printf(m, "ring_size:       %d frames\n", RING_FRAMES);
	seq_printf(m, "queued:          %u\n", st.queued);
	seq_printf(m, "frames_ok:       %u\n", st.frames_ok);
	seq_printf(m, "frames_bad:      %u\n", st.frames_bad);
	seq_printf(m, "frames_filtered: %u\n", st.frames_filtered);
	seq_printf(m, "overruns:        %u\n", st.overruns);
	seq_printf(m, "resync_bytes:    %u\n", st.resyncs);
	seq_printf(m, "bytes_in:        %u\n", st.bytes_in);
	seq_printf(m, "node_mask:       0x%08x\n", mask);
	return 0;
}

static int sg_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sg_proc_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops sg_proc_ops = {
	.proc_open    = sg_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations sg_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = sg_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};
#endif

/* make /dev/soilgw world read/writable for the demo */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static char *sg_devnode(const struct device *dev, umode_t *mode)
#else
static char *sg_devnode(struct device *dev, umode_t *mode)
#endif
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int __init soilgw_init(void)
{
	struct device *dev;
	int ret;

	spin_lock_init(&sg.lock);
	init_waitqueue_head(&sg.rq);
	sg.mask = 0xFFFFFFFFU;

	ret = alloc_chrdev_region(&sg.devt, 0, 1, SOILGW_DEV_NAME);
	if (ret)
		return ret;

	cdev_init(&sg.cdev, &sg_fops);
	sg.cdev.owner = THIS_MODULE;
	ret = cdev_add(&sg.cdev, sg.devt, 1);
	if (ret)
		goto err_region;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	sg.cls = class_create(SOILGW_DEV_NAME);
#else
	sg.cls = class_create(THIS_MODULE, SOILGW_DEV_NAME);
#endif
	if (IS_ERR(sg.cls)) {
		ret = PTR_ERR(sg.cls);
		goto err_cdev;
	}
	sg.cls->devnode = sg_devnode;

	dev = device_create(sg.cls, NULL, sg.devt, NULL, SOILGW_DEV_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_class;
	}

	sg.proc = proc_create(SOILGW_DEV_NAME, 0444, NULL, &sg_proc_ops);
	pr_info("soilgw: loaded, major=%d\n", MAJOR(sg.devt));
	return 0;

err_class:
	class_destroy(sg.cls);
err_cdev:
	cdev_del(&sg.cdev);
err_region:
	unregister_chrdev_region(sg.devt, 1);
	return ret;
}

static void __exit soilgw_exit(void)
{
	if (sg.proc)
		proc_remove(sg.proc);
	device_destroy(sg.cls, sg.devt);
	class_destroy(sg.cls);
	cdev_del(&sg.cdev);
	unregister_chrdev_region(sg.devt, 1);
	pr_info("soilgw: unloaded\n");
}

module_init(soilgw_init);
module_exit(soilgw_exit);
