/**********************************************************************************
* Copyright (c)  2008-2015 OnePlus Mobile Comm Corp., Ltd
* Description:     Healthinfo Monitor  Kernel Driver
*
* Version   : 1.0
* Date       : 2019-04-24
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0        2018-04-24       jared.wu@PSP         Created for Healthinfomonitor
***********************************************************************************/

#include <linux/oem/oneplus_healthinfo.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/kobject.h>
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#endif
#ifdef CONFIG_ONEPLUS_MEM_MONITOR
#include <linux/oem/memory_monitor.h>
#endif
#include <linux/cpufreq.h>
#include "../techpack/sched/walt.h"
#include "../include/linux/cred.h"

#define BUFFER_SIZE_S 256
#define BUFFER_SIZE_M 512
#define BUFFER_SIZE_M 512
#define BUFFER_SIZE_L 1024


#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
static struct timer_list task_load_info_timer;
u64 ohm_read_thresh = 1048576; /* default 1MB per 5s */
u64 ohm_write_thresh = 1048576; /* default 1MB per 5s */
u64 ohm_runtime_thresh_fg = 4000000000; /* default 4s per 5s */
u64 ohm_runtime_thresh_bg = 1500000000; /* default 1.5s per 5s */
static u32 ohm_sample_time = 5; /* default 5s */
#endif

struct io_latency_para{
	bool ctrl;
	bool logon;
	bool trig;

	int low_thresh_ms;
	u64 low_cnt;

	int high_thresh_ms;
	u64 high_cnt;

	u64 total_us;
	u64 emmc_total_us;
	u64 total_cnt;
	u64 fg_low_cnt;
	u64 fg_high_cnt;
	u64 fg_total_ms;
	u64 fg_total_cnt;
	u64 fg_max_delta_ms;
	u64 delta_ms;

	//fg
	u64 iosize_write_count_fg;
	u64 iosize_write_us_fg;
	u64 iosize_500ms_syncwrite_count_fg;
	u64 iosize_200ms_syncwrite_count_fg;
	u64 iosize_500ms_asyncwrite_count_fg;
	u64 iosize_200ms_asyncwrite_count_fg;
	u64 iosize_read_count_fg;
	u64 iosize_read_us_fg;
	u64 iosize_500ms_read_count_fg;
	u64 iosize_200ms_read_count_fg;

	//bg
	u64 iosize_write_count_bg;
	u64 iosize_write_us_bg;
	u64 iosize_2s_asyncwrite_count_bg;
	u64 iosize_500ms_asyncwrite_count_bg;
	u64 iosize_200ms_asyncwrite_count_bg;
	u64 iosize_2s_syncwrite_count_bg;
	u64 iosize_500ms_syncwrite_count_bg;
	u64 iosize_200ms_syncwrite_count_bg;
	u64 iosize_read_count_bg;
	u64 iosize_read_us_bg;
	u64 iosize_2s_read_count_bg;
	u64 iosize_500ms_read_count_bg;
	u64 iosize_200ms_read_count_bg;

	//4k
	u64 iosize_4k_read_count;
	u64 iosize_4k_read_us;
	u64 iosize_4k_write_count;
	u64 iosize_4k_write_us;
};

struct io_latency_para oneplus_io_para = {0};

struct sched_stat_para oneplus_sched_para[OHM_SCHED_TOTAL];
static char *sched_list[OHM_TYPE_TOTAL] = {
	"iowait",
	"sched_latency",
	"fsync",
	"emmcio",
	"downread",
	"downwrite",
	"dstate",
	"sched_default_05",
	"sched_default_06",
	"sched_default_07",
	"sched_default_10",
	"sched_default_11",
	"cur_cpu_load",
	"memory_monitor",
	"io_panic",
};

/******  Action  ******/
#define MAX_OHMEVENT_PARAM 3
static struct kobject *ohm_kobj;
static struct work_struct ohm_detect_ws;
static char *ohm_detect_env[MAX_OHMEVENT_PARAM] = { "OHMACTION=uevent", NULL };
static bool ohm_action_ctrl;

void ohm_action_trig(int type)
{
	if (!ohm_action_ctrl) {
		ohm_err("ctrl off\n");
		return;
	}
	ohm_debug("%s trig action\n", sched_list[type]);
	if (OHM_MEM_MON == type || OHM_SCHED_FSYNC == type) {
		if (!ohm_kobj) {
			ohm_err("kobj NULL\n");
			return;
		}
		sprintf(ohm_detect_env[1], "OHMTYPE=%s", sched_list[type]);
		ohm_detect_env[MAX_OHMEVENT_PARAM - 1] = NULL;
		schedule_work(&ohm_detect_ws);
	}
}

void ohm_detect_work(struct work_struct *work)
{
	ohm_debug("Uevent Para: %s, %s\n", ohm_detect_env[0], ohm_detect_env[1]);
	kobject_uevent_env(ohm_kobj, KOBJ_CHANGE, ohm_detect_env);
	ohm_debug("Uevent Done!\n");
}

void ohm_action_init(void)
{
	int i = 0;
	for (i = 1; i < MAX_OHMEVENT_PARAM - 1; i++) {
		ohm_detect_env[i] = kzalloc(50, GFP_KERNEL);
		if (!ohm_detect_env[i]) {
			ohm_err("kzalloc ohm uevent param failed\n");
			goto ohm_action_init_free_memory;
		}
	}

	ohm_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	if (!ohm_kobj) {
		goto ohm_action_init_kobj_failed;
	}
	INIT_WORK(&ohm_detect_ws, ohm_detect_work);
	ohm_debug("Success !\n");
	return;

ohm_action_init_kobj_failed:
	ohm_err("Ohm kobj init err\n");
ohm_action_init_free_memory:
	for (i--; i > 0; i--) {
		kfree(ohm_detect_env[i]);
	}
	ohm_err("Failed !\n");
}

void update_stuck_trace_info(struct task_struct *tsk, int trace_type, unsigned int cpu, u64 delta)
{

	static unsigned int ltt_cpu_nr = 0;
	static unsigned int mid_cpu_end = 6;
	static unsigned int big_cpu_end = 7;

	if (!tsk->stuck_trace) {
		return;
	}

	if (!ltt_cpu_nr) {
		ltt_cpu_nr = cpumask_weight(topology_core_cpumask(ltt_cpu_nr));
	}

	if (trace_type == STUCK_TRACE_RUNNABLE) { // runnable
		tsk->oneplus_stuck_info.runnable_state           += delta;
	} else if (trace_type == STUCK_TRACE_DSTATE) { // D state
		tsk->oneplus_stuck_info.d_state.cnt++;
		if (tsk->in_iowait) {
			tsk->oneplus_stuck_info.d_state.iowait_ns    += delta;
		} else if (tsk->in_mutex) {
			tsk->oneplus_stuck_info.d_state.mutex_ns     += delta;
		} else if (tsk->in_downread) {
			tsk->oneplus_stuck_info.d_state.downread_ns  += delta;
		} else if (tsk->in_downwrite) {
			tsk->oneplus_stuck_info.d_state.downwrite_ns += delta;
		} else {
			tsk->oneplus_stuck_info.d_state.other_ns     += delta;
		}
	} else if (trace_type == STUCK_TRACE_SSTATE) { // S state
		tsk->oneplus_stuck_info.s_state.cnt++;
		if (tsk->in_binder) {
			tsk->oneplus_stuck_info.s_state.binder_ns    += delta;
		} else if (tsk->in_futex) {
			tsk->oneplus_stuck_info.s_state.futex_ns     += delta;
		} else if (tsk->in_epoll) {
			tsk->oneplus_stuck_info.s_state.epoll_ns     += delta;
		} else {
			tsk->oneplus_stuck_info.s_state.other_ns     += delta;
		}
	} else if (trace_type == STUCK_TRACE_RUNNING) { // running
		if (cpu < ltt_cpu_nr) {
			tsk->oneplus_stuck_info.ltt_running_state += delta;
		} else if (cpu <= mid_cpu_end) {
			tsk->oneplus_stuck_info.mid_running_state += delta;
		} else if (cpu == big_cpu_end) {
			tsk->oneplus_stuck_info.big_running_state += delta;
		}
	}
}

static inline void ohm_sched_stat_record_common(struct sched_stat_para *sched_stat,struct sched_stat_common *stat_common, u64 delta_ms)
{
	stat_common->total_ms += delta_ms;
	stat_common->total_cnt++;
	if (delta_ms > stat_common->max_ms) {
		stat_common->max_ms = delta_ms;
	}
	if (delta_ms >= sched_stat->high_thresh_ms) {
		stat_common->high_cnt++;
	} else if (delta_ms >= sched_stat->low_thresh_ms) {
		stat_common->low_cnt++;
	}
}

void ohm_schedstats_record(int sched_type, struct task_struct *task, u64 delta_ms)
{

	struct sched_stat_para *sched_stat = &oneplus_sched_para[sched_type];

	if (unlikely(!sched_stat->ctrl)){
		return;
	}

	sched_stat->delta_ms = delta_ms;
	ohm_sched_stat_record_common(sched_stat, &sched_stat->all, delta_ms);

	if (tsk_is_fg(task)) {
		ohm_sched_stat_record_common(sched_stat, &sched_stat->fg, delta_ms);
		if (unlikely(delta_ms >= sched_stat->high_thresh_ms)){
			if (oneplus_sched_para[sched_type].logon) {

				ohm_debug("[%s / %s] high_cnt, delay = %llu ms\n",
							sched_list[sched_type], "fg", delta_ms);
			}

			if (oneplus_sched_para[sched_type].trig)
				ohm_action_trig(sched_type);

		}
	}

	return;
}

void ohm_iolatency_record(struct request *req, unsigned int nr_bytes, int fg, u64 delta_us)
{
	u64 delta_ms = delta_us / 1000;

	if (!oneplus_io_para.ctrl)
		return;
	if (!req)
		return;
	if (fg)
	{
		oneplus_io_para.fg_total_ms += delta_ms;
		oneplus_io_para.fg_total_cnt++;
		if (delta_ms > oneplus_io_para.fg_max_delta_ms)
		{
			oneplus_io_para.fg_max_delta_ms = delta_ms;
		}
	}

	if (delta_ms >= oneplus_io_para.high_thresh_ms)
	{
		oneplus_io_para.high_cnt++;

		if (oneplus_io_para.logon)
		{
			ohm_debug("[io latency / %s] high_cnt, delay = %llu ms\n",
					  (fg ? "fg" : "bg"), delta_ms);
		}
		if (fg)
		{
			oneplus_io_para.fg_high_cnt++;
			if (oneplus_io_para.trig)
				ohm_action_trig(OHM_SCHED_EMMCIO);
		}
	}
	else if (delta_ms >= oneplus_io_para.low_thresh_ms)
	{
		oneplus_io_para.low_cnt++;
		if (fg)
		{
			oneplus_io_para.fg_low_cnt++;
		}
	}

	if (fg)
	{
		if ((req_op(req) != REQ_OP_DISCARD) && (req_op(req) != REQ_OP_SECURE_ERASE))
		{
			if (req_op(req) == REQ_OP_WRITE || req_op(req) == REQ_OP_WRITE_SAME)
			{
				oneplus_io_para.iosize_write_count_fg++;
				oneplus_io_para.iosize_write_us_fg += delta_us;
				if (rq_is_sync(req))
				{
					if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_syncwrite_count_fg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_syncwrite_count_fg++;
					}
				}
				else
				{
					if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_asyncwrite_count_fg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_asyncwrite_count_fg++;
					}
				}
			}
			else
			{
				oneplus_io_para.iosize_read_count_fg++;
				oneplus_io_para.iosize_read_us_fg += delta_us;
				if (delta_ms > 500)
				{
					oneplus_io_para.iosize_500ms_read_count_fg++;
				}
				else if (delta_ms > 200)
				{
					oneplus_io_para.iosize_200ms_read_count_fg++;
				}
			}
		}
	}
	else
	{
		if ((req_op(req) != REQ_OP_DISCARD) && (req_op(req) != REQ_OP_SECURE_ERASE))
		{
			if (req_op(req) == REQ_OP_WRITE || req_op(req) == REQ_OP_WRITE_SAME)
			{
				oneplus_io_para.iosize_write_count_bg++;
				oneplus_io_para.iosize_write_us_bg += delta_us;
				if (rq_is_sync(req))
				{
					if (delta_ms > 2000)
					{
						oneplus_io_para.iosize_2s_syncwrite_count_bg++;
						if (oneplus_io_para.trig)
							ohm_action_trig(OHM_SCHED_EMMCIO);
					}
					else if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_syncwrite_count_bg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_syncwrite_count_bg++;
					}
				}
				else
				{
					if (delta_ms > 2000)
					{
						oneplus_io_para.iosize_2s_asyncwrite_count_bg++;
						if (oneplus_io_para.trig)
							ohm_action_trig(OHM_SCHED_EMMCIO);
					}
					else if (delta_ms > 500)
					{
						oneplus_io_para.iosize_500ms_asyncwrite_count_bg++;
					}
					else if (delta_ms > 200)
					{
						oneplus_io_para.iosize_200ms_asyncwrite_count_bg++;
					}
				}
			}
			else
			{
				oneplus_io_para.iosize_read_count_bg++;
				oneplus_io_para.iosize_read_us_bg += delta_us;
				if (delta_ms > 2000)
				{
					oneplus_io_para.iosize_2s_read_count_bg++;
					if (oneplus_io_para.trig)
						ohm_action_trig(OHM_SCHED_EMMCIO);
				}
				else if (delta_ms > 500)
				{
					oneplus_io_para.iosize_500ms_read_count_bg++;
				}
				else if (delta_ms > 200)
				{
					oneplus_io_para.iosize_200ms_read_count_bg++;
				}
			}
		}
	}
	//4k
	if ((req_op(req) != REQ_OP_DISCARD) && (req_op(req) != REQ_OP_SECURE_ERASE))
	{
		if (req_op(req) == REQ_OP_WRITE || req_op(req) == REQ_OP_WRITE_SAME)
		{
			if (blk_rq_bytes(req) == 4096)
			{
				oneplus_io_para.iosize_4k_write_count++;
				oneplus_io_para.iosize_4k_write_us += delta_us;
			}
		}
		else
		{
			if (blk_rq_bytes(req) == 4096)
			{
				oneplus_io_para.iosize_4k_read_count++;
				oneplus_io_para.iosize_4k_read_us += delta_us;
			}
		}
	}
	oneplus_io_para.delta_ms = delta_ms;
	oneplus_io_para.total_us += delta_us;
	oneplus_io_para.emmc_total_us += req->flash_io_latency;
	oneplus_io_para.total_cnt++;

	return;
}

#define OHM_LIST_MAGIC          0x5a000000
#define OHM_CTRL_MAX            32
#define OHM_INT_MAX             20
#define OHM_CTRL_IOWAIT         BIT(OHM_SCHED_IOWAIT)
#define OHM_CTRL_SCHEDLATENCY   BIT(OHM_SCHED_SCHEDLATENCY)
#define OHM_CTRL_FSYNC          BIT(OHM_SCHED_FSYNC)
#define OHM_CTRL_EMMCIO         BIT(OHM_SCHED_EMMCIO)
#define OHM_CTRL_DSTATE         BIT(OHM_SCHED_DSTATE)
#define OHM_CTRL_SCHEDTOTAL     (OHM_CTRL_EMMCIO | OHM_CTRL_FSYNC | OHM_CTRL_SCHEDLATENCY |OHM_CTRL_IOWAIT | OHM_CTRL_DSTATE)
#define OHM_CTRL_CPU_CUR        BIT(OHM_CPU_LOAD_CUR)
#define OHM_CTRL_MEMMON         BIT(OHM_MEM_MON)
#define OHM_CTRL_IOPANIC_MON    BIT(OHM_IOPANIC_MON)


static int ohm_ctrl_list = OHM_LIST_MAGIC | OHM_CTRL_CPU_CUR | OHM_CTRL_MEMMON | OHM_CTRL_SCHEDTOTAL;
static int ohm_logon_list = OHM_LIST_MAGIC;
static int ohm_trig_list = OHM_LIST_MAGIC;

bool ohm_cpu_ctrl = true;
bool ohm_cpu_logon;
bool ohm_cpu_trig;

bool ohm_memmon_ctrl;
bool ohm_memmon_logon;
bool ohm_memmon_trig;

bool ohm_iopanic_mon_ctrl;
bool ohm_iopanic_mon_logon;
bool ohm_iopanic_mon_trig;

#define LOW_THRESH_MS_DEFAULT   100
#define HIGH_THRESH_MS_DEFAULT  500
#define LOW_THRESH_MS_LOW       10
#define LOW_THRESH_MS_HIGH      1000
#define HIGH_THRESH_MS_LOW      50
#define HIGH_THRESH_MS_HIGH     5000

struct thresh_para {
	int l_ms;
	int h_ms;
};

struct thresh_para ohm_thresh_para[OHM_SCHED_TOTAL] = {
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
	{ LOW_THRESH_MS_DEFAULT, HIGH_THRESH_MS_DEFAULT},
};

void ohm_para_update(void)
{
	int i;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		if (ohm_thresh_para[i].l_ms < LOW_THRESH_MS_LOW
			|| ohm_thresh_para[i].l_ms > LOW_THRESH_MS_HIGH
			|| ohm_thresh_para[i].h_ms < HIGH_THRESH_MS_LOW
			|| ohm_thresh_para[i].h_ms > HIGH_THRESH_MS_HIGH) {
			/********** Legal Check **********/
			ohm_err("Para illegal: sched_type %s, l_ms %d, h_ms %d\n",
					sched_list[i], ohm_thresh_para[i].l_ms, ohm_thresh_para[i].h_ms);
			ohm_thresh_para[i].l_ms = LOW_THRESH_MS_DEFAULT;
			ohm_thresh_para[i].h_ms = HIGH_THRESH_MS_DEFAULT;
			return;
		}
		oneplus_sched_para[i].low_thresh_ms = ohm_thresh_para[i].l_ms;
		oneplus_sched_para[i].high_thresh_ms = ohm_thresh_para[i].h_ms;
	}
	ohm_debug("Success update ohm_para!\n");
}

/****  Init  ****/
static inline void _ohm_para_init(struct sched_stat_para *sched_para)
{
	   sched_para->delta_ms = 0;
	   memset(&sched_para->all, 0 , sizeof(struct sched_stat_common));
	   memset(&sched_para->ux, 0 , sizeof(struct sched_stat_common));
	   memset(&sched_para->fg, 0 , sizeof(struct sched_stat_common));

	   return;
}

void ohm_trig_init(void)
{
	int i;
	ohm_memmon_trig = (ohm_trig_list & OHM_CTRL_MEMMON) ? true : false;
	ohm_cpu_trig = (ohm_trig_list & OHM_CTRL_CPU_CUR) ? true : false;
	ohm_iopanic_mon_trig = (ohm_trig_list & OHM_CTRL_IOPANIC_MON) ? true : false;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		oneplus_sched_para[i].trig = (ohm_trig_list & BIT(i)) ? true : false;
		if(i == OHM_SCHED_EMMCIO )
			oneplus_io_para.trig = (ohm_trig_list & BIT(i)) ? true : false;
	}
	return;
}

void ohm_logon_init(void)
{
	int i;
	ohm_cpu_logon = (ohm_logon_list & OHM_CTRL_CPU_CUR) ? true : false;
	ohm_memmon_logon = (ohm_logon_list & OHM_CTRL_MEMMON) ? true : false;
	ohm_iopanic_mon_logon = (ohm_logon_list & OHM_CTRL_IOPANIC_MON) ? true : false;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		oneplus_sched_para[i].logon = (ohm_logon_list & BIT(i)) ? true : false;
		if(i == OHM_SCHED_EMMCIO )
			oneplus_io_para.logon = (ohm_logon_list & BIT(i)) ? true : false;
	}
	return;
}

void ohm_ctrl_init(void)
{
	int i;
	ohm_cpu_ctrl = (ohm_ctrl_list & OHM_CTRL_CPU_CUR) ? true : false;
	ohm_memmon_ctrl = (ohm_ctrl_list & OHM_CTRL_MEMMON) ? true : false;
	ohm_iopanic_mon_ctrl = (ohm_ctrl_list & OHM_CTRL_IOPANIC_MON) ? true : false;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		oneplus_sched_para[i].ctrl = (ohm_ctrl_list & BIT(i)) ? true : false;
		if(i == OHM_SCHED_EMMCIO )
			oneplus_io_para.ctrl = (ohm_ctrl_list & BIT(i)) ? true : false;
	}
	return;
}

void ohm_para_init(void)
{
	int i;
	for (i = 0; i < OHM_SCHED_TOTAL; i++) {
		_ohm_para_init(&oneplus_sched_para[i]);
		oneplus_sched_para[i].low_thresh_ms = LOW_THRESH_MS_DEFAULT;
		oneplus_sched_para[i].high_thresh_ms = HIGH_THRESH_MS_DEFAULT;
	}
	oneplus_sched_para[OHM_SCHED_EMMCIO].low_thresh_ms = LOW_THRESH_MS_DEFAULT;
	oneplus_sched_para[OHM_SCHED_EMMCIO].high_thresh_ms = HIGH_THRESH_MS_DEFAULT;

	oneplus_io_para.low_thresh_ms = 100;
	oneplus_io_para.high_thresh_ms = 200;

	ohm_ctrl_init();
	ohm_logon_init();
	ohm_trig_init();
	ohm_debug("origin list: ctrl 0x%08x, logon 0x%08x, trig 0x%08x\n", ohm_ctrl_list, ohm_logon_list, ohm_trig_list);
	return;
}


static ssize_t cpu_load_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[BUFFER_SIZE_S] = {0};
	int len = 0;
	int load = ohm_get_cur_cpuload(ohm_cpu_ctrl);

	if (load < 0)
		load = 0;
	len = sprintf(page, "cur_cpuloading: %d\n""cur_cpu_ctrl: %s\n""cur_cpu_logon: %s\n""cur_cpu_trig: %s\n",
					load, (ohm_cpu_ctrl ? "true" : "false"), (ohm_cpu_logon ? "true" : "false"), (ohm_cpu_trig ? "true" : "false"));

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_cpu_load_fops = {
	.read = cpu_load_read,
};

static ssize_t sched_latency_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;
	int type = OHM_SCHED_SCHEDLATENCY;

	len = sprintf(page, "sched_latency_ctrl: %s\n""sched_latency_logon: %s\n"
				"sched_latency_trig: %s\n""sched_delta_ms: %lld\n"
				"sched_low_thresh_ms: %d\n""sched_high_thresh_ms: %d\n"
				"sched_all_max_ms: %lld\n""sched_all_high_cnt: %lld\n"
				"sched_all_low_cnt: %lld\n""sched_all_total_ms: %lld\n"
				"sched_all_total_cnt: %lld\n""sched_fg_max_ms: %lld\n"
				"sched_fg_high_cnt: %lld\n""sched_fg_low_cnt: %lld\n"
				"sched_fg_total_ms: %lld\n""sched_fg_total_cnt: %lld\n",
				oneplus_sched_para[type].ctrl ? "true" : "false",
				oneplus_sched_para[type].logon ? "true" : "false",
				oneplus_sched_para[type].trig ? "true" : "false",
				oneplus_sched_para[type].delta_ms,
				oneplus_sched_para[type].low_thresh_ms,
				oneplus_sched_para[type].high_thresh_ms,
				oneplus_sched_para[type].all.max_ms,
				oneplus_sched_para[type].all.high_cnt,
				oneplus_sched_para[type].all.low_cnt,
				oneplus_sched_para[type].all.total_ms,
				oneplus_sched_para[type].all.total_cnt,
				oneplus_sched_para[type].fg.max_ms,
				oneplus_sched_para[type].fg.high_cnt,
				oneplus_sched_para[type].fg.low_cnt,
				oneplus_sched_para[type].fg.total_ms,
				oneplus_sched_para[type].fg.total_cnt);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_sched_latency_fops = {
	.read = sched_latency_read,
};

static ssize_t iowait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;
	int type = OHM_SCHED_IOWAIT;

	len = sprintf(page,"iowait_ctrl: %s\n""iowait_logon: %s\n""iowait_trig: %s\n" \
				"iowait_delta_ms: %u\n""iowait_low_thresh_ms: %u\n""iowait_high_thresh_ms: %u\n" \
				"iowait_all_max_ms: %llu\n""iowait_all_high_cnt: %llu\n""iowait_all_low_cnt: %llu\n" \
				"iowait_all_total_ms: %llu\n""iowait_all_total_cnt: %llu\n" \
				"iowait_fg_max_ms: %llu\n""iowait_fg_high_cnt: %llu\n""iowait_fg_low_cnt: %llu\n" \
				"iowait_fg_total_ms: %llu\n""iowait_fg_total_cnt: %llu\n", \
				oneplus_sched_para[type].ctrl ? "true":"false", \
				oneplus_sched_para[type].logon ? "true":"false", \
				oneplus_sched_para[type].trig ? "true":"false", \
				oneplus_sched_para[type].delta_ms, \
				oneplus_sched_para[type].low_thresh_ms, \
				oneplus_sched_para[type].high_thresh_ms, \
				oneplus_sched_para[type].all.max_ms, \
				oneplus_sched_para[type].all.high_cnt, \
				oneplus_sched_para[type].all.low_cnt, \
				oneplus_sched_para[type].all.total_ms, \
				oneplus_sched_para[type].all.total_cnt, \
				oneplus_sched_para[type].fg.max_ms, \
				oneplus_sched_para[type].fg.high_cnt, \
				oneplus_sched_para[type].fg.low_cnt, \
				oneplus_sched_para[type].fg.total_ms, \
				oneplus_sched_para[type].fg.total_cnt);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;

	return (len < count ? len : count);
}

static const struct file_operations proc_iowait_fops = {
	.read = iowait_read,
};

static ssize_t fsync_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;
	int type = OHM_SCHED_FSYNC;

	len = sprintf(page, "fsync_ctrl: %s\n""fsync_logon: %s\n""fsync_trig: %s\n" \
				"fsync_delta_ms: %llu\n""fsync_low_thresh_ms: %u\n""fsync_high_thresh_ms: %u\n" \
				"fsync_all_max_ms: %llu\n""fsync_all_high_cnt: %llu\n""fsync_all_low_cnt: %llu\n" \
				"fsync_all_total_ms: %llu\n""fsync_all_total_cnt: %llu\n" \
				"fsync_fg_max_ms: %llu\n""fsync_fg_high_cnt: %llu\n""fsync_fg_low_cnt: %llu\n" \
				"fsync_fg_total_ms: %llu\n""fsync_fg_total_cnt: %llu\n", \
				oneplus_sched_para[type].ctrl ? "true":"false", \
				oneplus_sched_para[type].logon ? "true":"false", \
				oneplus_sched_para[type].trig ? "true":"false", \
				oneplus_sched_para[type].delta_ms, \
				oneplus_sched_para[type].low_thresh_ms, \
				oneplus_sched_para[type].high_thresh_ms, \
				oneplus_sched_para[type].all.max_ms, \
				oneplus_sched_para[type].all.high_cnt, \
				oneplus_sched_para[type].all.low_cnt, \
				oneplus_sched_para[type].all.total_ms, \
				oneplus_sched_para[type].all.total_cnt, \
				oneplus_sched_para[type].fg.max_ms, \
				oneplus_sched_para[type].fg.high_cnt, \
				oneplus_sched_para[type].fg.low_cnt, \
				oneplus_sched_para[type].fg.total_ms, \
				oneplus_sched_para[type].fg.total_cnt);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;

	return (len < count ? len : count);
}

static const struct file_operations proc_fsync_wait_fops = {
	.read = fsync_wait_read,
};

int ohm_flash_type = OHM_FLASH_TYPE_UFS;
static ssize_t emmcio_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	int len = 0;
	char *page = kzalloc(2048, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	//int type = OHM_SCHED_EMMCIO;
	len = sprintf(page, "emcdrv_iowait_low_thresh_ms: %d\n" //low thresh parameter
						"emcdrv_iowait_low_cnt: %lld\n"
						"emcdrv_iowait_high_thresh_ms: %d\n"
						"emcdrv_iowait_high_cnt: %lld\n"
						"emcdrv_iowait_total_ms: %lld\n"
						"flashio_total_latency: %lld\n"
						"blockio_total_latency: %lld\n"
						"emcdrv_iowait_total_cnt: %lld\n"
						"emcdrv_iowait_fg_low_cnt: %lld\n"
						"emcdrv_iowait_fg_high_cnt: %lld\n"
						"emcdrv_iowait_fg_total_ms: %lld\n"
						"emcdrv_iowait_fg_total_cnt: %lld\n"
						"emcdrv_iowait_fg_max_ms: %lld\n"
						"emcdrv_iowait_delta_ms: %lld\n"
						// fg
						"iosize_write_count_fg: %lld\n"
						"iosize_write_us_fg: %lld\n"
						"iosize_500ms_syncwrite_count_fg: %lld\n"
						"iosize_200ms_syncwrite_count_fg: %lld\n"
						"iosize_500ms_asyncwrite_count_fg: %lld\n"
						"iosize_200ms_asyncwrite_count_fg: %lld\n"
						"iosize_read_count_fg: %lld\n"
						"iosize_read_us_fg: %lld\n"
						"iosize_500ms_read_count_fg: %lld\n"
						"iosize_200ms_read_count_fg: %lld\n"
						//bg
						"iosize_write_count_bg: %lld\n"
						"iosize_write_us_bg: %lld\n"
						"iosize_2s_asyncwrite_count_bg: %lld\n"
						"iosize_500ms_asyncwrite_count_bg: %lld\n"
						"iosize_200ms_asyncwrite_count_bg: %lld\n"
						"iosize_2s_syncwrite_count_bg: %lld\n"
						"iosize_500ms_syncwrite_count_bg: %lld\n"
						"iosize_200ms_syncwrite_count_bg: %lld\n"
						"iosize_read_count_bg: %lld\n"
						"iosize_read_us_bg: %lld\n"
						"iosize_2s_read_count_bg: %lld\n"
						"iosize_500ms_read_count_bg: %lld\n"
						"iosize_200ms_read_count_bg: %lld\n"
						//4k
						"iosize_4k_read_count: %lld\n"
						"iosize_4k_read_ms: %lld\n"
						"iosize_4k_write_count: %lld\n"
						"iosize_4k_write_ms: %lld\n"
						// option
						"emcdrv_iowait_ctrl: %s\n"
						"emcdrv_iowait_logon: %s\n"
						"emcdrv_iowait_trig: %s\n",
				  oneplus_io_para.low_thresh_ms, //low thresh parameter
				  oneplus_io_para.low_cnt,
				  oneplus_io_para.high_thresh_ms,
				  oneplus_io_para.high_cnt,
				  (oneplus_io_para.total_us / 1000),
				  (oneplus_io_para.emmc_total_us / 1000),
				  (oneplus_io_para.total_us - oneplus_io_para.emmc_total_us) / 1000,
				  oneplus_io_para.total_cnt,
				  oneplus_io_para.fg_low_cnt,
				  oneplus_io_para.fg_high_cnt,
				  oneplus_io_para.fg_total_ms,
				  oneplus_io_para.fg_total_cnt,
				  oneplus_io_para.fg_max_delta_ms,
				  oneplus_io_para.delta_ms,
				  oneplus_io_para.iosize_write_count_fg,
				  oneplus_io_para.iosize_write_us_fg,
				  oneplus_io_para.iosize_500ms_syncwrite_count_fg,
				  oneplus_io_para.iosize_200ms_syncwrite_count_fg,
				  oneplus_io_para.iosize_500ms_asyncwrite_count_fg,
				  oneplus_io_para.iosize_200ms_asyncwrite_count_fg,
				  oneplus_io_para.iosize_read_count_fg,
				  oneplus_io_para.iosize_read_us_fg,
				  oneplus_io_para.iosize_500ms_read_count_fg,
				  oneplus_io_para.iosize_200ms_read_count_fg,
				  //bg
				  oneplus_io_para.iosize_write_count_bg,
				  oneplus_io_para.iosize_write_us_bg,
				  oneplus_io_para.iosize_2s_asyncwrite_count_bg,
				  oneplus_io_para.iosize_500ms_asyncwrite_count_bg,
				  oneplus_io_para.iosize_200ms_asyncwrite_count_bg,
				  oneplus_io_para.iosize_2s_syncwrite_count_bg,
				  oneplus_io_para.iosize_500ms_syncwrite_count_bg,
				  oneplus_io_para.iosize_200ms_syncwrite_count_bg,
				  oneplus_io_para.iosize_read_count_bg,
				  oneplus_io_para.iosize_read_us_bg,
				  oneplus_io_para.iosize_2s_read_count_bg,
				  oneplus_io_para.iosize_500ms_read_count_bg,
				  oneplus_io_para.iosize_200ms_read_count_bg,
				  //4k
				  oneplus_io_para.iosize_4k_read_count,
				  (oneplus_io_para.iosize_4k_read_us / 1000),
				  oneplus_io_para.iosize_4k_write_count,
				  (oneplus_io_para.iosize_4k_write_us / 1000),
				  oneplus_io_para.ctrl ? "true" : "false",
				  oneplus_io_para.logon ? "true" : "false",
				  oneplus_io_para.trig ? "true" : "false");

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		kfree(page);
		return -EFAULT;
	}
	kfree(page);
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_emmcio_fops = {
	.read = emmcio_read,
};

static inline ssize_t sched_data_to_user(char __user *buff, size_t count, loff_t *off, char *format_str, int len)
{
    if (len > *off) {
        len -= *off;
    } else {
        len = 0;
    }
    if (raw_copy_to_user(buff, format_str, (len < count ? len : count))) {
        return -EFAULT;
    }
    *off += len < count ? len : count;

    return (len < count ? len : count);
}


#define LATENCY_STRING_FORMAT(BUF, MODULE, SCHED_STAT) sprintf(BUF, \
        #MODULE"_ctrl: %s\n"#MODULE"_logon: %s\n"#MODULE"_trig: %s\n" \
        #MODULE"_delta_ms: %u\n"#MODULE"_low_thresh_ms: %u\n"#MODULE"_high_thresh_ms: %u\n" \
        #MODULE"_all_max_ms: %u\n"#MODULE"_all_high_cnt: %llu\n"#MODULE"_all_low_cnt: %llu\n" \
        #MODULE"_all_total_ms: %llu\n"#MODULE"_all_total_cnt: %llu\n" \
        #MODULE"_fg_max_ms: %u\n"#MODULE"_fg_high_cnt: %llu\n"#MODULE"_fg_low_cnt: %llu\n" \
        #MODULE"_fg_total_ms: %llu\n"#MODULE"_fg_total_cnt: %llu\n", \
        SCHED_STAT->ctrl ? "true":"false", \
        SCHED_STAT->logon ? "true":"false", \
        SCHED_STAT->trig ? "true":"false", \
        SCHED_STAT->delta_ms, \
        SCHED_STAT->low_thresh_ms, \
        SCHED_STAT->high_thresh_ms, \
        SCHED_STAT->all.max_ms, \
        SCHED_STAT->all.high_cnt, \
        SCHED_STAT->all.low_cnt, \
        SCHED_STAT->all.total_ms, \
        SCHED_STAT->all.total_cnt, \
        SCHED_STAT->fg.max_ms, \
        SCHED_STAT->fg.high_cnt, \
        SCHED_STAT->fg.low_cnt, \
        SCHED_STAT->fg.total_ms, \
        SCHED_STAT->fg.total_cnt)

static ssize_t dstate_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
        char page[BUFFER_SIZE_L] = {0};
        int len = 0;
        int type = OHM_SCHED_DSTATE;

	struct sched_stat_para *sched_stat = &oneplus_sched_para[type];

	len = LATENCY_STRING_FORMAT(page, dstate, sched_stat);
	return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_dstate_fops = {
       .read = dstate_read,
};

extern unsigned int cpufreq_quick_get_max(unsigned int cpu);
static ssize_t cpu_info_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
    char page[BUFFER_SIZE_L] = {0};
    int len = 0;
    unsigned int cpu;
    unsigned long scale_capacity = 0, last_capacity = 0;

    for_each_possible_cpu(cpu) {
        scale_capacity = arch_scale_cpu_capacity(cpu);
        if (scale_capacity == last_capacity) {
            continue;
        }
        last_capacity = scale_capacity;
        len += snprintf(page + len, sizeof(page) - len, "\ncpu%d  ""policy->max: %u  ""policy->cur: %u \n", cpu, cpufreq_quick_get_max(cpu), cpufreq_quick_get(cpu));
    }

    return sched_data_to_user(buff, count, off, page, len);
}

static const struct file_operations proc_cpu_info_fops = {
       .read = cpu_info_read,
};

#ifdef CONFIG_ONEPLUS_MEM_MONITOR
static ssize_t alloc_wait_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;

	len = sprintf(page, "total_alloc_wait_h_cnt: %lld\n""total_alloc_wait_l_cnt: %lld\n"
				"fg_alloc_wait_h_cnt: %lld\n""fg_alloc_wait_l_cnt: %lld\n"
				"total_alloc_wait_max_ms: %lld\n""total_alloc_wait_max_order: %lld\n"
				"fg_alloc_wait_max_ms: %lld\n""fg_alloc_wait_max_order: %lld\n"
				"alloc_wait_ctrl: %s\n""alloc_wait_logon: %s\n""alloc_wait_trig: %s\n",
				allocwait_para.total_alloc_wait_h_cnt, allocwait_para.total_alloc_wait_l_cnt,
				allocwait_para.fg_alloc_wait_h_cnt, allocwait_para.fg_alloc_wait_l_cnt,
				allocwait_para.total_alloc_wait_max_ms, allocwait_para.total_alloc_wait_max_order,
				allocwait_para.fg_alloc_wait_max_ms, allocwait_para.fg_alloc_wait_max_order,
				ohm_memmon_ctrl ? "true" : "false", ohm_memmon_logon ? "true":"false", ohm_memmon_trig ? "true":"false");

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_alloc_wait_fops = {
	.read = alloc_wait_read,
};
#endif /*CONFIG_ONEPLUS_MEM_MONITOR*/

static ssize_t ohm_para_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page, "action: %s\n""ctrl: 0x%08x\n""logon: 0x%08x\n""trig: 0x%08x\n",
					(ohm_action_ctrl ? "true":"false"), ohm_ctrl_list, ohm_logon_list, ohm_trig_list);

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t ohm_para_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
	char write_data[32] = {0};
	char ctrl_list[32] = {0};

	if (raw_copy_from_user(&write_data, buff, len)) {
		ohm_err("write error.\n");
		return -EFAULT;
	}
	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	if (0 == strncmp(write_data, "ohmctrl", 7)) {
		strncpy(ctrl_list, &write_data[7], OHM_INT_MAX);
		ctrl_list[OHM_INT_MAX] = '\0';
		ohm_ctrl_list = (int)simple_strtol(ctrl_list, NULL, 10);
		ohm_ctrl_init();
	} else if (0 == strncmp(write_data, "ohmlogon", 8)) {
		strncpy(ctrl_list, &write_data[8], OHM_INT_MAX);
		ctrl_list[OHM_INT_MAX] = '\0';
		ohm_logon_list = (int)simple_strtol(ctrl_list, NULL, 10);
		ohm_logon_init();
	} else if (0 == strncmp(write_data, "ohmtrig", 7)) {
		strncpy(ctrl_list, &write_data[7], OHM_INT_MAX);
		ctrl_list[OHM_INT_MAX] = '\0';
		ohm_trig_list = (int)simple_strtol(ctrl_list, NULL, 10);
		ohm_trig_init();
	} else if (0 == strncmp(write_data, "ohmparaupdate", 13)) {
		ohm_para_update();
		return len;
	} else {
		ohm_err("input illegal\n");
		return -EFAULT;
	}
	ohm_debug("write: %s, set: %s, ctrl: 0x%08x, logon: 0x%08x, trig: 0x%08x\n",
				write_data, ctrl_list, ohm_ctrl_list, ohm_logon_list, ohm_trig_list);
	return len;
}

static const struct file_operations proc_para_fops = {
	.read = ohm_para_read,
	.write = ohm_para_write,
};

unsigned int  iowait_hung_cnt;
unsigned int  iowait_panic_cnt;
static ssize_t iowait_hung_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[1024] = {0};
	int len = 0;

	len = sprintf(page, "iowait_hung_cnt: %u\n""iowait_panic_cnt: %u\n"
				"ohm_iopanic_mon_ctrl: %s\n""ohm_iopanic_mon_logon: %s\n""ohm_iopanic_mon_trig: %s\n",
				iowait_hung_cnt, iowait_panic_cnt,
				(ohm_iopanic_mon_ctrl ? "true" : "false"), (ohm_iopanic_mon_logon ? "true" : "false"), (ohm_iopanic_mon_trig ? "true" : "false"));

	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (raw_copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static const struct file_operations proc_iowait_hung_fops = {
	.read = iowait_hung_read,
};

#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
static int rw_overload_show(struct seq_file *s, void *v)
{
	struct task_struct *p;
	struct task_struct *group;
	u64 window_index = sample_window.window_index;
	u64 timestamp = sample_window.timestamp;
	u64 spead;
	u64 task_index;
	bool index = ODD(window_index);
	seq_printf(s, "window_index:%llu timestamp:%llu\n", window_index, timestamp);
	seq_printf(s, "%-10s\t%-10s\t%-16s\t%-16s\t%-8s\t%-16s\n", "TID", "TGID", "COMM", "spead", "r/w", "task_index");
	do_each_thread(group, p) {
		if (window_index != (p->tli[!index].task_sample_index + 1))
			continue;
		if (p->tli[!index].tli_overload_flag & TASK_WRITE_OVERLOAD_FLAG) {
			spead = p->tli[!index].write_bytes;
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-8s\t%-16llu\n", p->pid, p->tgid, p->comm, spead, "write", task_index);
		}
		if (p->tli[!index].tli_overload_flag & TASK_READ_OVERLOAD_FLAG) {
			spead = p->tli[!index].read_bytes;
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-8s\t%-16llu\n", p->pid, p->tgid, p->comm, spead, "read", task_index);
		}
	} while_each_thread(group, p);
	return 0;
}

static int rw_overload_open(struct inode *inode, struct file *file)
{
	return single_open(file, rw_overload_show, NULL);
}

static const struct file_operations proc_rw_overload_fops = {
	.open = rw_overload_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int runtime_overload_show(struct seq_file *s, void *v)
{
	struct task_struct *p;
	struct task_struct *group;
	u64 window_index = sample_window.window_index;
	u64 timestamp = sample_window.timestamp;
	u64 runtime;
	u64 task_index;
	u64 rt;
	bool index = ODD(window_index);
	seq_printf(s, "window_index:%llu timestamp:%llu\n", window_index, timestamp);
	seq_printf(s, "%-10s\t%-10s\t%-16s\t%-16s\t%-6s\t%-6s\t%-16s\n", "TID", "TGID", "COMM", "runtime", "FG/BG", "RT", "task_index");
	do_each_thread(group, p) {
		if (window_index != (p->tli[!index].task_sample_index + 1))
			continue;
		rt = p->tli[!index].tli_overload_flag & TASK_RT_THREAD_FLAG;
		if (p->tli[!index].tli_overload_flag & TASK_CPU_OVERLOAD_FG_FLAG) {
			runtime = p->tli[!index].runtime[1];
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-6s\t%-6s\t%-16llu\n", p->pid, p->tgid, p->comm, runtime, "FG", rt?"YES":"NO", task_index);
		}
		if (p->tli[!index].tli_overload_flag & TASK_CPU_OVERLOAD_BG_FLAG) {
			runtime = p->tli[!index].runtime[0];
			task_index = p->tli[!index].task_sample_index;
			seq_printf(s, "%-10d\t%-10d\t%-16s\t%-16llu\t%-6s\t%-6s\t%-16llu\n", p->pid, p->tgid, p->comm, runtime, "BG", rt?"YES":"NO", task_index);
		}
	} while_each_thread(group, p);
	return 0;
}

static int runtime_overload_open(struct inode *inode, struct file *file)
{
	return single_open(file, runtime_overload_show, NULL);
}

static const struct file_operations proc_runtime_overload_fops = {
	.open = runtime_overload_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif


#define HEALTHINFO_PROC_NODE "oneplus_healthinfo"
static struct proc_dir_entry *oneplus_healthinfo;

#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
static void adjust_window(struct timer_list * timer) {
	sample_window.timestamp = jiffies_64;
	sample_window.window_index++;
	mod_timer(&task_load_info_timer, jiffies + ohm_sample_time*HZ);  /* 5s */
}
#endif

static int __init oneplus_healthinfo_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;

	ohm_para_init();
	ohm_action_init();
	oneplus_healthinfo =  proc_mkdir(HEALTHINFO_PROC_NODE, NULL);
	if (!oneplus_healthinfo) {
		ohm_err("can't create oneplus_healthinfo proc\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("para_update", S_IRUGO | S_IWUGO, oneplus_healthinfo, &proc_para_fops);
	if (!pentry) {
		ohm_err("create healthinfo_switch proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("fsync_wait", S_IRUGO, oneplus_healthinfo, &proc_fsync_wait_fops);
	if (!pentry) {
		ohm_err("create fsync_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("cpu_loading", S_IRUGO, oneplus_healthinfo, &proc_cpu_load_fops);
	if (!pentry) {
		ohm_err("create cpu_loading proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("iowait", S_IRUGO, oneplus_healthinfo, &proc_iowait_fops);
	if (!pentry) {
		ohm_err("create iowait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("sched_latency", S_IRUGO, oneplus_healthinfo, &proc_sched_latency_fops);
	if (!pentry) {
		ohm_err("create sched_latency proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("emcdrv_iowait", S_IRUGO, oneplus_healthinfo, &proc_emmcio_fops);
	if (!pentry) {
		ohm_err("create emmc_driver_io_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("iowait_hung", S_IRUGO, oneplus_healthinfo, &proc_iowait_hung_fops);
	if (!pentry) {
		ohm_err("create iowait_hung proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("dstate", S_IRUGO, oneplus_healthinfo, &proc_dstate_fops);
	if (!pentry) {
                ohm_err("create dstate proc failed.\n");
                goto ERROR_INIT_VERSION;
        }

	pentry = proc_create("cpu_info", S_IRUGO, oneplus_healthinfo, &proc_cpu_info_fops);
    if(!pentry) {
        ohm_err("create cpu info proc failed.\n");
        goto ERROR_INIT_VERSION;
    }

#ifdef CONFIG_ONEPLUS_MEM_MONITOR
	pentry = proc_create("alloc_wait", S_IRUGO, oneplus_healthinfo, &proc_alloc_wait_fops);
	if (!pentry) {
		ohm_err("create alloc_wait proc failed.\n");
		goto ERROR_INIT_VERSION;
	}

#endif /*CONFIG_ONEPLUS_MEM_MONITOR*/
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
	sample_window.timestamp = jiffies;
	sample_window.window_index = 0;
	timer_setup(&task_load_info_timer, NULL, TIMER_DEFERRABLE);
	task_load_info_timer.function = &adjust_window;
	task_load_info_timer.expires = jiffies + ohm_sample_time*HZ;
	add_timer(&task_load_info_timer);

	pentry = proc_create("rw_overload", S_IRUGO, oneplus_healthinfo, &proc_rw_overload_fops);
	if (!pentry) {
		ohm_err("create rw_overload proc failed.\n");
		goto ERROR;
	}

	pentry = proc_create("runtime_overload", S_IRUGO, oneplus_healthinfo, &proc_runtime_overload_fops);
	if (!pentry) {
		ohm_err("create runtime_overload proc failed.\n");
		goto ERROR;
	}
#endif
	ohm_debug("Success \n");
	return ret;
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
ERROR:
	del_timer(&task_load_info_timer);
#endif
ERROR_INIT_VERSION:
	remove_proc_entry(HEALTHINFO_PROC_NODE, NULL);
	return -ENOENT;
}

module_init(oneplus_healthinfo_init);

module_param_named(ohm_action_ctrl, ohm_action_ctrl, bool, S_IRUGO | S_IWUSR);
module_param_named(ohm_iowait_l_ms, ohm_thresh_para[OHM_SCHED_IOWAIT].l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_iowait_h_ms, ohm_thresh_para[OHM_SCHED_IOWAIT].h_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_schedlatency_l_ms, ohm_thresh_para[OHM_SCHED_SCHEDLATENCY].l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_schedlatency_h_ms, ohm_thresh_para[OHM_SCHED_SCHEDLATENCY].h_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_fsync_l_ms, ohm_thresh_para[OHM_SCHED_FSYNC].l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_fsync_h_ms, ohm_thresh_para[OHM_SCHED_FSYNC].h_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_emmcio_l_ms, ohm_thresh_para[OHM_SCHED_EMMCIO].l_ms, int, S_IRUGO | S_IWUSR);
module_param_named(ohm_emmcio_h_ms, ohm_thresh_para[OHM_SCHED_EMMCIO].h_ms, int, S_IRUGO | S_IWUSR);
#ifdef CONFIG_ONEPLUS_TASKLOAD_INFO
module_param_named(ohm_write_thresh, ohm_write_thresh, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_read_thresh, ohm_read_thresh, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_runtime_thresh_fg, ohm_runtime_thresh_fg, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_runtime_thresh_bg, ohm_runtime_thresh_bg, ullong, S_IRUGO | S_IWUSR);
module_param_named(ohm_sample_time, ohm_sample_time, uint, S_IRUGO | S_IWUSR);
#endif

MODULE_DESCRIPTION("OnePlus healthinfo monitor");
MODULE_LICENSE("GPL v2");
