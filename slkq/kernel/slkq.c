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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexey Mikhailov <alexey.mikhailov@gmail.com>");
MODULE_DESCRIPTION("Simple Linux Kernel Queue");
MODULE_VERSION("0.1");

#define SLKQ_FIFO_LENGTH 1024
#define SLKQ_SHRINK_TO 768
#define SLKQ_SPOOL_COLLAPSE_LIMIT (4096 * 1024)
#define SLKQ_DISK_BLK_SIZE 4096

struct slkq_fifo_msg {
	u_int16_t size;
	unsigned char *buf;
};

DECLARE_KFIFO(msg_fifo, struct slkq_fifo_msg, SLKQ_FIFO_LENGTH);

static struct kmem_cache *msg_cache;
static wait_queue_head_t msg_full_q;
static wait_queue_head_t msg_new_q;
static struct mutex in_fifo_lock;
static struct mutex out_fifo_lock;

static dev_t dev;
static struct device *devp;
static struct cdev chr_dev;
static struct class *dev_cls = NULL;
static struct proc_dir_entry *status_ent;

static struct file *sfile;
static unsigned int spool_size = 0;
static loff_t spool_pos = 0;
static struct mutex spool_lock;
static struct task_struct *spool_thr;

static struct proc_dir_entry *status_ent;
static char read_buf[SLKQ_MSG_MAX_SIZE];

static int slkq_dev_open (struct inode *inode, struct file *file)
{
	return 0;
}

static int slkq_shrink_fifo (void)
{
	ssize_t ret;
	struct slkq_fifo_msg m;
	loff_t pos;

	ret = mutex_lock_interruptible(&out_fifo_lock);
	if (ret) {
		return ret;
	}

	ret = mutex_lock_interruptible(&spool_lock);
	if (ret) {
		mutex_unlock(&out_fifo_lock);
		return ret;
	}

	while (kfifo_len(&msg_fifo) > SLKQ_SHRINK_TO) {
		ret = kfifo_get(&msg_fifo, &m);
		pos = vfs_llseek(sfile, 0, SEEK_END);
		ret = kernel_write(sfile, (char *)&(m.size), 2, pos);
		pos += 2;
		ret = kernel_write(sfile, m.buf, m.size, pos);
		spool_size++;
		kmem_cache_free(msg_cache, m.buf);
	}

	mutex_unlock(&spool_lock);
	mutex_unlock(&out_fifo_lock);

	return 0;
}

static ssize_t slkq_dev_read (struct file *file, char __user *ubuf,
			      size_t len, loff_t *off)
{
	ssize_t ret;
	unsigned int copied;
	struct slkq_fifo_msg m;

	do {
		u_int16_t siz;
		int rlen;

		ret = mutex_lock_interruptible(&spool_lock);
		if (ret) {
			dev_err(devp, "%s: mutex_lock\n", __func__);
			return ret;
		}

		if (spool_size == 0) {
			mutex_unlock(&spool_lock);
			break;
		}

		rlen = kernel_read(sfile, spool_pos, (void *)&siz, 2);

		if (rlen != 2) {
			dev_err(devp, "%s: kernel_read() failed (header, %d)\n",
			       __func__, rlen);
			mutex_unlock(&spool_lock);
			return -EIO;
		}

		spool_pos += 2;

		rlen = kernel_read(sfile, spool_pos, read_buf, siz);
		if (rlen != siz) {
			dev_err(devp, "%s: kernel_read() failed(data, rlen=%d, siz=%d)\n",
			       __func__, rlen, siz);
			mutex_unlock(&spool_lock);
			return -EIO;
		}

		spool_pos += rlen;
		spool_size--;

		ret = copy_to_user(ubuf, read_buf, rlen);
		if (ret) {
			dev_err(devp, "%s: copy_to_user() failed (%ld)\n", __func__ , ret);
			mutex_unlock(&spool_lock);
			return -EFAULT;
		}

		if (spool_pos > SLKQ_SPOOL_COLLAPSE_LIMIT) {
			dev_dbg(devp, "%s: going to collapse\n", __func__);
			if (vfs_fallocate(sfile, FALLOC_FL_COLLAPSE_RANGE, 0,
					  (spool_pos / SLKQ_DISK_BLK_SIZE) * SLKQ_DISK_BLK_SIZE)) {
				dev_dbg(devp, "%s: vfs_fallocate() failed(ret = %ld)\n",
					__func__, ret);
			} else {
				spool_pos = spool_pos % SLKQ_DISK_BLK_SIZE;
			}

		}

		mutex_unlock(&spool_lock);
		return rlen;
	} while (0);

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

	ret = kfifo_peek(&msg_fifo, &m);

	if (ret != 1) {
		dev_err(devp, "kfifo_peek failed\n");
		goto unlock;
	}

	if (m.size > len) {
		dev_err(devp, "buffer is too small (%u > %lu)\n", m.size, len);
		ret = -EFAULT;
	}

	ret = copy_to_user(ubuf, m.buf, m.size);

	if (WARN_ON(ret)) {
		dev_err(devp, "copy_to_user failed\n");
		ret = -EFAULT;
		goto unlock;
	}

	kfifo_skip(&msg_fifo);

	mutex_unlock(&out_fifo_lock);

	return (m.size);
unlock:
	mutex_unlock(&out_fifo_lock);
	return (ret) ? (ret) : (copied);
}

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

	ret = mutex_lock_interruptible(&in_fifo_lock);
	if (ret) {
		dev_err(devp, "%s: mutex_lock()\n", __func__);
		return ret;
	}

	if (kfifo_is_full(&msg_fifo)) {
		if (file->f_flags & O_NONBLOCK) {
			dev_dbg(devp, "queue is full, signaling\n");
			wake_up_interruptible(&msg_full_q);
			mutex_unlock(&in_fifo_lock);
			return -EAGAIN;
		}

		dev_dbg(devp, "queue is full, shrinking\n");

		if (slkq_shrink_fifo()) {
			dev_err(devp, "failed to shrink\n");
			mutex_unlock(&in_fifo_lock);
			return -EFAULT;
		}
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
        .open = slkq_dev_open,
        .write = slkq_dev_write,
        .read = slkq_dev_read,
};

static ssize_t slkq_status_read (struct file *file, char __user *ubuf,
				 size_t count, loff_t *off)
{
	char buf[128];
	int len = 0;

	if (*off) {
		return 0;
	}

	len += sprintf(buf, "%u %u %u %u\n", kfifo_len(&msg_fifo), kfifo_avail(&msg_fifo),
		       kfifo_size(&msg_fifo), spool_size);

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

static int slkq_spool_thread (void *data)
{
	int ret;

	for (;;) {
                wait_event_interruptible(msg_full_q, kthread_should_stop() ||
					 kfifo_is_full(&msg_fifo));

		if (kthread_should_stop()) {
			return 0;
		}

		ret = mutex_lock_interruptible(&in_fifo_lock);
		if (ret) {
			dev_err(devp, "%s: mutex_lock()\n", __func__);
			return ret;
		}

		slkq_shrink_fifo();
		mutex_unlock(&in_fifo_lock);
        }

	return 0;
}

static int slkq_init (void)
{
	int ret;

	INIT_KFIFO(msg_fifo);

        init_waitqueue_head(&msg_full_q);
        init_waitqueue_head(&msg_new_q);

	mutex_init(&in_fifo_lock);
	mutex_init(&out_fifo_lock);
	mutex_init(&spool_lock);

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

	sfile = filp_open(SLKQ_SPOOL_FILENAME, O_RDWR | O_TRUNC | O_CREAT | O_SYNC, 0);

	if (IS_ERR(sfile)) {
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
	filp_close(sfile, NULL);
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
	filp_close(sfile, NULL);
	device_destroy(dev_cls, dev);
	cdev_del(&chr_dev);
	unregister_chrdev_region(dev, 1);
	class_destroy(dev_cls);
	kfifo_free(&msg_fifo);

        return;
}

module_init(slkq_init);
module_exit(slkq_exit);
