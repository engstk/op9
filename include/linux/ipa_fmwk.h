/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018 - 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _IPA_FMWK_H_
#define _IPA_FMWK_H_

#include <linux/types.h>
#include <linux/ipa.h>
#include <linux/ipa_uc_offload.h>
#include <linux/ipa_mhi.h>
#include <linux/ipa_wigig.h>
#include <linux/ipa_wdi3.h>
#include <linux/ipa_usb.h>
#include <linux/ipa_odu_bridge.h>
#include <linux/ipa_qmi_service_v01.h>
#include <linux/ipa_eth.h>

struct ipa_core_data {
	int (*ipa_tx_dp)(enum ipa_client_type dst, struct sk_buff *skb,
		struct ipa_tx_meta *metadata);

	enum ipa_hw_type (*ipa_get_hw_type)(void);

	int (*ipa_get_smmu_params)(struct ipa_smmu_in_params *in,
		struct ipa_smmu_out_params *out);

	int (*ipa_is_vlan_mode)(enum ipa_vlan_ifaces iface, bool *res);

	bool (*ipa_get_lan_rx_napi)(void);

	int (*ipa_dma_init)(void);

	int (*ipa_dma_enable)(void);

	int (*ipa_dma_disable)(void);

	int (*ipa_dma_sync_memcpy)(u64 dest, u64 src, int len);

	int (*ipa_dma_async_memcpy)(u64 dest, u64 src, int len,
		void (*user_cb)(void *user1), void *user_param);

	void (*ipa_dma_destroy)(void);

	int (*ipa_get_ep_mapping)(enum ipa_client_type client);

	int (*ipa_send_msg)(struct ipa_msg_meta *meta, void *buff,
		ipa_msg_free_fn callback);

	void (*ipa_free_skb)(struct ipa_rx_data *data);

	int (*ipa_setup_sys_pipe)(struct ipa_sys_connect_params *sys_in,
		u32 *clnt_hdl);

	int (*ipa_teardown_sys_pipe)(u32 clnt_hdl);

	int (*ipa_get_wdi_stats)(struct IpaHwStatsWDIInfoData_t *stats);

	int (*ipa_uc_bw_monitor)(struct ipa_wdi_bw_info *info);

	int (*ipa_broadcast_wdi_quota_reach_ind)(uint32_t fid,
		uint64_t num_bytes);

	int (*ipa_uc_wdi_get_dbpa)(struct ipa_wdi_db_params *out);

	int (*ipa_cfg_ep_ctrl)(u32 clnt_hdl,
		const struct ipa_ep_cfg_ctrl *ep_ctrl);

	int (*ipa_add_rt_rule)(struct ipa_ioc_add_rt_rule *rules);

	int (*ipa_put_rt_tbl)(u32 rt_tbl_hdl);

	int (*ipa_register_intf)(const char *name,
		const struct ipa_tx_intf *tx,
		const struct ipa_rx_intf *rx);

	int (*ipa_set_aggr_mode)(enum ipa_aggr_mode mode);

	int (*ipa_set_qcncm_ndp_sig)(char sig[3]);

	int (*ipa_set_single_ndp_per_mbim)(bool enable);

	int (*ipa_add_interrupt_handler)(enum ipa_irq_type interrupt,
		ipa_irq_handler_t handler,
		bool deferred_flag,
		void *private_data);

	int (*ipa_restore_suspend_handler)(void);

	const struct ipa_gsi_ep_config *(*ipa_get_gsi_ep_info)(
		enum ipa_client_type client);

	int (*ipa_stop_gsi_channel)(u32 clnt_hdl);

	int (*ipa_rmnet_ctl_xmit)(struct sk_buff *skb);

	int (*ipa_register_rmnet_ctl_cb)(
		void (*ipa_rmnet_ctl_ready_cb)(void *user_data1),
		void *user_data1,
		void (*ipa_rmnet_ctl_stop_cb)(void *user_data2),
		void *user_data2,
		void (*ipa_rmnet_ctl_rx_notify_cb)(
			void *user_data3, void *rx_data),
		void *user_data3);

	int (*ipa_unregister_rmnet_ctl_cb)(void);
};

struct ipa_usb_data {
	int (*ipa_usb_init_teth_prot)(enum ipa_usb_teth_prot teth_prot,
		struct ipa_usb_teth_params *teth_params,
		int (*ipa_usb_notify_cb)(enum ipa_usb_notify_event,
			void *),
		void *user_data);

	int (*ipa_usb_xdci_connect)(
		struct ipa_usb_xdci_chan_params *ul_chan_params,
		struct ipa_usb_xdci_chan_params *dl_chan_params,
		struct ipa_req_chan_out_params *ul_out_params,
		struct ipa_req_chan_out_params *dl_out_params,
		struct ipa_usb_xdci_connect_params *connect_params);

	int (*ipa_usb_xdci_disconnect)(u32 ul_clnt_hdl, u32 dl_clnt_hdl,
		enum ipa_usb_teth_prot teth_prot);

	int (*ipa_usb_deinit_teth_prot)(enum ipa_usb_teth_prot teth_prot);

	int (*ipa_usb_xdci_suspend)(u32 ul_clnt_hdl, u32 dl_clnt_hdl,
		enum ipa_usb_teth_prot teth_prot,
		bool with_remote_wakeup);

	int (*ipa_usb_xdci_resume)(u32 ul_clnt_hdl, u32 dl_clnt_hdl,
		enum ipa_usb_teth_prot teth_prot);
};

struct ipa_wdi3_data {
	int (*ipa_wdi_init)(struct ipa_wdi_init_in_params *in,
		struct ipa_wdi_init_out_params *out);

	int (*ipa_wdi_cleanup)(void);

	int (*ipa_wdi_reg_intf)(
		struct ipa_wdi_reg_intf_in_params *in);

	int (*ipa_wdi_dereg_intf)(const char *netdev_name);

	int (*ipa_wdi_conn_pipes)(struct ipa_wdi_conn_in_params *in,
		struct ipa_wdi_conn_out_params *out);

	int (*ipa_wdi_disconn_pipes)(void);

	int (*ipa_wdi_enable_pipes)(void);

	int (*ipa_wdi_disable_pipes)(void);

	int (*ipa_wdi_set_perf_profile)(struct ipa_wdi_perf_profile *profile);

	int (*ipa_wdi_create_smmu_mapping)(u32 num_buffers,
		struct ipa_wdi_buffer_info *info);

	int (*ipa_wdi_release_smmu_mapping)(u32 num_buffers,
		struct ipa_wdi_buffer_info *info);

	int (*ipa_wdi_get_stats)(struct IpaHwStatsWDIInfoData_t *stats);

	int (*ipa_wdi_bw_monitor)(struct ipa_wdi_bw_info *info);

	int (*ipa_wdi_sw_stats)(struct ipa_wdi_tx_info *info);

	int (*ipa_get_wdi_version)(void);
};

struct ipa_gsb_data {
	int (*ipa_bridge_init)(struct ipa_bridge_init_params *params, u32 *hdl);

	int (*ipa_bridge_connect)(u32 hdl);

	int (*ipa_bridge_set_perf_profile)(u32 hdl, u32 bandwidth);

	int (*ipa_bridge_disconnect)(u32 hdl);

	int (*ipa_bridge_suspend)(u32 hdl);

	int (*ipa_bridge_resume)(u32 hdl);

	int (*ipa_bridge_tx_dp)(u32 hdl, struct sk_buff *skb,
		struct ipa_tx_meta *metadata);

	int (*ipa_bridge_cleanup)(u32 hdl);
};

struct ipa_uc_offload_data {
	int (*ipa_uc_offload_reg_intf)(
		struct ipa_uc_offload_intf_params *in,
		struct ipa_uc_offload_out_params *out);

	int (*ipa_uc_offload_cleanup)(u32 clnt_hdl);

	int (*ipa_uc_offload_conn_pipes)(
		struct ipa_uc_offload_conn_in_params *in,
		struct ipa_uc_offload_conn_out_params *out);

	int (*ipa_uc_offload_disconn_pipes)(u32 clnt_hdl);

	int (*ipa_set_perf_profile)(struct ipa_perf_profile *profile);

	int (*ipa_uc_offload_reg_rdyCB)(struct ipa_uc_ready_params *param);

	void (*ipa_uc_offload_dereg_rdyCB)(enum ipa_uc_offload_proto proto);
};

struct ipa_mhi_data {
	int (*ipa_mhi_init)(struct ipa_mhi_init_params *params);

	int (*ipa_mhi_start)(struct ipa_mhi_start_params *params);

	int (*ipa_mhi_connect_pipe)(struct ipa_mhi_connect_params *in,
		u32 *clnt_hdl);

	int (*ipa_mhi_disconnect_pipe)(u32 clnt_hdl);

	int (*ipa_mhi_suspend)(bool force);

	int (*ipa_mhi_resume)(void);

	void (*ipa_mhi_destroy)(void);

	int (*ipa_mhi_handle_ipa_config_req)(
		struct ipa_config_req_msg_v01 *config_req);

	int (*ipa_mhi_update_mstate)(enum ipa_mhi_mstate mstate_info);
};

struct ipa_wigig_data {
	int (*ipa_wigig_init)(struct ipa_wigig_init_in_params *in,
	struct ipa_wigig_init_out_params *out);

	int (*ipa_wigig_cleanup)(void);

	bool (*ipa_wigig_is_smmu_enabled)(void);

	int (*ipa_wigig_reg_intf)(struct ipa_wigig_reg_intf_in_params *in);

	int (*ipa_wigig_dereg_intf)(const char *netdev_name);

	int (*ipa_wigig_conn_rx_pipe)(struct ipa_wigig_conn_rx_in_params *in,
		struct ipa_wigig_conn_out_params *out);

	int (*ipa_wigig_conn_rx_pipe_smmu)(
		struct ipa_wigig_conn_rx_in_params_smmu *in,
		struct ipa_wigig_conn_out_params *out);

	int (*ipa_wigig_conn_client)(struct ipa_wigig_conn_tx_in_params *in,
		struct ipa_wigig_conn_out_params *out);

	int (*ipa_wigig_conn_client_smmu)(
		struct ipa_wigig_conn_tx_in_params_smmu *in,
		struct ipa_wigig_conn_out_params *out);

	int (*ipa_wigig_disconn_pipe)(enum ipa_client_type client);

	int (*ipa_wigig_enable_pipe)(enum ipa_client_type client);

	int (*ipa_wigig_disable_pipe)(enum ipa_client_type client);

	int (*ipa_wigig_tx_dp)(enum ipa_client_type dst, struct sk_buff *skb);

	int (*ipa_wigig_set_perf_profile)(u32 max_supported_bw_mbps);

	int (*ipa_wigig_save_regs)(void);
};

struct ipa_eth_data {
	int (*ipa_eth_register_ready_cb)(struct ipa_eth_ready *ready_info);

	int (*ipa_eth_unregister_ready_cb)(struct ipa_eth_ready *ready_info);

	int (*ipa_eth_client_conn_pipes)(struct ipa_eth_client *client);

	int (*ipa_eth_client_disconn_pipes)(struct ipa_eth_client *client);

	int (*ipa_eth_client_reg_intf)(struct ipa_eth_intf_info *intf);

	int (*ipa_eth_client_unreg_intf)(struct ipa_eth_intf_info *intf);

	int (*ipa_eth_client_set_perf_profile)(struct ipa_eth_client *client,
		struct ipa_eth_perf_profile *profile);

	int (*ipa_eth_client_conn_evt)(struct ipa_ecm_msg *msg);

	int (*ipa_eth_client_disconn_evt)(struct ipa_ecm_msg *msg);
};

#if IS_ENABLED(CONFIG_IPA3)

int ipa_fmwk_register_ipa(const struct ipa_core_data *in);

int ipa_fmwk_register_ipa_usb(const struct ipa_usb_data *in);

int ipa_fmwk_register_ipa_wdi3(const struct ipa_wdi3_data *in);

int ipa_fmwk_register_gsb(const struct ipa_gsb_data *in);

int ipa_fmwk_register_uc_offload(const struct ipa_uc_offload_data *in);

int ipa_fmwk_register_ipa_mhi(const struct ipa_mhi_data *in);

int ipa_fmwk_register_ipa_wigig(const struct ipa_wigig_data *in);

int ipa_fmwk_register_ipa_eth(const struct ipa_eth_data *in);

#else /* IS_ENABLED(CONFIG_IPA3) */

int ipa_fmwk_register_ipa(const struct ipa_core_data *in)
{
	return -EPERM;
}

int ipa_fmwk_register_ipa_usb(const struct ipa_usb_data *in)
{
	return -EPERM;
}

int ipa_fmwk_register_ipa_wdi3(const struct ipa_wdi3_data *in)
{
	return -EPERM;
}

int ipa_fmwk_register_gsb(const struct ipa_gsb_data *in)
{
	return -EPERM;
}

int ipa_fmwk_register_uc_offload(const struct ipa_uc_offload_data *in)
{
	return -EPERM;
}

int ipa_fmwk_register_ipa_mhi(const struct ipa_mhi_data *in)
{
	return -EPERM;
}

int ipa_fmwk_register_ipa_wigig(const struct ipa_wigig_data *in)
{
	return -EPERM;
}

int ipa_fmwk_register_ipa_eth(const struct ipa_eth_data *in)
{
	return -EPERM;
}

#endif /* IS_ENABLED(CONFIG_IPA3) */

#endif /* _IPA_FMWK_H_ */
