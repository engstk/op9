// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <linux/haven/hh_dbl.h>
#include <linux/haven/hh_errno.h>
#include <linux/haven/hcall.h>

struct hh_dbl_desc {
	enum hh_dbl_label label;
};

enum hh_dbl_dir {
	HH_DBL_DIRECTION_TX,
	HH_DBL_DIRECTION_RX
};

struct hh_dbl_cap_table {
	struct hh_dbl_desc *client_desc;
	spinlock_t cap_entry_lock;
	hh_capid_t tx_cap_id;
	int tx_reg_done;

	hh_capid_t rx_cap_id;
	int rx_irq;
	int rx_reg_done;
	const char *rx_irq_name;
	dbl_rx_cb_t rx_callback;
	void *rx_priv_data;
	wait_queue_head_t cap_wq;
};

static bool hh_dbl_initialized;
static struct hh_dbl_cap_table hh_dbl_cap_table[HH_DBL_LABEL_MAX];

/**
 * hh_dbl_validate_params - Validate doorbell common parameters
 */
static int hh_dbl_validate_params(struct hh_dbl_desc *client_desc,
			enum hh_dbl_dir dir, const unsigned long flags)
{
	struct hh_dbl_cap_table *cap_table_entry;
	int ret;

	if (IS_ERR_OR_NULL(client_desc))
		return -EINVAL;

	/* Check if the client has manipulated the label */
	if (client_desc->label < 0 || client_desc->label >= HH_DBL_LABEL_MAX)
		return -EINVAL;

	cap_table_entry = &hh_dbl_cap_table[client_desc->label];

	spin_lock(&cap_table_entry->cap_entry_lock);

	if (cap_table_entry->client_desc != client_desc) {
		spin_unlock(&cap_table_entry->cap_entry_lock);
		pr_err("%s: Invalid client descriptor\n", __func__);
		return -EINVAL;
	}

	/*
	 * rx_cap_id == NULL and tx_cap_id == NULL means TWO things
	 * either "hh_dbl_populate_cap_info()" call from RM is not over
	 * or
	 * There are no doorbell setup for Tx or Rx
	 */
	if (dir == HH_DBL_DIRECTION_RX) {
		if (!cap_table_entry->rx_reg_done) {
			ret = -EINVAL;
			goto err;
		}

		if ((cap_table_entry->rx_cap_id == HH_CAPID_INVAL) &&
			(flags & HH_DBL_NONBLOCK)) {
			ret = -EAGAIN;
			goto err;
		}

		spin_unlock(&cap_table_entry->cap_entry_lock);

		if (wait_event_interruptible(cap_table_entry->cap_wq,
				cap_table_entry->rx_cap_id != HH_CAPID_INVAL))
			return -ERESTARTSYS;

	} else {
		if (!cap_table_entry->tx_reg_done) {
			ret = -EINVAL;
			goto err;
		}

		if ((cap_table_entry->tx_cap_id == HH_CAPID_INVAL) &&
			(flags & HH_DBL_NONBLOCK)) {
			ret = -EAGAIN;
			goto err;
		}

		spin_unlock(&cap_table_entry->cap_entry_lock);

		if (wait_event_interruptible(cap_table_entry->cap_wq,
				cap_table_entry->tx_cap_id != HH_CAPID_INVAL))
			return -ERESTARTSYS;

	}

	return 0;
err:
	spin_unlock(&cap_table_entry->cap_entry_lock);
	return ret;
}

/**
 * hh_dbl_read_and_clean - Automatically read and clear the flags in doorbell
 * @client_desc: client handle to indetify the doorbell object
 * @clear_flags: clear the bits mentioned in the clear_flags
 * @flags: Optional flags to pass to send the data. For the list of flags,
 *         see linux/haven/hh_dbl.h
 *
 * Reads and clears the flags of the Doorbell object. If there is a pending
 * bound virtual interrupt, it will be de-asserted
 *
 * Returns:
 * 0 on success, @clear_flags contains the doorbell’s previous unmasked flags
 * before the @clear_flags were removed.
 */
int hh_dbl_read_and_clean(void *dbl_client_desc, hh_dbl_flags_t *clear_flags,
			  const unsigned long flags)
{
	struct hh_dbl_cap_table *cap_table_entry;
	struct hh_hcall_dbl_recv_resp recv_resp;
	struct hh_dbl_desc *client_desc = dbl_client_desc;
	int ret, hh_ret;

	if (!clear_flags)
		return -EINVAL;

	ret = hh_dbl_validate_params(client_desc, HH_DBL_DIRECTION_RX, flags);
	if (ret)
		return ret;

	cap_table_entry = &hh_dbl_cap_table[client_desc->label];

	hh_ret = hh_hcall_dbl_recv(cap_table_entry->rx_cap_id,
					*clear_flags, &recv_resp);

	ret = hh_remap_error(hh_ret);
	if (ret != 0)
		pr_err("%s: Hypercall failed, ret = %d\n", __func__, hh_ret);
	else
		*clear_flags = recv_resp.old_flags;

	return ret;
}
EXPORT_SYMBOL(hh_dbl_read_and_clean);

/**
 * hh_dbl_set_mask - Set doorbell object mask
 * @client_desc: client handle to indetify the doorbell object
 * @enable_mask: The mask of flags that will cause an assertion of
 *				 the doorbell's bound virtual interrupt
 * @ack_mask: Controls which flags should be automatically cleared
 *			  when the interrupt is asserted
 * @flags: Optional flags to pass to send the data. For the list of flags,
 *         see linux/haven/hh_dbl.h
 *
 * Sets the Doorbell object’s masks. A doorbell object has two masks
 * which are configured by the receiver to control which flags it is
 * interested in, and which flags if any should be automatically acknowledged.
 *
 * Returns:
 * 0 on success
 */
int hh_dbl_set_mask(void *dbl_client_desc, hh_dbl_flags_t enable_mask,
		    hh_dbl_flags_t ack_mask, const unsigned long flags)
{
	struct hh_dbl_cap_table *cap_table_entry;
	struct hh_dbl_desc *client_desc = dbl_client_desc;
	int ret, hh_ret;

	ret = hh_dbl_validate_params(client_desc, HH_DBL_DIRECTION_RX, flags);
	if (ret)
		return ret;

	cap_table_entry = &hh_dbl_cap_table[client_desc->label];

	hh_ret = hh_hcall_dbl_mask(cap_table_entry->rx_cap_id,
						enable_mask, ack_mask);

	ret = hh_remap_error(hh_ret);
	if (ret != 0)
		pr_err("%s: Hypercall failed ret = %d\n", __func__, hh_ret);

	return ret;
}
EXPORT_SYMBOL(hh_dbl_set_mask);

/**
 * hh_dbl_send - Set flags in the doorbell
 * @client_desc: client handle to indetify the doorbell object
 * @newflags: flags to set in the doorbell. This flag along with enable_mask
 *			  in the doorbell decide whehter to raise vIRQ are not.
 * @flags: Optional flags to pass to send the data. For the list of flags,
 *         see linux/haven/hh_dbl.h
 *
 * Set flags in the doorbell. If following the send, the set of enabled flags
 * as defined by the bitwise-AND of the doorbell flags with the EnableMask,
 * is non-zero, any bound virtual interrupt will be asserted.
 *
 * Returns:
 * 0 on success, @newflags contains the doorbell’s previous unmasked flags
 * before the @newflags were added.
 */
int hh_dbl_send(void *dbl_client_desc, hh_dbl_flags_t *newflags,
		unsigned long flags)
{
	struct hh_dbl_cap_table *cap_table_entry;
	struct hh_hcall_dbl_send_resp send_resp;
	struct hh_dbl_desc *client_desc = dbl_client_desc;
	int ret, hh_ret;

	if (!newflags)
		return -EINVAL;

	ret = hh_dbl_validate_params(client_desc, HH_DBL_DIRECTION_TX, flags);
	if (ret)
		return ret;

	cap_table_entry = &hh_dbl_cap_table[client_desc->label];

	hh_ret = hh_hcall_dbl_send(cap_table_entry->tx_cap_id, *newflags,
								&send_resp);

	ret = hh_remap_error(hh_ret);
	if (ret != 0)
		pr_err("%s: Hypercall failed ret = %d\n", __func__, hh_ret);
	else
		*newflags = send_resp.old_flags;

	return ret;
}
EXPORT_SYMBOL(hh_dbl_send);

/**
 * hh_dbl_reset - clear all the flags of the doorbell and sets all bits in
 *				  the Doorbell’s mask.
 * @client_desc: client handle to indetify the doorbell object
 * @flags: Optional flags to pass to send the data. For the list of flags,
 *         see linux/haven/hh_dbl.h
 *
 * Clears all the flags of the doorbell and sets all bits in the doorbell’s
 * mask. If there is a pending bound virtual interrupt, it will be de-asserted.
 *
 * Returns:
 * 0 on success
 */
int hh_dbl_reset(void *dbl_client_desc, const unsigned long flags)
{
	struct hh_dbl_cap_table *cap_table_entry;
	struct hh_dbl_desc *client_desc = dbl_client_desc;
	int ret, hh_ret;

	ret = hh_dbl_validate_params(client_desc, HH_DBL_DIRECTION_RX, flags);
	if (ret)
		return ret;

	cap_table_entry = &hh_dbl_cap_table[client_desc->label];

	hh_ret = hh_hcall_dbl_reset(cap_table_entry->rx_cap_id);

	ret = hh_remap_error(hh_ret);
	if (ret != 0)
		pr_err("%s: Hypercall failed ret = %d\n", __func__, hh_ret);

	return ret;
}
EXPORT_SYMBOL(hh_dbl_reset);

static irqreturn_t hh_dbl_rx_callback_thread(int irq, void *data)
{
	struct hh_dbl_cap_table *cap_table_entry = data;

	if (!cap_table_entry->rx_callback)
		return IRQ_HANDLED;

	cap_table_entry->rx_callback(irq, cap_table_entry->rx_priv_data);
	return IRQ_HANDLED;
}

/**
 * hh_dbl_tx_register: Register as a Tx client to use the doorbell
 * @label: The label associated to the doorbell that the client wants
 *	   to send a message to other VM.
 *
 * The function returns a descriptor for the clients to send a message.
 * Else, returns -EBUSY if some other client is already registered
 * to this label, and -EINVAL for invalid arguments. The caller should check
 * the return value using IS_ERR_OR_NULL() and PTR_ERR() to extract the error
 * code.
 */
void *hh_dbl_tx_register(enum hh_dbl_label label)
{
	struct hh_dbl_cap_table *cap_table_entry;
	struct hh_dbl_desc *client_desc;
	int ret;

	if (label < 0 || label >= HH_DBL_LABEL_MAX)
		return ERR_PTR(-EINVAL);

	if (!hh_dbl_initialized)
		return ERR_PTR(-EPROBE_DEFER);

	cap_table_entry = &hh_dbl_cap_table[label];

	spin_lock(&cap_table_entry->cap_entry_lock);

	/* Avoid multiple client Tx registration for the same doorbell */
	if (cap_table_entry->tx_reg_done) {
		ret = -EBUSY;
		goto err;
	}

	if (cap_table_entry->client_desc) {
		client_desc = cap_table_entry->client_desc;
	} else {
		client_desc = kzalloc(sizeof(*client_desc), GFP_ATOMIC);
		if (!client_desc) {
			ret = -ENOMEM;
			goto err;
		}

		client_desc->label = label;
		cap_table_entry->client_desc = client_desc;
	}

	cap_table_entry->tx_reg_done = 1;

	pr_debug("%s: Registered Tx client for label: %d\n", __func__, label);

	spin_unlock(&cap_table_entry->cap_entry_lock);
	return client_desc;

err:
	spin_unlock(&cap_table_entry->cap_entry_lock);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(hh_dbl_tx_register);

/**
 * hh_dbl_rx_register: Register as a Rx client to use the doorbell
 * @label: The label associated to the doorbell that the client wants
 *	   to read a message.
 * @rx_cb: Callback of the client when there is a vIRQ on doorbell
 * @priv: Private data of the driver
 *
 * The function returns a descriptor for the clients to receieve a message.
 * Else, returns -EBUSY if some other client is already registered
 * to this label, and -EINVAL for invalid arguments. The caller should check
 * the return value using IS_ERR_OR_NULL() and PTR_ERR() to extract the error
 * code.
 */

void *hh_dbl_rx_register(enum hh_dbl_label label, dbl_rx_cb_t rx_cb, void *priv)
{
	struct hh_dbl_cap_table *cap_table_entry;
	struct hh_dbl_desc *client_desc;
	int ret;

	if (label < 0 || label >= HH_DBL_LABEL_MAX)
		return ERR_PTR(-EINVAL);

	if (!hh_dbl_initialized)
		return ERR_PTR(-EPROBE_DEFER);

	cap_table_entry = &hh_dbl_cap_table[label];

	spin_lock(&cap_table_entry->cap_entry_lock);

	/* Avoid multiple client Rx registration for the same doorbell */
	if (cap_table_entry->rx_reg_done) {
		ret = -EBUSY;
		goto err;
	}

	if (cap_table_entry->client_desc) {
		client_desc = cap_table_entry->client_desc;
	} else {
		client_desc = kzalloc(sizeof(*client_desc), GFP_ATOMIC);
		if (!client_desc) {
			ret = -ENOMEM;
			goto err;
		}

		client_desc->label = label;
		cap_table_entry->client_desc = client_desc;
	}

	cap_table_entry->rx_callback = rx_cb;
	cap_table_entry->rx_priv_data = priv;

	cap_table_entry->rx_reg_done = 1;

	pr_debug("%s: Registered Rx client for label: %d\n", __func__, label);

	spin_unlock(&cap_table_entry->cap_entry_lock);
	return client_desc;

err:
	pr_debug("%s: Registration for Rx client for label failed: %d\n",
		__func__, label);
	spin_unlock(&cap_table_entry->cap_entry_lock);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(hh_dbl_rx_register);

/**
 * hh_dbl_tx_unregister: Unregister Tx client to use the doorbell
 * @client_desc: The descriptor that was passed via hh_dbl_tx_register() or
 *		 hh_dbl_rx_register()
 *
 * The function returns 0 is the client was unregistered successfully. Else,
 * -EINVAL for invalid arguments.
 */
int hh_dbl_tx_unregister(void *dbl_client_desc)
{
	struct hh_dbl_desc *client_desc = dbl_client_desc;
	struct hh_dbl_cap_table *cap_table_entry;

	if (IS_ERR_OR_NULL(client_desc))
		return -EINVAL;

	/* Check if the client has manipulated the label */
	if (client_desc->label < 0 || client_desc->label >= HH_DBL_LABEL_MAX)
		return -EINVAL;

	cap_table_entry = &hh_dbl_cap_table[client_desc->label];

	spin_lock(&cap_table_entry->cap_entry_lock);

	/* Is the client trying to free someone else's doorbell? */
	if (cap_table_entry->client_desc != client_desc) {
		pr_err("%s: Trying to free invalid client descriptor!\n",
			__func__);
		spin_unlock(&cap_table_entry->cap_entry_lock);
		return -EINVAL;
	}

	pr_debug("%s: Unregistering client for label: %d\n",
			__func__, client_desc->label);

	/* Rx client still holding the "client_desc". Do not remove now. */
	if (!cap_table_entry->rx_reg_done) {
		cap_table_entry->client_desc = NULL;
		kfree(client_desc);
	} else {
		pr_debug("%s: Rx client holding the client_desc.\n", __func__);
	}

	cap_table_entry->tx_reg_done = 0;
	spin_unlock(&cap_table_entry->cap_entry_lock);

	return 0;
}
EXPORT_SYMBOL(hh_dbl_tx_unregister);

/**
 * hh_dbl_rx_unregister: Unregister Rx client to use the doorbell
 * @client_desc: The descriptor that was passed via hh_dbl_tx_register() or
 *				 hh_dbl_rx_register()
 *
 * The function returns 0 is the client was unregistered successfully. Else,
 * -EINVAL for invalid arguments.
 */
int hh_dbl_rx_unregister(void *dbl_client_desc)
{
	struct hh_dbl_desc *client_desc = dbl_client_desc;
	struct hh_dbl_cap_table *cap_table_entry;

	if (IS_ERR_OR_NULL(client_desc))
		return -EINVAL;

	/* Check if the client has manipulated the label */
	if (client_desc->label < 0 || client_desc->label >= HH_DBL_LABEL_MAX)
		return -EINVAL;

	cap_table_entry = &hh_dbl_cap_table[client_desc->label];

	spin_lock(&cap_table_entry->cap_entry_lock);

	/* Is the client trying to free someone else's doorbell? */
	if (cap_table_entry->client_desc != client_desc) {
		pr_err("%s: Trying to free invalid client descriptor!\n",
			__func__);
		spin_unlock(&cap_table_entry->cap_entry_lock);
		return -EINVAL;
	}

	pr_debug("%s: Unregistering client for label: %d\n", __func__,
			 client_desc->label);

	/* Tx client still holding the "client_desc". Do not remove now.*/
	if (!cap_table_entry->tx_reg_done) {
		cap_table_entry->client_desc = NULL;
		kfree(client_desc);
	} else {
		pr_debug("%s: Tx client holding the client_desc.\n", __func__);
	}

	cap_table_entry->rx_callback = NULL;
	cap_table_entry->rx_priv_data = NULL;
	cap_table_entry->rx_reg_done = 0;

	spin_unlock(&cap_table_entry->cap_entry_lock);

	return 0;
}
EXPORT_SYMBOL(hh_dbl_rx_unregister);

/**
 * This API is called by RM driver to populate doorbell objects
 */
int hh_dbl_populate_cap_info(enum hh_dbl_label label, u64 cap_id,
				int direction, int rx_irq)
{
	struct hh_dbl_cap_table *cap_table_entry;
	int ret = 0;

	if (!hh_dbl_initialized)
		return -EAGAIN;

	if (label < 0 || label >= HH_DBL_LABEL_MAX) {
		pr_err("%s: Invalid label passed\n", __func__);
		return -EINVAL;
	}

	cap_table_entry = &hh_dbl_cap_table[label];

	switch (direction) {
	case HH_DBL_DIRECTION_TX:
		/* No interrupt should associated with Tx doorbell*/
		if (rx_irq > 0) {
			pr_err("%s: No IRQ associated for Tx doorbell!\n",
				__func__);
			ret = -ENXIO;
			goto err;
		}

		spin_lock(&cap_table_entry->cap_entry_lock);
		cap_table_entry->tx_cap_id = cap_id;
		spin_unlock(&cap_table_entry->cap_entry_lock);

		wake_up_interruptible(&cap_table_entry->cap_wq);

		pr_debug("%s: label: %d; tx_cap_id: %llu; dir: %d; rx_irq: %d\n",
			__func__, label, cap_id, direction, rx_irq);
		break;
	case HH_DBL_DIRECTION_RX:
		if (rx_irq <= 0) {
			pr_err("%s: Invalid IRQ number for Rx doorbell\n",
				__func__);
			ret = -ENXIO;
			goto err;
		}

		cap_table_entry->rx_irq = rx_irq;
		ret = request_threaded_irq(cap_table_entry->rx_irq,
				   NULL,
				   hh_dbl_rx_callback_thread,
				   IRQF_ONESHOT | IRQF_TRIGGER_RISING,
				   cap_table_entry->rx_irq_name,
				   cap_table_entry);

		if (ret < 0) {
			pr_err("%s: IRQ registration failed\n", __func__);
			goto err;
		}

		irq_set_irq_wake(rx_irq, 1);

		spin_lock(&cap_table_entry->cap_entry_lock);
		cap_table_entry->rx_cap_id = cap_id;
		spin_unlock(&cap_table_entry->cap_entry_lock);

		wake_up_interruptible(&cap_table_entry->cap_wq);

		pr_debug("%s: label: %d; rx_cap_id: %llu; dir: %d; rx_irq: %d\n",
			__func__, label, cap_id, direction, rx_irq);
		break;
	default:
		pr_err("%s: Invalid direction(%d) for doorbell\n",
			__func__, direction);
		ret = -EINVAL;
	}

err:
	return ret;
}
EXPORT_SYMBOL(hh_dbl_populate_cap_info);

static void hh_dbl_cleanup(int begin_idx)
{
	struct hh_dbl_cap_table *cap_table_entry;
	int i;

	if (begin_idx >= HH_DBL_LABEL_MAX)
		begin_idx = HH_DBL_LABEL_MAX - 1;

	for (i = begin_idx; i >= 0; i--) {
		cap_table_entry = &hh_dbl_cap_table[i];
		kfree(cap_table_entry->rx_irq_name);
	}
}

static int __init hh_dbl_init(void)
{
	struct hh_dbl_cap_table *entry;
	int ret;
	int i;

	for (i = 0; i < HH_DBL_LABEL_MAX; i++) {
		entry = &hh_dbl_cap_table[i];
		spin_lock_init(&entry->cap_entry_lock);
		init_waitqueue_head(&entry->cap_wq);
		entry->tx_cap_id = HH_CAPID_INVAL;
		entry->rx_cap_id = HH_CAPID_INVAL;
		entry->rx_irq_name = kasprintf(GFP_KERNEL, "hh_dbl_rx_%d", i);
		if (!entry->rx_irq_name) {
			ret = -ENOMEM;
			goto err;
		}
	}

	hh_dbl_initialized = true;

	return 0;

err:
	hh_dbl_cleanup(i);
	return ret;
}
module_init(hh_dbl_init);

static void __exit hh_dbl_exit(void)
{
	hh_dbl_cleanup(HH_DBL_LABEL_MAX - 1);
}
module_exit(hh_dbl_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Haven Doorbell Driver");
MODULE_LICENSE("GPL v2");
