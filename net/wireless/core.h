/*
 * Wireless configuration interface internals.
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 */
#ifndef __NET_WIRELESS_CORE_H
#define __NET_WIRELESS_CORE_H
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/kref.h>
#include <linux/rbtree.h>
#include <linux/debugfs.h>
#include <linux/rfkill.h>
#include <linux/workqueue.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include "reg.h"

struct cfg80211_registered_device {
	const struct cfg80211_ops *ops;
	struct list_head list;
	struct mutex mtx;

	
	struct rfkill_ops rfkill_ops;
	struct rfkill *rfkill;
	struct work_struct rfkill_sync;

	char country_ie_alpha2[2];

	enum environment_cap env;

	
	int wiphy_idx;

	
	struct mutex devlist_mtx;
	
	struct list_head netdev_list;
	int devlist_generation;
	int opencount; 
	wait_queue_head_t dev_wait;

	u32 ap_beacons_nlpid;

	
	spinlock_t bss_lock;
	struct list_head bss_list;
	struct rb_root bss_tree;
	u32 bss_generation;
	struct cfg80211_scan_request *scan_req; 
	struct cfg80211_sched_scan_request *sched_scan_req;
	unsigned long suspend_at;
	struct work_struct scan_done_wk;
	struct work_struct sched_scan_results_wk;

	struct mutex sched_scan_mtx;

#ifdef CONFIG_NL80211_TESTMODE
	struct genl_info *testmode_info;
#endif

	struct work_struct conn_work;
	struct work_struct event_work;

	struct cfg80211_wowlan *wowlan;

	struct wiphy wiphy __attribute__((__aligned__(NETDEV_ALIGN)));
};

static inline
struct cfg80211_registered_device *wiphy_to_dev(struct wiphy *wiphy)
{
	BUG_ON(!wiphy);
	return container_of(wiphy, struct cfg80211_registered_device, wiphy);
}

static inline
bool wiphy_idx_valid(int wiphy_idx)
{
	return wiphy_idx >= 0;
}

static inline void
cfg80211_rdev_free_wowlan(struct cfg80211_registered_device *rdev)
{
	int i;

	if (!rdev->wowlan)
		return;
	for (i = 0; i < rdev->wowlan->n_patterns; i++)
		kfree(rdev->wowlan->patterns[i].mask);
	kfree(rdev->wowlan->patterns);
	kfree(rdev->wowlan);
}

extern struct workqueue_struct *cfg80211_wq;
extern struct mutex cfg80211_mutex;
extern struct list_head cfg80211_rdev_list;
extern int cfg80211_rdev_list_generation;

static inline void assert_cfg80211_lock(void)
{
	lockdep_assert_held(&cfg80211_mutex);
}

#define WIPHY_IDX_STALE -1

struct cfg80211_internal_bss {
	struct list_head list;
	struct rb_node rbn;
	unsigned long ts;
	struct kref ref;
	atomic_t hold;
	bool beacon_ies_allocated;
	bool proberesp_ies_allocated;

	
	struct cfg80211_bss pub;
};

static inline struct cfg80211_internal_bss *bss_from_pub(struct cfg80211_bss *pub)
{
	return container_of(pub, struct cfg80211_internal_bss, pub);
}

static inline void cfg80211_hold_bss(struct cfg80211_internal_bss *bss)
{
	atomic_inc(&bss->hold);
}

static inline void cfg80211_unhold_bss(struct cfg80211_internal_bss *bss)
{
	int r = atomic_dec_return(&bss->hold);
	WARN_ON(r < 0);
}


struct cfg80211_registered_device *cfg80211_rdev_by_wiphy_idx(int wiphy_idx);
int get_wiphy_idx(struct wiphy *wiphy);

struct cfg80211_registered_device *
__cfg80211_rdev_from_info(struct genl_info *info);

extern struct cfg80211_registered_device *
cfg80211_get_dev_from_info(struct genl_info *info);

struct wiphy *wiphy_idx_to_wiphy(int wiphy_idx);

extern struct cfg80211_registered_device *
cfg80211_get_dev_from_ifindex(struct net *net, int ifindex);

int cfg80211_switch_netns(struct cfg80211_registered_device *rdev,
			  struct net *net);

static inline void cfg80211_lock_rdev(struct cfg80211_registered_device *rdev)
{
	mutex_lock(&rdev->mtx);
}

static inline void cfg80211_unlock_rdev(struct cfg80211_registered_device *rdev)
{
	BUG_ON(IS_ERR(rdev) || !rdev);
	mutex_unlock(&rdev->mtx);
}

static inline void wdev_lock(struct wireless_dev *wdev)
	__acquires(wdev)
{
	mutex_lock(&wdev->mtx);
	__acquire(wdev->mtx);
}

static inline void wdev_unlock(struct wireless_dev *wdev)
	__releases(wdev)
{
	__release(wdev->mtx);
	mutex_unlock(&wdev->mtx);
}

#define ASSERT_RDEV_LOCK(rdev) lockdep_assert_held(&(rdev)->mtx)
#define ASSERT_WDEV_LOCK(wdev) lockdep_assert_held(&(wdev)->mtx)

enum cfg80211_event_type {
	EVENT_CONNECT_RESULT,
	EVENT_ROAMED,
	EVENT_DISCONNECTED,
	EVENT_IBSS_JOINED,
};

struct cfg80211_event {
	struct list_head list;
	enum cfg80211_event_type type;

	union {
		struct {
			u8 bssid[ETH_ALEN];
			const u8 *req_ie;
			const u8 *resp_ie;
			size_t req_ie_len;
			size_t resp_ie_len;
			u16 status;
		} cr;
		struct {
			const u8 *req_ie;
			const u8 *resp_ie;
			size_t req_ie_len;
			size_t resp_ie_len;
			struct cfg80211_bss *bss;
		} rm;
		struct {
			const u8 *ie;
			size_t ie_len;
			u16 reason;
		} dc;
		struct {
			u8 bssid[ETH_ALEN];
		} ij;
	};
};

struct cfg80211_cached_keys {
	struct key_params params[6];
	u8 data[6][WLAN_MAX_KEY_LEN];
	int def, defmgmt;
};


extern void cfg80211_dev_free(struct cfg80211_registered_device *rdev);

extern int cfg80211_dev_rename(struct cfg80211_registered_device *rdev,
			       char *newname);

void ieee80211_set_bitrate_flags(struct wiphy *wiphy);

void cfg80211_bss_expire(struct cfg80211_registered_device *dev);
void cfg80211_bss_age(struct cfg80211_registered_device *dev,
                      unsigned long age_secs);

int __cfg80211_join_ibss(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct cfg80211_ibss_params *params,
			 struct cfg80211_cached_keys *connkeys);
int cfg80211_join_ibss(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       struct cfg80211_ibss_params *params,
		       struct cfg80211_cached_keys *connkeys);
void cfg80211_clear_ibss(struct net_device *dev, bool nowext);
int __cfg80211_leave_ibss(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, bool nowext);
int cfg80211_leave_ibss(struct cfg80211_registered_device *rdev,
			struct net_device *dev, bool nowext);
void __cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid);
int cfg80211_ibss_wext_join(struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev);

extern const struct mesh_config default_mesh_config;
extern const struct mesh_setup default_mesh_setup;
int __cfg80211_join_mesh(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 const struct mesh_setup *setup,
			 const struct mesh_config *conf);
int cfg80211_join_mesh(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       const struct mesh_setup *setup,
		       const struct mesh_config *conf);
int cfg80211_leave_mesh(struct cfg80211_registered_device *rdev,
			struct net_device *dev);

int __cfg80211_mlme_auth(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct ieee80211_channel *chan,
			 enum nl80211_auth_type auth_type,
			 const u8 *bssid,
			 const u8 *ssid, int ssid_len,
			 const u8 *ie, int ie_len,
			 const u8 *key, int key_len, int key_idx);
int cfg80211_mlme_auth(struct cfg80211_registered_device *rdev,
		       struct net_device *dev, struct ieee80211_channel *chan,
		       enum nl80211_auth_type auth_type, const u8 *bssid,
		       const u8 *ssid, int ssid_len,
		       const u8 *ie, int ie_len,
		       const u8 *key, int key_len, int key_idx);
int __cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			  struct net_device *dev,
			  struct ieee80211_channel *chan,
			  const u8 *bssid, const u8 *prev_bssid,
			  const u8 *ssid, int ssid_len,
			  const u8 *ie, int ie_len, bool use_mfp,
			  struct cfg80211_crypto_settings *crypt,
			  u32 assoc_flags, struct ieee80211_ht_cap *ht_capa,
			  struct ieee80211_ht_cap *ht_capa_mask);
int cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			struct net_device *dev, struct ieee80211_channel *chan,
			const u8 *bssid, const u8 *prev_bssid,
			const u8 *ssid, int ssid_len,
			const u8 *ie, int ie_len, bool use_mfp,
			struct cfg80211_crypto_settings *crypt,
			u32 assoc_flags, struct ieee80211_ht_cap *ht_capa,
			struct ieee80211_ht_cap *ht_capa_mask);
int __cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *bssid,
			   const u8 *ie, int ie_len, u16 reason,
			   bool local_state_change);
int cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, const u8 *bssid,
			 const u8 *ie, int ie_len, u16 reason,
			 bool local_state_change);
int cfg80211_mlme_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *bssid,
			   const u8 *ie, int ie_len, u16 reason,
			   bool local_state_change);
void cfg80211_mlme_down(struct cfg80211_registered_device *rdev,
			struct net_device *dev);
void __cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			       const u8 *req_ie, size_t req_ie_len,
			       const u8 *resp_ie, size_t resp_ie_len,
			       u16 status, bool wextev,
			       struct cfg80211_bss *bss);
int cfg80211_mlme_register_mgmt(struct wireless_dev *wdev, u32 snd_pid,
				u16 frame_type, const u8 *match_data,
				int match_len);
void cfg80211_mlme_unregister_socket(struct wireless_dev *wdev, u32 nlpid);
void cfg80211_mlme_purge_registrations(struct wireless_dev *wdev);
int cfg80211_mlme_mgmt_tx(struct cfg80211_registered_device *rdev,
			  struct net_device *dev,
			  struct ieee80211_channel *chan, bool offchan,
			  enum nl80211_channel_type channel_type,
			  bool channel_type_valid, unsigned int wait,
			  const u8 *buf, size_t len, bool no_cck,
			  bool dont_wait_for_ack, u64 *cookie);
void cfg80211_oper_and_ht_capa(struct ieee80211_ht_cap *ht_capa,
			       const struct ieee80211_ht_cap *ht_capa_mask);

int __cfg80211_connect(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       struct cfg80211_connect_params *connect,
		       struct cfg80211_cached_keys *connkeys,
		       const u8 *prev_bssid);
int cfg80211_connect(struct cfg80211_registered_device *rdev,
		     struct net_device *dev,
		     struct cfg80211_connect_params *connect,
		     struct cfg80211_cached_keys *connkeys);
int __cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, u16 reason,
			  bool wextev);
int cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u16 reason,
			bool wextev);
void __cfg80211_roamed(struct wireless_dev *wdev,
		       struct cfg80211_bss *bss,
		       const u8 *req_ie, size_t req_ie_len,
		       const u8 *resp_ie, size_t resp_ie_len);
int cfg80211_mgd_wext_connect(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev);

void cfg80211_conn_work(struct work_struct *work);
void cfg80211_sme_failed_assoc(struct wireless_dev *wdev);
bool cfg80211_sme_failed_reassoc(struct wireless_dev *wdev);

bool cfg80211_supported_cipher_suite(struct wiphy *wiphy, u32 cipher);
int cfg80211_validate_key_settings(struct cfg80211_registered_device *rdev,
				   struct key_params *params, int key_idx,
				   bool pairwise, const u8 *mac_addr);
void __cfg80211_disconnected(struct net_device *dev, const u8 *ie,
			     size_t ie_len, u16 reason, bool from_ap);
void cfg80211_sme_scan_done(struct net_device *dev);
void cfg80211_sme_rx_auth(struct net_device *dev, const u8 *buf, size_t len);
void cfg80211_sme_disassoc(struct net_device *dev,
			   struct cfg80211_internal_bss *bss);
void __cfg80211_scan_done(struct work_struct *wk);
void ___cfg80211_scan_done(struct cfg80211_registered_device *rdev, bool leak);
void __cfg80211_sched_scan_results(struct work_struct *wk);
int __cfg80211_stop_sched_scan(struct cfg80211_registered_device *rdev,
			       bool driver_initiated);
void cfg80211_upload_connect_keys(struct wireless_dev *wdev);
int cfg80211_change_iface(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, enum nl80211_iftype ntype,
			  u32 *flags, struct vif_params *params);
void cfg80211_process_rdev_events(struct cfg80211_registered_device *rdev);
void cfg80211_process_wdev_events(struct wireless_dev *wdev);

int cfg80211_can_change_interface(struct cfg80211_registered_device *rdev,
				  struct wireless_dev *wdev,
				  enum nl80211_iftype iftype);

static inline int
cfg80211_can_add_interface(struct cfg80211_registered_device *rdev,
			   enum nl80211_iftype iftype)
{
	return cfg80211_can_change_interface(rdev, NULL, iftype);
}

struct ieee80211_channel *
rdev_freq_to_chan(struct cfg80211_registered_device *rdev,
		  int freq, enum nl80211_channel_type channel_type);
int cfg80211_set_freq(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev, int freq,
		      enum nl80211_channel_type channel_type);

u16 cfg80211_calculate_bitrate(struct rate_info *rate);
int ieee80211_get_ratemask(struct ieee80211_supported_band *sband,
			   const u8 *rates, unsigned int n_rates,
			   u32 *mask);

int ieee80211_get_ratemask(struct ieee80211_supported_band *sband,
			   const u8 *rates, unsigned int n_rates,
			   u32 *mask);

int cfg80211_validate_beacon_int(struct cfg80211_registered_device *rdev,
				 u32 beacon_int);

#ifdef CONFIG_CFG80211_DEVELOPER_WARNINGS
#define CFG80211_DEV_WARN_ON(cond)	WARN_ON(cond)
#else
#define CFG80211_DEV_WARN_ON(cond)	({bool __r = (cond); __r; })
#endif

#endif 
