/*-
 * Copyright (c) 2005 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: ieee80211_scan.h 2366 2007-05-23 08:43:05Z mrenzmann $
 */
#ifndef _NET80211_IEEE80211_SCAN_H_
#define _NET80211_IEEE80211_SCAN_H_

#include <net/iw_handler.h>
#define	IEEE80211_SCAN_MAX	IEEE80211_CHAN_MAX

struct ieee80211_scanner;

struct ieee80211_scan_ssid {
	int len;					/* length in bytes */
	u_int8_t ssid[IEEE80211_NWID_LEN];	/* ssid contents */
};
#define	IEEE80211_SCAN_MAX_SSID	1

struct ieee80211_scan_ssid_list {
	int len;
	u_int8_t ssid[IEEE80211_NWID_LEN];
	struct ieee80211_scan_ssid_list *next;
};

struct ieee80211_chan_phy_stats {
	uint8_t updated;
	uint8_t bw;
	uint8_t isbsschan;
	uint8_t ispassive;
	uint32_t cca_try;
	uint32_t cca_tx;
	uint32_t cca_pri;
	uint32_t cca_sec20;
	uint32_t cca_sec40;
	uint32_t cca_pri_intf;
	uint32_t cca_sec20_intf;
	uint32_t cca_sec40_intf;
	uint32_t sample_duration;
	int32_t  hw_noise;
	unsigned long sample_jiffies;
};

struct ieee80211_scan_state {
	struct ieee80211vap *ss_vap;
	const struct ieee80211_scanner *ss_ops;	/* policy hookup, see below */
	void *ss_priv;				/* scanner private state */
	void *ss_scs_priv;			/* scs private state independent of the scan state */
	uint8_t scan_addr[IEEE80211_ADDR_LEN];
	u_int16_t ss_flags;
#define	IEEE80211_SCAN_NOPICK			0x0001	/* scan only, no selection */
#define	IEEE80211_SCAN_ACTIVE			0x0002	/* active scan (probe req) */
#define	IEEE80211_SCAN_PICK1ST			0x0004	/* ``hey sailor'' mode */
#define	IEEE80211_SCAN_BGSCAN			0x0008	/* bg scan, exit ps at end */
#define	IEEE80211_SCAN_ONCE			0x0010	/* do one complete pass */
#define	IEEE80211_SCAN_NO_DFS			0x0020	/* avoid DFS channels, AP only */
#define IEEE80211_SCAN_DFS_ACTION		0x0040	/* scan end, do DFS action */
#define IEEE80211_SCAN_QTN_BGSCAN		0x0080	/* quantenna background scanning required */
#define IEEE80211_SCAN_OPCHAN			0x0100	/* quantenna scanning on operating channel only */
#define IEEE80211_SCAN_QTN_SEARCH_MBS		0x0200	/* seach MBS, no action on scan end */
#define IEEE80211_SCAN_OBSS			0x0400	/* OBSS scan */
#define	IEEE80211_SCAN_ACTIVE_RANDOM		0x0800	/* active scan random macaddr & seq (probe req) */
/* Note: top 4 bits for internal use only */
#define	IEEE80211_SCAN_GOTPICK			0x1000	/* got candidate, can stop */
/*
 * ss_flags is used out,so add new ss_ext_flags,for the up layer scan_flags
 * lower 16 bit is used, so it start from 0x00010000
 */
	u_int32_t ss_ext_flags;
#define IEEE80211_SCAN_EXT_FLAGS_MASK		0xffff0000
#define	IEEE80211_SCAN_BGSCAN_CHECK_TRAFFIC	0x00010000  /* background scan with check traffic */

	u_int16_t ss_pick_flags;			/* pick a channel via a algorithm in a special domain */

  /*
   * Be aware only the lower 12 bits actually make it to ss_flags from
   * the flags parameter to ieee80211_start_scan.
   */
	u_int8_t	ss_nssid;			/* # ssid's to probe/match */
	struct ieee80211_scan_ssid ss_ssid[IEEE80211_SCAN_MAX_SSID];
						/* ssid's to probe/match */
						/* ordered channel set */
	struct ieee80211_channel *ss_chans[IEEE80211_SCAN_MAX];
	u_int16_t ss_next;			/* ix of next chan to scan */
	u_int16_t ss_last;			/* ix + 1 of last chan to scan */
	u_int16_t ss_scan_bw;			/* scan bandwidth */
	unsigned long ss_mindwell;		/* min dwell on channel */
	unsigned long ss_mindwell_passive;	/* min dwell time on a passive channel */
	unsigned long ss_maxdwell;		/* max dwell on channel */
	unsigned long ss_maxdwell_passive;	/* max dwell time on a passive channel */
	/* Active dwell time provided by an entity requesting a scan */
	unsigned int ss_dwell_active_override;
	/* Passive dwell time provided by an entity requesting a scan */
	unsigned int ss_dwell_passive_override;
	/* Single channel sample duration provided by an entity requesting a scan */
	unsigned int ss_sample_duration_override;
	u_int ss_duration;			/* used for calling ieee80211_start_scan() */
	u_int8_t is_scan_valid;			/* was the channel scan initiated by supplicant */
	uint8_t ss_bssid[IEEE80211_ADDR_LEN];	/* scan required BSSID */
	struct ieee80211_chan_phy_stats *phy_stats[IEEE80211_SCAN_MAX];
};

/*
 * The upper 16 bits of the flags word is used to communicate
 * information to the scanning code that is NOT recorded in
 * ss_flags.  It might be better to split this stuff out into
 * a separate variable to avoid confusion.
 */
#define	IEEE80211_SCAN_FLUSH	0x00010000		/* flush candidate table */
#define	IEEE80211_SCAN_NOSSID	0x00020000		/* don't update ssid list */
#define	IEEE80211_SCAN_USECACHE	0x00040000		/* Must use a result from the cache */
#define	IEEE80211_SCAN_KEEPMODE	0x00080000		/* Must keep the same wireless mode (11a, 11g, or 11at, etc) */
#define	IEEE80211_SCAN_BW40	0x00100000		/* Set scan bandwidth as 40M */
#define	IEEE80211_SCAN_BW80	0x00200000		/* Set scan bandwidth as 80M */
#define	IEEE80211_SCAN_BW160	0x00400000		/* Set scan bandwidth as 160M */
#define	IEEE80211_SCAN_BW20	0x00800000		/* Set scan bandwidth as 20M */


/*
 * Parameters for managing cache entries:
 *
 * o a station with STA_FAILS_MAX failures is not considered
 *   when picking a candidate
 * o a station that hasn't had an update in STA_PURGE_SCANS
 *   (background) scans is discarded
 * o after STA_FAILS_AGE seconds we clear the failure count
 */
#define	STA_FAILS_MAX	2		/* assoc failures before ignored */
#define	STA_FAILS_AGE	(2 * 60)	/* time before clearing fails (secs) */
#define	STA_PURGE_SCANS	2		/* age for purging sta entries (scans) */
#define	AP_PURGE_SCANS	50		/* age for purging ap entries (scans) */
#define	AP_PURGE_SCS	60		/* age for purging ap entries (SCS) */

#define RSSI_LPF_LEN	10
#define	RSSI_EP_MULTIPLIER	(1<<7)	/* pow2 to optimize out * and / */
#define RSSI_IN(x)		((x) * RSSI_EP_MULTIPLIER)
#define LPF_RSSI(x, y, len)	(((x) * ((len) - 1) + (y)) / (len))
#define RSSI_LPF(x, y) do {						\
    if ((y) >= -20)							\
	    x = LPF_RSSI((x), RSSI_IN((y)), RSSI_LPF_LEN);		\
} while (0)
#define	EP_RND(x, mul) \
	((((x)%(mul)) >= ((mul)/2)) ? howmany(x, mul) : (x)/(mul))
#define	RSSI_GET(x)	EP_RND(x, RSSI_EP_MULTIPLIER)
#define	ISPROBE(_st)	((_st) == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
#define	STA_HASHSIZE	32
/* simple hash is enough for variation of macaddr */
#define	STA_HASH(addr)	\
	(((const u_int8_t *)(addr))[IEEE80211_ADDR_LEN - 1] % STA_HASHSIZE)
#define	IEEE80211_RSSI_UPDATE_LIMIT	10
#define	IEEE80211_RSSI_IGNORE_LIMIT	2

#define IEEE80211_EVENT_SCAN_ABORT_TAG	"SCAN ABORT"
#define IEEE80211_SCAN_ENERGY_LEAK_RSSI_THRESHOLD		15
#define IEEE80211_SCAN_ENERGY_LEAK_RSSI_MARGIN			30

struct ieee80211com;
void ieee80211_scan_attach(struct ieee80211com *);
void ieee80211_scan_detach(struct ieee80211com *);
void ieee80211_scan_vattach(struct ieee80211vap *);
void ieee80211_scan_vdetach(struct ieee80211vap *);

void ieee80211_scan_dump_channels(const struct ieee80211_scan_state *);

#define	IEEE80211_SCAN_FOREVER	0x7fffffff
int ieee80211_start_scan(struct ieee80211vap *, int, u_int, u_int,
	const struct ieee80211_scan_ssid ssids[]);
int ieee80211_check_scan(struct ieee80211vap *, int, u_int, u_int,
	const struct ieee80211_scan_ssid ssids[],
	int (*action)(struct ieee80211vap *, const struct ieee80211_scan_entry *));
int ieee80211_should_scan(struct ieee80211vap *vap);
int ieee80211_bg_scan(struct ieee80211vap *);
void ieee80211_cancel_scan(struct ieee80211vap *);
void ieee80211_cancel_scan_no_wait(struct ieee80211vap *vap);

void ieee80211_add_scs_off_chan(struct ieee80211vap *, const struct ieee80211_scanparams *,
	const struct ieee80211_frame *, int , int , int);
void ieee80211_scan_scs_sample(struct ieee80211vap *vap);
void ieee80211_scan_scs_sample_cancel(struct ieee80211vap *vap);

int ieee80211_scan_dfs_action(struct ieee80211vap *, const struct ieee80211_scan_entry *);

struct ieee80211_scanparams;
void ieee80211_parse_ap_scan_result(struct ieee80211_scan_state *, const struct ieee80211_scanparams *,
	const struct ieee80211_frame *, int, int, int, int);
void ieee80211_add_scan(struct ieee80211vap *, const struct ieee80211_scanparams *,
	const struct ieee80211_frame *, int, int, int);
void ieee80211_scan_timeout(unsigned long arg);

void ieee80211_scan_assoc_success(struct ieee80211com *,
	const u_int8_t mac[IEEE80211_ADDR_LEN]);
enum {
	IEEE80211_SCAN_FAIL_TIMEOUT	= 1,	/* no response to mgmt frame */
	IEEE80211_SCAN_FAIL_STATUS	= 2	/* negative response to " " */
};
void ieee80211_scan_assoc_fail(struct ieee80211com *,
	const u_int8_t mac[IEEE80211_ADDR_LEN], int);
void ieee80211_scan_flush(struct ieee80211com *);
void ieee80211_scan_remove(struct ieee80211vap *);
struct ieee80211_channel *ieee80211_scan_pickchannel(struct ieee80211com *ic, int flags);
void ieee80211_scan_refresh_scan_module_chan_list(struct ieee80211com *ic, struct ieee80211vap *vap);
void ieee80211_sta_allow_beacon_reception(struct ieee80211vap *vap, int allow);

struct ieee80211_scan_entry;
typedef int ieee80211_scan_iter_func(void *, const struct ieee80211_scan_entry *);
int ieee80211_scan_iterate(struct ieee80211com *, ieee80211_scan_iter_func *, void *);

/*
 * Parameters supplied when adding/updating an entry in a
 * scan cache.  Pointer variables should be set to NULL
 * if no data is available.  Pointer references can be to
 * local data; any information that is saved will be copied.
 * All multi-byte values must be in host byte order.
 */
struct ieee80211_scanparams {
	u_int16_t capinfo;	/* 802.11 capabilities */
	u_int16_t fhdwell;	/* FHSS dwell interval */
	u_int8_t chan;		/* */
	u_int8_t bchan;
	u_int8_t fhindex;
	u_int8_t erp;
	u_int16_t bintval;
	u_int8_t timoff;
	u_int8_t *tim;
	u_int8_t *tstamp;
	u_int8_t *country;
	u_int8_t *ssid;
	u_int8_t *rates;
	u_int8_t *xrates;
	u_int8_t *htcap;
	u_int8_t *htinfo;
	u_int8_t *csa;
	u_int8_t *csa_tsf;
	u_int8_t *measreq;
	u_int8_t *wpa;
	u_int8_t *rsn;
	u_int8_t *wme;
	u_int8_t *wsc;
	u_int8_t *ath;
	u_int8_t *qtn;
	u_int8_t *vhtcap;
	u_int8_t *vhtop;
	u_int8_t *pwr_constraint;
	u_int8_t *obss_scan;
	u_int8_t *pairing_ie;
	int8_t	local_max_txpwr;
	struct ieee80211_channel *rxchan;
	uint8_t extender_role;
	uint8_t *ext_bssid_ie;
	uint8_t *bssload;
	uint8_t *mdie;
	uint8_t *repeater;
	uint8_t *owe_trans_mode;
	uint8_t *rp_info;
};

/*
 * Scan cache entry format used when exporting data from a policy
 * module; this data may be represented some other way internally.
 */
struct ieee80211_scan_entry {
	u_int8_t se_macaddr[IEEE80211_ADDR_LEN];
	u_int8_t se_bssid[IEEE80211_ADDR_LEN];
	u_int8_t se_ssid[2 + IEEE80211_NWID_LEN];
	u_int8_t se_owe_ssid[2 + IEEE80211_NWID_LEN];
	u_int8_t se_rates[2 + IEEE80211_RATE_MAXSIZE];
	u_int8_t se_xrates[2 + IEEE80211_RATE_MAXSIZE];
	u_int32_t se_rstamp;		/* recv timestamp */
	union {
		u_int8_t data[8];
		__le64 tsf;
	} se_tstamp;			/* from last rcv'd beacon */
	u_int16_t se_intval;		/* beacon interval (host byte order) */
	u_int16_t se_capinfo;		/* capabilities (host byte order) */
	struct ieee80211_channel *se_chan;/* channel where sta found */
	u_int16_t se_timoff;		/* byte offset to TIM ie */
	u_int16_t se_fhdwell;		/* FH only (host byte order) */
	u_int8_t se_fhindex;		/* FH only */
	u_int8_t se_erp;		/* ERP from beacon/probe resp */
	int8_t se_rssi;			/* avg'd recv ssi */
	u_int8_t se_dtimperiod;		/* DTIM period */
	u_int8_t *se_wpa_ie;		/* captured WPA ie */
	u_int8_t *se_rsn_ie;		/* captured RSN ie */
	u_int8_t *se_wme_ie;		/* captured WME ie */
	u_int8_t *se_wsc_ie;		/* captured WSC ie */
	u_int8_t *se_htcap_ie;		/* captured HT Capability Info ie */
	u_int8_t *se_htinfo_ie;		/* captured HT Information ie */
	u_int8_t *se_vhtcap_ie;		/* captured VHT Capability Info ie */
	u_int8_t *se_vhtop_ie;		/* captured VHT Operation Info ie */
	u_int8_t *se_ath_ie;		/* captured Atheros ie */
	u_int8_t *se_qtn_ie;		/* captured qtn ie */
	u_int8_t se_qtn_ie_flags;	/* captured Quantenna flags */
	u_int8_t se_is_qtn_dev;		/* 1 - is QTN device, 0 - non-QTN device */
	u_int8_t se_ext_role;		/* 0 - default mode, 1 - MBS, 2 - RBS */
	int8_t  se_last_rssi;		/* the last rssi */
	u_int8_t *se_ext_bssid_ie;	/* captured extender bssid ie */
	u_int8_t *se_pairing_ie;	/* captured pairing ie */
	u_int8_t *se_obss_scan;		/* captured overlapping BSS scan IE */
	u_int se_age;			/* age of entry (0 on create) */
	int8_t local_max_txpwr;		/* local max transmit power from Beacon/Probe response */
	int8_t se_rssi_ignored_cnt;	/* record the count of ignored RSSI samples */
	u_int8_t *se_bss_load_ie;	/* bss load ie */
	u_int8_t *se_md_ie;		/* mobility domain ie */
	u_int8_t *se_country_ie;	/* Country IE */
	u_int8_t *se_repeater_ie;	/* U-Repeater Cascade ie */
	u_int8_t *se_owe_trans_ie;	/* OWE transition mode ie */
	uint32_t se_jiffies;		/* last udpated jiffies time */
	u_int8_t *se_rp_info_ie;	/* captured rp_info ie */
};

/*
 * Template for an in-kernel scan policy module.
 * Modules register with the scanning code and are
 * typically loaded as needed.
 */
struct ieee80211_scanner {
	const char *scan_name;		/* printable name */
	int (*scan_attach)(struct ieee80211_scan_state *);
	int (*scan_detach)(struct ieee80211_scan_state *);
	int (*scan_start)(struct ieee80211_scan_state *, struct ieee80211vap *);
	int (*scan_restart)(struct ieee80211_scan_state *, struct ieee80211vap *);
	int (*scan_cancel)(struct ieee80211_scan_state *, struct ieee80211vap *);
	int (*scan_end)(struct ieee80211_scan_state *, struct ieee80211vap *,
		int (*action)(struct ieee80211vap *, const struct ieee80211_scan_entry *),
		u_int32_t);
	int (*scan_flush)(struct ieee80211_scan_state *);
	void (*scan_remove)(struct ieee80211_scan_state *, struct ieee80211_node *);
	struct ieee80211_channel *(*scan_pickchan)(struct ieee80211com *ic,
			struct ieee80211_scan_state *, int);
	/* add an entry to the cache */
	int (*scan_add)(struct ieee80211_scan_state *,
		const struct ieee80211_scanparams *,
		const struct ieee80211_frame *, int, int, int);
	/* age and/or purge entries in the cache */
	void (*scan_age)(struct ieee80211_scan_state *);
	/* note that association failed for an entry */
	void (*scan_assoc_fail)(struct ieee80211_scan_state *,
		const u_int8_t macaddr[IEEE80211_ADDR_LEN], int);
	/* note that association succeed for an entry */
	void (*scan_assoc_success)(struct ieee80211_scan_state *,
		const u_int8_t macaddr[IEEE80211_ADDR_LEN]);
	/* iterate over entries in the scan cache */
	int (*scan_iterate)(struct ieee80211_scan_state *,
		ieee80211_scan_iter_func *, void *);
	/* lock the ap scan private data */
	int (*scan_lock)(struct ieee80211_scan_state *);
	/* unlock the ap scan private data */
	void (*scan_unlock)(struct ieee80211_scan_state *,
		int bh_disabled);
	/* default action to take when found scan match */
	int (*scan_default)(struct ieee80211vap *,
		const struct ieee80211_scan_entry *);
};


struct sta_entry {
	struct ieee80211_scan_entry base;
	TAILQ_ENTRY(sta_entry) se_list;
	LIST_ENTRY(sta_entry) se_hash;
	u_int8_t se_fails;		/* failure to associate count */
	u_int8_t se_seen;		/* seen during current scan */
	u_int8_t se_notseen;		/* not seen in previous scans */
	u_int32_t se_avgrssi;		/* LPF rssi state */
	unsigned long se_lastupdate;	/* time of last update */
	unsigned long se_lastfail;	/* time of last failure */
	unsigned long se_lastassoc;	/* time of last association */
	u_int se_scangen;		/* iterator scan gen# */
	int se_inuse;			/* indicate that entry is in use and cannot be freed */
	int se_request_to_free;		/* indicate that entry must be freed after se_inuse cleared up */
};

struct sta_table {
	spinlock_t st_lock;			/* on scan table */
	TAILQ_HEAD(st_entry_head, sta_entry) st_entry;	/* all entries */
	ATH_LIST_HEAD(, sta_entry) st_hash[STA_HASHSIZE];
	uint32_t st_entry_num;
	spinlock_t st_scanlock;			/* on st_scangen */
	u_int st_scangen;			/* gen# for iterator */
	int st_newscan;
#define STA_TABLE_FLAG_PRINTK_LIMIT	0x1
	uint32_t st_flag;
	struct IEEE80211_TQ_STRUCT st_actiontq;	/* tasklet for "action" */
	struct ieee80211_scan_entry st_selbss;	/* selected bss for action tasklet */
};

struct ap_scan_list {
	TAILQ_HEAD(, ap_scan_entry) asl_head;
};

struct ap_scan_entry {
	struct ieee80211_scan_entry base;
	TAILQ_ENTRY(ap_scan_entry) ase_list;
	unsigned long se_lastupdate;	/* time of last update */
	u_int32_t se_avgrssi;		/* LPF rssi state */
	u_int8_t se_seen;	/* seen during current scan */
	u_int8_t se_notseen;	/* not seen in previous scans */
	u_int32_t se_inuse;	/* indicate that entry is in use and cannot be freed */
	u_int8_t se_request_to_free;	/* request to free entry after se_inuse cleared. */
};

struct ap_state {
	unsigned int as_numbeacons[IEEE80211_CHAN_MAX];
	uint8_t as_obss_chanlayout[IEEE80211_CHAN_MAX];

	int as_maxrssi[IEEE80211_CHAN_MAX];
	int as_cci[IEEE80211_CHAN_MAX];		/* CCI, Co-Channel Interference */
	int as_aci[IEEE80211_CHAN_MAX];		/* ACI, Adjacent Channel Interference */
	int as_numpkts[IEEE80211_CHAN_MAX];
	int as_chanmetric[IEEE80211_CHAN_MAX];
	uint32_t as_chanmetric_timestamp[IEEE80211_CHAN_MAX];
	uint32_t as_chanmetric_pref[IEEE80211_CHAN_MAX];
	struct IEEE80211_TQ_STRUCT as_actiontq;	/* tasklet for "action" */
	struct ieee80211_scan_entry as_selbss;	/* selected bss for action tasklet */
	int (*as_action)(struct ieee80211vap *, const struct ieee80211_scan_entry *);
	struct ap_scan_list as_scan_list[IEEE80211_CHAN_MAX];
	uint32_t as_entry_num;			/* scan entry number */
	spinlock_t asl_lock;			/* to protect as_scan_list */

	u_int8_t as_chan_xped[IEEE80211_CHAN_BYTES];	/* whether once used as working channel */
	uint32_t as_scs_ranking_cnt;

	/* CCA interference */
#define SCS_CCA_INTF_INVALID	0xFFFF
#define SCS_CCA_IDLE_INVALID	0xFFFF
	uint16_t as_cca_intf_smth;
	uint16_t as_cca_intf_raw[IEEE80211_CHAN_MAX];
	uint16_t as_cca_intf[IEEE80211_CHAN_MAX];
	uint16_t as_cca_intf_pri[IEEE80211_CHAN_MAX];
	uint16_t as_cca_intf_sec[IEEE80211_CHAN_MAX];
	uint16_t as_cca_intf_sec40[IEEE80211_CHAN_MAX];
	uint32_t as_cca_intf_jiffies[IEEE80211_CHAN_MAX];
	uint32_t as_pmbl_err_ap[IEEE80211_CHAN_MAX];
	uint32_t as_pmbl_err_sta[IEEE80211_CHAN_MAX];
	/* tx and rx time */
	uint32_t as_tx_ms;
	uint32_t as_rx_ms;
	uint32_t as_tx_ms_smth;
	uint32_t as_rx_ms_smth;
	/* rssi from all STAs, used to calculate attenuation */
#define SCS_RSSI_UNINITED		-1
#define SCS_RSSI_VALID(_rssi)		(((_rssi) < -1) && ((_rssi) > -1200))
#define SCS_RSSI_PRECISION_RECIP	10	/* 0.1db */
#define SCS_ATTEN_UNINITED		0
#define SCS_ATTEN_VALID(_atten)         (_atten != SCS_ATTEN_UNINITED)
	int8_t	as_sta_atten_num;
	int32_t	as_sta_atten_sum;
	int32_t	as_sta_atten_min;
	int32_t	as_sta_atten_max;
	int32_t as_sta_atten_expect;    /* the attenuation expected in low power channel */
	int32_t as_dfs_reentry_cnt;
	int32_t as_dfs_reentry_level;
	uint32_t as_age;
};

void ieee80211_add_scan_entry(struct ieee80211_scan_entry *ise,
			const struct ieee80211_scanparams *sp,
			const struct ieee80211_frame *wh,
			int subtype, int rssi, int rstamp);
int ieee80211_scan_check_secondary_channel(struct ieee80211_scan_state *ss,
			struct ieee80211_scan_entry *ise);
void ieee80211_scan_mark_channel_obssflag(struct ieee80211com *ic,
			struct ieee80211_channel *chan_pri, int bss_bw);
struct ieee80211_channel *ieee80211_scan_switch_pri_chan(struct ieee80211_scan_state *ss,
			struct ieee80211_channel *chan_pri);
struct ieee80211_channel *ieee80211_scs_switch_pri_chan(struct ieee80211_scan_state *ss,
			struct ieee80211_channel *chan_pri);
int ieee80211_wps_active(uint8_t *wsc_ie);
void ieee80211_dump_scan_res(struct ieee80211_scan_state *ss);
int ieee80211_get_type_of_neighborhood(struct ieee80211com *ic);
void ieee80211_check_type_of_neighborhood(struct ieee80211com *ic);
char * ieee80211_neighborhood_type2str(int type);

const struct ieee80211_scanner *ieee80211_scanner_get(enum ieee80211_opmode,
	int);
void ieee80211_scanner_register(enum ieee80211_opmode,
	const struct ieee80211_scanner *);
void ieee80211_scanner_unregister(enum ieee80211_opmode,
	const struct ieee80211_scanner *);
void ieee80211_scanner_unregister_all(const struct ieee80211_scanner *);
int ieee80211_scan_phy_stats_update(struct ieee80211_scan_state *ss,
		struct ieee80211_channel *chan);

struct ap_scan_iter {
	struct ieee80211vap *vap;
	char *current_env;
	char *end_buf;
	int32_t	ap_counts;
	int mode;
};

struct iwscanreq {
	struct ieee80211vap *vap;
	char *current_ev;
	char *end_buf;
	int mode;
	struct iw_request_info *info;
};

#define QFDR_SIWSCAN            1
#define QFDR_SIWSCAN_SIMPLE     2
#define QFDR_GIWSCAN            3
#define QFDR_AP_SCAN_RESULT     4
#define QFDR_REMOTE_BSS_INFO    5
#define QFDR_REMOTE_CMD         0x1000

struct qfdr_remote_aplist_req {
	int32_t type;
	char dev_name[IFNAMSIZ];
	struct iw_request_info info;
	int32_t extra_len;
};

struct qfdr_remote_scan_req {
	int32_t type;
	char dev_name[IFNAMSIZ];
	u_int16_t flags;
	u_int16_t length;
	char pointer[0];
};

struct qfdr_bss_info {
	int32_t type;
	char dev_name[IFNAMSIZ];
	int32_t rssi;
	uint32_t channel;
	uint8_t ssid[2 + IEEE80211_NWID_LEN];
	uint16_t beacon_len;
	uint8_t beacon[0];
};

struct qfdr_remote_aplist_rep {
	int32_t res;
	int32_t type;
	int32_t ap_counts;
	int32_t length;
	char extra[0];
};

typedef int (*qfdr_remote_siwscan_hook_t)(char *dev_name, struct iw_point *data);
void ieee80211_register_qfdr_remote_siwscan_hook(qfdr_remote_siwscan_hook_t hook);
int qfdr_siwscan_for_remote(struct qfdr_remote_scan_req *req_remote);

typedef int (*qfdr_remote_giwscan_hook_t)(struct iwscanreq *req);
void ieee80211_register_qfdr_remote_giwscan_hook(qfdr_remote_giwscan_hook_t hook);
struct qfdr_remote_aplist_rep *qfdr_giwscan_for_remote(struct qfdr_remote_aplist_req *req_remote);

typedef int (*qfdr_remote_ap_scan_results_hook_t)(struct ap_scan_iter *iter);
void ieee80211_register_qfdr_remote_ap_scan_results_hook(qfdr_remote_ap_scan_results_hook_t hook);
struct qfdr_remote_aplist_rep *qfdr_ap_scan_results_for_remote(struct qfdr_remote_aplist_req *req_remote);

typedef int (*qfdr_send_bss_info_hook_t)(struct ieee80211vap *vap, uint8_t channel);
void ieee80211_register_qfdr_send_bss_info_hook(qfdr_send_bss_info_hook_t hook);
void qfdr_recv_bss_info(struct qfdr_bss_info *remote_bss_info);
#endif /* _NET80211_IEEE80211_SCAN_H_ */
