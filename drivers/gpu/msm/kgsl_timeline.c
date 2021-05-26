// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-fence.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/sync_file.h>
#include <linux/xarray.h>

#include "kgsl_device.h"
#include "kgsl_sharedmem.h"
#include "kgsl_timeline.h"
#include "kgsl_trace.h"

static DEFINE_SPINLOCK(fence_lock);

struct kgsl_timeline_fence {
	struct dma_fence base;
	struct kgsl_timeline *timeline;
	struct list_head node;
};

struct dma_fence *kgsl_timelines_to_fence_array(struct kgsl_device *device,
		u64 timelines, u64 count, u64 usize, bool any)
{
	void __user *uptr = u64_to_user_ptr(timelines);
	struct dma_fence_array *array;
	struct dma_fence **fences;
	int i, ret = 0;

	if (!count)
		return ERR_PTR(-EINVAL);

	fences = kcalloc(count, sizeof(*fences),
		GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);

	if (!fences)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < count; i++) {
		struct kgsl_timeline_val val;
		struct kgsl_timeline *timeline;

		if (copy_struct_from_user(&val, sizeof(val), uptr, usize)) {
			ret = -EFAULT;
			goto err;
		}

		if (val.padding) {
			ret = -EINVAL;
			goto err;
		}

		timeline = kgsl_timeline_by_id(device, val.timeline);
		if (!timeline) {
			ret = -ENOENT;
			goto err;
		}

		fences[i] = kgsl_timeline_fence_alloc(timeline, val.seqno);
		kgsl_timeline_put(timeline);

		if (IS_ERR(fences[i])) {
			ret = PTR_ERR(fences[i]);
			goto err;
		}

		uptr += usize;
	}

	/* No need for a fence array for only one fence */
	if (count == 1) {
		struct dma_fence *fence = fences[0];

		kfree(fences);
		return fence;
	}

	array = dma_fence_array_create(count, fences,
		dma_fence_context_alloc(1), 0, any);

	if (array)
		return &array->base;

	ret = -ENOMEM;
err:
	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(fences[i]))
			dma_fence_put(fences[i]);
	}

	kfree(fences);
	return ERR_PTR(ret);
}

void kgsl_timeline_destroy(struct kref *kref)
{
	struct kgsl_timeline *timeline = container_of(kref,
		struct kgsl_timeline, ref);

	WARN_ON(!list_empty(&timeline->fences));

	trace_kgsl_timeline_destroy(timeline->id);

	kfree(timeline);
}

struct kgsl_timeline *kgsl_timeline_get(struct kgsl_timeline *timeline)
{
	if (timeline) {
		if (!kref_get_unless_zero(&timeline->ref))
			return NULL;
	}

	return timeline;
}

static struct kgsl_timeline *kgsl_timeline_alloc(struct kgsl_device *device,
		u64 initial)
{
	struct kgsl_timeline *timeline;
	u32 id;
	int ret;

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline)
		return ERR_PTR(-ENOMEM);

	ret = xa_alloc(&device->timelines, &id,
		timeline, xa_limit_32b, GFP_KERNEL);

	if (ret) {
		kfree(timeline);
		return ERR_PTR(ret);
	}

	timeline->context = dma_fence_context_alloc(1);
	timeline->id = id;
	INIT_LIST_HEAD(&timeline->fences);
	timeline->value = initial;

	snprintf((char *) timeline->name, sizeof(timeline->name),
		"kgsl-sw-timeline-%d", id);

	trace_kgsl_timeline_alloc(id, initial);

	spin_lock_init(&timeline->lock);

	kref_init(&timeline->ref);

	return timeline;
}

static struct kgsl_timeline_fence *to_timeline_fence(struct dma_fence *fence)
{
	return container_of(fence, struct kgsl_timeline_fence, base);
}

static void timeline_fence_release(struct dma_fence *fence)
{
	struct kgsl_timeline_fence *f = to_timeline_fence(fence);
	struct kgsl_timeline *timeline = f->timeline;
	struct kgsl_timeline_fence *cur, *temp;
	unsigned long flags;

	spin_lock_irqsave(&fence_lock, flags);

	/* If the fence is still on the active list, remove it */
	list_for_each_entry_safe(cur, temp, &timeline->fences, node) {
		if (f != cur)
			continue;

		list_del_init(&f->node);
		break;
	}
	spin_unlock_irqrestore(&fence_lock, flags);

	trace_kgsl_timeline_fence_release(f->timeline->id, fence->seqno);

	kgsl_timeline_put(f->timeline);
	dma_fence_free(fence);
}

static bool timeline_fence_signaled(struct dma_fence *fence)
{
	struct kgsl_timeline_fence *f = to_timeline_fence(fence);

	return !__dma_fence_is_later(fence->seqno, f->timeline->value,
		fence->ops);
}

static const char *timeline_get_driver_name(struct dma_fence *fence)
{
	return "kgsl-sw-timeline";
}

static const char *timeline_get_timeline_name(struct dma_fence *fence)
{
	struct kgsl_timeline_fence *f = to_timeline_fence(fence);

	return f->timeline->name;
}

static void timeline_get_value_str(struct dma_fence *fence,
		char *str, int size)
{
	struct kgsl_timeline_fence *f = to_timeline_fence(fence);

	snprintf(str, size, "%lld", f->timeline->value);
}

static const struct dma_fence_ops timeline_fence_ops = {
	.get_driver_name = timeline_get_driver_name,
	.get_timeline_name = timeline_get_timeline_name,
	.signaled = timeline_fence_signaled,
	.release = timeline_fence_release,
	.timeline_value_str = timeline_get_value_str,
	.use_64bit_seqno = true,
};

static void kgsl_timeline_add_fence(struct kgsl_timeline *timeline,
		struct kgsl_timeline_fence *fence)
{
	struct kgsl_timeline_fence *entry;
	unsigned long flags;

	spin_lock_irqsave(&fence_lock, flags);
	list_for_each_entry(entry, &timeline->fences, node) {
		if (fence->base.seqno < entry->base.seqno) {
			list_add_tail(&fence->node, &entry->node);
			spin_unlock_irqrestore(&fence_lock, flags);
			return;
		}
	}

	list_add_tail(&fence->node, &timeline->fences);
	spin_unlock_irqrestore(&fence_lock, flags);
}

void kgsl_timeline_signal(struct kgsl_timeline *timeline, u64 seqno)
{
	struct kgsl_timeline_fence *fence, *tmp;
	struct list_head temp;

	INIT_LIST_HEAD(&temp);

	spin_lock_irq(&timeline->lock);

	if (seqno < timeline->value)
		goto unlock;

	trace_kgsl_timeline_signal(timeline->id, seqno);

	timeline->value = seqno;

	/* Copy the list out so we can walk it without holding the lock */
	spin_lock(&fence_lock);
	list_replace_init(&timeline->fences, &temp);
	spin_unlock(&fence_lock);

	list_for_each_entry_safe(fence, tmp, &temp, node) {
		if (timeline_fence_signaled(&fence->base)) {
			list_del_init(&fence->node);
			dma_fence_signal_locked(&fence->base);
		}
	}

	/* Put the active fences back in the timeline list */
	list_for_each_entry_safe(fence, tmp, &temp, node) {
		list_del_init(&fence->node);
		kgsl_timeline_add_fence(timeline, fence);
	}

unlock:
	spin_unlock_irq(&timeline->lock);
}

struct dma_fence *kgsl_timeline_fence_alloc(struct kgsl_timeline *timeline,
		u64 seqno)
{
	struct kgsl_timeline_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->timeline = kgsl_timeline_get(timeline);

	dma_fence_init(&fence->base, &timeline_fence_ops,
		&timeline->lock, timeline->context, seqno);

	INIT_LIST_HEAD(&fence->node);

	if (!dma_fence_is_signaled(&fence->base))
		kgsl_timeline_add_fence(timeline, fence);

	trace_kgsl_timeline_fence_alloc(timeline->id, seqno);

	return &fence->base;
}

long kgsl_ioctl_timeline_create(struct kgsl_device_private *dev_priv,
		unsigned int cmd, void *data)
{
	struct kgsl_device *device = dev_priv->device;
	struct kgsl_timeline_create *param = data;
	struct kgsl_timeline *timeline;

	timeline = kgsl_timeline_alloc(device, param->seqno);
	if (IS_ERR(timeline))
		return PTR_ERR(timeline);

	param->id = timeline->id;
	return 0;
}

struct kgsl_timeline *kgsl_timeline_by_id(struct kgsl_device *device,
		u32 id)
{
	struct kgsl_timeline *timeline = xa_load(&device->timelines, id);

	return kgsl_timeline_get(timeline);
}

long kgsl_ioctl_timeline_wait(struct kgsl_device_private *dev_priv,
		unsigned int cmd, void *data)
{
	struct kgsl_device *device = dev_priv->device;
	struct kgsl_timeline_wait *param = data;
	struct dma_fence *fence;
	unsigned long timeout;
	signed long ret;

	if (param->flags != KGSL_TIMELINE_WAIT_ANY &&
		param->flags != KGSL_TIMELINE_WAIT_ALL)
		return -EINVAL;

	if (param->padding)
		return -EINVAL;

	fence = kgsl_timelines_to_fence_array(device, param->timelines,
		param->count, param->timelines_size,
		(param->flags == KGSL_TIMELINE_WAIT_ANY));

	if (IS_ERR(fence))
		return PTR_ERR(fence);

	if (param->tv_sec >= KTIME_SEC_MAX)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else {
		ktime_t time = ktime_set(param->tv_sec, param->tv_nsec);

		timeout = msecs_to_jiffies(ktime_to_ms(time));
	}

	trace_kgsl_timeline_wait(param->flags, param->tv_sec, param->tv_nsec);

	/* secs.nsecs to jiffies */
	if (!timeout)
		ret = dma_fence_is_signaled(fence) ? 0 : -EBUSY;
	else {
		ret = dma_fence_wait_timeout(fence, true, timeout);

		if (!ret)
			ret = -ETIMEDOUT;
		else if (ret > 0)
			ret = 0;
	}

	dma_fence_put(fence);

	return ret;
}

long kgsl_ioctl_timeline_query(struct kgsl_device_private *dev_priv,
		unsigned int cmd, void *data)
{
	struct kgsl_timeline_val *param = data;
	struct kgsl_timeline *timeline;

	if (param->padding)
		return -EINVAL;

	timeline = kgsl_timeline_by_id(dev_priv->device, param->timeline);
	if (!timeline)
		return -ENODEV;

	param->seqno = timeline->value;
	kgsl_timeline_put(timeline);

	return 0;
}

long kgsl_ioctl_timeline_fence_get(struct kgsl_device_private *dev_priv,
		unsigned int cmd, void *data)
{
	struct kgsl_device *device = dev_priv->device;
	struct kgsl_timeline_fence_get *param = data;
	struct kgsl_timeline *timeline;
	struct sync_file *sync_file;
	struct dma_fence *fence;
	int ret = 0, fd;

	timeline = kgsl_timeline_by_id(device, param->timeline);
	if (!timeline)
		return -ENODEV;

	fence = kgsl_timeline_fence_alloc(timeline, param->seqno);

	if (IS_ERR(fence)) {
		kgsl_timeline_put(timeline);
		return PTR_ERR(fence);
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto out;
	}

	sync_file = sync_file_create(fence);
	if (sync_file) {
		fd_install(fd, sync_file->file);
		param->handle = fd;
	} else {
		put_unused_fd(fd);
		ret = -ENOMEM;
	}

out:
	dma_fence_put(fence);
	kgsl_timeline_put(timeline);

	return ret;
}

long kgsl_ioctl_timeline_signal(struct kgsl_device_private *dev_priv,
		unsigned int cmd, void *data)
{
	struct kgsl_device *device = dev_priv->device;
	struct kgsl_timeline_signal *param = data;
	u64 timelines;
	int i;

	if (!param->timelines_size) {
		param->timelines_size = sizeof(struct kgsl_timeline_val);
		return -EAGAIN;
	}

	if (!param->count)
		return -EINVAL;

	timelines = param->timelines;

	for (i = 0; i < param->count; i++) {
		struct kgsl_timeline *timeline;
		struct kgsl_timeline_val val;

		if (copy_struct_from_user(&val, sizeof(val),
			u64_to_user_ptr(timelines), param->timelines_size))
			return -EFAULT;

		if (val.padding)
			return -EINVAL;

		timeline = kgsl_timeline_by_id(device, val.timeline);
		if (!timeline)
			return -ENODEV;

		kgsl_timeline_signal(timeline, val.seqno);

		kgsl_timeline_put(timeline);

		timelines += param->timelines_size;
	}

	return 0;
}

long kgsl_ioctl_timeline_destroy(struct kgsl_device_private *dev_priv,
		unsigned int cmd, void *data)
{
	struct kgsl_device *device = dev_priv->device;
	struct kgsl_timeline_fence *fence, *tmp;
	struct kgsl_timeline *timeline;
	struct list_head temp;
	u32 *param = data;

	if (*param == 0)
		return -ENODEV;

	timeline = xa_erase(&device->timelines, *param);
	if (!timeline)
		return -ENODEV;

	INIT_LIST_HEAD(&temp);

	spin_lock_irq(&timeline->lock);

	/* Copy any still pending fences to a temporary list */
	list_replace_init(&timeline->fences, &temp);

	spin_unlock_irq(&timeline->lock);

	/*
	 * Set an error on each still pending fence and signal them outside of
	 * the lock so we don't get a spinlock recursion if the fences get
	 * destroyed inside of the signal callback
	 */
	list_for_each_entry_safe(fence, tmp, &temp, node) {
		list_del_init(&fence->node);
		dma_fence_set_error(&fence->base, -ENOENT);
		dma_fence_signal(&fence->base);
	}

	kgsl_timeline_put(timeline);

	return timeline ? 0 : -ENODEV;
}
