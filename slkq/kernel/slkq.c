/*
 * slkq.c: Linux kernel module that implements in-kernel queue with on-disk
 *         spool
 *
 * Copyright (C) 2019 Alexey Mikhailov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "../common/slkq.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/falloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexey Mikhailov <alexey.mikhailov@gmail.com>");
MODULE_DESCRIPTION("Simple Linux Kernel Queue");
MODULE_VERSION("0.1");

/**
 * In-kernel FIFO queue being maintained with spool using on-disk file
 * as backing device. Interaction between in-kernel spool and external stoarge can
 * be described in simple way.  There is kernel thread serving such interaction,
 * it wakes up (waitqueue) in following cases:
 *
 * - On 'push': kernel FIFO becomes full after enquing message (slkq_dev_write)
 * - On 'pop': kernel FIFO has enough space to accommodate more elements and on-disk
 *   spool is not empty after dequing message (slkq_dev_read)
 *
 * This behavior is controlled by constants defined below:
 *
 * - SLKQ_FIFO_SHRINK_TO: number of elements (length) that FIFO gets shrinked to when overfulls
 * - SLKQ_FIFO_EXTEND_(LIMIT|TO):  if queue length is lower than SLKQ_FIFO_EXTEND_LIMIT and
 *   on-disk spool is not empty, queue gets populated from spool (to SLKQ_FIFO_EXTEND_TO
 *   elements at max)
 */

#define SLKQ_FIFO_LENGTH 1024

struct slkq_fifo_msg {
	u_int16_t size;
	unsigned char *buf;
};

DECLARE_KFIFO(msg_fifo, struct slkq_fifo_msg, SLKQ_FIFO_LENGTH);

#define SLKQ_FIFO_EXTEND_LIMIT ((SLKQ_FIFO_LENGTH * 1) / 2)
#define SLKQ_FIFO_EXTEND_TO ((SLKQ_FIFO_LENGTH * 3) / 4)
#define SLKQ_FIFO_EXTEND_COND()					   \
	((kfifo_len(&msg_fifo) <= SLKQ_FIFO_EXTEND_LIMIT)	   \
	 && (atomic_read(&spool_size) > 0))

#define SLKQ_FIFO_SHRINK_TO ((SLKQ_FIFO_LENGTH * 3) / 4)

/**
 * This module uses single on-disk file as queue spool. File gets extended as data
 * being pushed. File being read from the beginning while extracting (popping) data out,
 * pointer (offset) being maintained/used for this. This scheme has obvious disadvatange:
 * on-disk spool will keep growing. To eliminate this issue, FALLOC_FL_COLLAPSE_RANGE
 * being used (refer to fallocate(2)). SLKQ_SPOOL_COLLAPSE_LIMIT defines threshold
 * which is used to decide when to "cut" the beginning of file. So collapse happens
 * when offset becomes greater than threshold value.
 *
 * It must be noted that FALLOC_FL_COLLAPSE_RANGE is currently only supported
 * by ext4 and XFS filesystem (see fallocate(2)).
 */

#define SLKQ_SPOOL_COLLAPSE_LIMIT (4096 * 1024)
#define SLKQ_DISK_BLK_SIZE 4096

static wait_queue_head_t msg_new_q;   /* (dev_write && NEW) => dev_read unblocks  */
static wait_queue_head_t msg_spool_q; /* (dev_write && FULL) || (dev_read && EXTEND_COND)
				        => spool_thread wakes*/

static struct mutex in_fifo_lock;
static struct mutex out_fifo_lock;

static struct file *spool_f;
static atomic_t spool_size = ATOMIC_INIT(0);
static loff_t spool_pos = 0;
static struct task_struct *spool_thr;

/* slab allocator is used for in-kernel queue elements (messages) */
static struct kmem_cache *msg_cache;

/**
 * /dev/slkq character device is used for communication between kernel-space
 * and user-space applications. read() syscall implies 'pop' operation and
 * write() is used for 'push'
 */
static dev_t dev;
static struct device *devp;
static struct cdev chr_dev;
static struct class *dev_cls = NULL;

/**
 * /proc/slkq_status is used to get queue's statistics
 *
 * Status string consists of 4 numbers: used elements, free elements, total elements,
 * spool size. E.g.
 *
 * $ cat /proc/slkq_status
 * 768 256 1024 259
 *
 * There is in-kernel queue with total of 1024 elements (768 used, 256 free), and on-disk
 * spool holds 259 elements
  */
static struct proc_dir_entry *status_ent;

/**
 * __unload_to_spool -- shrinks in-kernel queue by moving elements to disk spool
 *
 * Must be called with out_fifo_lock down (race with slkq_dev_read)
 */
static ssize_t __unload_to_spool (void)
{
	ssize_t ret = 0;
	struct slkq_fifo_msg m;
	loff_t pos;

	dev_dbg(devp, "shrinking to %d\n", SLKQ_FIFO_SHRINK_TO);

	while (kfifo_len(&msg_fifo) > SLKQ_FIFO_SHRINK_TO && kfifo_get(&msg_fifo, &m)) {
		pos = vfs_llseek(spool_f, 0, SEEK_END);

		/**
		 * Spool file uses variable record format where the record's first two
		 * bytes indicate the length of the record.
		 */
		ret = kernel_write(spool_f, (char *)&(m.size), 2, pos);
		if (ret != 2) {
			dev_err(devp, "%s\n", __func__);
			if (ret > 0)
				ret = -ENOMEM;
			ret = -1;
			break;
		}

		pos += 2;
		ret = kernel_write(spool_f, m.buf, m.size, pos);

		if (ret != m.size) {
			dev_err(devp, "%s\n", __func__);
			if (ret > 0)
				ret = -ENOMEM;
			ret = -1;
			break;
		}

		atomic_inc(&spool_size);
		ret = 0;
	}

	dev_dbg(devp, "unload done, %u\n", kfifo_len(&msg_fifo));

	kmem_cache_free(msg_cache, m.buf);
	return ret;
}

/**
 * __load_from_spool -- loads messages to queue from disk spool
 *
 * Must be called with in_fifo_lock down (race with slkq_dev_write)
 */
static int __load_from_spool (void)
{
	u_int16_t siz;
	struct slkq_fifo_msg m;

	dev_dbg(devp, "extend_to = %d, len = %d, spool_size = %d\n",
		SLKQ_FIFO_EXTEND_TO, kfifo_len(&msg_fifo), atomic_read(&spool_size));

	while ((kfifo_len(&msg_fifo) < SLKQ_FIFO_EXTEND_TO) &&
	       (atomic_read(&spool_size) > 0) &&
	       (!kfifo_is_full(&msg_fifo)))
	{
		loff_t pos = spool_pos;

		if ((kernel_read(spool_f, pos, (void *)&siz, 2)) != 2) {
			dev_err(devp, "%s: kernel_read (size)\n",
				__func__);
			return -EIO;
		}

		m.size = siz;
		m.buf = kmem_cache_alloc(msg_cache, GFP_KERNEL);

		pos += 2;

		if ((kernel_read(spool_f, pos, m.buf, siz)) != siz) {
			dev_err(devp, "%s: kernel_read (data) %u\n",
				__func__, siz);
			kmem_cache_free(msg_cache, m.buf);
			return -EIO;
		}

		pos += siz;

		if (kfifo_put(&msg_fifo, m) != 1) {
			kmem_cache_free(msg_cache, m.buf);
			return -EIO;

		}

		spool_pos = pos;;
		atomic_dec(&spool_size);
	}

	/* "Cut" the beginning of file using FALLOCATE (if limit is reached) */
	if (spool_pos > SLKQ_SPOOL_COLLAPSE_LIMIT) {
		dev_dbg(devp, "%s: going to collapse\n", __func__);
		if (vfs_fallocate(spool_f, FALLOC_FL_COLLAPSE_RANGE, 0,
				  (spool_pos / SLKQ_DISK_BLK_SIZE) * SLKQ_DISK_BLK_SIZE)) {
			dev_dbg(devp, "%s: vfs_fallocate() failed\n",
				__func__);
		} else {
			spool_pos = spool_pos % SLKQ_DISK_BLK_SIZE;
		}
	}

	return 0;
}

/**
 * slkq_dev_read -- pops queue element which is triggered by reading /dev/slkq
 *
 * Note that the ordering can't be guaranteed because first blocks of queue are
 * moved to on-disk storage when it's full, and it gets recovered only when enough
 * space available (as we try to minimize amount of I/O)
 *
 */
static ssize_t slkq_dev_read (struct file *file, char __user *ubuf,
			      size_t len, loff_t *off)
{
	ssize_t ret;
	unsigned int copied;
	struct slkq_fifo_msg m;

	ret = mutex_lock_interruptible(&out_fifo_lock);
	if (ret)
		return ret;

	while (kfifo_is_empty(&msg_fifo)) {
		mutex_unlock(&out_fifo_lock);

		if (file->f_flags & O_NONBLOCK) {
			return -EAGAIN;
		}

		ret = wait_event_interruptible(msg_new_q,
					       !kfifo_is_empty(&msg_fifo));

		if (ret) {
			return ret;
		}

		ret = mutex_lock_interruptible(&out_fifo_lock);
		if (ret) {
			return ret;
		}
	}

	/* Just peeking at this point because message size can be larger than
	 * user provided buffer so syscall is going to fail
	 */
	ret = kfifo_peek(&msg_fifo, &m);

	if (ret != 1) {
		dev_err(devp, "kfifo_peek failed\n");
		goto unlock;
	}

	if (m.size > len) {
		dev_err(devp, "buffer is too small (%u > %lu)\n", m.size, len);
		ret = -EFAULT;
		goto unlock;
	}

	ret = copy_to_user(ubuf, m.buf, m.size);

	if (WARN_ON(ret)) {
		dev_err(devp, "copy_to_user failed\n");
		ret = -EFAULT;
		goto unlock;
	}

	/* Safe to skip at this point */
	kfifo_skip(&msg_fifo);

	mutex_unlock(&out_fifo_lock);

	if (SLKQ_FIFO_EXTEND_COND()) {
		wake_up_interruptible(&msg_spool_q);
	}

	return (m.size);
unlock:
	mutex_unlock(&out_fifo_lock);
	return (ret) ? (ret) : (copied);
}

/**
 * slkq_dev_write -- push element to queue element which is trigerred by
 * writing to /dev/slkq
 *
 * Note that the ordering can't be guaranteed because first blocks of queue are
 * moved to on-disk storage when it's full, and it gets recovered only when enough
 * space available (as we try to minimize amount of I/O)
 */
static ssize_t slkq_dev_write (struct file *file, const char __user *ubuf,
			       size_t len, loff_t *off)
{
	ssize_t ret;
	struct slkq_fifo_msg m;

	if (len > SLKQ_MSG_MAX_SIZE) {
		dev_err(devp, "%s: wrong len = %lu\n", __func__, len);
		return -EINVAL;
	}

	if (*off != 0) {
		dev_err(devp, "%s: offset specified\n", __func__);
		return -EINVAL;
	}


	ret = mutex_trylock(&in_fifo_lock);
	if ((ret == 0) && (file->f_flags & O_NONBLOCK)) {
		return -EAGAIN;
	} else if (ret == 0) {
		ret = mutex_lock_interruptible(&in_fifo_lock);
	}

	if (kfifo_is_full(&msg_fifo)) {
		mutex_unlock(&in_fifo_lock);
		wake_up_interruptible(&msg_spool_q);

		dev_err(devp, "%s: FATAL queue_full, this shouldn't happen\n",
			__func__);

		return -1;
	}

	m.buf = kmem_cache_alloc(msg_cache, GFP_KERNEL);
	if (!m.buf) {
		dev_err(devp, "kmem_cache_alloc failed\n");
		goto err1;
	}

	ret = copy_from_user(m.buf, ubuf, len);
	if (ret) {
		dev_err(devp, "user => kernel failed\n");
		goto err;
	}

	m.size = len;

	ret = kfifo_put(&msg_fifo, m);
	if (ret != 1) {
		dev_err(devp, "kfifo_put returned %zd\n", ret);
		goto err;
	}

	if (kfifo_is_full(&msg_fifo)) {
		dev_dbg(devp, "%s: full, waking spool_thread\n", __func__);
		wake_up_interruptible(&msg_spool_q);
	}

	wake_up_interruptible(&msg_new_q);
	mutex_unlock(&in_fifo_lock);
	return len;

err:
	kmem_cache_free(msg_cache, m.buf);
err1:
	mutex_unlock(&in_fifo_lock);
	return -EFAULT;
}

static const struct file_operations slkq_dev_ops = {
        .owner = THIS_MODULE,
        .write = slkq_dev_write,
        .read = slkq_dev_read,
};

/**
 * slkq_status_read -- return status string that is accessed by reading
 * /proc/slkq_status entry
 */
static ssize_t slkq_status_read (struct file *file, char __user *ubuf,
				 size_t count, loff_t *off)
{
	char buf[128];
	int len = 0;

	if (*off) {
		return 0;
	}

	len += sprintf(buf, "%u %u %u %d\n", kfifo_len(&msg_fifo), kfifo_avail(&msg_fifo),
		       kfifo_size(&msg_fifo), atomic_read(&spool_size));

	if (copy_to_user(ubuf, buf, len)) {
		return -EFAULT;
	}

	*off = len;

	return len;
}

static const struct file_operations slkq_status_ops = {
	.owner = THIS_MODULE,
	.read = slkq_status_read,
};

/**
 * slkq_spool_thread() handles in-kernel queue <=> spool interaction described above
 */
static int slkq_spool_thread (void *data)
{
	for (;;) {
		ssize_t ret;

		wait_event_interruptible(msg_spool_q, kthread_should_stop() ||
					 kfifo_is_full(&msg_fifo) || SLKQ_FIFO_EXTEND_COND());

		if (kthread_should_stop()) {
			return 0;
		}

		/* FIFO is full and part of it must be unloaded to disk */
		if (kfifo_is_full(&msg_fifo)) {
			dev_dbg(devp, "%s: going to unload\n", __func__);

			ret = mutex_lock_interruptible(&out_fifo_lock);
			if (ret)
				continue;

			__unload_to_spool();
			mutex_unlock(&out_fifo_lock);
			continue;
		}

		/* Check if there is enough room for more elements, and load
		 * from spool if available */
		if (SLKQ_FIFO_EXTEND_COND()) {
			dev_dbg(devp, "%s: going to load\n", __func__);

			ret = mutex_lock_interruptible(&in_fifo_lock);
			if (ret)
				continue;

			if (__load_from_spool() != 0) {
				dev_err(devp, "%s: load_from_spool() failed\n",
					__func__);
				mutex_unlock(&in_fifo_lock);
				BUG(); /* XXX: shouldn't happen */
				return -1;
			}

			mutex_unlock(&in_fifo_lock);
			continue;
		}

		BUG();
        }

	return 0;
}

static int slkq_init (void)
{
	int ret;

	INIT_KFIFO(msg_fifo);

        init_waitqueue_head(&msg_new_q);
        init_waitqueue_head(&msg_spool_q);

	mutex_init(&in_fifo_lock);
	mutex_init(&out_fifo_lock);

	if ((ret = alloc_chrdev_region(&dev, 0, 1, SLKQ_NAME)) < 0) {
		pr_err("%s: failed to alloc dev region (ret = %d)", __func__,
		       ret);
		goto err0;
	}

	cdev_init(&chr_dev, &slkq_dev_ops);

	if ((ret = cdev_add(&chr_dev, dev, 1))) {
		pr_err("%s: failed to add device (ret = %d)\n", __func__, ret);
		goto err1;
	}

	dev_cls = class_create(THIS_MODULE, SLKQ_NAME);

	if (dev_cls == NULL) {
		pr_err("%s: failed to register device class\n", __func__);
		goto err2;
	}

	devp = device_create(dev_cls, NULL, dev, NULL, SLKQ_NAME);

	if (IS_ERR(devp)) {
		pr_err("%s: failed to create device\n", __func__);
		goto err3;
	}

	spool_f = filp_open(SLKQ_SPOOL_FILENAME, O_RDWR | O_TRUNC | O_CREAT | O_SYNC, 0);

	if (IS_ERR(spool_f)) {
		pr_err("%s: failed to open spool file\n", __func__);
		goto err4;
	}

	msg_cache = kmem_cache_create(SLKQ_NAME, SLKQ_MSG_MAX_SIZE, 0, 0, NULL);

	if (!msg_cache) {
		pr_err("%s: failed to create cache\n", __func__);
		goto err5;
	}

	spool_thr = kthread_run(slkq_spool_thread, NULL, "slkq_spool_thr");

	if (IS_ERR(spool_thr)) {
		pr_err("%s: start of spool thread failed\n", __func__);
		goto err6;
	}


	status_ent = proc_create(SLKQ_PROC_STATUS_FILENAME, 0, NULL,
				 &slkq_status_ops);

	if (!status_ent) {
		pr_err("%s: failed to create proc file\n", __func__);
		goto err7;
	}

	dev_dbg(devp, "started\n");

	return 0;

	remove_proc_entry(SLKQ_PROC_STATUS_FILENAME, NULL);
err7:
	kthread_stop(spool_thr);
err6:
	kmem_cache_destroy(msg_cache);
err5:
	filp_close(spool_f, NULL);
err4:
	device_destroy(dev_cls, dev);
err3:
	class_destroy(dev_cls);
err2:
	cdev_del(&chr_dev);
err1:
	unregister_chrdev_region(dev, 1);
err0:
	kfifo_free(&msg_fifo);
	return -ENODEV;
}

static void slkq_exit (void)
{
	remove_proc_entry(SLKQ_PROC_STATUS_FILENAME, NULL);
	kthread_stop(spool_thr);
	kmem_cache_destroy(msg_cache);
	filp_close(spool_f, NULL);
	device_destroy(dev_cls, dev);
	cdev_del(&chr_dev);
	unregister_chrdev_region(dev, 1);
	class_destroy(dev_cls);
	kfifo_free(&msg_fifo);

        return;
}

module_init(slkq_init);
module_exit(slkq_exit);
