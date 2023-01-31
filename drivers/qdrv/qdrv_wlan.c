/**
  Copyright (c) 2008 - 2013 Quantenna Communications Inc
  All Rights Reserved

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

**/
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/version.h>

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/netdevice.h>
#include <linux/igmp.h>
#include <net/iw_handler.h> /* wireless_send_event(..) */
#include <net/sch_generic.h>
#include <asm/hardware.h>
#include <asm/gpio.h>
#include <qtn/qdrv_sch.h>
#include "qdrv_features.h"
#include "qdrv_debug.h"
#include "qdrv_mac.h"
#include "qdrv_soc.h"
#include "qdrv_comm.h"
#include "qtn/qdrv_bld.h"
#include "qdrv_wlan.h"
#include "qdrv_hal.h"
#include "qdrv_vap.h"	/* For vnet_send_ioctl() etc ... */
#include "qdrv_control.h"
#include "qdrv_txbf.h"
#include "qdrv_radar.h"
#include "qdrv_pktlogger.h"
#include "qdrv_config.h"
#include "qdrv_pcap.h"
#include "qdrv_auc.h"
#include "qdrv_mac_reserve.h"
#include "qdrv_spdia.h"

#include "qdrv_netdebug_checksum.h"
#include <qtn/qtn_buffers.h>
#include <qtn/qtn_global.h>
#include <qtn/registers.h> /* To get to mac->reg-> .... */
#include <qtn/muc_phy_stats.h> /* To get to qtn_stats_log .... */
#include <qtn/shared_params.h>
#include <qtn/hardware_revision.h>
#include <qtn/bootcfg.h>
#include <qtn/qtn_trace.h>
#include <qtn/qtn_global.h>
#include <qtn/bootcfg_hw_board_config.h>
#include "qtn_hw_mod.h"
#include "qdrv_muc_stats.h"
#include "qdrv_sch_const.h"
#include "qdrv_sch_wmm.h"
#include "net80211/ieee80211_beacon_desc.h"
#include "net80211/ieee80211_ioctl.h"
#include "net80211/ieee80211_repeater.h"
#ifdef CONFIG_QVSP
#include "qtn/qvsp.h"
#endif

#include <qtn/muc_phy_stats.h>

#include <linux/file.h>
#include <linux/syscalls.h>
#include <linux/ctype.h>
#include <linux/pm_qos_params.h>
#include <common/ruby_pm.h>
#include "common/qtn_bits.h"

#include <asm/board/board_config.h>
#include <asm/board/troubleshoot.h>
#include <asm/cacheflush.h>
#include <qtn/topaz_hbm.h>
#include <qtn/topaz_fwt_sw.h>
#include <qtn/topaz_congest_queue.h>
#include "soc.h"
#include "qtn_logging.h"
#include <net/arp.h>
#ifdef CONFIG_IPV6
#include <net/ip6_checksum.h>
#endif
#if defined(CONFIG_QTN_BSA_SUPPORT)
#include "net80211/ieee80211_qrpe.h"
#include "net80211/ieee80211_bsa.h"
#endif

/* Delay prior to enabling hang detection */
#define QDRV_WLAN_HR_DELAY_SECS 3

#define QDRV_WLAN_IGMP_QUERY_INTERVAL 125
#define NET_IP_ALIGN 2

#define SE95_DEVICE_ADDR	0x49

#define RSSI_OFFSET_FROM_10THS_DBM    900

u_int8_t g_bb_enabled = 0;

extern uint32_t g_carrier_id;

extern __sram_data const int qdrv_sch_band_prio[5];
extern struct qdrv_sch_band_aifsn qdrv_sch_band_chg_prio[5];
#ifdef CONFIG_QVSP
static void qdrv_wlan_manual_ba_throt(struct qdrv_wlan *qw, struct qdrv_vap *qv, unsigned int value);
static void qdrv_wlan_manual_wme_throt(struct qdrv_wlan *qw, struct qdrv_vap *qv, unsigned int value);
#endif

int g_qdrv_non_qtn_assoc = 0;

void enable_bb(int index, u32 channel);
void bb_rf_drv_set_channel(u32 bb_index, u32 freq_band, u32 channel);

struct timer_list qdrv_wps_button_timer;

int g_triggers_on = 0;

static void qdrv_wlan_set_11g_erp(struct ieee80211vap *vap, int on);
static void qdrv_wlan_80211_setparam(struct ieee80211_node *ni, int param,
	int value, unsigned char *data, int len);
static struct qtn_rateentry rate_table_11a[] =
{
/*	ieee	rate	ctl	short	basic	phy			*/
/*	rate	100kbps	indx	pre	rate	type			*/
	{12,	 60,	0,	0,	1,	QTN_RATE_PHY_OFDM},
	{18,	 90,	0,	0,	0,	QTN_RATE_PHY_OFDM},
	{24,	120,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{36,	180,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{48,	240,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{72,	360,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{96,	480,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{108,	540,	4,	0,	0,	QTN_RATE_PHY_OFDM},
};

static struct qtn_rateentry rate_table_11b[] =
{
/*	ieee	rate	ctl	short	basic	phy			*/
/*	rate	100kbps	indx	pre	rate	type			*/
	{2,	 10,	0,	0,	1,	QTN_RATE_PHY_CCK},
	{4,	 20,	1,	1,	1,	QTN_RATE_PHY_CCK},
	{11,	 55,	1,	1,	0,	QTN_RATE_PHY_CCK},
	{22,	110,	1,	1,	0,	QTN_RATE_PHY_CCK},
};

static struct qtn_rateentry rate_table_11g[] =
{
/*	ieee	rate	ctl		short	basic	phy			*/
/*	rate	100kbps	indx	pre		rate	type			*/
	{2,	10,	0,		0,		1,		QTN_RATE_PHY_CCK},
	{4,	20,	1,		1,		1,		QTN_RATE_PHY_CCK},
	{11,	55,	2,		1,		1,		QTN_RATE_PHY_CCK},
	{22,	110,	3,		1,		1,		QTN_RATE_PHY_CCK},
	{12,	60,	4,		0,		1,		QTN_RATE_PHY_OFDM},
	{18,	90,	4,		0,		0,		QTN_RATE_PHY_OFDM},
	{24,	120,	6,		0,		0,		QTN_RATE_PHY_OFDM},
	{36,	180,	6,		0,		0,		QTN_RATE_PHY_OFDM},
	{48,	240,	8,		0,		0,		QTN_RATE_PHY_OFDM},
	{72,	360,	8,		0,		0,		QTN_RATE_PHY_OFDM},
	{96,	480,	8,		0,		0,		QTN_RATE_PHY_OFDM},
	{108,	540,	8,		0,		0,		QTN_RATE_PHY_OFDM},
};

struct qtn_rateentry rate_table_11na[] = {
/*	ieee	rate	ctl	short	basic	phy			*/
/*	rate	100kbps	indx	pre	rate	type			*/
	{12,	 60,	0,	0,	1,	QTN_RATE_PHY_OFDM},
	{18,	 90,	0,	0,	0,	QTN_RATE_PHY_OFDM},
	{24,	120,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{36,	180,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{48,	240,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{72,	360,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{96,	480,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{108,	540,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{QTN_RATE_11N | 0,	 65,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 1,	130,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 2,	195,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 3,	260,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 4,	390,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 5,	520,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 6,	585,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 7,	650,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 8,	130,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 9,	260,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 10,	390,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 11,	520,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 12,	780,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 13,   1040,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 14,   1170,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 15,   1300,	4,	0,	0,	QTN_RATE_PHY_HT},

};

/*
 * Only legacy rate are been added to it. Other rate flags will be updated
 * when we MCS changes are made
 */
struct qtn_rateentry rate_table_11ac[] = {
/*	ieee	rate	ctl	short	basic	phy			*/
/*	rate	100kbps	indx	pre	rate	type			*/
	{12,	 60,	0,	0,	1,	QTN_RATE_PHY_OFDM},
	{18,	 90,	0,	0,	0,	QTN_RATE_PHY_OFDM},
	{24,	120,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{36,	180,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{48,	240,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{72,	360,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{96,	480,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{108,	540,	4,	0,	0,	QTN_RATE_PHY_OFDM},
};

static struct qtn_rateentry rate_table_11ng[] =
{
/*	ieee			rate	ctl	short	basic	phy		*/
/*	rate			100kbps	indx	pre	rate	type		*/
	{2,			10,	0,	0,	1,	QTN_RATE_PHY_CCK},
	{4,			20,	1,	1,	1,	QTN_RATE_PHY_CCK},
	{11,			55,	2,	1,	1,	QTN_RATE_PHY_CCK},
	{22,			110,	3,	1,	1,	QTN_RATE_PHY_CCK},
	{12,			60,	0,	0,	1,	QTN_RATE_PHY_OFDM},
	{18,			90,	0,	0,	0,	QTN_RATE_PHY_OFDM},
	{24,			120,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{36,			180,	2,	0,	0,	QTN_RATE_PHY_OFDM},
	{48,			240,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{72,			360,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{96,			480,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{108,			540,	4,	0,	0,	QTN_RATE_PHY_OFDM},
	{QTN_RATE_11N | 0,	 65,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 1,	130,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 2,	195,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 3,	260,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 4,	390,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 5,	520,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 6,	585,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 7,	650,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 8,	130,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 9,	260,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 10,	390,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 11,	520,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 12,	780,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 13,	1040,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 14,	1170,	4,	0,	0,	QTN_RATE_PHY_HT},
	{QTN_RATE_11N | 15,	1300,	4,	0,	0,	QTN_RATE_PHY_HT},
};

struct qdrv_channel_info {
	u8 chnum;	/* IEEE channel number */
	u8 cchan_40;	/* Channel Center Frequency for 40MHz */
	u8 cchan_80;	/* Channel Center Frequency for 80MHz */
	u8 cchan_160;	/* Channel Center Frequency for 160MHz */
	u16 freq;	/* Channel frequency */
	u32 flags;	/* Channel flags */
	u32 ext_flags;	/* Extra channel flags for 80/160MHZ mode */
};

#define QDRV_CHAN_DEFFLAGS_2G	(IEEE80211_CHAN_HT20 | IEEE80211_CHAN_OFDM | IEEE80211_CHAN_HT40 | \
		IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_CCK)

#define QDRV_CHAN_DEFFLAGS_5G	(IEEE80211_CHAN_HT20 | IEEE80211_CHAN_HT40 | \
		IEEE80211_CHAN_OFDM | IEEE80211_CHAN_5GHZ)

static const struct qdrv_channel_info qdrv_channels_init_info[] = {
	{
		.chnum = 1, .cchan_40 = 3, .cchan_80 = 0, .cchan_160 = 0, .freq = 2412,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U,
		.ext_flags = 0,
	},
	{
		.chnum = 2, .cchan_40 = 4, .cchan_80 = 0, .cchan_160 = 0, .freq = 2417,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U,
		.ext_flags = 0,
	},
	{
		.chnum = 3, .cchan_40 = 5, .cchan_80 = 0, .cchan_160 = 0, .freq = 2422,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U,
		.ext_flags = 0,
	},
	{
		.chnum = 4, .cchan_40 = 6, .cchan_80 = 0, .cchan_160 = 0, .freq = 2427,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U,
		.ext_flags = 0,
	},
	{
		.chnum = 5, .cchan_40 = 7, .cchan_80 = 0, .cchan_160 = 0, .freq = 2432,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 6, .cchan_40 = 8, .cchan_80 = 0, .cchan_160 = 0, .freq = 2437,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 7, .cchan_40 = 9, .cchan_80 = 0, .cchan_160 = 0, .freq = 2442,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 8, .cchan_40 = 6, .cchan_80 = 0, .cchan_160 = 0, .freq = 2447,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 9, .cchan_40 = 7, .cchan_80 = 0, .cchan_160 = 0, .freq = 2452,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 10, .cchan_40 = 8, .cchan_80 = 0, .cchan_160 = 0, .freq = 2457,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 11, .cchan_40 = 9, .cchan_80 = 0, .cchan_160 = 0, .freq = 2462,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 12, .cchan_40 = 10, .cchan_80 = 0, .cchan_160 = 0, .freq = 2467,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 13, .cchan_40 = 11, .cchan_80 = 0, .cchan_160 = 0, .freq = 2472,
		.flags = QDRV_CHAN_DEFFLAGS_2G | IEEE80211_CHAN_HT40D,
		.ext_flags = 0,
	},
	{
		.chnum = 36, .cchan_40 = 38, .cchan_80 = 42, .cchan_160 = 0, .freq = 5180,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LL
	},
	{
		.chnum = 40, .cchan_40 = 38, .cchan_80 = 42, .cchan_160 = 0, .freq = 5200,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LU
	},
	{
		.chnum = 44, .cchan_40 = 46, .cchan_80 = 42, .cchan_160 = 0, .freq = 5220,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UL
	},
	{
		.chnum = 48, .cchan_40 = 46, .cchan_80 = 42, .cchan_160 = 0, .freq = 5240,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UU
	},
	{
		.chnum = 52, .cchan_40 = 54, .cchan_80 = 58, .cchan_160 = 0, .freq = 5260,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LL
	},
	{
		.chnum = 56, .cchan_40 = 54, .cchan_80 = 58, .cchan_160 = 0, .freq = 5280,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LU
	},
	{
		.chnum = 60, .cchan_40 = 62, .cchan_80 = 58, .cchan_160 = 0, .freq = 5300,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UL
	},
	{
		.chnum = 64, .cchan_40 = 62, .cchan_80 = 58, .cchan_160 = 0, .freq = 5320,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UU
	},
	{
		.chnum = 100, .cchan_40 = 102, .cchan_80 = 106, .cchan_160 = 0, .freq = 5500,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LL
	},
	{
		.chnum = 104, .cchan_40 = 102, .cchan_80 = 106, .cchan_160 = 0, .freq = 5520,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LU
	},
	{
		.chnum = 108, .cchan_40 = 110, .cchan_80 = 106, .cchan_160 = 0, .freq = 5540,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UL
	},
	{
		.chnum = 112, .cchan_40 = 110, .cchan_80 = 106, .cchan_160 = 0, .freq = 5560,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UU
	},
	{
		.chnum = 116, .cchan_40 = 118, .cchan_80 = 122, .cchan_160 = 0, .freq = 5580,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LL
	},
	{
		.chnum = 120, .cchan_40 = 118, .cchan_80 = 122, .cchan_160 = 0, .freq = 5600,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LU
	},
	{
		.chnum = 124, .cchan_40 = 126, .cchan_80 = 122, .cchan_160 = 0, .freq = 5620,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UL
	},
	{
		.chnum = 128, .cchan_40 = 126, .cchan_80 = 122, .cchan_160 = 0, .freq = 5640,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UU
	},
	{
		.chnum = 132, .cchan_40 = 134, .cchan_80 = 138, .cchan_160 = 0, .freq = 5660,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LL
	},
	{
		.chnum = 136, .cchan_40 = 134, .cchan_80 = 138, .cchan_160 = 0, .freq = 5680,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LU
	},
	{
		.chnum = 140, .cchan_40 = 142, .cchan_80 = 138, .cchan_160 = 0, .freq = 5700,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UL
	},
	{
		.chnum = 144, .cchan_40 = 142, .cchan_80 = 138, .cchan_160 = 0, .freq = 5720,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UU
	},
	{
		.chnum = 149, .cchan_40 = 151, .cchan_80 = 155, .cchan_160 = 0, .freq = 5745,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LL
	},
	{
		.chnum = 153, .cchan_40 = 151, .cchan_80 = 155, .cchan_160 = 0, .freq = 5765,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LU
	},
	{
		.chnum = 157, .cchan_40 = 159, .cchan_80 = 155, .cchan_160 = 0, .freq = 5785,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UL
	},
	{
		.chnum = 161, .cchan_40 = 159, .cchan_80 = 155, .cchan_160 = 0, .freq = 5805,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UU
	},
	{
		.chnum = 165, .cchan_40 = 0, .cchan_80 = 0, .cchan_160 = 0, .freq = 5825,
		.flags = QDRV_CHAN_DEFFLAGS_5G & ~IEEE80211_CHAN_HT40,
		.ext_flags = 0
	},
	{
		.chnum = 169, .cchan_40 = 0, .cchan_80 = 0, .cchan_160 = 0, .freq = 5845,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D,
		.ext_flags = 0
	},
	{
		.chnum = 184, .cchan_40 = 0, .cchan_80 = 190, .cchan_160 = 0, .freq = 4920,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LL
	},
	{
		.chnum = 188, .cchan_40 = 0, .cchan_80 = 190, .cchan_160 = 0, .freq = 4940,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_LU
	},
	{
		.chnum = 192, .cchan_40 = 0, .cchan_80 = 190, .cchan_160 = 0, .freq = 4960,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UL
	},
	{
		.chnum = 196, .cchan_40 = 0, .cchan_80 = 190, .cchan_160 = 0, .freq = 4980,
		.flags = QDRV_CHAN_DEFFLAGS_5G | IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80,
		.ext_flags = IEEE80211_CHAN_VHT80_UU
	},
};

static int set_rates(struct qdrv_wlan *qw, enum ieee80211_phymode mode)
{
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211_rateset *rs;
	struct qtn_ratetable *rt;
	int maxrates, i;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

#define N(a)	(sizeof(a)/sizeof(a[0]))
	switch (mode) {
	case IEEE80211_MODE_11A:
		qw->qw_rates[mode].rt_entries = rate_table_11a;
		qw->qw_rates[mode].rt_num = N(rate_table_11a);
		qw->qw_rates[mode].rt_legacy_num = N(rate_table_11a);
		break;
	case IEEE80211_MODE_11B:
		qw->qw_rates[mode].rt_entries = rate_table_11b;
		qw->qw_rates[mode].rt_num = N(rate_table_11b);
		qw->qw_rates[mode].rt_legacy_num = N(rate_table_11b);
		break;
	case IEEE80211_MODE_11G:
		qw->qw_rates[mode].rt_entries = rate_table_11g;
		qw->qw_rates[mode].rt_num = N(rate_table_11g);
		qw->qw_rates[mode].rt_legacy_num = N(rate_table_11g);
		break;
	case IEEE80211_MODE_11NA:
	case IEEE80211_MODE_11NA_HT40PM:
		qw->qw_rates[mode].rt_entries = rate_table_11na;
		qw->qw_rates[mode].rt_num = N(rate_table_11na);
		qw->qw_rates[mode].rt_legacy_num = N(rate_table_11a);
		break;
	case IEEE80211_MODE_11NG:
	case IEEE80211_MODE_11NG_HT40PM:
	case IEEE80211_MODE_11AC_NG_VHT20PM:
	case IEEE80211_MODE_11AC_NG_VHT40PM:
		qw->qw_rates[mode].rt_entries = rate_table_11ng;
		qw->qw_rates[mode].rt_num = N(rate_table_11ng);
		qw->qw_rates[mode].rt_legacy_num = N(rate_table_11g);
		break;
	case IEEE80211_MODE_11AC_VHT20PM:
	case IEEE80211_MODE_11AC_VHT40PM:
	case IEEE80211_MODE_11AC_VHT80PM:
		qw->qw_rates[mode].rt_entries = rate_table_11ac;
		qw->qw_rates[mode].rt_num = N(rate_table_11ac);
		qw->qw_rates[mode].rt_legacy_num = N(rate_table_11a);
		break;
	default:
		DBGPRINTF_E("mode unknown\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -EINVAL;
	}
#undef N

	if ((rt = &qw->qw_rates[mode]) == NULL) {
		DBGPRINTF_E("rt NULL\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -EINVAL;
	}

	if (rt->rt_num > IEEE80211_RATE_MAXSIZE) {
		DBGPRINTF_E("Rate table is too small (%u > %u)\n",
			rt->rt_num, IEEE80211_RATE_MAXSIZE);
		maxrates = IEEE80211_RATE_MAXSIZE;
	} else {
		maxrates = rt->rt_num;
	}

	rs = &ic->ic_sup_rates[mode];
	memset(rs, 0, sizeof(struct ieee80211_rateset));

	for (i = 0; i < maxrates; i++) {
		rs->rs_rates[i] = (rt->rt_entries[i].re_basicrate) ?
			(rt->rt_entries[i].re_ieeerate | IEEE80211_RATE_BASIC) :
			rt->rt_entries[i].re_ieeerate;
	}

	rs->rs_legacy_nrates = rt->rt_legacy_num;
	rs->rs_nrates = maxrates;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

static int set_mode(struct qdrv_wlan *qw, enum ieee80211_phymode mode)
{
	qw->qw_currt = &qw->qw_rates[mode];
	qw->qw_curmode = mode;
#if 0
	qw->qw_minrateix = 0;
#endif

	return 0;
}

/*
 * Set a per-node threshold for packets queued to the MuC, based on the number of associated nodes,
 * including WDS nodes.
 * PERNODE_TBD - all WDS nodes should count as one under the current scheme
 */
static void qdrv_wlan_muc_node_thresh_set(struct qdrv_wlan *qw, struct ieee80211com *ic,
						uint8_t assoc_cnt)
{
	if (assoc_cnt == 0) {
		assoc_cnt = 1;
	}

	qw->tx_if.muc_thresh_high = MAX(
		(qw->tx_if.list_max_size / assoc_cnt), QDRV_TXDESC_THRESH_MAX_MIN);
	qw->tx_if.muc_thresh_low = qw->tx_if.muc_thresh_high - QDRV_TXDESC_THRESH_MIN_DIFF;
	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN,
		"per-node thresholds changed - high=%u low=%u nodes=%u\n",
		qw->tx_if.muc_thresh_high, qw->tx_if.muc_thresh_low, ic->ic_sta_assoc);
}

extern int g_wlan_tot_node_alloc;
extern int g_wlan_tot_node_alloc_tmp;
extern int g_wlan_tot_node_free;
extern int g_wlan_tot_node_free_tmp;

static struct ieee80211_node *qdrv_node_alloc(struct ieee80211_node_table *nt,
		struct ieee80211vap *vap, const uint8_t *macaddr, const uint8_t tmp_node)
{
	struct qdrv_node *qn;
	struct ieee80211com *ic = vap->iv_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct net_device *vdev = vap->iv_dev;
	struct qtn_node_shared_stats_list *shared_stats;
	unsigned long flags;

	qn = kmalloc(sizeof(struct qdrv_node), GFP_ATOMIC);

	if (qn == NULL) {
		DBGPRINTF_E("kmalloc failed\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return NULL;
	}
	memset(qn, 0, sizeof(struct qdrv_node));

	dev_hold(vdev); /* Increase the reference count of the VAP netdev */
	ic->ic_node_count++;
	DBGPRINTF(DBG_LL_INFO, QDRV_LF_TRACE | QDRV_LF_WLAN,
			"Allocated node %p (total %d/%d)\n",
			qn, ic->ic_node_count, atomic_read(&vdev->refcnt));

	qn->qn_node.ni_vap = vap;
	TAILQ_INSERT_TAIL(&qv->allnodes, qn, qn_next);

	if (!tmp_node) {
		local_irq_save(flags);
		shared_stats = TAILQ_FIRST(&qw->shared_pernode_stats_head);
		if (shared_stats == NULL) {
			DBGPRINTF_E("Failed to obtain shared_stats for new node\n");
			local_irq_restore(flags);
			dev_put(vdev);
			ic->ic_node_count--;
			kfree(qn);
			DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
			return NULL;
		}

		TAILQ_REMOVE(&qw->shared_pernode_stats_head, shared_stats, next);
		local_irq_restore(flags);

		memset(shared_stats, 0, sizeof(*shared_stats));

		qn->qn_node.ni_shared_stats = (struct qtn_node_shared_stats *) shared_stats;
		qn->qn_node.ni_shared_stats_phys = (void*)((unsigned long)shared_stats -
				(unsigned long)qw->shared_pernode_stats_pool +
				(unsigned long)qw->shared_pernode_stats_phys);

#ifdef CONFIG_QVSP
		qvsp_node_init(&qn->qn_node);
#endif
		g_wlan_tot_node_alloc++;

		qdrv_wlan_muc_node_thresh_set(qw, ic, ic->ic_sta_assoc + 1);
	} else {
		g_wlan_tot_node_alloc_tmp++;
	}

	return &qn->qn_node;
}

static void qdrv_node_free(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qdrv_node *qn = container_of(ni, struct qdrv_node, qn_node);
	struct ieee80211vap *vap = ni->ni_vap;
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct net_device *vdev = vap->iv_dev;
	unsigned long flags;

	if (ni->ni_shared_stats) {
		local_irq_save(flags);
		TAILQ_INSERT_TAIL(&qw->shared_pernode_stats_head,
				(struct qtn_node_shared_stats_list *) ni->ni_shared_stats, next);
		local_irq_restore(flags);
		g_wlan_tot_node_free++;
		qdrv_wlan_muc_node_thresh_set(qw, ic, ic->ic_sta_assoc);
	} else {
		g_wlan_tot_node_free_tmp++;
	}

	TAILQ_REMOVE(&qv->allnodes, qn, qn_next);
	dev_put(vdev);
}

static u_int32_t qdrv_set_channel_setup(const struct ieee80211com *ic,
		const struct ieee80211_channel *chan)
{
	u_int32_t ieee_chan;
	uint32_t qtn_chan = 0;
	int32_t pwr;
	int force_bw_20 = 0;
	int force_bw_40 = 0;
	int max_bw = BW_HT80;
	int tdls_offchan = !!(chan->ic_ext_flags & IEEE80211_CHAN_TDLS_OFF_CHAN);

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	ieee_chan = (u_int32_t)chan->ic_ieee;
	pwr = chan->ic_maxpower;

	force_bw_20 = (ic->ic_flags_ext & IEEE80211_FEXT_SCAN_20) &&
			((ic->ic_htcap.cap & IEEE80211_HTCAP_C_CHWIDTH40) || tdls_offchan);

	force_bw_40 = ic->ic_flags_ext & IEEE80211_FEXT_SCAN_40;

	if ((ic->ic_opmode == IEEE80211_M_STA) && ic->ic_bss_bw && !tdls_offchan)
		max_bw = ic->ic_bss_bw;

	qtn_chan = SM(ieee_chan, QTN_CHAN_IEEE);

	if (!force_bw_20 && ((ic->ic_htcap.cap & IEEE80211_HTCAP_C_CHWIDTH40) || tdls_offchan)) {
		if ((chan->ic_flags & IEEE80211_CHAN_HT40) && (max_bw >= BW_HT40)) {
			if (ieee80211_is_chan40d(chan))
				qtn_chan |= QTN_CHAN_FLG_PRI_HI | QTN_CHAN_FLG_HT40;
			else if (ieee80211_is_chan40u(chan))
				qtn_chan |= QTN_CHAN_FLG_HT40;
		}
		if (!force_bw_40 && IS_IEEE80211_VHT_ENABLED(ic) &&
				IEEE80211_IS_VHT_80(ic) &&
				(chan->ic_flags & IEEE80211_CHAN_VHT80) &&
				(max_bw >= BW_HT80)) {
			qtn_chan |= QTN_CHAN_FLG_VHT80;
		}
	}
	qtn_chan |= SM(pwr, QTN_CHAN_PWR);
	if (chan->ic_flags & IEEE80211_CHAN_DFS) {
		qtn_chan |= QTN_CHAN_FLG_DFS;
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN,
				"Hlink setting channel %08X Chan %d Pwr %d\n",
			qtn_chan, ieee_chan, pwr);

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return qtn_chan;
}

static uint32_t qdrv_set_channel_freqband_setup(const struct ieee80211com *ic, const struct ieee80211_channel *chan)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	uint32_t freqband = 0;

	freqband |= SM(qw->rf_chipid, QTN_BAND_FREQ);

	return freqband;
}

static int qdrv_wlan_80211_get_cap_bw(struct ieee80211com *ic)
{
	int bw;

	switch (ic->ic_phymode) {
	case IEEE80211_MODE_11A:
	case IEEE80211_MODE_11B:
	case IEEE80211_MODE_11G:
	case IEEE80211_MODE_11NA:
	case IEEE80211_MODE_11NG:
	case IEEE80211_MODE_11AC_VHT20PM:
	case IEEE80211_MODE_11AC_NG_VHT20PM:
		bw = BW_HT20;
		break;
	case IEEE80211_MODE_11NA_HT40PM:
	case IEEE80211_MODE_11NG_HT40PM:
	case IEEE80211_MODE_11AC_VHT40PM:
	case IEEE80211_MODE_11AC_NG_VHT40PM:
		bw = BW_HT40;
		break;
	case IEEE80211_MODE_11AC_VHT80PM:
		bw = BW_HT80;
		break;
	case IEEE80211_MODE_11AC_VHT160PM:
		bw = BW_HT160;
		break;
	default:
		bw = BW_INVALID;
		break;
	}

	return bw;
}

static int qdrv_check_channel(struct ieee80211com *ic, struct ieee80211_channel *chan,
			uint32_t flags)
{
	uint16_t band_flags;
	int check_curchan = !ieee80211_is_scanning(ic);
	int bw;
	int fast_switch = flags & IEEE80211_CHANNEL_CHECK_FASTSWITCH;
	int allow_current = flags & IEEE80211_CHANNEL_CHECK_ALLOW_CURRENT;
	int is_bw_switch = flags & IEEE80211_CHANNEL_CHECK_BW_SWITCH;

	if (is_bw_switch && IS_VALID_BW(ic->ic_des_bw))
		bw = ic->ic_des_bw;
	else
		bw = qdrv_wlan_80211_get_cap_bw(ic);

	if (!is_ieee80211_chan_valid(ic->ic_curchan)) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s: rejected - ic_curchan invalid\n", __func__);
		return 0;
	}

	if (!is_ieee80211_chan_valid(chan)) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s: rejected - channel invalid\n", __func__);
		return 0;
	}

	band_flags = ic->ic_curchan->ic_flags &
			(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ);

	if (fast_switch && (!(ieee80211_is_chan_available(chan)) ||
			isset(ic->ic_chan_pri_inactive, chan->ic_ieee))) {
		return 0;
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN,
		"%s: chan=%u isset=%u bw_switch=%d bw=%u flags=0x%08x/0x%04x cur=%u set=%u\n",
		__func__,
		chan->ic_ieee, !!isset(ic->ic_chan_active, chan->ic_ieee), is_bw_switch, bw,
		chan->ic_flags, band_flags, ic->ic_curchan->ic_ieee, !allow_current);

	if (check_curchan && (chan->ic_freq == ic->ic_curchan->ic_freq) && !allow_current) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s: rejected - current\n", __func__);
		return 0;
	};

	if (!ieee80211_is_active_channel_per_bw(ic, chan, bw)) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s: rejected - inactive\n", __func__);
		return 0;
	}

	if (!chan->ic_flags & band_flags) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s: rejected - off band\n", __func__);
		return 0;
	};

	/* ignore channels that do not match the required bandwidth */
	if (IEEE80211_IS_VHT_80(ic) && !is_bw_switch) {
		if (!(chan->ic_flags & IEEE80211_CHAN_VHT80)) {
			DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s: rejected - bw\n", __func__);
			return 0;
		}
	}

	/* ignore channels where radar has been detected */
	switch (bw) {
	case BW_HT160:
	case BW_INVALID:
		DBGPRINTF_E("invalid phy mode %u (needs adding to qdrv_wlan_80211_get_cap_bw?)\n",
				ic->ic_phymode);
		return 0;
	case BW_HT80:
	case BW_HT40:
	case BW_HT20:
		if (ieee80211_is_radar_detected_channel_per_bw(ic, chan, bw)) {
			DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN,
				"%s: rejected - radar detected channel existed\n", __func__);
			printk("selected channel %u cannot be used for %dMHz - has radar\n",
						chan->ic_ieee, bw);
			return 0;
		}
		break;
	}

	ic->ic_chan_is_set = 1;

	DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"%s: channel %u (%u MHz) selected\n",
			__func__, chan->ic_ieee, chan->ic_freq);

	return 1;
}

static void qdrv_chan_occupy_record_finish(struct ieee80211com *ic, uint8_t new_chan)
{
	struct ieee80211_chan_occupy_record *occupy_record = &ic->ic_chan_occupy_record;
	uint8_t cur_chan = occupy_record->cur_chan;

	if ((ic->ic_flags & IEEE80211_F_SCAN) &&
			(ic->ic_scan->ss_flags & IEEE80211_SCAN_NOPICK)) {
		return;
	}

	if (cur_chan && (new_chan != cur_chan)) {
		occupy_record->cur_chan = 0;
		occupy_record->prev_chan = cur_chan;
		occupy_record->duration[cur_chan] += (jiffies - INITIAL_JIFFIES) / HZ -
				occupy_record->occupy_start;
	}
}

static void qdrv_chan_occupy_record_start(struct ieee80211com *ic, uint8_t new_chan)
{
	struct ieee80211_chan_occupy_record *occupy_record = &ic->ic_chan_occupy_record;

	if (occupy_record->cur_chan == 0) {
		occupy_record->cur_chan = new_chan;
		if (occupy_record->prev_chan != new_chan) {
			occupy_record->times[new_chan]++;
		}
		occupy_record->occupy_start = (jiffies - INITIAL_JIFFIES) / HZ;
	}
}

static bool qdrv_chan_compare_equality(struct ieee80211com *ic,
		struct ieee80211_channel *prev_chan, struct ieee80211_channel *new_chan)
{
	int i;
	int ret = false;
	int bw= BW_INVALID;
	struct ieee80211_channel *chan_list[IEEE80211_MAX_CHANNELS_IN_BW(BW_HT80)] = { NULL };

	if ((!prev_chan) || (!new_chan))
		return ret;

	bw = qdrv_wlan_80211_get_cap_bw(ic);
	ic->ic_get_full_bss_chan(ic, prev_chan, chan_list, bw);

	for (i = 0; i < IEEE80211_MAX_CHANNELS_IN_BW(bw); i++) {
		if (chan_list[i] && (chan_list[i] == new_chan))
			ret = true;
	}

	return ret;
}

static void ieee80211_get_full_bss_chan(struct ieee80211com *ic, struct ieee80211_channel *chan,
		struct ieee80211_channel *chan_list[], int cur_bw)
{
	int i = 0;
	struct ieee80211_channel *low_chan = NULL;
	int no_channels = IEEE80211_MAX_CHANNELS_IN_BW(cur_bw);

	if (!is_ieee80211_chan_valid(chan) || (NULL == chan_list) || (cur_bw > BW_HT80))
		return;

	for (i = 0; i < IEEE80211_MAX_CHANNELS_IN_BW(BW_HT80); i++)
		chan_list[i] = NULL;

	switch (cur_bw) {
		case BW_HT80:
			if (chan->ic_ext_flags & IEEE80211_CHAN_VHT80_LL)
				low_chan = chan;
			else if (chan->ic_ext_flags & IEEE80211_CHAN_VHT80_LU)
				low_chan = chan - 1;
			else if (chan->ic_ext_flags & IEEE80211_CHAN_VHT80_UL)
				low_chan = chan - 2;
			else if (chan->ic_ext_flags & IEEE80211_CHAN_VHT80_UU)
				low_chan =chan - 3;
			break;
		case BW_HT40:
			if (chan->ic_flags & IEEE80211_CHAN_HT40D)
				low_chan = chan - 1;
			else
				low_chan = chan;
			break;
		case BW_HT20:
			low_chan = chan;
			break;
		default:
			DBGPRINTF_N("%s: Invalid bandwidth\n", __func__);
			break;
	}

	if (!low_chan)
		return;

	for (i = 0; i < no_channels; i++) {
		if (low_chan + i)
			chan_list[i] = (low_chan + i);
	}
}

static void ieee80211_checkset_chan_availability_status_by_bw(struct ieee80211com *ic, int cur_bw,
		struct ieee80211_channel *chan,
		int to_status,
		bool *chan_update_flag)
{
	int i = 0;
	struct ieee80211_channel *chan_list[IEEE80211_MAX_CHANNELS_IN_BW(BW_HT80)] = { NULL };

#define IEEE80211_UPDATE_CHAN_STATUS(channel_arg, chan_status_arg)		\
	{									\
		ic->ic_mark_channel_availability_status(ic, (channel_arg),	\
				(chan_status_arg));				\
		if (!*chan_update_flag)						\
			*chan_update_flag = true;				\
	}

	if ((!is_ieee80211_chan_valid(chan)) || (NULL == chan_update_flag))
		return;


	ic->ic_get_full_bss_chan(ic, chan, chan_list, cur_bw);

	for (i = 0; i < IEEE80211_MAX_CHANNELS_IN_BW(cur_bw); i++) {
		if (chan_list[i] && (ic->ic_chan_availability_status[chan_list[i]->ic_ieee] == to_status))
			IEEE80211_UPDATE_CHAN_STATUS(chan, to_status);
	}

	ieee80211_reset_nop_timers(ic, chan, chan_list, cur_bw);

	return;
}

static bool ieee80211_checkset_dfs_channel_availability_status(struct ieee80211com *ic, int cur_bw, struct ieee80211_channel *chan)
{
	int j = 0;
	int chan_status [] = {  IEEE80211_CHANNEL_STATUS_NOT_AVAILABLE_RADAR_DETECTED,
		IEEE80211_CHANNEL_STATUS_NOT_AVAILABLE_CAC_REQUIRED,
		IEEE80211_CHANNEL_STATUS_AVAILABLE,
		0 } ;

	bool chan_update_flag = false;

	if (!is_ieee80211_chan_valid(chan) || !ic->ic_dfs_is_status_save_region())
		return false;

	while (chan_status[j])
		ieee80211_checkset_chan_availability_status_by_bw(ic, cur_bw, chan, chan_status[j++], &chan_update_flag);

	return chan_update_flag;
}

#if defined(CONFIG_QTN_BSA_SUPPORT)
static void qdrv_check_send_xcac_event(struct ieee80211com *ic)
{
	if ((ic->ic_xcac_status.pri_chan != ic->ic_curchan->ic_ieee) &&
			!ic->ic_xcac_status.perform_status && IEEE80211_XCAC_CAC_EVENT_EN(ic)) {
		struct ieee80211vap *pri_vap = ieee80211_get_primary_vap(ic, 0);

		if (pri_vap) {
			ieee80211_qrpe_send_event_xcac_status_update(pri_vap,
				IEEE80211_QRPE_XCAC_CANCELLED,
				ic->ic_xcac_status.pri_chan,
				IEEE80211_QRPE_CHAN_STATUS_NOT_AVAILABLE_CAC_REQUIRED);
			ic->ic_xcac_req_flags = 0;
		}
	}
}
#endif

static void qdrv_set_channel(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	uint32_t freq_band;
	uint32_t qtn_chan;
	int handle_radar = !(IEEE80211_IS_CHAN_CACDONE(ic->ic_curchan)) &&
			!(IEEE80211_IS_CHAN_CAC_IN_PROGRESS(ic->ic_curchan));

	ic->sta_dfs_info.allow_measurement_report = false;

	qtn_chan = qdrv_set_channel_setup(ic, ic->ic_curchan);
	freq_band = qdrv_set_channel_freqband_setup(ic, ic->ic_curchan);
	qw->tx_stats.tx_channel = ic->ic_curchan->ic_ieee;

	/* store normal txpower for short range workaround */
	qdrv_hostlink_store_txpow(qw, ic->ic_curchan->ic_maxpower_normal);

	if (ic->ic_chan_compare_equality(ic, qdrv_radar_get_current_cac_chan(), ic->ic_curchan) == false) {
#if defined(CONFIG_QTN_BSA_SUPPORT)
		qdrv_check_send_xcac_event(ic);
#endif
		qdrv_radar_stop_active_cac();
	}

	if (handle_radar) {
		qdrv_radar_before_newchan();
	}

	qdrv_hostlink_setchan(qw, freq_band, qtn_chan);

	qdrv_radar_on_newchan();

	qdrv_chan_occupy_record_finish(ic, ic->ic_curchan->ic_ieee);

	if (!(ic->ic_flags & IEEE80211_F_SCAN)) {
		ic->ic_chan_switch_record(ic, ic->ic_curchan, ic->ic_csw_reason);
		/* Reset ocac_rx_state as we have moved to another channel and should start gathering afresh */
		spin_lock(&ic->ic_ocac.ocac_lock);
		memset(&ic->ic_ocac.ocac_rx_state, 0, sizeof(ic->ic_ocac.ocac_rx_state));
		spin_unlock(&ic->ic_ocac.ocac_lock);
	}
}

static void qtn_scan_start(struct ieee80211com *ic, const uint8_t *scan_addr)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	if (!vap)
		return;

	if (vap->iv_opmode != IEEE80211_M_STA)
		return;

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "Sending SCAN START to MuC\n");
	qdrv_hostlink_setscanmode(qw, true, scan_addr);
}

static void qtn_scan_end(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	if (!vap)
		return;

	if (vap->iv_opmode != IEEE80211_M_STA)
		return;

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "Sending SCAN STOP to MuC\n");
	qdrv_hostlink_setscanmode(qw, false, NULL);
}

static int qdrv_wlan_get_ocs_frame(struct ieee80211com *ic, struct ieee80211_node *ni,
		struct sk_buff *skb, uint32_t *frame_host, uint32_t *frame_bus,
		uint16_t *frame_len, uint16_t *node_idx)
{
	void *buf_virt;
	uintptr_t flush_start;
	size_t flush_size;

	buf_virt = topaz_hbm_get_payload_virt(TOPAZ_HBM_BUF_EMAC_RX_POOL);

	if (unlikely(!buf_virt))
		return -1;

	memcpy(buf_virt, skb->data, skb->len);

	flush_start = (uintptr_t) align_buf_cache(buf_virt);
	flush_size = align_buf_cache_size(buf_virt, skb->len);
	flush_and_inv_dcache_range(flush_start, flush_start + flush_size);

	*frame_host = (uint32_t)buf_virt;
	*frame_bus = (uint32_t)virt_to_bus(buf_virt);
	*frame_len = skb->len;
	*node_idx = IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx);
	dev_kfree_skb(skb);

	return 0;
}

static void qdrv_wlan_release_ocs_frame(struct ieee80211com *ic, uint32_t frame_host)
{
	void *buf_bus = (void *)virt_to_bus((void *)frame_host);
	const int8_t pool = topaz_hbm_payload_get_pool_bus(buf_bus);

	if (unlikely(!topaz_hbm_pool_valid(pool)))
		printk("%s: buf %x is not from hbm pool!\n", __FUNCTION__, frame_host);
	else
		topaz_hbm_put_payload_realign_bus(buf_bus, pool);
}


int qdrv_set_txctl(struct ieee80211com *ic, uint32_t txctl)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	int rc;

	if ((txctl == SET_TXCTL_SUSPEND) && (ic->ic_flags_qtn & IEEE80211_QTN_TXCTL))
		return 0;

	rc = qdrv_hostlink_set_txctl(qw, txctl);
	if (rc < 0 || rc & QTN_HLINK_RC_ERR) {
		DBGPRINTF_E("set_txctl failed, rc %d\n", rc);
		return -1;
	}

	if (txctl == SET_TXCTL_SUSPEND)
		ic->ic_flags_qtn |= IEEE80211_QTN_TXCTL;
	else if (txctl == SET_TXCTL_RESUME)
		ic->ic_flags_qtn &= ~IEEE80211_QTN_TXCTL;

	return 0;
}

static int qdrv_robust_csa_release_frame(struct ieee80211com *ic)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_robust_csa_info *pinfo;
	struct ieee80211vap *vap;
	struct qdrv_vap *qv;
	uint8_t vapid;

	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		qv = container_of(vap, struct qdrv_vap, iv);
		vapid = qv->qv_vap_idx;
		pinfo = sp->robust_csa_lhost[vapid];

		if (pinfo && pinfo->csa_txdesc_host) {
			qdrv_wlan_release_ocs_frame(ic,
					pinfo->csa_txdesc_host);
			pinfo->csa_txdesc_host = 0;
			pinfo->csa_txdesc_bus = 0;
		}
	}

	return 0;
}


static int qdrv_robust_csa_set_frame(struct ieee80211com *ic,
		uint8_t vapid,
		struct ieee80211_node *ni,
		struct sk_buff *skb)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_robust_csa_info *pinfo = sp->robust_csa_lhost[vapid];

	if (!pinfo) {
		DBGPRINTF_E("no csa info for vapid %d\n", vapid);
		return -1;
	}

	if (pinfo->csa_txdesc_host)
		return 0;

	if (!skb) {
		DBGPRINTF_E("skb is null\n");
		return -1;
	}

	if (qdrv_wlan_get_ocs_frame(ic, ni, skb,
			&pinfo->csa_txdesc_host, &pinfo->csa_txdesc_bus,
			&pinfo->csa_frame_len, &pinfo->tx_node_idx)) {
		DBGPRINTF_E("failed to get ocs frame\n");
		return -1;
	}

	return 0;
}


int qdrv_robust_csa_send_frame(struct ieee80211vap *vap,
		u_int8_t csa_mode, u_int8_t csa_chan,
		u_int8_t csa_count, u_int64_t tsf)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct ieee80211_node *ni = vap->iv_bss;
	struct ieee80211com *ic = vap->iv_ic;
	struct qdrv_wlan *qw = qv->parent;
	uint8_t vapid = qv->qv_vap_idx;
	struct sk_buff *skb = NULL;

#ifdef ARTSMNG_SUPPORT
	if (vap->iv_opmode == IEEE80211_M_WDS)
		ni = TAILQ_FIRST(&vap->iv_ic->ic_vaps)->iv_bss;

	if (ni == NULL) {
		DBGPRINTF_E("unable to get node\n");
		return -1;
	}
#endif

	ieee80211_ref_node(ni);
	skb = ieee80211_robust_csa_get_frame(vap, csa_mode, csa_chan,
			csa_count, tsf);
	if (!skb) {
		ieee80211_free_node(ni);
		DBGPRINTF_E("failed to get csa frame\n");
		return -1;
	}

	if (qdrv_robust_csa_set_frame(ic, vapid, ni, skb)) {
		dev_kfree_skb(skb);
		ieee80211_free_node(ni);
		DBGPRINTF_E("failed to set csa frame\n");
		return -1;
	}
	ieee80211_free_node(ni);

	if (qdrv_hostlink_robust_csa_send_frame(qw,
				sp->robust_csa_bus[vapid]) < 0) {
		DBGPRINTF_E("send csa hostlink failed\n");
		return -1;
	}
	IEEE80211_NODE_STAT(ni, tx_mgmt);

	return 0;
}


#if defined(QBMPS_ENABLE)
/*
 * qdrv_bmps_release_frame: release the null frame queued in sp->bmps_lhost
 * return 0: succeed; -1: failed
 */
static int qdrv_bmps_release_frame(struct ieee80211com *ic)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_bmps_info *bmps = sp->bmps_lhost;

	if (bmps->state != BMPS_STATE_OFF) {
		printk("%s: release frame error - status: %d!\n", __FUNCTION__, bmps->state);
		return -1;
	}

	if (bmps->null_txdesc_host) {
		qdrv_wlan_release_ocs_frame(ic, bmps->null_txdesc_host);
		bmps->null_txdesc_host = 0;
		bmps->null_txdesc_bus = 0;
	}

	return 0;
}

/*
 * qdrv_bmps_set_frame: set the frame to sp->bmps_lhost
 * NOTE: MUST be called with a reference to the node entry within
 * the SKB CB structure, and free the reference to the node entry
 * after this calling.
 * return 0: succeed; -1: faild, need free skb by the caller.
 */
static int qdrv_bmps_set_frame(struct ieee80211com *ic,
		struct ieee80211_node *ni, struct sk_buff *skb)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_bmps_info *bmps = sp->bmps_lhost;

	if (bmps->state != BMPS_STATE_OFF) {
		printk("%s: set frame error - status: %d!\n", __FUNCTION__, bmps->state);
		return -1;
	}

	if (!skb) {
		printk("%s: set frame error - skb is null\n", __FUNCTION__);
		return -1;
	}

	/*
         * Release the previous null frame if it has not been released,
         * and then get one new tx descriptor
         */
	if (qdrv_bmps_release_frame(ic) != 0) {
		printk("%s: set frame error - release previous frame fail!\n", __FUNCTION__);
		return -1;
	}

	if (qdrv_wlan_get_ocs_frame(ic, ni, skb,
			&bmps->null_txdesc_host, &bmps->null_txdesc_bus,
			&bmps->null_frame_len, &bmps->tx_node_idx)) {
		printk("%s: set frame error - no ocs frame\n", __FUNCTION__);
		return -1;
	}

	return 0;
}
#endif

#ifdef QSCS_ENABLED
/*
 * qdrv_scs_release_frame: release the qosnull frame queued in sp->chan_sample_lhost
 * return 0: succeed; -1: failed
 */
static int qdrv_scs_release_frame(struct ieee80211com *ic, int force)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_samp_chan_info *sample = sp->chan_sample_lhost;
	struct qtn_off_chan_info *off_chan_info = &sample->base;

	if (!force && off_chan_info->muc_status != QTN_CCA_STATUS_IDLE) {
		SCSDBG(SCSLOG_INFO, "release frame error - status: %u!\n",
				off_chan_info->muc_status);
		return -1;
	}

	if (sample->qosnull_txdesc_host) {
		qdrv_wlan_release_ocs_frame(ic, sample->qosnull_txdesc_host);

		sample->qosnull_txdesc_host = 0;
		sample->qosnull_txdesc_bus = 0;
	}

	SCSDBG(SCSLOG_VERBOSE, "qosnull frame released.\n");

	return 0;
}

/*
 * qdrv_scs_set_frame: set the frame to sp->chan_sample_lhost
 * return 0: succeed; -1: failed.
 */
static int
qdrv_scs_set_frame(struct ieee80211vap *vap, struct qtn_samp_chan_info *sample)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct qtn_off_chan_info *off_chan_info = &sample->base;
	struct sk_buff *skb = NULL;
	int ret = -1;

	if (off_chan_info->muc_status != QTN_CCA_STATUS_IDLE) {
		SCSDBG(SCSLOG_INFO, "set frame error - status=%u\n",
				off_chan_info->muc_status);
		return -1;
	}

	if (sample->qosnull_txdesc_host) {
		SCSDBG(SCSLOG_NOTICE, "Qos Null frame already configured -ignore\n");
		return 0;
	}

	ieee80211_ref_node(ni);

	/* set qosnull frame */
	skb = ieee80211_get_qosnulldata(ni, WME_AC_VO);
	if (!skb) {
		SCSDBG(SCSLOG_NOTICE, "get qosnulldata skb error\n");
		goto done;
	}
	if (qdrv_wlan_get_ocs_frame(ic, ni, skb,
			&sample->qosnull_txdesc_host, &sample->qosnull_txdesc_bus,
			&sample->qosnull_frame_len, &sample->tx_node_idx)) {
		dev_kfree_skb_irq(skb);
		SCSDBG(SCSLOG_NOTICE, "set frame error - no ocs frame\n");
		goto done;
	}

	SCSDBG(SCSLOG_VERBOSE, "set qosnull frame successfully.\n");
	ret = 0;

done:
	ieee80211_free_node(ni);
	return ret;
}

static void qdrv_scs_update_scan_stats(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	if (qdrv_hostlink_send_ioctl_args(qw, IOCTL_DEV_SCS_UPDATE_SCAN_STATS, 0, 0)) {
		SCSDBG(SCSLOG_INFO, "IOCTL_DEV_SCS_UPDATE_SCAN_STATS failed\n");
	}
}

static void qdrv_scs_override_mode(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	if (qdrv_hostlink_send_ioctl_args(qw, IOCTL_DEV_SCS_OVERRIDE_MODE, 0, 0) < 0) {
		SCSDBG(SCSLOG_INFO, "IOCTL_DEV_SCS_OVERRIDE_MODE failed\n");
	}
}

static int qtn_is_traffic_heavy_for_sampling(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qdrv_mac *mac = qw->mac;
	struct net_device *dev;
	struct netdev_queue *txq;
	struct Qdisc *sch;
	u_int32_t i, total_queued_pkts = 0;
	struct ap_state *as = ic->ic_scan->ss_scs_priv;

	/* check the airtime of this bss */
	if (as->as_tx_ms_smth > ic->ic_scs.scs_thrshld_smpl_airtime ||
			as->as_rx_ms_smth > ic->ic_scs.scs_thrshld_smpl_airtime) {
		SCSDBG(SCSLOG_INFO, "not sampling - tx_ms_smth: %u, rx_ms_smth: %u\n",
				as->as_tx_ms_smth, as->as_rx_ms_smth);
		return 1;
	}

	/* check the packet number in tx queue */
	for (i = 0; i <= mac->vnet_last; ++i) {
		dev = mac->vnet[i];
		if (dev && (dev->flags & IFF_UP)) {
			txq = netdev_get_tx_queue(dev, 0);
			sch = txq->qdisc;
			if (sch) {
				total_queued_pkts += sch->q.qlen;
			}
		}
	}
	if (total_queued_pkts > ic->ic_scs.scs_thrshld_smpl_pktnum) {
		SCSDBG(SCSLOG_INFO, "not sampling - queued packet number: %u\n",
				total_queued_pkts);
		return 1;
	}

	return 0;
}

/*
 * disable radar detection and tx queue,
 * trigger a cca read to start.
 * returns 0 if a cca read ioctl was sent to the MuC successfully,
 * < 0 if we are currently performing a cca read or other error
 */
int qdrv_async_cca_read(struct ieee80211com *ic, const struct ieee80211_channel *cca_channel,
		u_int64_t start_tsf, u_int32_t duration)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_samp_chan_info *sample = sp->chan_sample_lhost;
	struct qtn_off_chan_info *off_chan_info = &sample->base;

	if (ic == NULL || cca_channel == NULL || duration == 0) {
		return -EINVAL;
	}

	/* sample channel not allowed in power-saving mode */
	if (((ic->ic_opmode == IEEE80211_M_HOSTAP)
#if defined(QBMPS_ENABLE)
	     || (ic->ic_opmode == IEEE80211_M_STA)
#endif
	    ) && (ic->ic_pm_state[QTN_PM_CURRENT_LEVEL] >= BOARD_PM_LEVEL_DUTY)) {
		SCSDBG(SCSLOG_INFO, "not sampling - CoC idle\n");
		return -EWOULDBLOCK;
	}

	if (off_chan_info->muc_status != QTN_CCA_STATUS_IDLE) {
		return -EWOULDBLOCK;
	}
	sample->start_tsf = start_tsf;
	off_chan_info->dwell_msecs = duration;
	off_chan_info->freq_band = qdrv_set_channel_freqband_setup(ic, cca_channel);
	off_chan_info->channel = qdrv_set_channel_setup(ic, cca_channel);
	off_chan_info->flags = QTN_OFF_CHAN_FLAG_PASSIVE_ONESHOT;
	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
			!(off_chan_info->flags & QTN_OFF_CHAN_FLAG_ACTIVE))
		off_chan_info->flags |= QTN_OFF_CHAN_TURNOFF_RF;
	sample->type = QTN_CCA_TYPE_DIRECTLY;

	/*
	 * TODO SCS: radar.
	 */

	off_chan_info->muc_status = QTN_CCA_STATUS_HOST_IOCTL_SENT;
	if (qdrv_hostlink_sample_chan(qw, sp->chan_sample_bus) >= 0) {
		off_chan_info->muc_status = QTN_CCA_STATUS_IDLE;
	}

	/* indicate sample channel is on-going */
	ic->ic_flags_qtn |= IEEE80211_QTN_SAMP_CHAN;

	return 0;
}
EXPORT_SYMBOL(qdrv_async_cca_read);

static int qdrv_sample_channel_cancel(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_samp_chan_info *sample = sp->chan_sample_lhost;
	struct qtn_off_chan_info *off_chan_info = &sample->base;

	if (qdrv_hostlink_sample_chan_cancel(qw, sp->chan_sample_bus) < 0) {
		off_chan_info->muc_status = QTN_CCA_STATUS_IDLE;
		SCSDBG(SCSLOG_INFO, "hostlink cancel off channel sampling error!\n");
		return -1;
	}

	SCSDBG(SCSLOG_INFO, "cancel off channel sampling\n");

	return 0;
}

static int qdrv_sample_channel(struct ieee80211vap *vap, struct ieee80211_channel *sampled_channel)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_samp_chan_info *sample = sp->chan_sample_lhost;
	struct qtn_off_chan_info *off_chan_info = &sample->base;

	QDRV_SCS_LOG_TSF(sample, SCS_LOG_TSF_POS_LHOST_TASK_KICKOFF);

	if (ic == NULL || sampled_channel == NULL) {
		return -1;
	}

	if (ieee80211_is_scanning(ic)) {
		SCSDBG(SCSLOG_INFO, "not sampling - scan in progress\n");
		return -EAGAIN;
	}

	if (!qdrv_radar_can_sample_chan()) {
		IEEE80211_SCS_CNT_INC(&ic->ic_scs, IEEE80211_SCS_CNT_RADAR);
		SCSDBG(SCSLOG_INFO, "not sampling - radar\n");
		return -1;
	}

	if (qtn_is_traffic_heavy_for_sampling(ic)) {
		IEEE80211_SCS_CNT_INC(&ic->ic_scs, IEEE80211_SCS_CNT_TRAFFIC_HEAVY);
		SCSDBG(SCSLOG_INFO, "not sampling - traffic too heavy\n");
		return -EAGAIN;
	}

	/* sample channel not allowed in power-saving mode */
	if (((ic->ic_opmode == IEEE80211_M_HOSTAP)
#if defined(QBMPS_ENABLE)
	     || (ic->ic_opmode == IEEE80211_M_STA)
#endif
	    ) && (ic->ic_pm_state[QTN_PM_CURRENT_LEVEL] >= BOARD_PM_LEVEL_DUTY)) {
		SCSDBG(SCSLOG_INFO, "not sampling - CoC idle\n");
		return -1;
	}

	if (off_chan_info->muc_status != QTN_CCA_STATUS_IDLE) {
		SCSDBG(SCSLOG_INFO, "not sampling - sampling in progress!\n");
		return -EAGAIN;
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "sampling channel %u\n", sampled_channel->ic_ieee);

	if (!sample->qosnull_txdesc_host && qdrv_scs_set_frame(vap, sample)) {
		IEEE80211_SCS_CNT_INC(&ic->ic_scs, IEEE80211_SCS_CNT_QOSNULL_NOTREADY);
		SCSDBG(SCSLOG_INFO, "not sampling - set qosnull frame error\n");
		return -1;
	}
	sample->start_tsf = 0;
	off_chan_info->dwell_msecs = ic->ic_scs.scs_smpl_dwell_time;
	off_chan_info->freq_band = qdrv_set_channel_freqband_setup(ic, sampled_channel);
	off_chan_info->channel = qdrv_set_channel_setup(ic, sampled_channel);
	off_chan_info->flags = ic->ic_scs.scs_sample_type;
	sample->type = QTN_CCA_TYPE_BACKGROUND;

	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
			!(off_chan_info->flags & QTN_OFF_CHAN_FLAG_ACTIVE)) {
		off_chan_info->flags |= QTN_OFF_CHAN_TURNOFF_RF;
	}

	QDRV_SCS_LOG_TSF(sample, SCS_LOG_TSF_POS_LHOST_IOCTL2MUC);
	IEEE80211_SCS_CNT_INC(&ic->ic_scs, IEEE80211_SCS_CNT_IOCTL);

	off_chan_info->muc_status = QTN_CCA_STATUS_HOST_IOCTL_SENT;
	/* This action should be after changing MUC status */
	if (ic->ic_flags_qtn & IEEE80211_QTN_OCS_NAC_MON)
		off_chan_info->flags |= QTN_OFF_CHAN_FLAG_OCS_NAC_MON;

	if (qdrv_hostlink_sample_chan(qw, sp->chan_sample_bus) < 0) {
		if (ic->ic_flags_qtn & IEEE80211_QTN_OCS_NAC_MON)
			off_chan_info->flags &= ~QTN_OFF_CHAN_FLAG_OCS_NAC_MON;
		off_chan_info->muc_status = QTN_CCA_STATUS_IDLE;
		SCSDBG(SCSLOG_INFO, "hostlink sample channel error!\n");

		return -1;
	}

	/* indicate sample channel is on-going */
	ic->ic_flags_qtn |= IEEE80211_QTN_SAMP_CHAN;
	ic->ic_chan_switch_reason_record(ic, IEEE80211_CSW_REASON_SAMPLING);

	return 0;
}
#endif /* QSCS_ENABLED */

#ifdef QTN_BG_SCAN
/*
 * qdrv_bgscan_start: reset bgscan status if it is not idle or completed
 * return 0: succeed; -1: failed
 */
static int qdrv_bgscan_start(struct ieee80211com *ic)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_scan_chan_info *scan_host = sp->chan_scan_lhost;
	struct qtn_off_chan_info *off_chan_info = &scan_host->base;

	if (off_chan_info->muc_status != QTN_SCAN_CHAN_MUC_IDLE
			&& off_chan_info->muc_status != QTN_SCAN_CHAN_MUC_COMPLETED) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: status (%u) is not idle or completed!\n",
					off_chan_info->muc_status);
		}
		off_chan_info->muc_status = QTN_SCAN_CHAN_MUC_IDLE;
	}

	return 0;
}

/*
 * qdrv_bgscan_release_frame: delete the frame queued in sp->chan_scan_lhost
 * return 0: succeed; -1: failed
 */
static int qdrv_bgscan_release_frame(struct ieee80211com *ic, int frm_flag, int force)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_scan_chan_info *scan_host = sp->chan_scan_lhost;
	struct qtn_off_chan_info *off_chan_info = &scan_host->base;

	if (!force && off_chan_info->muc_status != QTN_SCAN_CHAN_MUC_IDLE
			&& off_chan_info->muc_status != QTN_SCAN_CHAN_MUC_COMPLETED) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: release frame error for current muc_status: %u!\n",
					off_chan_info->muc_status);
		}
		return -1;
	}

	if (frm_flag == IEEE80211_SCAN_FRAME_START
			|| frm_flag == IEEE80211_SCAN_FRAME_ALL) {
		if (scan_host->start_txdesc_host) {
			qdrv_wlan_release_ocs_frame(ic, scan_host->start_txdesc_host);
			scan_host->start_txdesc_host = 0;
			scan_host->start_txdesc_bus = 0;
			if (ic->ic_qtn_bgscan.debug_flags >= 3) {
				printk("BG_SCAN: delete start frame!\n");
			}
		}
	}

	if (frm_flag == IEEE80211_SCAN_FRAME_PRBREQ
			|| frm_flag == IEEE80211_SCAN_FRAME_ALL) {
		if (scan_host->prbreq_txdesc_host) {
			qdrv_wlan_release_ocs_frame(ic, scan_host->prbreq_txdesc_host);
			scan_host->prbreq_txdesc_host = 0;
			scan_host->prbreq_txdesc_bus = 0;
			if (ic->ic_qtn_bgscan.debug_flags >= 3) {
				printk("BG_SCAN: delete prbreq frame!\n");
			}
		}
	}

	if (frm_flag == IEEE80211_SCAN_FRAME_SPEC_PRBREQ
			|| frm_flag == IEEE80211_SCAN_FRAME_ALL) {
		if (scan_host->spec_prbreq_txdesc_host) {
			qdrv_wlan_release_ocs_frame(ic, scan_host->spec_prbreq_txdesc_host);
			scan_host->spec_prbreq_txdesc_host = 0;
			scan_host->spec_prbreq_txdesc_bus = 0;
			if (ic->ic_qtn_bgscan.debug_flags >= 3) {
				printk("BG_SCAN: delete spec_prbreq frame!\n");
			}
		}
	}

	if (frm_flag == IEEE80211_SCAN_FRAME_FINISH
			|| frm_flag == IEEE80211_SCAN_FRAME_ALL) {
		if (scan_host->finish_txdesc_host) {
			qdrv_wlan_release_ocs_frame(ic, scan_host->finish_txdesc_host);
			scan_host->finish_txdesc_host = 0;
			scan_host->finish_txdesc_bus = 0;
			if (ic->ic_qtn_bgscan.debug_flags >= 3) {
				printk("BG_SCAN: delete finish frame!\n");
			}
		}
	}

	return 0;
}
/*
 * qdrv_bgscan_set_frame: set the frame to sp->chan_scan_lhost
 * NOTE: MUST be called with a reference to the node entry within
 * the SKB CB structure, and free the reference to the node entry
 * after this calling.
 * return 0: succeed; -1: faild, need free skb by the caller.
 */
static int qdrv_bgscan_set_frame(struct ieee80211com *ic,
		struct ieee80211_node *ni, struct sk_buff *skb, int frm_flag)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_scan_chan_info *scan_host = sp->chan_scan_lhost;
	uint32_t frame_host;
	uint32_t frame_bus;
	uint16_t frame_len;
	uint16_t node_idx;

	if (qdrv_wlan_get_ocs_frame(ic, ni, skb,
			&frame_host, &frame_bus, &frame_len, &node_idx)) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: set frame error - no ocs frame\n");
		}
		return -1;
	}

	switch (frm_flag) {
	case IEEE80211_SCAN_FRAME_START:
		scan_host->start_txdesc_host = frame_host;
		scan_host->start_txdesc_bus = frame_bus;
		scan_host->start_frame_len = frame_len;
		scan_host->start_node_idx = node_idx;
		break;
	case IEEE80211_SCAN_FRAME_PRBREQ:
		scan_host->prbreq_txdesc_host = frame_host;
		scan_host->prbreq_txdesc_bus = frame_bus;
		scan_host->prbreq_frame_len = frame_len;
		scan_host->prbreq_node_idx = node_idx;
		break;
	case IEEE80211_SCAN_FRAME_SPEC_PRBREQ:
		scan_host->spec_prbreq_txdesc_host = frame_host;
		scan_host->spec_prbreq_txdesc_bus = frame_bus;
		scan_host->spec_prbreq_frame_len = frame_len;
		scan_host->spec_prbreq_node_idx = node_idx;
		break;
	case IEEE80211_SCAN_FRAME_FINISH:
		scan_host->finish_txdesc_host = frame_host;
		scan_host->finish_txdesc_bus = frame_bus;
		scan_host->finish_frame_len = frame_len;
		scan_host->finish_node_idx = node_idx;
		break;
	}

	if (ic->ic_qtn_bgscan.debug_flags >= 3) {
		printk("BG_SCAN: set frame flag %u\n", frm_flag);
	}

	return 0;
}

static int
qdrv_bgscan_init_frames(struct ieee80211vap *vap,
		struct qtn_scan_chan_info *scan_host, uint8_t *scan_bssid)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct net_device *dev = vap->iv_dev;
	struct sk_buff *skb = NULL;
	int ret = -1;

	ieee80211_ref_node(ni);

	if (!scan_host->start_txdesc_host) {
		/* set start frame */
		skb = ieee80211_get_qosnulldata(ni, WME_AC_VO);
		if (!skb) {
			if (ic->ic_qtn_bgscan.debug_flags >= 1) {
				printk("BG_SCAN: get qosnulldata skb error\n");
			}
			goto done;
		}
		if (qdrv_bgscan_set_frame(ic, ni, skb, IEEE80211_SCAN_FRAME_START)) {
			dev_kfree_skb_irq(skb);
			goto done;
		}
	}

	if (!scan_host->prbreq_txdesc_host) {
		/* set probe request frame */
		skb = ieee80211_get_probereq(vap->iv_bss,
			vap->iv_myaddr, scan_bssid,
			dev->broadcast, (u_int8_t *)"", 0,
			vap->iv_opt_ie, vap->iv_opt_ie_len);
		if (!skb) {
			if (ic->ic_qtn_bgscan.debug_flags >= 1) {
				printk("BG_SCAN: get probereq skb error\n");
			}
			goto done;
		}
		if (qdrv_bgscan_set_frame(ic, ni, skb, IEEE80211_SCAN_FRAME_PRBREQ)) {
			dev_kfree_skb_irq(skb);
			goto done;
		}
	}

	if ((vap->iv_des_nssid > 0) && (!scan_host->spec_prbreq_txdesc_host)) {
		/* set specific ssid probe request frame */
		skb = ieee80211_get_probereq(vap->iv_bss,
			vap->iv_myaddr, scan_bssid,
			dev->broadcast, vap->iv_des_ssid[0].ssid,
			vap->iv_des_ssid[0].len, vap->iv_opt_ie,
			vap->iv_opt_ie_len);
		if (!skb) {
			if (ic->ic_qtn_bgscan.debug_flags >= 1) {
				printk("BG_SCAN: get specific ssid probereq skb error\n");
			}
			goto done;
		}
		if (qdrv_bgscan_set_frame(ic, ni, skb, IEEE80211_SCAN_FRAME_SPEC_PRBREQ)) {
			dev_kfree_skb_irq(skb);
			goto done;
		}
	}

	ret = 0;

done:
	ieee80211_free_node(ni);
	return ret;
}

enum scan_mode_index {
	SCAN_MODE_INDEX_ACTIVE = 0,
	SCAN_MODE_INDEX_PASSIVE_FAST,
	SCAN_MODE_INDEX_PASSIVE_NORMAL,
	SCAN_MODE_INDEX_PASSIVE_SLOW,
	SCAN_MODE_INDEX_PASSIVE_ONCE,
};

static char *scan_mode_str[] = {
		"active",
		"passive_fast",
		"passive_normal",
		"passive_slow",
		"passive_once"
};

static int qdrv_bgscan_channel(struct ieee80211vap *vap,
		struct ieee80211_channel *scanned_channel, int bgscan_flags,
		int dwelltime, int dwell_total, int scan_bw, int max_duration, uint8_t *scan_bssid)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_scan_chan_info *scan_host = sp->chan_scan_lhost;
	struct qtn_off_chan_info *off_chan_host = &scan_host->base;
	int mode_index;

	if ((vap->iv_state != IEEE80211_S_RUN) && !(ic->ic_flags_ext & IEEE80211_FEXT_REPEATER))
		return -1;

	if (scanned_channel == NULL) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1)
			printk("BG_SCAN: stop - wrong parameters\n");
		return -1;
	}

	if (ic->ic_flags & IEEE80211_F_SCAN) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: stop - regular scan in progress\n");
		}
		return -1;
	}

	if (!qdrv_radar_can_sample_chan()) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: stop - CAC in progress\n");
		}
		return -1;
	}

	if (off_chan_host->muc_status != QTN_SCAN_CHAN_MUC_IDLE
			&& off_chan_host->muc_status != QTN_SCAN_CHAN_MUC_COMPLETED) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: stop - status=%u\n",
					off_chan_host->muc_status);
		}
		return -1;
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "scanning channel %u\n", scanned_channel->ic_ieee);

	if (!scan_host->start_txdesc_host || !scan_host->prbreq_txdesc_host ||
			(vap->iv_des_nssid > 0) || (ic->ic_rf_chipid == CHIPID_DUAL)) {
		if (scan_host->spec_prbreq_txdesc_host) {
			/*
			 * SSID is subject to change by upper layer; A new specific
			 * probe request is allocated every time. The old buffer is freed
			 */
			if (qdrv_bgscan_release_frame(ic,
					IEEE80211_SCAN_FRAME_SPEC_PRBREQ, 0) != 0) {
				printk(KERN_WARNING
					"BG_SCAN: failed to free specific probe request\n");
				return -1;
			}
		}

		if (scan_host->prbreq_txdesc_host) {
			/*
			 * RFIC6 needs to scan both 2.4G and 5G bands, some IEs
			 * (supported rates) would change between two bands, so
			 * free probe req and reallocate it
			 */
			if (qdrv_bgscan_release_frame(ic,
					IEEE80211_SCAN_FRAME_PRBREQ, 0) != 0) {
				printk(KERN_WARNING
					"BG_SCAN: failed to free probe request\n");
				return -1;
			}
		}

		if (qdrv_bgscan_init_frames(vap, scan_host, scan_bssid)) {
			if (ic->ic_qtn_bgscan.debug_flags >= 1) {
				printk("BG_SCAN: Initiate scan frames error\n");
			}
			return -1;
		}
	}

	off_chan_host->freq_band = qdrv_set_channel_freqband_setup(ic, scanned_channel);
	if (scan_bw == BW_HT20)
		ic->ic_flags_ext |= IEEE80211_FEXT_SCAN_20;
	else if (scan_bw == BW_HT40)
		ic->ic_flags_ext |= IEEE80211_FEXT_SCAN_40;

	off_chan_host->channel = qdrv_set_channel_setup(ic, scanned_channel);
	ic->ic_flags_ext &= (~(IEEE80211_FEXT_SCAN_20 | IEEE80211_FEXT_SCAN_40));
	off_chan_host->dwell_msecs = dwelltime;
	off_chan_host->dwell_total = dwell_total;
	off_chan_host->max_duration = max_duration;
	scan_host->start_node_idx = IEEE80211_NODE_IDX_UNMAP(vap->iv_bss->ni_node_idx) ?
			IEEE80211_NODE_IDX_UNMAP(vap->iv_bss->ni_node_idx) :
			IEEE80211_NODE_IDX_UNMAP(vap->iv_vapnode_idx);
	scan_host->prbreq_node_idx = scan_host->start_node_idx;
	scan_host->spec_prbreq_node_idx = scan_host->start_node_idx;
	scan_host->finish_node_idx = scan_host->start_node_idx;
	off_chan_host->flags = bgscan_flags;

	if (bgscan_flags & QTN_OFF_CHAN_FLAG_ACTIVE)
		mode_index = SCAN_MODE_INDEX_ACTIVE;
	else if (bgscan_flags & QTN_OFF_CHAN_FLAG_PASSIVE_FAST)
		mode_index = SCAN_MODE_INDEX_PASSIVE_FAST;
	else if (bgscan_flags & QTN_OFF_CHAN_FLAG_PASSIVE_NORMAL)
		mode_index = SCAN_MODE_INDEX_PASSIVE_NORMAL;
	else if (bgscan_flags & QTN_OFF_CHAN_FLAG_PASSIVE_ONESHOT)
		mode_index = SCAN_MODE_INDEX_PASSIVE_ONCE;
	else
		mode_index = SCAN_MODE_INDEX_PASSIVE_SLOW;

	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
			!(off_chan_host->flags & QTN_OFF_CHAN_FLAG_ACTIVE)) {
		off_chan_host->flags |= QTN_OFF_CHAN_TURNOFF_RF;
	}

	if ((ic->ic_flags_ext2 & IEEE80211_FEXT2_BGSCAN_TURNOFF_RF) &&
			!ieee80211_is_chan_available(scanned_channel))
		off_chan_host->flags |= QTN_OFF_CHAN_TURNOFF_RF;

	if (ic->ic_country_code == CTRY_DEFAULT && off_chan_host->flags & QTN_OFF_CHAN_FLAG_ACTIVE)
		off_chan_host->flags &= ~QTN_OFF_CHAN_TURNOFF_RF;
	if (ic->ic_qtn_bgscan.debug_flags >= 3) {
		pr_info("BG_SCAN: channel %u, mode: %s, dwell %d, dur %d, cca_idle: %u, turnoff_rf %u, ps %u\n",
			scanned_channel->ic_ieee,
			scan_mode_str[mode_index],
			dwelltime, max_duration,
			ic->ic_scs.scs_cca_idle_smthed,
			off_chan_host->flags & QTN_OFF_CHAN_TURNOFF_RF ? 1 : 0,
			off_chan_host->flags & QTN_OFF_CHAN_FAKE_POWERSAVE ? 1 : 0);
	}

	IEEE80211_LOCK_IRQ(ic);
	ic->ic_flags |= IEEE80211_F_SCAN;
	IEEE80211_UNLOCK_IRQ(ic);
	QDRV_SCAN_LOG_TSF(scan_host, SCAN_CHAN_TSF_LHOST_HOSTLINK_IOCTL);
	if (qdrv_hostlink_bgscan_chan(qw, sp->chan_scan_bus) < 0) {
		IEEE80211_LOCK_IRQ(ic);
		ic->ic_flags &= ~IEEE80211_F_SCAN;
		IEEE80211_UNLOCK_IRQ(ic);
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: hostlink error\n");
		}
		return -1;
	}
	ic->ic_chan_switch_reason_record(ic, IEEE80211_CSW_REASON_BGSCAN);

	return 0;
}

static int qdrv_bgscan_end(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();

	if (qdrv_hostlink_bgscan_end(qw, sp->chan_scan_bus) < 0) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1)
			printk("BG_SCAN_END: hostlink error\n");
		return -1;
	}
	return 0;
}
#endif /* QTN_BG_SCAN */

static void qdrv_update_sta_dfs_strict_flags(struct ieee80211com *ic)
{
	struct ieee80211_channel *chan;

	if (ic->sta_dfs_info.sta_dfs_radar_detected_timer) {
		ic->sta_dfs_info.sta_dfs_radar_detected_timer = false;
		del_timer(&ic->sta_dfs_info.sta_radar_timer);
		chan = ieee80211_find_channel_by_ieee(ic,
				ic->sta_dfs_info.sta_dfs_radar_detected_channel);
		ic->ic_mark_channel_availability_status(ic, chan,
				IEEE80211_CHANNEL_STATUS_NOT_AVAILABLE_RADAR_DETECTED);
		ic->sta_dfs_info.sta_dfs_radar_detected_channel = 0;
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN,
				"\n%s: sta_radar_timer: deleted\n", __func__);
	}

	if (ic->ic_chan_compare_equality(ic, ic->ic_curchan, ic->ic_prevchan) == false) {
		if (ieee80211_is_chan_not_available(ic->ic_curchan)) {
			ic->ic_mark_channel_availability_status(ic,
					ic->ic_curchan,
					IEEE80211_CHANNEL_STATUS_AVAILABLE);
		}
		if (ieee80211_is_chan_available(ic->ic_prevchan)) {
			ic->ic_mark_channel_availability_status(ic,
					ic->ic_prevchan,
					IEEE80211_CHANNEL_STATUS_NON_AVAILABLE);
		}
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s: qdrv_set_channel_deferred: "
		"ic->ic_prevchan = %d, ic->ic_curchan = %d\n",
		__func__, ic->ic_prevchan->ic_ieee, ic->ic_curchan->ic_ieee);
	ic->sta_dfs_info.allow_measurement_report = false;
}


/*
 * deferred channel change, where the MuC handles the channel change,
 * aiming to change at a particular tsf, and notifies the lhost when it occurs.
 */
static void qdrv_set_channel_deferred(struct ieee80211com *ic, u64 tsf, int csaflags)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_csa_info *csa = sp->csa_lhost;
	unsigned long irqflags;
	int newchan_radar = qdrv_radar_is_rdetection_required(ic->ic_csa_chan);
	uint8_t prev_ieee = ic->ic_curchan->ic_ieee;
	uint8_t cur_ieee = ic->ic_csa_chan->ic_ieee;

	spin_lock_irqsave(&qw->csa_lock, irqflags);

	if (csaflags & IEEE80211_SET_CHANNEL_DEFERRED_CANCEL) {
		/*
		 * the muc may pick this up, but if it doesn't it will
		 * complete the entire channel change
		 */
		csa->lhost_status |= QTN_CSA_CANCEL;
		spin_unlock_irqrestore(&qw->csa_lock, irqflags);
		return;
	}

	if (csa->lhost_status & QTN_CSA_STATUS_LHOST_ACTIVE) {
		/* csa in progress */
		spin_unlock_irqrestore(&qw->csa_lock, irqflags);
		DBGPRINTF_E("CSA already active\n");
		return;
	}

	csa->lhost_status = QTN_CSA_STATUS_LHOST_ACTIVE;
	if (csaflags & IEEE80211_SET_CHANNEL_TSF_OFFSET) {
		csa->lhost_status |= QTN_CSA_STATUS_LHOST_UNITS_OFFSET;
	}
	if (!newchan_radar) {
		csa->lhost_status |= QTN_CSA_RESTART_QUEUE;
	}
	spin_unlock_irqrestore(&qw->csa_lock, irqflags);

	ic->ic_prevchan = ic->ic_curchan;
	ic->ic_curchan = ic->ic_des_chan = ic->ic_csa_chan;
	ic->ic_bsschan = ic->ic_csa_chan;
	csa->channel = qdrv_set_channel_setup(ic, ic->ic_curchan);
	csa->freq_band = qdrv_set_channel_freqband_setup(ic, ic->ic_curchan);
	csa->req_tsf = tsf;
	csa->pre_notification_tu = 10; /* Time Units */
	csa->post_notification_tu = 10;

	if (ic->sta_dfs_info.sta_dfs_strict_mode) {
		qdrv_update_sta_dfs_strict_flags(ic);
	}

	qdrv_hostlink_setchan_deferred(qw, sp->csa_bus);

	ic->ic_aci_cci_cce.cce_previous = prev_ieee;
	ic->ic_aci_cci_cce.cce_current = cur_ieee;
}

void qdrv_wlan_region_update(struct net_device *ndev, unsigned int iso_code,
			enum ieee80211_opmode opmode)
{
	char if_mode[4];
	char region[6] = "none";
	char *argv[] = { "/scripts/set_region", ndev->name, region, if_mode, "0", NULL };
	char *envp[] = { "HOME=/",
			 "TERM=linux",
			 "PATH=/sbin:/usr/sbin:/bin:/usr/bin:/scripts",
			 NULL };
	int ret;

	if (opmode == IEEE80211_M_STA)
		snprintf(if_mode, sizeof(if_mode), "%s", "sta");
	else
		snprintf(if_mode, sizeof(if_mode), "%s", "ap");

	if (iso_code != CTRY_DEFAULT) {
		ret = ieee80211_countryid_to_country_string(iso_code, region);

		if (ret != 0) {
			DBGPRINTF_E("Update region failed. Invalid country id %d\n",
				iso_code);
			return;
		}

		region[0] = tolower(region[0]);
		region[1] = tolower(region[1]);
	}

	pr_info("Setting region for %s (mode=%s) to %s\n", ndev->name,
		if_mode, region);

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (ret)
		DBGPRINTF_E("Update region failed. usermodehelper returns %d\n", ret);
}
EXPORT_SYMBOL(qdrv_wlan_region_update);

static void qdrv_update_region_work(struct work_struct *work)
{
	struct ieee80211com *ic = container_of(work, struct ieee80211com, region_work);
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	if (!vap)
		return;

	if (vap->iv_opmode != IEEE80211_M_STA) {
		DBGPRINTF_E("Update region failed. Invalid vap mode %d\n", vap->iv_opmode);
		return;
	}

	qdrv_wlan_region_update(vap->iv_dev, ic->ic_country_code_for_update, vap->iv_opmode);
}

static void csa_work(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, csa_wq);
	struct ieee80211com *ic = &qw->ic;
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_csa_info *csa = sp->csa_lhost;
	struct ieee80211_channel *chan = qw->ic.ic_curchan;
	unsigned long irqflags;
	u64 tsf = csa->switch_tsf;
#if defined(CONFIG_QTN_80211K_SUPPORT)
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 0);
#endif

	/* mark the entire process as done */
	spin_lock_irqsave(&qw->csa_lock, irqflags);
	csa->lhost_status = 0;
	/* clear csa count at last to avoid any possibility of dual CS */
	ic->ic_csa_count = 0;
	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "total rx CSA frame: beacon=%d, action=%d\n",
			ic->ic_csa_frame[IEEE80211_CSA_FRM_BEACON],
			ic->ic_csa_frame[IEEE80211_CSA_FRM_ACTION]);
	memset(ic->ic_csa_frame, 0x0, sizeof(ic->ic_csa_frame));
	spin_unlock_irqrestore(&qw->csa_lock, irqflags);

	if (qw->csa_callback) {
		qw->csa_callback(chan, tsf);
	}

#if defined(CONFIG_QTN_80211K_SUPPORT)
	if (vap && vap->iv_state == IEEE80211_S_RUN)
		ieee80211_send_action_dfs_report(vap->iv_bss);
#endif
}

static void channel_work(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, channel_work_wq);
	struct ieee80211com *ic = &qw->ic;

	ieee80211_channel_switch_post(ic);
	qdrv_radar_on_newchan();

	/* Check if there is pending CSA */
	ieee80211_trigger_pending_csa(ic);
}

static void remain_channel_work(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, remain_chan_wq);
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	if (!vap)
		return;

	ieee80211_sta_pwrsave(vap, 0);
	vap->tdls_chan_switching = 0;
	vap->chanswitch_node = NULL;

	if (vap->tdls_cs_disassoc_pending == 1) {
		vap->tdls_cs_disassoc_pending = 0;
		vap->iv_newstate(vap, IEEE80211_S_INIT, 0);
	}

#if defined(QBMPS_ENABLE)
	/* indicate sample channel is done */
	/* start power-saving if allowed */
	ic->ic_flags_qtn &= ~IEEE80211_QTN_SAMP_CHAN;
	ic->ic_pm_reason = IEEE80211_PM_LEVEL_REMAIN_CHANNEL_WORK;
	ieee80211_pm_queue_work(ic);
#endif
}

static void qdrv_csa_irqhandler(void *arg1, void *arg2)
{
	struct qdrv_wlan *qw = arg1;
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_remain_chan_info *rc = sp->remain_chan_lhost;
	struct qtn_csa_info *csa = sp->csa_lhost;
	struct ieee80211_channel *chan = NULL;
	u32 muc_status = csa->muc_status;
	u32 lhost_status = csa->lhost_status;

	if (!vap)
		return;

	if (rc->status == QTN_REM_CHAN_STATUS_MUC_STARTED) {
		chan = ic->ic_findchannel(ic,
			QTNCHAN_TO_IEEENUM(rc->off_channel), ic->ic_des_mode);
		if (chan) {
			ic->ic_curchan = chan;
			vap->tdls_chan_switching = 1;
			ieee80211_remain_on_chan_prepare_send_event(vap, ic->roc_frequency,
									ic->roc_duration, 0);
		}
		return;
	} else if ((rc->status == QTN_REM_CHAN_STATUS_MUC_COMPLETE) ||
			(rc->status == QTN_REM_CHAN_STATUS_MUC_CANCELLED)) {
		rc->status = QTN_REM_CHAN_STATUS_IDLE;
		ic->ic_curchan = ic->ic_bsschan;
		schedule_work(&qw->remain_chan_wq);
		return;
	}

	if (muc_status & QTN_CSA_STATUS_MUC_PRE &&
			!(lhost_status & QTN_CSA_STATUS_LHOST_PRE_DONE)) {
		qdrv_radar_before_newchan();
		lhost_status |= QTN_CSA_STATUS_LHOST_PRE_DONE;
	}

	if (muc_status & QTN_CSA_STATUS_MUC_SWITCHED &&
			!(lhost_status & QTN_CSA_STATUS_LHOST_SWITCH_DONE)) {
		/* just switched channel, restart radar */

		qw->tx_stats.tx_channel = ic->ic_curchan->ic_ieee;
		schedule_work(&qw->channel_work_wq);

		lhost_status |= QTN_CSA_STATUS_LHOST_SWITCH_DONE;
	}

	if (muc_status & QTN_CSA_STATUS_MUC_POST &&
			!(lhost_status & QTN_CSA_STATUS_LHOST_POST_DONE)) {
		/* all done! */
		lhost_status |= QTN_CSA_STATUS_LHOST_POST_DONE;
		/* call workqueue to handle callback */
		schedule_work(&qw->csa_wq);
	}

	/* successfully cancelled */
	if ((muc_status & QTN_CSA_STATUS_MUC_CANCELLED) &&
			(muc_status & QTN_CSA_STATUS_MUC_COMPLETE)) {
		lhost_status = 0;
		ic->ic_csa_count = 0;
		memset(ic->ic_csa_frame, 0x0, sizeof(ic->ic_csa_frame));
	}

	csa->lhost_status = lhost_status;
}

static int qdrv_init_csa_irqhandler(struct qdrv_wlan *qw)
{
	struct int_handler int_handler;

	int_handler.handler = qdrv_csa_irqhandler;
	int_handler.arg1 = qw;
	int_handler.arg2 = NULL;

	if (qdrv_mac_set_handler(qw->mac, RUBY_M2L_IRQ_LO_CSA, &int_handler) != 0) {
		DBGPRINTF_E("Could not set csa irq handler\n");
		return -EINVAL;
	}

	qdrv_mac_enable_irq(qw->mac, RUBY_M2L_IRQ_LO_CSA);

	return 0;
}

static struct off_chan_tsf_dbg scs_tsf_index_name[] = {
	{SCS_LOG_TSF_POS_LHOST_TASK_KICKOFF,			"host_task_start"},
	{SCS_LOG_TSF_POS_LHOST_IOCTL2MUC,			"host_send_ioctl"},
	{SCS_LOG_TSF_POS_MUC_POLL_IOCTL_FROM_LHOST,		"muc_ioctl_proc"},
	{SCS_LOG_TSF_POS_MUC_QOSNULL_SENT,			"muc_qosnull_sent"},
	{SCS_LOG_TSF_POS_MUC_SMPL_START_BEFORE_CHAN_CHG,	"muc_goto_offchan"},
	{SCS_LOG_TSF_POS_MUC_SMPL_START_AFTER_CHAN_CHG,		"muc_offchan_done"},
	{SCS_LOG_TSF_POS_MUC_SMPL_FINISH_BEFORE_CHAN_CHG,	"muc_goto_datachan"},
	{SCS_LOG_TSF_POS_MUC_SMPL_FINISH_AFTER_CHAN_CHG,	"muc_datachan_done"},
	{SCS_LOG_TSF_POS_LHOST_CCA_WORK,			"host_scswork"},
	{0,NULL}
};

static int
qdrv_bgscan_phy_stats_update(struct qdrv_wlan *qw, struct qtn_off_chan_info *off_chan_info)
{
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_scs_bgscan_info *bgscan_info = &sp->scs_info_lhost->bgscan_info;
	struct ieee80211_chan_phy_stats phy_stats = {0};
	uint8_t cnt = 0;
	uint8_t chan = 0;
	uint8_t bw = 0;
	uint32_t cca_try = 0;
	uint32_t cca_pri = 0;
	uint32_t cca_sec20 = 0;
	uint32_t cca_sec40 = 0;
	uint32_t dwell_msecs = 0;
	int32_t hw_noise = 0;

	if (bgscan_info->oc_info_count) {
		int i;
		struct qtn_scs_oc_info *p_oc_info;

		for (i = 0; i < bgscan_info->oc_info_count; i++) {
			p_oc_info = &bgscan_info->oc_info[i];
			if (p_oc_info->off_chan_cca_try_cnt) {
				if (!ieee80211_find_channel_by_ieee(ic, p_oc_info->off_channel))
					continue;
				if (chan) {
					if (chan != p_oc_info->off_channel) {
						DBGPRINTF_E("bgscan channel mismatch!\n");
						break;
					}
				} else {
					chan = p_oc_info->off_channel;
				}

				ieee80211_scs_scale_offchan_data(ic, p_oc_info);

				bw = p_oc_info->off_chan_bw_sel;
				cca_try += p_oc_info->off_chan_cca_try_cnt;
				cca_pri += p_oc_info->off_chan_cca_pri;
				cca_sec20 += p_oc_info->off_chan_cca_sec;
				cca_sec40 += p_oc_info->off_chan_cca_sec40;
				dwell_msecs += off_chan_info->dwell_msecs;
				hw_noise += p_oc_info->off_chan_hw_noise;
				cnt++;
			}
		}
		if (cnt) {
			phy_stats.bw = bw;
			phy_stats.cca_try = cca_try / cnt;
			phy_stats.cca_pri = cca_pri / cnt;
			phy_stats.cca_sec20 = cca_sec20 / cnt;
			phy_stats.cca_sec40 = cca_sec40 / cnt;
			phy_stats.sample_duration = dwell_msecs;
			phy_stats.hw_noise = hw_noise / cnt;
			phy_stats.ispassive = !(off_chan_info->flags & QTN_OFF_CHAN_FLAG_ACTIVE);
			ieee80211_chan_phy_stats_update(ss, &phy_stats, chan);
		}
	}
	return 0;
}

static void cca_work(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, cca_wq);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_samp_chan_info *sample = sp->chan_sample_lhost;
	struct qtn_off_chan_info *off_chan_info = &sample->base;
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211vap *vap;
	uint32_t tsf_hi;
	uint32_t tsf_lo;
	uint32_t delta;
	uint32_t cur_index;
	uint32_t pre_index = 0;
	int i;

	if (off_chan_info->muc_status == QTN_CCA_STATUS_MUC_COMPLETE) {
		qdrv_bgscan_phy_stats_update(qw, off_chan_info);
		QDRV_SCS_LOG_TSF(sample, SCS_LOG_TSF_POS_LHOST_CCA_WORK);
		IEEE80211_SCS_CNT_INC(&ic->ic_scs, IEEE80211_SCS_CNT_COMPLETE);
		ic->ic_scs.scs_last_smpl_chan = ic->ic_scs.scs_des_smpl_chan;

		if (ic->ic_flags & IEEE80211_F_CCA) {
			ic->ic_flags &= ~IEEE80211_F_CCA;
			TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
				if (vap->iv_opmode != IEEE80211_M_HOSTAP)
					continue;

				if ((vap->iv_state != IEEE80211_S_RUN) && (vap->iv_state != IEEE80211_S_SCAN))
					continue;

				ic->ic_beacon_update(vap);
			}
		}

		SCSDBG(SCSLOG_INFO, "Sample channel %u completed\n",
				QTNCHAN_TO_IEEENUM(off_chan_info->channel));
		off_chan_info->muc_status = QTN_CCA_STATUS_IDLE;
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "Sampling finished\n");

		if (ic->ic_scs.scs_debug_enable == 3) {
			for (i = 0; scs_tsf_index_name[i].log_name; i++) {
				cur_index = scs_tsf_index_name[i].pos_index;
				tsf_hi = U64_HIGH32(sample->tsf[cur_index]);
				tsf_lo = U64_LOW32(sample->tsf[cur_index]);
				if (i) {
					delta = (uint32_t)(sample->tsf[cur_index] -
								sample->tsf[pre_index]);
				} else {
					delta = 0;
					printk("\n\nSCS_SAMPLE_tsf:\n");
				}
				pre_index = cur_index;
				printk("  %s:    %08x_%08x (+%u)\n",
					scs_tsf_index_name[i].log_name, tsf_hi, tsf_lo, delta);
			}
		}
	} else if (off_chan_info->muc_status == QTN_CCA_STATUS_MUC_CANCELLED) {
		SCSDBG(SCSLOG_INFO, "Sample channel %u cancelled\n",
				QTNCHAN_TO_IEEENUM(off_chan_info->channel));
		off_chan_info->muc_status = QTN_CCA_STATUS_IDLE;
	}

	if (ic->ic_flags_qtn & IEEE80211_QTN_OCS_NAC_MON) {
		ic->ic_flags_qtn &= ~IEEE80211_QTN_OCS_NAC_MON;
		off_chan_info->flags &= ~QTN_OFF_CHAN_FLAG_OCS_NAC_MON;
	}

	ic->ic_flags_qtn &= ~IEEE80211_QTN_SAMP_CHAN;
#if defined(QBMPS_ENABLE)
	/* indicate sample channel is done */
	/* start power-saving if allowed */
	if (ic->ic_opmode == IEEE80211_M_STA) {
		ic->ic_pm_reason = IEEE80211_PM_LEVEL_CCA_WORK;
		ieee80211_pm_queue_work(ic);
	}
#endif
}

static void qdrv_cca_irqhandler(void *arg1, void *arg2)
{
	struct qdrv_wlan *qw = arg1;
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_samp_chan_info *cca = sp->chan_sample_lhost;
	struct qtn_off_chan_info *off_chan_info = &cca->base;

	/*
	 * TODO SCS: radar disable during the cca read
	 */
	if (off_chan_info->muc_status == QTN_CCA_STATUS_MUC_COMPLETE ||
			off_chan_info->muc_status == QTN_CCA_STATUS_MUC_CANCELLED) {
		schedule_work(&qw->cca_wq);
	}
}

static int qdrv_init_cca_irqhandler(struct qdrv_wlan *qw)
{
	struct int_handler int_handler;

	int_handler.handler = qdrv_cca_irqhandler;
	int_handler.arg1 = qw;
	int_handler.arg2 = NULL;

	if (qdrv_mac_set_handler(qw->mac, RUBY_M2L_IRQ_LO_SCS, &int_handler) != 0) {
		DBGPRINTF_E("Could not set cca irq handler\n");
		return -1;
	}

	qdrv_mac_enable_irq(qw->mac, RUBY_M2L_IRQ_LO_SCS);

	return 0;
}

static char *meas_err_msg[] = {
	"off-channel not supported",
	"duration too short for measurement",
	"macfw timer scheduled fail",
	"measurement type unsupport"
};

static void meas_work(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, meas_wq);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_meas_chan_info *meas_info = sp->chan_meas_lhost;
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211_global_measure_info *ic_meas_info = &ic->ic_measure_info;

	if (ic_meas_info->status == MEAS_STATUS_DISCRAD) {
		ic_meas_info->status = MEAS_STATUS_IDLE;
		return;
	}

	if (meas_info->meas_reason == QTN_MEAS_REASON_SUCC) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "measurement success\n");
		switch (meas_info->meas_type) {
		case QTN_MEAS_TYPE_CCA:
		{
			u_int16_t cca_result;

			cca_result = (uint16_t)meas_info->inter_data.cca_and_chanload.cca_busy_ms;
			cca_result = cca_result * 1000 / meas_info->inter_data.cca_and_chanload.cca_try_ms;
			cca_result = cca_result * 255 / 1000;
			ic_meas_info->results.cca = (u_int8_t)cca_result;

			break;
		}
		case QTN_MEAS_TYPE_RPI:
		{
			u_int32_t rpi_sum;
			u_int32_t i;

			rpi_sum = 0;
			for (i = 0; i < 8; i++)
				rpi_sum += meas_info->inter_data.rpi_counts[i];
			if (rpi_sum == 0) {
				memset(ic_meas_info->results.rpi, 0, sizeof(ic_meas_info->results.rpi));
			} else {
				for (i = 0; i < 8; i++)
					ic_meas_info->results.rpi[i] = (u_int8_t)((meas_info->inter_data.rpi_counts[i] * 255) / rpi_sum);
			}

			break;
		}
		case QTN_MEAS_TYPE_BASIC:
		{
			int radar_num;

			radar_num = ic->ic_radar_detections_num(ic_meas_info->param.basic.channel);
			if ((radar_num >= 0) && ((radar_num - meas_info->inter_data.basic_radar_num) > 0))
				ic_meas_info->results.basic |=  IEEE80211_MEASURE_BASIC_REPORT_RADAR;
			ic_meas_info->results.basic |= meas_info->inter_data.basic;
			break;
		}
		case QTN_MEAS_TYPE_CHAN_LOAD:
		{
			u_int16_t cca_result;

			cca_result = (uint16_t)meas_info->inter_data.cca_and_chanload.cca_busy_ms;
			cca_result = cca_result * 1000 / meas_info->inter_data.cca_and_chanload.cca_try_ms;
			cca_result = cca_result * 255 / 1000;
			ic_meas_info->results.chan_load = (u_int8_t)cca_result;

			break;
		}
		case QTN_MEAS_TYPE_NOISE_HIS:
		{
			int32_t local_noise = 0;
			struct ieee80211_phy_stats phy_stats;

			if (ic->ic_get_phy_stats
					&& !ic->ic_get_phy_stats(ic->ic_dev, ic, &phy_stats, 0)) {
				local_noise = (int32_t)phy_stats.rx_noise;
			}

			ic_meas_info->results.noise_his.anpi = ABS(local_noise) / 10;
			memset(&ic_meas_info->results.noise_his.ipi, 0,
					sizeof(ic_meas_info->results.noise_his.ipi));

			break;
		}
		default:
			break;
		}

		ic->ic_finish_measurement(ic, 0);
	} else {
		printk("measurement fail:%s\n",meas_err_msg[meas_info->meas_reason - QTN_MEAS_REASON_OFF_CHANNEL_UNSUPPORT]);
		ic->ic_finish_measurement(ic, IEEE80211_CCA_REPMODE_REFUSE);
	}
	ic_meas_info->status = MEAS_STATUS_IDLE;
}

static void qdrv_meas_irqhandler(void *arg1, void *arg2)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *)arg1;

	schedule_work(&qw->meas_wq);
}

static int qdrv_init_meas_irqhandler(struct qdrv_wlan *qw)
{
	struct int_handler int_handler;

	int_handler.handler = qdrv_meas_irqhandler;
	int_handler.arg1 = qw;
	int_handler.arg2 = NULL;

	if (qdrv_mac_set_handler(qw->mac, RUBY_M2L_IRQ_LO_MEAS, &int_handler) != 0) {
		DBGPRINTF_E("Could not set measurement irq handler\n");
		return -1;
	}

	qdrv_mac_enable_irq(qw->mac, RUBY_M2L_IRQ_LO_MEAS);

	return 0;
}

#ifdef QTN_BG_SCAN
static struct off_chan_tsf_dbg scan_tsf_index_name[] = {
		{SCAN_CHAN_TSF_LHOST_HOSTLINK_IOCTL,		"host_send_ioctl"},
		{SCAN_CHAN_TSF_MUC_IOCTL_PROCESS,		"muc_ioctl_proc"},
		{SCAN_CHAN_TSF_MUC_SEND_START_FRM,		"muc_send_start"},
		{SCAN_CHAN_TSF_MUC_SEND_START_FRM_DONE,		"muc_start_done"},
		{SCAN_CHAN_TSF_MUC_GOTO_OFF_CHAN,		"muc_goto_offchan"},
		{SCAN_CHAN_TSF_MUC_GOTO_OFF_CHAN_DONE,		"muc_offchan_done"},
		{SCAN_CHAN_TSF_MUC_SEND_PRBREQ_FRM,		"muc_send_prbreq"},
		{SCAN_CHAN_TSF_MUC_SEND_PRBREQ_FRM_DONE,	"muc_prbreq_done"},
		{SCAN_CHAN_TSF_MUC_GOTO_DATA_CHAN,		"muc_goto_datachan"},
		{SCAN_CHAN_TSF_MUC_GOTO_DATA_CHAN_DONE,		"muc_datachan_done"},
		{SCAN_CHAN_TSF_LHOST_SCANWORK,			"host_scanwork"},
		{0,NULL}
};
static void scan_work(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, scan_wq);
	struct ieee80211com *ic = &qw->ic;
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_scan_chan_info *scan_host = sp->chan_scan_lhost;
	struct qtn_off_chan_info *off_chan_info = &scan_host->base;

	QDRV_SCAN_LOG_TSF(scan_host, SCAN_CHAN_TSF_LHOST_SCANWORK);

	if ((off_chan_info->muc_status == QTN_SCAN_CHAN_MUC_FAILED) ||
			(off_chan_info->muc_status == QTN_SCAN_CHAN_MUC_TERMINATED)) {
		if (ic->ic_qtn_bgscan.debug_flags >= 1) {
			printk("BG_SCAN: scan channel %u failed!\n",
				QTNCHAN_TO_IEEENUM(off_chan_info->channel));
		}
		off_chan_info->muc_status = QTN_SCAN_CHAN_MUC_IDLE;
	} else {
		qdrv_bgscan_phy_stats_update(qw, off_chan_info);
		if (ic->ic_qtn_bgscan.debug_flags == 2) {
			u32 tsf_hi;
			u32 tsf_lo;
			u32 delta;
			u32 cur_index;
			u32 pre_index = 0;
			int i;

			for (i = 0; scan_tsf_index_name[i].log_name; i++) {
				cur_index = scan_tsf_index_name[i].pos_index;
				tsf_hi = U64_HIGH32(scan_host->tsf[cur_index]);
				tsf_lo = U64_LOW32(scan_host->tsf[cur_index]);
				if (i) {
					delta = (u32)(scan_host->tsf[cur_index] -
							scan_host->tsf[pre_index]);
				} else {
					delta = 0;
					pr_warn("\n\nBGSCAN_tsf:\n");
				}
				pre_index = cur_index;
				pr_warn("  %s:    %08x_%08x (+%u)\n",
						scan_tsf_index_name[i].log_name,
						tsf_hi, tsf_lo, delta);
			}
		}
	}
}

static void qdrv_scan_irqhandler(void *arg1, void *arg2)
{
	struct qdrv_wlan *qw = arg1;
	struct ieee80211com *ic = &qw->ic;
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_scan_chan_info *scan_host = sp->chan_scan_lhost;
	u32 muc_status = scan_host->base.muc_status;

	if ((muc_status == QTN_SCAN_CHAN_MUC_COMPLETED) ||
			(muc_status == QTN_SCAN_CHAN_MUC_FAILED) ||
			(muc_status == QTN_SCAN_CHAN_MUC_TERMINATED)) {
		if (ic->ic_flags_qtn & IEEE80211_QTN_BGSCAN)
			ic->ic_flags &= ~IEEE80211_F_SCAN;
		schedule_work(&qw->scan_wq);
	}
}

static int qdrv_init_scan_irqhandler(struct qdrv_wlan *qw)
{
	struct int_handler int_handler;

	int_handler.handler = qdrv_scan_irqhandler;
	int_handler.arg1 = qw;
	int_handler.arg2 = NULL;

	if (qdrv_mac_set_handler(qw->mac, RUBY_M2L_IRQ_LO_SCAN, &int_handler) != 0) {
		DBGPRINTF_E("BG_SCAN: could not set irq handler\n");
		return -1;
	}

	qdrv_mac_enable_irq(qw->mac, RUBY_M2L_IRQ_LO_SCAN);

	return 0;
}
#endif /* QTN_BG_SCAN */

/*
 * Set the qosnull frame to sp->ocac_lhost
 * NOTE: MUST be called with a reference to the node entry within
 * the SKB CB structure, and free the reference to the node entry
 * after this call.
 * Return 0 for success or -1 on failure.
 * If failed, skb must be freed by the caller.
 */
static int qdrv_ocac_set_frame(struct ieee80211com *ic,
		struct ieee80211_node *ni, struct sk_buff *skb)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_ocac_info *ocac_info = sp->ocac_lhost;

	if (ocac_info->qosnull_txdesc_host) {
		OCACDBG(OCACLOG_NOTICE, "qosnull frame exists\n");
		return 0;
	}

	if (!skb) {
		OCACDBG(OCACLOG_WARNING, "set frame error - skb is null\n");
		return -1;
	}

	if (qdrv_wlan_get_ocs_frame(ic, ni, skb,
			&ocac_info->qosnull_txdesc_host, &ocac_info->qosnull_txdesc_bus,
			&ocac_info->qosnull_frame_len, &ocac_info->tx_node_idx)) {
		OCACDBG(OCACLOG_WARNING, "set frame error - no ocs frame\n");
		return -1;
	}

	OCACDBG(OCACLOG_VERBOSE, "set qosnull frame successfully\n");

	return 0;
}

static int qdrv_ocac_release_frame(struct ieee80211com *ic, int force)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_ocac_info *ocac_info = sp->ocac_lhost;

	if (!force && ic->ic_ocac.ocac_running) {
		OCACDBG(OCACLOG_WARNING, "release frame error - CAC in progress!\n");
		return -1;
	}

	if (ocac_info->qosnull_txdesc_host) {
		qdrv_wlan_release_ocs_frame(ic, ocac_info->qosnull_txdesc_host);
		ocac_info->qosnull_txdesc_host = 0;
		ocac_info->qosnull_txdesc_bus = 0;
	}

	OCACDBG(OCACLOG_VERBOSE, "qosnull frame released.\n");

	return 0;
}

static void qdrv_update_ocac_state_ie(struct ieee80211com *ic, uint8_t state, uint8_t param)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	qdrv_hostlink_update_ocac_state_ie(qw, state, param);
}

static int qdrv_set_ocac(struct ieee80211vap *vap, struct ieee80211_channel *chan)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_ocac_info *ocac_info = sp->ocac_lhost;
	struct sk_buff *skb = NULL;
	uint32_t off_channel;
	struct ieee80211_ocac_params *ocac_params = &ic->ic_ocac.ocac_cfg.ocac_params;

	if (chan) {
		/* start ocac in MuC */
		off_channel = qdrv_set_channel_setup(ic, chan);
		if (off_channel == ocac_info->off_channel) {
			ic->ic_ocac.ocac_counts.skip_set_run++;
			OCACDBG(OCACLOG_VERBOSE, "duplicate setting, skip it!\n");
			return -1;
		}
		if (ocac_params->traffic_ctrl && !ocac_info->qosnull_txdesc_host) {
			/* Set qosnull frame */
			ieee80211_ref_node(ni);
			skb = ieee80211_get_qosnulldata(ni, WMM_AC_VO);
			if (!skb) {
				ieee80211_free_node(ni);
				OCACDBG(OCACLOG_WARNING, "alloc skb error!\n");
				ic->ic_ocac.ocac_counts.alloc_skb_error++;
				return -1;
			}
			if (qdrv_ocac_set_frame(ic, ni, skb)) {
				dev_kfree_skb_irq(skb);
				ieee80211_free_node(ni);
				OCACDBG(OCACLOG_WARNING, "set ocs frame error!\n");
				ic->ic_ocac.ocac_counts.set_frame_error++;
				return -1;
			}
			ieee80211_free_node(ni);
		}
		ocac_info->freq_band = qdrv_set_channel_freqband_setup(ic, chan);
		ocac_info->off_channel = off_channel;
		ocac_info->secure_dwell = ocac_params->secure_dwell_ms;
		ocac_info->threshold_fat = ocac_params->thresh_fat;
		ocac_info->threshold_traffic = ocac_params->thresh_traffic;
		ocac_info->threshold_fat_dec = ocac_params->thresh_fat_dec;
		ocac_info->traffic_ctrl = ocac_params->traffic_ctrl;
		ocac_info->offset_txhalt = ocac_params->offset_txhalt;
		ocac_info->offset_offchan = ocac_params->offset_offchan;
		ocac_info->threshold_video_frames = ocac_params->thresh_video_frames;
		if (ieee80211_is_on_weather_channel(ic, chan))
			ocac_info->dwell_time = ocac_params->wea_dwell_time_ms;
		else
			ocac_info->dwell_time = ocac_params->dwell_time_ms;
		ic->ic_ocac.ocac_counts.set_run++;

		if (ocac_params->traffic_ctrl)
			ic->ic_update_ocac_state_ie(ic, OCAC_STATE_ONGOING, 0);
	} else {
		/* stop ocac in MuC */
		if (ocac_info->off_channel == 0) {
			OCACDBG(OCACLOG_VERBOSE, "duplicate setting, skip it!\n");
			ic->ic_ocac.ocac_counts.skip_set_pend++;
			return -1;
		}
		ocac_info->off_channel = 0;
		ic->ic_ocac.ocac_counts.set_pend++;

		if (ocac_params->traffic_ctrl)
			ic->ic_update_ocac_state_ie(ic, OCAC_STATE_NONE, 0);
	}

	if (qdrv_hostlink_set_ocac(qw, sp->ocac_bus) < 0) {
		ic->ic_ocac.ocac_counts.hostlink_err++;
		OCACDBG(OCACLOG_WARNING, "hostlink set seamless dfs error!\n");
		return -1;
	}
	ic->ic_ocac.ocac_counts.hostlink_ok++;

	OCACDBG(OCACLOG_VERBOSE, "hostlink set seamless dfs succeed. off-chan: %u\n",
			chan ? chan->ic_ieee : 0);

	return 0;
}

int qtn_do_measurement(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_meas_chan_info *meas_info = sp->chan_meas_lhost;
	struct ieee80211_global_measure_info *ic_meas_info = &ic->ic_measure_info;
	int ret;

	meas_info->meas_reason = 0;
	meas_info->work_channel = qdrv_set_channel_setup(ic, ic->ic_curchan);

	switch (ic_meas_info->type) {
	case IEEE80211_CCA_MEASTYPE_BASIC:
	{
		struct ieee80211_channel *meas_channel;
		u_int8_t ieee_ch;

		/* channel 0 means current channel */
		if (ic_meas_info->param.basic.channel == 0)
			ieee_ch = ic->ic_curchan->ic_ieee;
		else
			ieee_ch = ic_meas_info->param.basic.channel;

		meas_channel = findchannel(ic, ieee_ch, ic->ic_des_mode);
		if (NULL == meas_channel) {
			return -EINVAL;
		}

		if (ic_meas_info->param.basic.duration_tu == 0) {
			return -EINVAL;
		}

		meas_info->meas_type = QTN_MEAS_TYPE_BASIC;
		meas_info->meas_channel = qdrv_set_channel_setup(ic, meas_channel);
		meas_info->meas_dur_ms = IEEE80211_TU_TO_MS(ic_meas_info->param.basic.duration_tu);
		meas_info->meas_start_tsf = ic_meas_info->param.basic.tsf;
		meas_info->inter_data.basic_radar_num = ic->ic_radar_detections_num(ieee_ch);

		break;
	}
	case IEEE80211_CCA_MEASTYPE_CCA:
	{
		struct ieee80211_channel *meas_channel;
		int ieee_ch;

		if (ic_meas_info->param.cca.channel == 0)
			ieee_ch = ic->ic_curchan->ic_ieee;
		else
			ieee_ch = ic_meas_info->param.cca.channel;
		meas_channel = findchannel(ic, ieee_ch, 0);
		if (NULL == meas_channel)
			return -EINVAL;

		if (ic_meas_info->param.cca.duration_tu == 0)
			return -EINVAL;

		meas_info->meas_type = QTN_MEAS_TYPE_CCA;
		meas_info->meas_channel = qdrv_set_channel_setup(ic, meas_channel);
		meas_info->meas_dur_ms = IEEE80211_TU_TO_MS(ic_meas_info->param.cca.duration_tu);
		meas_info->meas_start_tsf = ic_meas_info->param.cca.tsf;

		break;
	}
	case IEEE80211_CCA_MEASTYPE_RPI:
	{
		struct ieee80211_channel *meas_channel;
		int ieee_ch;

		if (ic_meas_info->param.rpi.channel == 0)
			ieee_ch = ic->ic_curchan->ic_ieee;
		else
			ieee_ch = ic_meas_info->param.rpi.channel;
		meas_channel = findchannel(ic, ieee_ch, 0);
		if (NULL == meas_channel)
			return -EINVAL;

		if (ic_meas_info->param.rpi.duration_tu == 0)
			return -EINVAL;

		meas_info->meas_type = QTN_MEAS_TYPE_RPI;
		meas_info->meas_channel = qdrv_set_channel_setup(ic, meas_channel);
		meas_info->meas_dur_ms = IEEE80211_TU_TO_MS(ic_meas_info->param.rpi.duration_tu);
		meas_info->meas_start_tsf = ic_meas_info->param.rpi.tsf;

		break;
	}
	case IEEE80211_RM_MEASTYPE_CH_LOAD:
	{
		struct ieee80211_channel *meas_channel;
		int ieee_ch;

		if (ic_meas_info->param.chan_load.op_class == 0 &&
				ic_meas_info->param.chan_load.channel == 0)
			ieee_ch = ic->ic_curchan->ic_ieee;
		else
			ieee_ch = ic_meas_info->param.chan_load.channel;

		meas_channel = findchannel(ic, ieee_ch, 0);
		if (NULL == meas_channel)
			return -EINVAL;

		if (ic_meas_info->param.chan_load.duration_tu == 0)
			return -EINVAL;

		meas_info->meas_type = QTN_MEAS_TYPE_CHAN_LOAD;
		meas_info->meas_channel = qdrv_set_channel_setup(ic, meas_channel);
		meas_info->meas_dur_ms = IEEE80211_TU_TO_MS(ic_meas_info->param.chan_load.duration_tu);
		meas_info->meas_start_tsf = 0;

		break;
	}
	case IEEE80211_RM_MEASTYPE_NOISE:
	{
		struct ieee80211_channel *meas_channel;
		int ieee_ch;

		if (ic_meas_info->param.noise_his.op_class == 0 &&
				ic_meas_info->param.noise_his.channel == 0)
			ieee_ch = ic->ic_curchan->ic_ieee;
		else
			ieee_ch = ic_meas_info->param.noise_his.channel;

		meas_channel = findchannel(ic, ieee_ch, 0);
		if (NULL == meas_channel)
			return -EINVAL;

		if (ic_meas_info->param.noise_his.duration_tu == 0)
			return -EINVAL;

		meas_info->meas_type = QTN_MEAS_TYPE_NOISE_HIS;
		meas_info->meas_channel = qdrv_set_channel_setup(ic, meas_channel);
		meas_info->meas_dur_ms = IEEE80211_TU_TO_MS(ic_meas_info->param.noise_his.duration_tu);
		meas_info->meas_start_tsf = 0;

		break;
	}
	default:
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_WLAN,
			"[%s]unsupported measurement type\n",
			__func__);
		return -EINVAL;
	}

	ic_meas_info->status = MEAS_STATUS_RUNNING;
	ret = qdrv_hostlink_meas_chan(qw, sp->chan_meas_bus);
	if (ret & QTN_HLINK_RC_ERR) {
		printk("measurement fail:%s\n",
				meas_err_msg[meas_info->meas_reason - QTN_MEAS_REASON_OFF_CHANNEL_UNSUPPORT]);
		ic_meas_info->status = MEAS_STATUS_IDLE;
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(qtn_do_measurement);

static int qdrv_remain_on_channel(struct ieee80211com *ic,
		struct ieee80211_node *ni, struct ieee80211_channel *off_chan,
		int bandwidth, uint64_t start_tsf, uint32_t timeout, uint32_t duration, int flags)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct qtn_remain_chan_info *remain_info = sp->remain_chan_lhost;
	uint8_t bc_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if (!ic || !off_chan)
		goto error;

	if ((bandwidth < BW_HT20) || (bandwidth > BW_HT160))
		goto error;

	if (ieee80211_is_scanning(ic)) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "REMAIN CHANNEL %s:"
			" Don't switch to off channel - scan in progress\n", __func__);
		goto error;
	}

	if (!qdrv_radar_can_sample_chan()) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "REMAIN CHANNEL %s:"
			" Don't switch to off channel - radar\n", __func__);
		goto error;
	}

	if (ic->ic_bsschan == IEEE80211_CHAN_ANYC)
		return 0;

	/* sample channel not allowed in power-saving mode */
	if (((ic->ic_opmode == IEEE80211_M_HOSTAP)
#if defined(QBMPS_ENABLE)
	     || (ic->ic_opmode == IEEE80211_M_STA)
#endif
	    ) && (ic->ic_pm_state[QTN_PM_CURRENT_LEVEL] >= BOARD_PM_LEVEL_DUTY)) {
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "REMAIN CHANNEL %s:"
			" Don't switch to off channel - CoC idle\n", __func__);
		goto error;
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "remain on channel %u %dus\n",
			off_chan->ic_ieee, duration);

	remain_info->start_tsf = start_tsf;
	remain_info->timeout_usecs = timeout;
	remain_info->duration_usecs = duration;
	remain_info->freq_band = qdrv_set_channel_freqband_setup(ic, ic->ic_bsschan);
	remain_info->data_channel = qdrv_set_channel_setup(ic, ic->ic_bsschan);

	if (ni)
		memcpy(remain_info->peer_mac, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	else
		memcpy(remain_info->peer_mac, bc_mac, IEEE80211_ADDR_LEN);

	off_chan->ic_ext_flags |= IEEE80211_CHAN_TDLS_OFF_CHAN;
	if (bandwidth == BW_HT20) {
		ic->ic_flags_ext |= IEEE80211_FEXT_SCAN_20;
		remain_info->off_channel = qdrv_set_channel_setup(ic, off_chan);
		ic->ic_flags_ext &= ~IEEE80211_FEXT_SCAN_20;
	} else if (bandwidth == BW_HT40) {
		ic->ic_flags_ext |= IEEE80211_FEXT_SCAN_40;
		remain_info->off_channel = qdrv_set_channel_setup(ic, off_chan);
		ic->ic_flags_ext &= ~IEEE80211_FEXT_SCAN_40;
	} else {
		remain_info->off_channel = qdrv_set_channel_setup(ic, off_chan);
	}
	off_chan->ic_ext_flags &= ~IEEE80211_CHAN_TDLS_OFF_CHAN;

	remain_info->status = QTN_REM_CHAN_STATUS_HOST_IOCTL_SENT;
	if (qdrv_hostlink_remain_chan(qw, sp->remain_chan_bus) < 0) {
		remain_info->status = QTN_REM_CHAN_STATUS_IDLE;
		DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "REMAIN CHANNEL %s:"
			" hostlink set remain channel error!\n", __func__);
		goto error;
	}
#if defined(QBMPS_ENABLE)
	/* indicate sample channel is on-going */
	ic->ic_flags_qtn |= IEEE80211_QTN_SAMP_CHAN;
#endif
	ic->ic_chan_switch_reason_record(ic, IEEE80211_CSW_REASON_TDLS_CS);

	return 0;

error:
	return -1;
}

static void
qdrv_use_rts_cts(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	qdrv_hostlink_use_rtscts(qw, ic->ic_local_rts);
}

#if QTN_ENABLE_TRACE_BUFFER
static void
qdrv_dump_trace_buffer(struct shared_params *sp)
{
	if (sp->p_debug_1 && sp->p_debug_2) {
		int i;
		/* NOTE: the number of records cannot dynamically change or buffer overflow may happen */
		int records = sp->debug_1_arg;
		static struct qtn_trace_record *p_local = NULL;
		u_int32_t index = *((u_int32_t *)(muc_to_lhost((int)sp->p_debug_2))) + 1;
		struct qtn_trace_record *p_first = (struct qtn_trace_record *)(muc_to_lhost)((int)sp->p_debug_1);
		struct qtn_trace_record *p_entry = p_first + index;
		struct qtn_trace_record *p_past = p_first + records;
		if (records) {
			size_t alloc_size = records * sizeof(struct qtn_trace_record);
			if (p_local == NULL) {
				p_local = kmalloc(alloc_size, GFP_KERNEL);
			}
			if (p_local) {
				memcpy((void *)p_local, (void *)muc_to_lhost((int)sp->p_debug_1), alloc_size);
				/*
				 * Re-cache the index - it may have changed by this point, if alloc above
				 * takes a while.
				 */
				index = *((u_int32_t *)(muc_to_lhost((int)sp->p_debug_2))) + 1;
				p_first = p_local;
				p_entry = p_first + index;
				p_past = p_first + records;
			}
		}
		if (index >= records) {
			index = 0;
		}
		printk("Trace buffer for %d records %p %p %d:\n", records, p_first,
				(u_int32_t *)(muc_to_lhost((int)sp->p_debug_2)), index);
		for (i = 0; i < records; i++) {
			printk(" T:0x%08X,E:0x%08X,D:0x%08X", p_entry->tsf, p_entry->event, p_entry->data);
			if (i && (((i + 1) % 2) == 0)) {
				printk("\n");
			}
			p_entry++;
			if (p_entry >= p_past) {
				p_entry = p_first;
			}
		}
	}
}

static void qdrv_sleep_ms(int ms)
{
	mdelay(ms);
}
#endif

void qdrv_muc_traceback(int force)
{
#if QTN_ENABLE_TRACE_BUFFER
	int i;
	struct net_device *dev;

	for (i = 1; ; i++) {
		dev = dev_get_by_index(&init_net, i);
		if (dev == NULL) {
			panic("Can't find a network device\n");
		}
		if (strncmp(dev->name, "wifi", 4) == 0) {
			break;
		} else {
			dev_put(dev);
		}
	}
	if (dev) {
		struct shared_params *sp = qtn_mproc_sync_shared_params_get();
		struct ieee80211vap *vap = netdev_priv(dev);
		struct ieee80211com *ic = vap->iv_ic;
		struct qdrv_wlan *qw =  container_of(ic, struct qdrv_wlan, ic);
		struct qdrv_mac *mac = qw->mac;
		if (!mac->dead || force) {
			qdrv_dump_trace_buffer(sp);
		}
		dev_put(dev);
	}
#endif
}
EXPORT_SYMBOL(qdrv_muc_traceback);

void qdrv_halt_muc(void)
{
#if QTN_ENABLE_TRACE_BUFFER
	int i;
	struct net_device *dev;

	for (i = 1; ; i++) {
		dev = dev_get_by_index(&init_net, i);
		if (dev == NULL) {
			panic("Can't find a network device\n");
		}
		if (strncmp(dev->name, "wifi", 4) == 0) {
			break;
		} else {
			dev_put(dev);
		}
	}
	if (dev) {
		struct shared_params *sp = qtn_mproc_sync_shared_params_get();
		struct ieee80211vap *vap = netdev_priv(dev);
		struct ieee80211com *ic = vap->iv_ic;
		struct qdrv_wlan *qw =  container_of(ic, struct qdrv_wlan, ic);
		struct qdrv_mac *mac = qw->mac;
		if (!mac->dead) {
			printk("Interrupting MuC to halt (MAC:%p)\n",  mac);
			mac->dead = 1;
			qdrv_mac_interrupt_muc_high(mac);
			qtn_sleep_ms(100);
			qdrv_dump_trace_buffer(sp);
		} else {
			printk("MAC already dead, not triggering another trace\n");
		}
		dev_put(dev);
	}
#endif
}
EXPORT_SYMBOL(qdrv_halt_muc);

static void qtn_set_coverageclass(struct ieee80211com *ic)
{
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
}

static u_int qtn_mhz2ieee(struct ieee80211com *ic, u_int freq, u_int flags)
{
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

static struct ieee80211vap *qtn_vap_create(struct ieee80211com *ic,
	const char *name, int unit, int opmode, int flags, struct net_device *dev)
{
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return NULL;
}

static void qtn_vap_delete(struct ieee80211vap *iv)
{
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return;
}

static uint8_t qdrv_get_vap_idx(struct ieee80211vap *vap)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);

	return qv->qv_vap_idx;
}

static void qdrv_wlan_80211_updateslot(struct ieee80211com *ic)
{
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	if (!vap)
		return;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");
	qdrv_wlan_80211_setparam(vap->iv_bss, IEEE80211_PARAM_SHORT_SLOT,
		ic->ic_flags & IEEE80211_F_SHSLOT, NULL, 0);
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return;
}

static int qdrv_wlan_80211_start(struct ieee80211com *ic)
{

	struct qdrv_wlan *qw =  container_of(ic, struct qdrv_wlan, ic);
	struct qdrv_mac *mac = qw->mac;
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");
	/* Just in case re-enable rx interrupts */
	qdrv_mac_enable_irq(mac, qw->rxirq);
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

static int qdrv_wlan_80211_reset(struct ieee80211com *ic)
{
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

static int qdrv_allow_report_frames_in_cac(struct ieee80211com *ic, struct ieee80211_node *ni,
						const int user_conf_switch, bool *allow_rpt_frm)
{
	if (NULL == allow_rpt_frm) {
		ieee80211_free_node(ni);
		return 0;
	}

	if (user_conf_switch) {
		if (*allow_rpt_frm) {
			*allow_rpt_frm = false;
		} else if ((ieee80211_is_chan_radar_detected(ic->ic_curchan)) ||
				(ieee80211_is_chan_cac_required(ic->ic_curchan))) {
			ieee80211_free_node(ni);
			return 0;
		}
	}

	return 1;
}

/*
 * Transmit an 802.11 encapped management or data frame via the management path.
 * This function must be called with a reference to the node structure.
 */
static int qdrv_wlan_80211_send(struct ieee80211com *ic, struct ieee80211_node *ni,
				struct sk_buff *skb, uint32_t priority, uint8_t is_mgmt)
{
	struct net_device *vdev;
	struct qdrv_vap *qv;


	if (!qdrv_allow_report_frames_in_cac(ic, ni, ic->sta_dfs_info.sta_dfs_strict_mode,
						&ic->sta_dfs_info.allow_measurement_report)) {
		return 0;
	}

#ifdef CONFIG_QHOP
	if (ic->ic_extender_role == IEEE80211_EXTENDER_ROLE_RBS) {
		if (!qdrv_allow_report_frames_in_cac(ic, ni, ic->rbs_mbs_dfs_info.rbs_mbs_allow_tx_frms_in_cac,
					&ic->rbs_mbs_dfs_info.rbs_allow_qhop_report)) {
			return 0;
		}
	}

	if (ic->ic_extender_role == IEEE80211_EXTENDER_ROLE_MBS) {
		if (!qdrv_allow_report_frames_in_cac(ic, ni, ic->rbs_mbs_dfs_info.rbs_mbs_allow_tx_frms_in_cac,
					&ic->rbs_mbs_dfs_info.mbs_allow_csa)) {
			return 0;
		}
	}
#endif
	qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	vdev = qv->ndev;

	skb->dev = vdev;
	skb->dest_port = ni->ni_node_idx;
	skb->priority = priority;

	QTN_SKB_CB_NI(skb) = ni;

	M_FLAG_SET(skb, M_NO_AMSDU);
	if (is_mgmt) {
		QTN_SKB_ENCAP(skb) = QTN_SKB_ENCAP_80211_MGMT;
		if (ni->ni_vap->iv_btm_term.flags & IEEE80211_VAP_BTM_REQ_SEND) {
			/* interface is marked down, so send directly via driver */
			const struct net_device_ops *ops = vdev->netdev_ops;
			int ret = NETDEV_TX_BUSY;
			if (ops->ndo_start_xmit)
				ret = ops->ndo_start_xmit(skb, vdev);
			if (!dev_xmit_complete(ret))
				kfree_skb(skb);
		} else {
			dev_queue_xmit(skb);
		}
	} else {
		QTN_SKB_ENCAP(skb) = QTN_SKB_ENCAP_80211_DATA;
		dev_queue_xmit(skb);
	}

	ieee80211_free_node(ni);

	return 0;
}

static void qdrv_wlan_80211_disassoc(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct qdrv_node *qn = container_of(ni, struct qdrv_node, qn_node);
	struct qtn_vlan_dev *vdev = vdev_tbl_lhost[QDRV_WLANID_FROM_DEVID(qv->devid)];
	struct host_ioctl *ioctl;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;

	DBGPRINTF(DBG_LL_CRIT, QDRV_LF_TRACE,
		"Node %02x:%02x:%02x:%02x:%02x:%02x %s for BSSID %02x:%02x:%02x:%02x:%02x:%02x\n",
		DBGMACFMT(ni->ni_macaddr),
		"dissociated",
		DBGMACFMT(ni->ni_bssid));

	if (ic->ic_wowlan.host_state && (ic->ic_wowlan.mask & WOWLAN_TRIG_DISCONNECT))
		wowlan_wakeup_host(vap, WOWLAN_TRIG_DISCONNECT);

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate DISASSOC message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	if (ic->sta_dfs_info.sta_dfs_strict_mode) {
		ic->ic_mark_channel_availability_status(ic, ic->ic_bsschan,
				IEEE80211_CHANNEL_STATUS_NON_AVAILABLE);
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)args);
	memset(args, 0, sizeof(*args));

	memcpy(args->ni_bssid, ni->ni_bssid, IEEE80211_ADDR_LEN);
	memcpy(args->ni_macaddr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	args->ni_associd = IEEE80211_NODE_AID(ni);

	ioctl->ioctl_command = IOCTL_DEV_DISASSOC;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);

	if (qn->qn_node_idx)
		switch_vlan_clr_node(vdev, qn->qn_node_idx);
}

static void qdrv_wlan_qtnie_parse(struct ieee80211_node *ni, struct ieee80211com *ic,
					struct qtn_node_args *args)
{
	struct ieee80211_ie_qtn *qtnie = (struct ieee80211_ie_qtn *)ni->ni_qtn_assoc_ie;

	if (IEEE80211_QTN_TYPE_ENVY(qtnie)) {
		args->ni_qtn_ie_flags = qtnie->qtn_ie_flags;
	} else {
		args->ni_qtn_ie_flags = qtnie->qtn_ie_my_flags;
	}

	if (IEEE80211_QTN_IE_GE_V5(qtnie)) {
		args->ni_ver_sw = min(ni->ni_ver_sw, ic->ic_ver_sw);
		args->ni_ver_hw = ni->ni_ver_hw;
		args->ni_ver_platform_id = ni->ni_ver_platform_id;
		args->ni_rate_train = ni->ni_rate_train;
		args->ni_rate_train_peer = ntohl(get_unaligned(&qtnie->qtn_ie_rate_train));
	}
}

static void qdrv_wlan_80211_newassoc(struct ieee80211_node *ni, int isnew)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct qdrv_node *qn = container_of(ni, struct qdrv_node, qn_node);
	struct ieee80211com *ic = ni->ni_vap->iv_ic;
	struct host_ioctl *ioctl;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;
	struct qtn_vlan_dev *vdev = vdev_tbl_lhost[QDRV_WLANID_FROM_DEVID(qv->devid)];
	int control_bh = 0;

	DBGPRINTF(DBG_LL_CRIT, QDRV_LF_TRACE,
		"Node %02x:%02x:%02x:%02x:%02x:%02x %s for BSSID %02x:%02x:%02x:%02x:%02x:%02x\n",
		DBGMACFMT(ni->ni_macaddr),
		isnew ? "associated" : "reassociated",
		DBGMACFMT(ni->ni_bssid));

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate NEWASSOC message\n");
		vnet_free_ioctl(ioctl);
		return;
	}
	memset(args, 0, sizeof(*args));

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(2)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	memset(ni->ni_shared_stats, 0, sizeof(*ni->ni_shared_stats));
	args->ni_node_idx = 0;
	args->ni_shared_stats = ni->ni_shared_stats_phys;
	memcpy(args->ni_bssid, ni->ni_bssid, IEEE80211_ADDR_LEN);
	memcpy(args->ni_macaddr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	args->ni_raw_bintval = ni->ni_raw_bintval;
	args->ni_associd = IEEE80211_NODE_AID(ni);
	args->ni_flags = ni->ni_flags;
	args->ni_qtn_flags = ni->ni_qtn_flags;
	args->ni_tdls_status = ni->tdls_status;
	args->ni_vendor = ni->ni_vendor;
	args->ni_bbf_disallowed = ni->ni_bbf_disallowed;
	args->ni_std_bf_disallowed = ni->ni_std_bf_disallowed;
	args->ni_uapsd = ni->ni_uapsd;
	memcpy(args->ni_rates, ni->ni_rates.rs_rates, IEEE80211_RATE_MAXSIZE);
	memcpy(args->ni_htrates, ni->ni_htrates.rs_rates, IEEE80211_HT_RATE_MAXSIZE);
	args->ni_nrates = ni->ni_rates.rs_nrates;
	args->ni_htnrates = ni->ni_htrates.rs_nrates;
	memcpy(args->ni_htcap, &ni->ni_htcap,
		sizeof(args->ni_htcap));
	memcpy(args->ni_htinfo, &ni->ni_htinfo,
		sizeof(args->ni_htinfo));
	if (IS_IEEE80211_DUALBAND_VHT_ENABLED(ic) && (ni->ni_flags & IEEE80211_NODE_VHT)) {
		memcpy(args->ni_vhtcap, &ni->ni_vhtcap,
			sizeof(args->ni_vhtcap));
		memcpy(args->ni_vhtop, &ni->ni_vhtop,
			sizeof(args->ni_vhtop));
		memcpy(args->ni_mu_grp, &ni->ni_mu_grp,
			sizeof(args->ni_mu_grp));
	}
	args->ni_rsn_caps = ni->ni_rsn.rsn_caps;
	args->rsn_ucastcipher = ni->ni_rsn.rsn_ucastcipher;
	args->tdls_peer_associd = ni->tdls_peer_associd;
	/* Automatic install of BA */
	if (ni->ni_implicit_ba_valid) {
		args->ni_implicit_ba_rx = ni->ni_implicit_ba;
		args->ni_implicit_ba_tx = ni->ni_vap->iv_implicit_ba;
		args->ni_implicit_ba_size = ni->ni_implicit_ba_size;
	}

	if (ni->ni_qtn_assoc_ie)
		qdrv_wlan_qtnie_parse(ni, ic, args);

	ioctl->ioctl_command = IOCTL_DEV_NEWASSOC;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_arg2 = isnew;
	ioctl->ioctl_argp = args_dma;

	/*
	 * For SAE mode, this function is running in process context
	 * during the communication of NEWASSOC ioctl command between Lhost and MuC,
	 * msleep is invoked and other task can be sheduled, so it is possible
	 * that before command completes, the node is removed due to client's DEAUTH or DISASSOC
	 * in order to avoid this scenario, prior to this command, bottom half is disabled
	 * to ensure this command communication is atomic
	 */
	if (!in_interrupt()) {
		local_bh_disable();
		control_bh = 1;
	}
	vnet_send_ioctl(qv, ioctl);
	if (control_bh)
		local_bh_enable();

	if (args->ni_node_idx == 0) {
		DBGPRINTF_E("[%pM] node alloc failed\n", ni->ni_macaddr);
	} else {
		uint32_t ni_node_idx = ni->ni_node_idx;

		ieee80211_idx_add(ni, args->ni_node_idx);
		if (((ni->ni_vap->iv_opmode == IEEE80211_M_WDS)
				&& (ni_node_idx != 0))
			|| (ni->ni_vap->iv_opmode == IEEE80211_M_HOSTAP
				&& ni_node_idx != ni->ni_vap->iv_bss->ni_node_idx)) {
			/* clear the node authorized flag, so that lhost will discard
			 * the packets that use the unauthorized node
			 */
			if (ni->ni_authmode == IEEE80211_AUTH_8021X)
				ni->ni_flags &= ~IEEE80211_NODE_AUTH;
			qdrv_remove_invalid_sub_port(ni->ni_vap, ni_node_idx);
		}

		if (qv->iv.iv_opmode == IEEE80211_M_HOSTAP
				&& QVLAN_IS_DYNAMIC(vdev)) {
			qn->qn_node_idx = args->ni_node_idx;
			switch_vlan_set_node(vdev, args->ni_node_idx, QVLAN_DEF_PVID); /* By default put a STA in VLAN 1 */
		}
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_wlan_80211_node_update(struct ieee80211_node *ni)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;

	DBGPRINTF(DBG_LL_CRIT, QDRV_LF_TRACE,
		"Node "DBGMACVAR" updated for BSSID "DBGMACVAR"\n",
		DBGMACFMT(ni->ni_macaddr),
		DBGMACFMT(ni->ni_bssid));

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate NODE_UPDATE message\n");
		vnet_free_ioctl(ioctl);
		return;
	}
	memset(args, 0, sizeof(*args));

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(2)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	memcpy(args->ni_macaddr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	args->ni_qtn_flags = ni->ni_qtn_flags;
	args->ni_vendor = ni->ni_vendor;
	args->ni_bbf_disallowed = ni->ni_bbf_disallowed;

	ioctl->ioctl_command = IOCTL_DEV_NODE_UPDATE;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_arg2 = 0;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);

	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_wlan_register_node(struct ieee80211_node *ni)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct qtn_vlan_dev *vdev = vdev_tbl_lhost[QDRV_WLANID_FROM_DEVID(qv->devid)];

	fwt_sw_register_node(ni->ni_node_idx);
	switch_vlan_register_node(IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx), vdev);
}

static void qdrv_wlan_unregister_node(struct ieee80211_node *ni)
{
	fwt_sw_unregister_node(ni->ni_node_idx);
	switch_vlan_unregister_node(IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx));
}

static void qdrv_wlan_80211_resetmaxqueue(struct ieee80211_node *ni)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;

	if ((ioctl = vnet_alloc_ioctl(qv)) == NULL)
		return;

	if ((args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC)) == NULL) {
		DBGPRINTF_E("Failed to allocate message for resetting queue\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	memset(args, 0, sizeof(*args));

	memcpy(args->ni_bssid, ni->ni_bssid, IEEE80211_ADDR_LEN);
	memcpy(args->ni_macaddr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	args->ni_associd = IEEE80211_NODE_AID(ni);
	args->ni_node_idx = ni->ni_node_idx;

	ioctl->ioctl_command = IOCTL_DEV_RST_QUEUE_DEPTH;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

/*
 *  Process setkey request
 */
static void qdrv_wlan_80211_setkey(struct ieee80211vap *vap,
	const struct ieee80211_key *k, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct qtn_key_args *args = NULL;
	dma_addr_t args_dma;
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate SETKEY message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	/* Copy the values over */
	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(3)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	memcpy((u_int8_t*)&args->key, (u_int8_t*)k, sizeof(struct qtn_key));
	memcpy(args->wk_addr, mac, IEEE80211_ADDR_LEN);

	ioctl->ioctl_command = IOCTL_DEV_SETKEY;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

/*
 *  Process delkey request
 */
static void qdrv_wlan_80211_delkey(struct ieee80211vap *vap,
	const struct ieee80211_key *k, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct qtn_key_args *args = NULL;
	dma_addr_t args_dma;
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E( "Failed to allocate DELKEY message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	/* Copy the values over */
	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(4)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	memcpy((u_int8_t*)&args->key, (u_int8_t*)k, sizeof(struct qtn_key));
	if (mac) {
		memcpy(args->wk_addr, mac, IEEE80211_ADDR_LEN);
	}

	ioctl->ioctl_command = IOCTL_DEV_DELKEY;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

/*
 *  Process addba request
 */
static void qdrv_wlan_80211_process_addba(struct ieee80211_node *ni, int tid,
				int direction)
{
	struct qtn_baparams_args *args = NULL;
	dma_addr_t args_dma;
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;

	if (tid >= WME_NUM_TID) {
		return;
	}

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate ADDBA message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(5)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	args->tid = tid;
	memcpy(args->ni_addr, ni->ni_macaddr, IEEE80211_ADDR_LEN);

	args->type = IEEE80211_BA_IMMEDIATE;
	if (direction) {
		args->state = ni->ni_ba_tx[tid].state;
		args->start_seq_num = ni->ni_ba_tx[tid].seq;
		args->window_size = ni->ni_ba_tx[tid].buff_size;
		args->lifetime = ni->ni_ba_tx[tid].timeout;
		args->flags = ni->ni_ba_tx[tid].flags;
	} else {
		args->state = ni->ni_ba_rx[tid].state;
		args->start_seq_num = ni->ni_ba_rx[tid].seq;
		args->window_size = ni->ni_ba_rx[tid].buff_size;
		args->lifetime = ni->ni_ba_rx[tid].timeout;
		args->flags = ni->ni_ba_rx[tid].flags;
	}

	if (direction) {
		ioctl->ioctl_command = IOCTL_DEV_BA_ADDED_TX;
	} else {
		ioctl->ioctl_command = IOCTL_DEV_BA_ADDED_RX;
	}

	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;
	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

/*
 * Send addba request
 */
static void qdrv_wlan_80211_send_addba(struct ieee80211_node *ni, int tid)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_action_data act;
	struct ba_action_req ba_req;

	memset(&act, 0, sizeof(act));
	memset(&ba_req, 0, sizeof(ba_req));
	act.cat = IEEE80211_ACTION_CAT_BA;
	act.action = IEEE80211_ACTION_BA_ADDBA_REQ;
	ba_req.tid = tid;
	ba_req.frag = 0;
	ba_req.type = IEEE80211_BA_IMMEDIATE;
	ba_req.seq = ni->ni_ba_tx[tid].seq;
	ba_req.buff_size = ni->ni_ba_tx[tid].buff_size;
	ba_req.timeout = ni->ni_ba_tx[tid].timeout;
	act.params = (void *)&ba_req;
	ic->ic_send_mgmt(ni, IEEE80211_FC0_SUBTYPE_ACTION, (int)&act);
}

static void qdrv_wlan_update_wmm_params(struct ieee80211vap *vap)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_wme_state *wme = &ic->ic_wme;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;
	struct host_ioctl *ioctl;
	int i;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate WMM params message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(6)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	for (i = 0; i < WME_NUM_AC; i++) {
		args->wmm_params[i] = wme->wme_chanParams.cap_wmeParams[i];
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s - %x Send new WMM parameters\n",
			vap->iv_dev->name, ioctl->ioctl_argp);

	ioctl->ioctl_command = IOCTL_DEV_WMM_PARAMS;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_arg2 = 1;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_wlan_update_chan_power_table(struct ieee80211vap *vap,
		struct ieee80211_channel *chan)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct ieee80211_chan_power_table *args = NULL;
	dma_addr_t args_dma;
	struct host_ioctl *ioctl;
	enum ieee80211_pwr_idx_fem_prichan fem_pri;
	enum ieee80211_pwr_idx_bf idx_bf;
	enum ieee80211_pwr_idx_nss idx_ss;
	enum ieee80211_pwr_idx_bw idx_bw;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate ioctl message for channel power table\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(6)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	args->chan_ieee = chan->ic_ieee;
	for (fem_pri = PWR_IDX_FEM0_PRI_LOW; fem_pri < PWR_IDX_FEM_PRICHAN_MAX; ++fem_pri) {
		for (idx_bf = PWR_IDX_BF_OFF; idx_bf < PWR_IDX_BF_MAX; ++idx_bf) {
			for (idx_ss = PWR_IDX_1SS; idx_ss < PWR_IDX_SS_MAX; ++idx_ss) {
				for (idx_bw = PWR_IDX_20M; idx_bw < PWR_IDX_BW_MAX; ++idx_bw) {
					args->maxpwr[fem_pri][idx_bf][idx_ss][idx_bw] =
							chan->maxpwr[fem_pri][idx_bf][idx_ss][idx_bw];
				}
			}
		}
	}

	ioctl->ioctl_command = IOCTL_DEV_SET_CHAN_POWER_TABLE;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_arg2 = 1;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_wlan_bbsort_prio(struct ieee80211_wme_state *wme)
{
	int i, j, temp_aifsn, temp_band;

	for (i = 1; i < QDRV_SCH_BANDS - 1; i++) {
		for (j = 1; j < QDRV_SCH_BANDS - i; j++) {
			if (qdrv_sch_band_chg_prio[j].aifsn > qdrv_sch_band_chg_prio[j + 1].aifsn) {
				temp_aifsn = qdrv_sch_band_chg_prio[j].aifsn;
				temp_band = qdrv_sch_band_chg_prio[j].band_prio;

				qdrv_sch_band_chg_prio[j].aifsn = qdrv_sch_band_chg_prio[j + 1].aifsn;
				qdrv_sch_band_chg_prio[j].band_prio = qdrv_sch_band_chg_prio[j + 1].band_prio;

				qdrv_sch_band_chg_prio[j + 1].aifsn = temp_aifsn;
				qdrv_sch_band_chg_prio[j + 1].band_prio = temp_band;
			}
		}
	}
}

static void qdrv_wlan_80211_join_bss(struct ieee80211vap *vap)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_wme_state *wme = &ic->ic_wme;
	struct ieee80211_node *ni = vap->iv_bss;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;
	struct host_ioctl *ioctl;
	int i;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate JOIN BSS message\n");
		vnet_free_ioctl(ioctl);
		return;
	}
	memset(args, 0, sizeof(*args));

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(6)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	for (i = 0; i < WME_NUM_AC; i++) {
		args->wmm_params[i] = wme->wme_chanParams.cap_wmeParams[i];

		/* This is to inform qdrv scheduler that better AC may have worse parameter settings */
		qdrv_sch_band_chg_prio[i].band_prio = qdrv_sch_band_prio[i];
		qdrv_sch_band_chg_prio[i + 1].aifsn =
			wme->wme_chanParams.cap_wmeParams[qdrv_sch_band_prio[i + 1]].wmm_aifsn;
	}

	qdrv_sch_band_chg_prio[i].band_prio = qdrv_sch_band_prio[i];
	qdrv_sch_band_chg_prio[0].aifsn = wme->wme_chanParams.cap_wmeParams[3].wmm_aifsn;

	qdrv_wlan_bbsort_prio(wme);

	memset(ni->ni_shared_stats, 0, sizeof(*ni->ni_shared_stats));
	args->ni_shared_stats = ni->ni_shared_stats_phys;
	memcpy(args->ni_bssid, ni->ni_bssid, IEEE80211_ADDR_LEN);
	memcpy(args->ni_macaddr, vap->iv_myaddr, IEEE80211_ADDR_LEN);
	args->ni_associd = IEEE80211_NODE_AID(ni);
	args->ni_flags = ni->ni_flags;
	args->ni_qtn_flags = ni->ni_qtn_flags;
	args->ni_vendor = ni->ni_vendor;
	memcpy(args->ni_rates, ni->ni_rates.rs_rates, IEEE80211_RATE_MAXSIZE);
	memcpy(args->ni_htrates, ni->ni_htrates.rs_rates, IEEE80211_HT_RATE_MAXSIZE);
	args->ni_nrates = ni->ni_rates.rs_nrates;
	args->ni_htnrates = ni->ni_htrates.rs_nrates;
	memcpy(args->ni_htcap, &ni->ni_htcap, sizeof(struct ieee80211_htcap));
	memcpy(args->ni_htinfo, &ni->ni_htinfo, sizeof(struct ieee80211_htinfo));
	if (IS_IEEE80211_DUALBAND_VHT_ENABLED(ic) && (ni->ni_flags & IEEE80211_NODE_VHT)) {
		memcpy(args->ni_vhtcap, &ni->ni_vhtcap,
			sizeof(args->ni_vhtcap));
		memcpy(args->ni_vhtop, &ni->ni_vhtop,
			sizeof(args->ni_vhtop));
	}

	if (ni->ni_rsn_ie != NULL) {
		args->ni_rsn_caps = ni->ni_rsn.rsn_caps;
	}

	args->rsn_ucastcipher = ni->ni_rsn.rsn_ucastcipher;

	/* Automatic install of BA */
	if (ni->ni_implicit_ba_valid) {
		args->ni_implicit_ba_rx = ni->ni_implicit_ba;
		args->ni_implicit_ba_tx = ni->ni_vap->iv_implicit_ba;
		args->ni_implicit_ba_size = ni->ni_implicit_ba_size;
	} else {
		args->ni_implicit_ba_rx = 0;
		args->ni_implicit_ba_tx = 0;
	}

	if (ni->ni_qtn_assoc_ie) {
		qdrv_wlan_qtnie_parse(ni, ic, args);
		g_qdrv_non_qtn_assoc = 0;
	} else {
		g_qdrv_non_qtn_assoc = 1;
	}

	DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN, "%s - %x Send new BSSID\n",
			vap->iv_dev->name, ioctl->ioctl_argp);

	ioctl->ioctl_command = IOCTL_DEV_NEWBSSID;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_arg2 = 1;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);

	ieee80211_idx_add(ni, args->ni_node_idx);

	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);

#if defined(QBMPS_ENABLE)
	if (ic->ic_flags_qtn & IEEE80211_QTN_BMPS) {
		/* allocate or free/re-allocate null frame */
		ieee80211_sta_bmps_update(vap);
	}
#endif
}

static void qdrv_wlan_80211_beacon_update(struct ieee80211vap *vap)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_wme_state *wme = &ic->ic_wme;
	struct ieee80211_node *ni = vap->iv_bss;
	struct qtn_beacon_args *bc_args = NULL;
	dma_addr_t args_dma;
	struct host_ioctl *ioctl;
	struct sk_buff *beacon_skb;
	int i;

	if (!(vap->iv_dev->flags & IFF_RUNNING) || ic->ic_bsschan == IEEE80211_CHAN_ANYC)
		return;

#ifdef ARTSMNG_SUPPORT
	/* should return also for STA mode */
	if (vap->iv_opmode == IEEE80211_M_WDS || vap->iv_opmode == IEEE80211_M_STA)
#else
	if (vap->iv_opmode == IEEE80211_M_WDS)
#endif /* ARTSMNG_SUPPORT */
		return;

	spin_lock_bh(&qv->bc_lock);
	if (ieee80211_beacon_create_param(vap) != 0) {
		spin_unlock_bh(&qv->bc_lock);
		return;
	}
	memset(&qv->qv_boff, 0, sizeof(qv->qv_boff));
	beacon_skb = ieee80211_beacon_alloc(ni, &qv->qv_boff);
	if (beacon_skb == NULL) {
		spin_unlock_bh(&qv->bc_lock);
		return;
	}

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(bc_args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*bc_args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate BEACON UPDATE message\n");
		dev_kfree_skb_any(beacon_skb);
		vnet_free_ioctl(ioctl);
		ieee80211_beacon_destroy_param(vap);
		spin_unlock_bh(&qv->bc_lock);
		return;
	}
	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(7)ioctl %p dma ptr %p\n", ioctl, (void *)bc_args);

	ieee80211_beacon_update(ni, &qv->qv_boff, beacon_skb, 0);

	for (i = 0; i < WME_NUM_AC; i++) {
		bc_args->wmm_params[i] = wme->wme_chanParams.cap_wmeParams[i];
	}

	bc_args->bo_tim_len = qv->qv_boff.bo_tim_len;
	bc_args->bintval = ic->ic_lintval;
	bc_args->bo_htcap = 0;
	if (ic->ic_htinfo.choffset) {
		/* Network is operating in 40 MHZ mode */
		bc_args->bo_htinfo = 1;
	} else {
		/* Network is operating in 20 MHZ mode */
		bc_args->bo_htinfo = 0;
	}
	/* This is an 11AC network */
	if (IS_IEEE80211_VHT_ENABLED(ic)) {
		bc_args->bo_vhtcap = 1;
		bc_args->bo_vhtop = ic->ic_vhtop.chanwidth;
	} else if (IS_IEEE80211_11NG_VHT_ENABLED(ic)) {
		bc_args->bo_vhtcap = 1;
		bc_args->bo_vhtop = ic->ic_vhtop_24g.chanwidth;
	}
	ieee80211_beacon_flush_param(vap->param);
	flush_dcache_range((uint32_t)beacon_skb->data,
			(uint32_t)beacon_skb->data + beacon_skb->len);
	/* Convert to MuC mapping address before ioctl request */
	bc_args->bc_ie_head = plat_kernel_addr_to_dma(NULL, vap->param->head);
	bc_args->bc_ie_buf_start = plat_kernel_addr_to_dma(NULL, vap->param->buf);
	ioctl->ioctl_command = IOCTL_DEV_BEACON_START;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_arg2 = beacon_skb->len | (1 << 16);
	ioctl->ioctl_argp = args_dma;
#ifdef LHOST_DEBUG_BEACON
	printk("LHOST send a beacon %p length %d\n", beacon_skb->data, beacon_skb->len);
	ieee80211_dump_beacon_desc_ie(vap->param);
#endif
	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*bc_args), bc_args, args_dma);

	dev_kfree_skb_any(beacon_skb);
	spin_unlock_bh(&qv->bc_lock);

	ic->ic_init(ic);
}

static void qdrv_wlan_80211_beacon_stop(struct ieee80211vap *vap)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;
	struct host_ioctl *ioctl;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate BEACON STOP message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(11)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	ioctl->ioctl_command = IOCTL_DEV_BEACON_STOP;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_wlan_80211_set_vap_macaddr(struct ieee80211vap *vap, int enable)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl = vnet_alloc_ioctl(qv);

	if (!ioctl)
		return;

	ioctl->ioctl_command = IOCTL_DEV_SET_VAP_MACADDR;
	ioctl->ioctl_arg1 = enable;

	vnet_send_ioctl(qv, ioctl);
}

/*
 *  Process delba request
 */
static void qdrv_wlan_80211_process_delba(struct ieee80211_node *ni, int tid,
				int direction)
{
	struct qtn_baparams_args *args = NULL;
	dma_addr_t args_dma;
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate PROCESS_DELBA message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(8)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	/* Copy the values over */
	if (direction) {
		args->state = ni->ni_ba_tx[tid].state;
		args->tid = tid;
		args->type = IEEE80211_BA_IMMEDIATE;
		args->start_seq_num = ni->ni_ba_tx[tid].seq;
		args->window_size = ni->ni_ba_tx[tid].buff_size;
		args->lifetime = ni->ni_ba_tx[tid].timeout;
	} else {
		args->tid = tid;
		args->state = ni->ni_ba_rx[tid].state;
		args->type = IEEE80211_BA_IMMEDIATE;
		args->start_seq_num = ni->ni_ba_rx[tid].seq;
		args->window_size = ni->ni_ba_rx[tid].buff_size;
		args->lifetime = ni->ni_ba_rx[tid].timeout;
	}
	memcpy(args->ni_addr, ni->ni_macaddr, IEEE80211_ADDR_LEN);

	ioctl->ioctl_command = direction ?  IOCTL_DEV_BA_REMOVED_TX : IOCTL_DEV_BA_REMOVED_RX;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_wlan_80211_tdls_operation(struct ieee80211_node *ni,
		uint32_t ioctl_cmd, int cmd, uint32_t *value)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	struct qtn_tdls_args *args = NULL;
	dma_addr_t args_dma;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate TDLS set message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)args);
	memset(args, 0, sizeof(*args));

	memcpy(args->ni_macaddr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	args->tdls_cmd = cmd;
	args->ni_ncidx = ni->ni_node_idx;
	args->tdls_params = *value;

	ioctl->ioctl_command = ioctl_cmd;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	*value = args->tdls_params;

	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_wlan_80211_tdls_set_params(struct ieee80211_node *ni, int cmd, int value)
{
	DBGPRINTF(DBG_LL_CRIT, QDRV_LF_TRACE, "Node %pM set tdls param %d to "
			"%d-0x%x\n", ni->ni_macaddr, cmd, value, value);

	qdrv_wlan_80211_tdls_operation(ni, IOCTL_DEV_SET_TDLS_PARAM, cmd, (uint32_t*)&value);
}

static uint32_t qdrv_wlan_80211_tdls_get_params(struct ieee80211_node *ni, int cmd)
{
	uint32_t value = 0;

	DBGPRINTF(DBG_LL_CRIT, QDRV_LF_TRACE, "Node %pM get tdls param %d\n",
			ni->ni_macaddr, cmd);

	qdrv_wlan_80211_tdls_operation(ni, IOCTL_DEV_GET_TDLS_PARAM, cmd, &value);

	return value;
}

/*
 *  Enter/Leave power save state on STA mode
 */
static void qdrv_wlan_80211_power_save(struct ieee80211_node *ni, int enable)
{
	struct qtn_power_save_args *args = NULL;
	dma_addr_t args_dma;
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate POWER_SAVE message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(8)ioctl %p dma ptr %p\n", ioctl, (void *)args);

	args->enable = !!enable;
	memcpy(args->ni_addr, ni->ni_macaddr, IEEE80211_ADDR_LEN);

	ioctl->ioctl_command = IOCTL_DEV_POWER_SAVE;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

#ifndef ifr_media
#define	ifr_media	ifr_ifru.ifru_ivalue
#endif

static void qdrv_wlan_80211_radar_set_bw(struct ieee80211com *ic)
{
	uint32_t bw = 0;

	bw = qdrv_wlan_80211_get_cap_bw(ic);

	if (!qdrv_radar_set_bw(bw))
		return;
	ic->ic_radar_bw = bw;
}

static void qdrv_wlan_80211_set_cap_bw(struct ieee80211_node *ni, int bw)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifreq ifr;
	int retv;

	if ((bw > BW_HT40) && !ieee80211_swfeat_is_supported(SWFEAT_ID_VHT, 1))
		return;

	if (bw == qdrv_wlan_80211_get_cap_bw(ic))
		return;

	if (bw == BW_HT20) {
		if (IS_IEEE80211_VHT_ENABLED(ic))
			ic->ic_phymode = IEEE80211_MODE_11AC_VHT20PM;
		else if (IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan))
			ic->ic_phymode = IEEE80211_MODE_11NA;
		else if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
			if (ic->ic_phymode == IEEE80211_MODE_11NG_HT40PM) {
				ic->ic_phymode = IEEE80211_MODE_11NG;
			} else if (ic->ic_phymode == IEEE80211_MODE_11AC_NG_VHT40PM) {
				ic->ic_phymode = IEEE80211_MODE_11AC_NG_VHT20PM;
			}
		}
	} else if (bw == BW_HT40) {
		if (IS_IEEE80211_VHT_ENABLED(ic))
			ic->ic_phymode = IEEE80211_MODE_11AC_VHT40PM;
		else {
			if (!IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
				ic->ic_phymode = IEEE80211_MODE_11NA_HT40PM;
			else {
				if (ic->ic_phymode == IEEE80211_MODE_11AC_NG_VHT20PM) {
					ic->ic_phymode = IEEE80211_MODE_11AC_NG_VHT40PM;
				} else {
					ic->ic_phymode = IEEE80211_MODE_11NG_HT40PM;
				}
			}
		}
	} else if (bw == BW_HT80) {
		if (IS_IEEE80211_VHT_ENABLED(ic))
			ic->ic_phymode = IEEE80211_MODE_11AC_VHT80PM;
		else {
			DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN,
				"BW %d cannot be configured in current phymode\n", bw);
			return;
		}
	} else {
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "BW %d is not valid\n", bw);
		return;
	}

	ic->ic_bss_bw = bw;
	ieee80211_update_bw_capa(vap, bw);

	memset(&ifr, 0, sizeof(ifr));
	if(vap->iv_media.ifm_cur == NULL)
		return;

	ifr.ifr_media = vap->iv_media.ifm_cur->ifm_media &~ IFM_MMASK;
	ifr.ifr_media |= IFM_MAKEMODE(ic->ic_phymode);
	retv = ifmedia_ioctl(vap->iv_dev, &ifr, &vap->iv_media, SIOCSIFMEDIA);
	if (retv == -ENETRESET) {
		ic->ic_des_mode = ic->ic_phymode;
		ieee80211_setmode(ic, ic->ic_des_mode);
	}
	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "QDRV: PHY mode %d\n", ic->ic_phymode);
}

static void qdrv_wlan_80211_set_cap_sgi(struct ieee80211_node *ni, int sgi)
{
	struct ieee80211com *ic = ni->ni_ic;

	if (sgi) {
		ic->ic_vhtcap.cap_flags |= IEEE80211_VHTCAP_C_SHORT_GI_80;
		ic->ic_vhtcap_24g.cap_flags |= IEEE80211_VHTCAP_C_SHORT_GI_80;
		if (ic->ic_htcap.cap & IEEE80211_HTCAP_C_CHWIDTH40)
			ic->ic_htcap.cap |= (IEEE80211_HTCAP_C_SHORTGI40 |
					     IEEE80211_HTCAP_C_SHORTGI20);
		else
			ic->ic_htcap.cap |= IEEE80211_HTCAP_C_SHORTGI20;

	} else {
		ic->ic_vhtcap.cap_flags &= ~IEEE80211_VHTCAP_C_SHORT_GI_80;
		ic->ic_vhtcap_24g.cap_flags &= ~IEEE80211_VHTCAP_C_SHORT_GI_80;
		if (ic->ic_htcap.cap & IEEE80211_HTCAP_C_CHWIDTH40)
			ic->ic_htcap.cap &= ~(IEEE80211_HTCAP_C_SHORTGI40 |
					      IEEE80211_HTCAP_C_SHORTGI20);
		else
			ic->ic_htcap.cap &= ~IEEE80211_HTCAP_C_SHORTGI20;
	}
}

static void qdrv_wlan_80211_set_ldpc(struct ieee80211_node *ni, int ldpc)
{
	struct ieee80211com *ic = ni->ni_ic;
	ic->ldpc_enabled = (ldpc & 0x1);
}

static void qdrv_wlan_80211_set_stbc(struct ieee80211_node *ni, int stbc)
{
	struct ieee80211com *ic = ni->ni_ic;
	ic->stbc_enabled = (stbc & 0x1);
}

static void qdrv_wlan_80211_set_rts_cts(struct ieee80211_node *ni, int rts_cts)
{
	struct ieee80211com *ic = ni->ni_ic;
	ic->rts_cts_prot = (rts_cts & 0x1);
}

static void qdrv_wlan_80211_set_peer_rts_mode(struct ieee80211_node *ni, int mode)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap;
	uint8_t beacon_update_required = 0;

	if (mode > IEEE80211_PEER_RTS_MAX) {
		mode = IEEE80211_PEER_RTS_DEFAULT;
	}
	ic->ic_peer_rts_mode = mode;

	if ((mode == IEEE80211_PEER_RTS_DYN) &&
			(ic->ic_peer_rts != ic->ic_dyn_peer_rts)) {
		ic->ic_peer_rts = ic->ic_dyn_peer_rts;
		beacon_update_required = 1;
	} else if (mode == IEEE80211_PEER_RTS_PMP) {
		if ((ic->ic_sta_assoc - ic->ic_nonqtn_sta) >= IEEE80211_MAX_STA_CCA_ENABLED) {
			if (ic->ic_peer_rts != 1) {
				ic->ic_peer_rts = 1;
				beacon_update_required = 1;
			}
		} else {
			if (ic->ic_peer_rts != 0) {
				ic->ic_peer_rts = 0;
				beacon_update_required = 1;
			}
		}
	} else if (mode == IEEE80211_PEER_RTS_OFF) {
		ic->ic_peer_rts = 0;
		beacon_update_required = 1;
	}

	if (beacon_update_required) {
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			if (vap->iv_opmode != IEEE80211_M_HOSTAP)
				continue;
			if (vap->iv_state != IEEE80211_S_RUN)
				continue;

			ic->ic_beacon_update(vap);
		}
	}
}

static void qdrv_wlan_80211_set_11n40_only_mode(struct ieee80211_node *ni, int mode)
{
	struct ieee80211com *ic = ni->ni_ic;
	ic->ic_11n_40_only_mode = (mode & 0x1);
}

static void qdrv_wlan_80211_set_legacy_retry(struct ieee80211_node *ni, int retry_count)
{
	struct ieee80211com *ic = ni->ni_ic;

	ic->ic_legacy_retry_limit = (u_int8_t)retry_count;
}

static void qdrv_wlan_80211_set_retry_count(struct ieee80211_node *ni, int retry_count)
{
	struct ieee80211com *ic = ni->ni_ic;

	ic->ic_retry_count = (u_int8_t)retry_count;
}

void qdrv_wlan_80211_set_ht_mcsset(enum ieee80211_ht_nss ht_nss_cap, int has_ueqm,
				   struct ieee80211_htcap *htcap)
{
	if (!htcap)
		return;

	memset(htcap->mcsset, 0, sizeof(htcap->mcsset));

	if (ht_nss_cap == IEEE80211_HT_NSS1) {
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS1] = 0xff;
	} else if (ht_nss_cap == IEEE80211_HT_NSS2) {
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS1] = 0xff;
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS2] = 0xff;
		if (has_ueqm)
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM1] =
				IEEE80211_HT_MCSSET_20_40_UEQM1_2SS;
	} else if (ht_nss_cap == IEEE80211_HT_NSS3) {
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS1] = 0xff;
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS2] = 0xff;
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS3] = 0xff;
		if (has_ueqm) {
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM1] =
				IEEE80211_HT_MCSSET_20_40_UEQM1_2SS |
				IEEE80211_HT_MCSSET_20_40_UEQM1_3SS;
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM2] =
				IEEE80211_HT_MCSSET_20_40_UEQM2_3SS;
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM3] =
				IEEE80211_HT_MCSSET_20_40_UEQM3_3SS;
		}
	} else if (ht_nss_cap == IEEE80211_HT_NSS4) {
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS1] = 0xff;
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS2] = 0xff;
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS3] = 0xff;
		htcap->mcsset[IEEE80211_HT_MCSSET_20_40_NSS4] = 0xff;
		if (has_ueqm) {
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM1] =
				IEEE80211_HT_MCSSET_20_40_UEQM1_2SS |
				IEEE80211_HT_MCSSET_20_40_UEQM1_3SS;
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM2] =
				IEEE80211_HT_MCSSET_20_40_UEQM2_3SS;
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM3] =
				IEEE80211_HT_MCSSET_20_40_UEQM3_3SS |
				IEEE80211_HT_MCSSET_20_40_UEQM3_4SS;
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM4] =
				IEEE80211_HT_MCSSET_20_40_UEQM4_4SS;
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM5] =
				IEEE80211_HT_MCSSET_20_40_UEQM5_4SS;
			htcap->mcsset[IEEE80211_HT_MCSSET_20_40_UEQM6] =
				IEEE80211_HT_MCSSET_20_40_UEQM6_4SS;
		}
	}
}
EXPORT_SYMBOL(qdrv_wlan_80211_set_ht_mcsset);

void qdrv_wlan_80211_set_ht_mcsparams(enum ieee80211_ht_nss ht_nss_cap, int has_ueqm,
				      struct ieee80211_htcap *htcap)
{
	if (!htcap)
		return;

	htcap->mcsparams = IEEE80211_HTCAP_MCS_TX_SET_DEFINED;
	htcap->numtxspstr = 0;

	switch (ht_nss_cap) {
	case IEEE80211_HT_NSS4:
		if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X4, 0)) {
			htcap->numtxspstr = IEEE80211_HTCAP_MCS_TWO_TX_SS;
			htcap->mcsparams |= IEEE80211_HTCAP_MCS_TX_RX_SET_NEQ;
			if (has_ueqm)
				htcap->mcsparams |= IEEE80211_HTCAP_MCS_TX_UNEQ_MOD;
		}
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(qdrv_wlan_80211_set_ht_mcsparams);

u_int16_t qdrv_wlan_80211_get_vhtmcs_map(enum ieee80211_vht_nss vhtnss,
				     enum ieee80211_vht_mcs_supported vhtmcs)
{
	/* For Spatial stream from 1-8; set MCS=3 (not supported) */
	u_int16_t vhtmcsmap = IEEE80211_VHTMCS_ALL_DISABLE;

	switch(vhtnss) {
	case IEEE80211_VHT_NSS8:
		vhtmcsmap &= 0x3FFF;
		vhtmcsmap |= (vhtmcs << 14);
	case IEEE80211_VHT_NSS7:
		vhtmcsmap &= 0xCFFF;
		vhtmcsmap |= (vhtmcs << 12);
	case IEEE80211_VHT_NSS6:
		vhtmcsmap &= 0xF3FF;
		vhtmcsmap |= (vhtmcs << 10);
	case IEEE80211_VHT_NSS5:
		vhtmcsmap &= 0xFCFF;
		vhtmcsmap |= (vhtmcs << 8);
	case IEEE80211_VHT_NSS4:
		vhtmcsmap &= 0xFF3F;
		vhtmcsmap |= (vhtmcs << 6);
	case IEEE80211_VHT_NSS3:
		vhtmcsmap &= 0xFFCF;
		vhtmcsmap |= (vhtmcs << 4);
	case IEEE80211_VHT_NSS2:
		vhtmcsmap &= 0xFFF3;
		vhtmcsmap |= (vhtmcs << 2);
	case IEEE80211_VHT_NSS1:
	default:	/* At least 1 spatial stream supported */
		vhtmcsmap &= 0xFFFC;
		vhtmcsmap |= vhtmcs;
		break;
	}
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_WLAN,
		"vhtmcsmap: %x for NSS=%d & MCS=%d \n", vhtmcsmap, vhtnss, vhtmcs);

	return (vhtmcsmap);
}
EXPORT_SYMBOL(qdrv_wlan_80211_get_vhtmcs_map);

static void qdrv_wlan_80211_set_vht_mcsset(struct ieee80211_vhtcap *vhtcap, enum ieee80211_vht_nss vht_nss_cap,
	enum ieee80211_vht_nss vht_rx_nss_cap, enum ieee80211_vht_mcs_supported vht_mcs_cap)
{
	enum ieee80211_vht_nss max_vht_tx_nss_cap;
	enum ieee80211_vht_nss max_vht_rx_nss_cap;

	if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X2, 0)) {
		max_vht_tx_nss_cap = IEEE80211_VHT_NSS2;
		max_vht_rx_nss_cap = IEEE80211_VHT_NSS2;
	} else if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X4, 0)) {
		max_vht_tx_nss_cap = IEEE80211_VHT_NSS2;
		max_vht_rx_nss_cap = IEEE80211_VHT_NSS4;
	} else if (ieee80211_swfeat_is_supported(SWFEAT_ID_3X3, 0)) {
		max_vht_tx_nss_cap = IEEE80211_VHT_NSS3;
		max_vht_rx_nss_cap = IEEE80211_VHT_NSS3;
	} else {
		max_vht_tx_nss_cap = IEEE80211_VHT_NSS4;
		max_vht_rx_nss_cap = IEEE80211_VHT_NSS4;
	}
	vhtcap->rxmcsmap = qdrv_wlan_80211_get_vhtmcs_map(min(max_vht_rx_nss_cap, vht_rx_nss_cap),
				vht_mcs_cap);
	vhtcap->txmcsmap = qdrv_wlan_80211_get_vhtmcs_map(min(max_vht_tx_nss_cap, vht_nss_cap),
				vht_mcs_cap);
}

static int qdrv_wlan_80211_get_11ac_mode(struct ieee80211com *ic)
{
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_WLAN,
		"802.11ac mode = %d\n", ic->ic_phymode);

	if (IS_IEEE80211_VHT_ENABLED(ic)) {
		return (QTN_11NAC_ENABLE);
	} else {
		return (QTN_11NAC_DISABLE);
	}
}

static void qdrv_wlan_80211_set_11ac_mode(struct ieee80211com *ic, int vht)
{
#ifdef QDRV_FEATURE_HT
	int ic_phymode_save = ic->ic_phymode;

	/*
	 * phymode has already been initialized through set_bw
	 * - need to reinitialize if in 11ac mode
	 */
	if (vht == QTN_11NAC_ENABLE) {
		if (!IS_IEEE80211_VHT_ENABLED(ic)) {
			if (ic->ic_phymode == IEEE80211_MODE_11NG)
				ic->ic_phymode = IEEE80211_MODE_11AC_NG_VHT20PM;
			else if (ic->ic_phymode == IEEE80211_MODE_11NG_HT40PM)
				ic->ic_phymode = IEEE80211_MODE_11AC_NG_VHT40PM;
			else if (ic->ic_phymode == IEEE80211_MODE_11NA)
				ic->ic_phymode = IEEE80211_MODE_11AC_VHT20PM;
			else if (ic->ic_phymode == IEEE80211_MODE_11NA_HT40PM)
				ic->ic_phymode = IEEE80211_MODE_11AC_VHT40PM;
			else
				ic->ic_phymode = IEEE80211_MODE_11AC_VHT80PM;
		}
	} else {
		if ((ic->ic_phymode == IEEE80211_MODE_11AC_VHT80PM) ||
				(ic->ic_phymode == IEEE80211_MODE_11AC_VHT40PM))
			ic->ic_phymode = IEEE80211_MODE_11NA_HT40PM;
		else if (ic->ic_phymode == IEEE80211_MODE_11AC_VHT20PM)
			ic->ic_phymode = IEEE80211_MODE_11NA;
		else if (ic->ic_phymode == IEEE80211_MODE_11AC_NG_VHT20PM)
			ic->ic_phymode = IEEE80211_MODE_11NG;
		else if (ic->ic_phymode == IEEE80211_MODE_11AC_NG_VHT40PM)
			ic->ic_phymode = IEEE80211_MODE_11NG_HT40PM;
	}

	if ((vht == QTN_11NAC_ENABLE) && !ieee80211_swfeat_is_supported(SWFEAT_ID_VHT, 1))
		ic->ic_phymode = ic_phymode_save;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_WLAN,
			"802.11ac mode = %d\n", ic->ic_phymode);
#endif
}

static void qdrv_wlan_80211_set_txbf_config_flags(struct ieee80211com *ic)
{
	uint32_t hw_option;

	hw_option = qdrv_soc_get_hw_options();

	if (qtn_hw_mod_bf_is_supported_in_5g(hw_option))
		ic->ic_flags_ext2 |= IEEE80211_FEXT2_TXBF_SUPPORT_IN_5G;

	return;
}

#ifdef CONFIG_QVSP
static void qdrv_wlan_notify_qvsp_coc_state_changed(struct qvsp_c *qvsp, struct ieee80211com *ic)
{
	if (qvsp) {
		if (ic->ic_pm_state[QTN_PM_CURRENT_LEVEL] >= BOARD_PM_LEVEL_DUTY) {
			qvsp_inactive_flag_set(qvsp, QVSP_INACTIVE_COC);
		} else {
			qvsp_inactive_flag_clear(qvsp, QVSP_INACTIVE_COC);
		}
	}
}
#endif

static void qdrv_wlan_notify_pm_state_changed(struct ieee80211com *ic, int pm_level_prev)
{
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);
	const char *tag = QEVT_PM_PREFIX;
	const char *msg = "PM-LEVEL-CHANGE";

	if (!vap)
		return;

	if (ic->ic_pm_state[QTN_PM_CURRENT_LEVEL] != pm_level_prev &&
			ic->ic_pm_state[QTN_PM_CURRENT_LEVEL] != BOARD_PM_LEVEL_FORCE_NO) {

		qdrv_eventf(vap->iv_dev, "%s%s from %u to %u", tag, msg,
				(unsigned)pm_level_prev,
				(unsigned)ic->ic_pm_state[QTN_PM_CURRENT_LEVEL]);
	}
}

static void qdrv_send_to_l2_ext_filter(struct ieee80211vap *vap, struct sk_buff *skb)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct qdrv_wlan *qw = qv->parent;

	qdrv_tqe_send_l2_ext_filter(qw, skb);
}

static void qdrv_wlan_txbf_set_cap (struct ieee80211_node *ni, int value)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct qdrv_wlan *qw = qv->parent;
	struct ieee80211com *ic = &qw->ic;

	if (!value) {
		/*
		 * Turn off BF capabilities in the beacon when bfoff. Should
		 * work for both AP beamformer and STA beamformee disabling
		 * when bfoff is set.
		 * */
		ic->ic_vhtcap.cap_flags &=
			~(IEEE80211_VHTCAP_C_SU_BEAM_FORMER_CAP |
			  IEEE80211_VHTCAP_C_SU_BEAM_FORMEE_CAP);
	} else {
		ic->ic_vhtcap.cap_flags |=
			(IEEE80211_VHTCAP_C_SU_BEAM_FORMER_CAP |
			  IEEE80211_VHTCAP_C_SU_BEAM_FORMEE_CAP);
	}

	if (qv->iv.iv_opmode == IEEE80211_M_HOSTAP)
		qdrv_wlan_80211_beacon_update((struct ieee80211vap *)qv);

	return;
}

static void qdrv_wlan_80211_setparam(struct ieee80211_node *ni, int param,
	int value, unsigned char *data, int len)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct ieee80211vap *vap = &qv->iv;
	struct qdrv_wlan *qw = qv->parent;
	struct ieee80211com *ic = &qw->ic;
	struct qtn_setparams_args *args = NULL;
	dma_addr_t args_dma;
	dma_addr_t ctrl_dma;
	struct host_ioctl *ioctl;
	u_int8_t tid;
	u_int16_t seq, size, time;

	switch (param) {
	case IEEE80211_PARAM_FORCE_MUC_TRACE:
		qdrv_muc_traceback(value == 0xdead ? 1 : 0);
		return;
	case IEEE80211_PARAM_FORCE_ENABLE_TRIGGERS:
		g_triggers_on = value;
		return;
	case IEEE80211_PARAM_FORCE_MUC_HALT:
		qdrv_halt_muc();
		return;
	case IEEE80211_PARAM_HTBA_SEQ_CTRL:
		seq = value & 0xFFFF;
		tid = (value & 0xFF0000) >> 16;
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_HTBA_SEQ_CTRL (%d), %d, %d\n",
			param, tid, seq);
		ni->ni_ba_tx[tid].seq = seq;
		return;
	case IEEE80211_PARAM_HTBA_SIZE_CTRL:
		size = value & 0xFFFF;
		tid = (value & 0xFF0000) >> 16;
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_HTBA_SIZE_CTRL (%d), %d, %d\n",
			param, tid, size);
		ni->ni_ba_tx[tid].buff_size = size;
		return;
	case IEEE80211_PARAM_HTBA_TIME_CTRL:
		time = value & 0xFFFF;
		tid = (value & 0xFF0000) >> 16;
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_HTBA_TIME_CTRL (%d), %d, %d\n",
			param, tid, time);
		ni->ni_ba_tx[tid].timeout = time;
		return;
	case IEEE80211_PARAM_HT_ADDBA:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_HT_ADDBA (%d)\n", param);
		qdrv_wlan_80211_send_addba(ni, value);
		return;
	case IEEE80211_PARAM_HT_DELBA:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_HT_DELBA (%d)\n", param);
		qdrv_wlan_drop_ba(ni, value, 1, IEEE80211_REASON_UNSPECIFIED);
		qdrv_wlan_drop_ba(ni, value, 0, IEEE80211_REASON_UNSPECIFIED);
		return;
	case IEEE80211_PARAM_TXBF_CTRL:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_TXBF_CTRL (%d)\n", param);
		qdrv_txbf_config_set((struct qdrv_wlan *) qv->parent, value);
		return;
	case IEEE80211_PARAM_BW_SEL_MUC:
	case IEEE80211_PARAM_BW_SEL:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_BW_SEL_MUC(%d)\n", param);
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_RADAR,
			"Stop CAC due to bandwidth change\n");
		qdrv_radar_stop_active_cac();
		qdrv_wlan_80211_set_cap_bw(ni, value);
		qdrv_wlan_80211_radar_set_bw(ic);
		if ((qv->iv.iv_opmode == IEEE80211_M_HOSTAP) &&
				!(ic->ic_flags & IEEE80211_F_CHANSWITCH))
			qdrv_wlan_80211_beacon_update((struct ieee80211vap *)qv);
		break;
	case IEEE80211_PARAM_SHORT_GI:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_SHORT_GI(%d)\n", param);
		qdrv_wlan_80211_set_cap_sgi(ni, value);
		break;
	case IEEE80211_PARAM_LDPC:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_LDPC(%d)\n", param);
		qdrv_wlan_80211_set_ldpc(ni, value);
		break;
	case IEEE80211_PARAM_STBC:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_STBC(%d)\n", param);
		qdrv_wlan_80211_set_stbc(ni, value);
		break;
	case IEEE80211_PARAM_RTS_CTS:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_RTS_CTS(%d)\n", param);
		qdrv_wlan_80211_set_rts_cts(ni, value);
		break;
	case IEEE80211_PARAM_TX_QOS_SCHED:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_TX_QOS_SCHED(%d)\n", param);
		ni->ni_ic->ic_tx_qos_sched = (value & 0xf);
		break;
	case IEEE80211_PARAM_PEER_RTS_MODE:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_PEER_RTS_MODE(%d)\n", param);
		qdrv_wlan_80211_set_peer_rts_mode(ni, value);
		break;
	case IEEE80211_PARAM_DYN_WMM:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_DYN_WMM(%d)\n", param);
		ic->ic_dyn_wmm = value;
		break;
	case IEEE80211_PARAM_GET_CH_INUSE:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_GET_CH_INUSE(%d)\n", param);
		if (value)
			ic->ic_flags_qtn |= IEEE80211_QTN_PRINT_CH_INUSE;
		else
			ic->ic_flags_qtn &= ~IEEE80211_QTN_PRINT_CH_INUSE;
		break;
	case IEEE80211_PARAM_11N_40_ONLY_MODE:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_11N_40_ONLY_MODE (%d)\n", param);
		qdrv_wlan_80211_set_11n40_only_mode(ni, value);
		break;
	case IEEE80211_PARAM_MAX_MGMT_FRAMES:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_MAX_MGMT_FRAMES (%d)\n", param);
		qw->tx_if.txdesc_cnt[QDRV_TXDESC_MGMT] = value;
		break;
	case IEEE80211_PARAM_MCS_ODD_EVEN:
		qw->mcs_odd_even = value;
		break;
	case IEEE80211_PARAM_RESTRICTED_MODE:
		qw->tx_restrict = value;
		break;
	case IEEE80211_PARAM_RESTRICT_RTS:
		qw->tx_restrict_rts = value;
		break;
	case IEEE80211_PARAM_RESTRICT_LIMIT:
		qw->tx_restrict_limit = value;
		break;
	case IEEE80211_PARAM_RESTRICT_RATE:
		qw->tx_restrict_rate = value;
		break;
	case IEEE80211_PARAM_TEST_LNCB:
		if (value) {
			qw->flags_ext |= QDRV_WLAN_DEBUG_TEST_LNCB;
		} else {
			qw->flags_ext &= ~QDRV_WLAN_DEBUG_TEST_LNCB;
		}
		break;
	case IEEE80211_PARAM_UNKNOWN_DEST_ARP:
		if (value) {
			qw->flags_ext |= QDRV_WLAN_FLAG_UNKNOWN_ARP;
		} else {
			qw->flags_ext &= ~QDRV_WLAN_FLAG_UNKNOWN_ARP;
		}
		break;
	case IEEE80211_PARAM_UNKNOWN_DEST_DISCOVER_INTVAL:
		if (value < jiffies_to_msecs(1) || value > MAX_QDRV_UNKNOWN_DEST_DISCOVER_INTVAL) {
			printk("Invalid value %d, must be between %u and %u\n",
				value, jiffies_to_msecs(1), MAX_QDRV_UNKNOWN_DEST_DISCOVER_INTVAL);
		} else {
			qw->discover_intval = value;
		}
		break;
	case IEEE80211_PARAM_MUC_FLAGS:
	case IEEE80211_PARAM_HT_NSS_CAP:
		qdrv_wlan_80211_set_ht_mcsset(ic->ic_ht_nss_cap,
					      ic->ic_caps & IEEE80211_C_UEQM,
					      &ic->ic_htcap);
		qdrv_wlan_80211_set_ht_mcsparams(ic->ic_ht_nss_cap,
						 ic->ic_caps & IEEE80211_C_UEQM,
						 &ic->ic_htcap);
		break;
	case IEEE80211_PARAM_VHT_MCS_CAP:
	case IEEE80211_PARAM_VHT_NSS_CAP:
	case IEEE80211_PARAM_VHT_RX_NSS_CAP:
		qdrv_wlan_80211_set_vht_mcsset(&ic->ic_vhtcap, ic->ic_vht_nss_cap, ic->ic_vht_rx_nss_cap, ic->ic_vht_mcs_cap);
		qdrv_wlan_80211_set_vht_mcsset(&ic->ic_vhtcap_24g, ic->ic_vht_nss_cap_24g, ic->ic_vht_rx_nss_cap_24g, ic->ic_vht_mcs_cap);
		break;
	case IEEE80211_PARAM_UNKNOWN_DEST_FWD:
		if (value) {
			qw->flags_ext |= QDRV_WLAN_FLAG_UNKNOWN_FWD;
		} else {
			qw->flags_ext &= ~QDRV_WLAN_FLAG_UNKNOWN_FWD;
		}
		break;
	case IEEE80211_PARAM_PWR_SAVE: {
		uint32_t pm_param = QTN_PM_UNPACK_PARAM(value);
		uint32_t pm_value = QTN_PM_UNPACK_VALUE(value);
		int level_prev = ic->ic_pm_state[QTN_PM_CURRENT_LEVEL];

		if (pm_param < QTN_PM_IOCTL_MAX) {
			ic->ic_pm_state[pm_param] = pm_value;
#ifdef CONFIG_QVSP
			qdrv_wlan_notify_qvsp_coc_state_changed(qw->qvsp, ic);
#endif
			qdrv_wlan_notify_pm_state_changed(ic, level_prev);
		}

		if (pm_param == QTN_PM_PDUTY_PERIOD_MS &&
				pm_qos_requirement(PM_QOS_POWER_SAVE) >= BOARD_PM_LEVEL_DUTY) {
			if (ic->ic_lintval != ieee80211_pm_period_tu(ic)) {
				/* Configure beacon interval to power duty interval */
				ieee80211_beacon_interval_set(ic, ieee80211_pm_period_tu(ic));
			}
		}
		break;
	}
	case IEEE80211_PARAM_TEST_TRAFFIC:
		value = msecs_to_jiffies(value);
		if (value != vap->iv_test_traffic_period) {
			vap->iv_test_traffic_period = value;
			if (value == 0) {
				del_timer(&vap->iv_test_traffic);
			} else {
				mod_timer(&vap->iv_test_traffic,
						jiffies + vap->iv_test_traffic_period);
			}
		}
		break;
	case IEEE80211_PARAM_QCAT_STATE: {
		struct net_device *dev = qv->iv.iv_dev;

		qdrv_eventf(dev, "QCAT state=%d", value);
		printk("QCAT state=%d\n", value);

		TXSTAT_SET(qw, qcat_state, value);
		break;
	}
	case IEEE80211_PARAM_MIMOMODE:
		qw->tx_mimomode = value;
		break;
	case IEEE80211_PARAM_SHORT_RETRY_LIMIT:
	case IEEE80211_PARAM_LONG_RETRY_LIMIT:
		//Currenty don't supporte to set this param, because we don't implement this feature in MacFW.
		break;
	case IEEE80211_PARAM_RETRY_COUNT:
		qdrv_wlan_80211_set_retry_count(ni, value);
		break;
	case IEEE80211_PARAM_LEGACY_RETRY_LIMIT:
		qdrv_wlan_80211_set_legacy_retry(ni, value);
		break;
	case IEEE80211_PARAM_RTSTHRESHOLD:
		/* pass through, let the rts threshold value packed as normal param below */
		break;
	case IEEE80211_PARAM_CARRIER_ID:
		g_carrier_id = value;
		break;
	case IEEE80211_PARAM_BA_THROT:
#ifdef CONFIG_QVSP
		qdrv_wlan_manual_ba_throt(qw, qv, value);
#endif
		return;
	case IEEE80211_PARAM_WME_THROT:
#ifdef CONFIG_QVSP
		qdrv_wlan_manual_wme_throt(qw, qv, value);
#endif
		return;
	case IEEE80211_PARAM_MODE:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_11AC_MODE (%d)\n", param);
		qdrv_wlan_80211_set_11ac_mode(ic, value);
		if (qv->iv.iv_opmode == IEEE80211_M_HOSTAP) {
			qdrv_wlan_80211_beacon_update((struct ieee80211vap *)qv);
		}
		break;
	case IEEE80211_PARAM_GENPCAP:
		if (qdrv_genpcap_set(qw, value, &ctrl_dma) == 0) {
			data = (uint8_t *) &ctrl_dma;
			len = sizeof(ctrl_dma);
		}
		break;
	case IEEE80211_PARAM_TXBF_PERIOD:
		if (ic->ic_flags_ext2 & IEEE80211_FEXT2_TXBF_SUPPORT_IN_5G) {
			ic->ic_txbf_period = value;
			qdrv_wlan_txbf_set_cap(ni, value);
		}
		break;
	case IEEE80211_PARAM_CONFIG_PMF:
		if (qv->iv.iv_opmode == IEEE80211_M_HOSTAP)
			qdrv_wlan_80211_beacon_update((struct ieee80211vap *)qv);
		break;
	case IEEE80211_PARAM_WOWLAN:
		if ((IEEE80211_WOWLAN_HOST_POWER_SAVE == (value>>16)) &&
				(1 == (value & 0xffff))) {
#ifndef TOPAZ_AMBER_IP
			gpio_config(WOWLAN_GPIO_OUTPUT_PIN, GPIO_MODE_OUTPUT);
			gpio_wowlan_output(WOWLAN_GPIO_OUTPUT_PIN, 0);
#else
			/*
			 * In Amber WOWLAN is handled by WIFI2SOC interrupt.
			 */
#endif
		}
		break;
	case IEEE80211_PARAM_MAX_AGG_SIZE:
		ic->ic_tx_max_ampdu_size = value;
		break;
	case IEEE80211_PARAM_RESTRICT_WLAN_IP:
		qw->restrict_wlan_ip = !!value;
		break;
	case IEEE80211_PARAM_OFF_CHAN_SUSPEND:
		qdrv_hostlink_suspend_off_chan(qw, !!value);
		break;
	case IEEE80211_PARAM_CCA_FIXED:
		ic->cca_fix_disable = !!value;
		break;
	case IEEE80211_PARAM_AUTO_CCA_ENABLE:
		ic->auto_cca_enable = !!value;
		break;
	case IEEE80211_PARAM_BEACON_HANG_TIMEOUT:
		ic->ic_bcn_hang_timeout = value;
		break;
	case IEEE80211_PARAM_BB_DEAFNESS_WAR_EN:
		ic->bb_deafness_war_disable = !!value;
		break;
	case IEEE80211_PARAM_DOTH:
		qdrv_hostlink_radar_set_param(qw, RADAR_PARAM_SYS_DOTH, !!value);
		break;
	case IEEE80211_PARAM_PHY_STATS_MODE:
		if (qdrv_csi_est_enabled()) {
			DBGPRINTF_E(
				"Unable to change phy stats mode because CSI feature is enabled\n");
			return;
		}
		ic->ic_mode_get_phy_stats = value;
		break;
	case IEEE80211_PARAM_AFE_DISABLE:
		value = !!value;
		break;
	case IEEE80211_PARAM_RXFILTER_BCAST_ALLOW:
		value = !!value;
		break;
	case IEEE80211_PARAM_SNAP_HDR_CHECK:
		printk(KERN_INFO "AMSDU subframe DA field SNAP header check enable %u\n", value);
		qw->amsdu_security_check = value;
		break;
	default:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"<0x%08x> (%d)\n", param, param);
		break;
	}

	/* Make sure the data fits if it is provided */
	if (data != NULL && len > sizeof(args->ni_data)) {
		DBGPRINTF_E("Unable to transport %d bytes of data (max is %ld)\n",
			len, sizeof(args->ni_data));
		return;
	}

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate SETPARAM message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	/* Copy the values over */
	args->ni_param = param;
	if (param == IEEE80211_PARAM_MODE) {
		args->ni_value = ic->ic_phymode;
	} else {
		args->ni_value = value;
	}
	args->ni_len = 0;
	if (data != NULL && len > 0) {
		memcpy(args->ni_data, data, len);
		args->ni_len = len;
		if (param == IEEE80211_PARAM_UPDATE_MU_GRP) {
			/* place the sta's mac addr at the end of its group/pos arrays */
			memcpy(&args->ni_data[len], &ni->ni_macaddr[0], IEEE80211_ADDR_LEN);
			args->ni_len += IEEE80211_ADDR_LEN;
		}
	}

	ioctl->ioctl_command = IOCTL_DEV_SETPARAMS;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static int qdrv_wlan_set_l2_ext_filter_port(struct ieee80211vap *vap, int port)
{
	struct net_device *pcie_dev;
	int cfg;
	int cfg_val;
	int cfg_mask;
	uint8_t tqe_port;

	switch (port) {
	case L2_EXT_FILTER_EMAC_0_PORT:
		cfg = BOARD_CFG_EMAC0;
		cfg_mask = EMAC_IN_USE;
		tqe_port = TOPAZ_TQE_EMAC_0_PORT;
		break;
	case L2_EXT_FILTER_EMAC_1_PORT:
		cfg = BOARD_CFG_EMAC1;
		cfg_mask = EMAC_IN_USE;
		tqe_port = TOPAZ_TQE_EMAC_1_PORT;
		break;
	case L2_EXT_FILTER_PCIE_PORT:
		cfg = BOARD_CFG_PCIE;
		cfg_mask = PCIE_IN_USE;
		/*
		 * PCIE TQE port is determined at runtime
		 */
		pcie_dev = dev_get_by_name(&init_net, "pcie0");
		if (!pcie_dev) {
			printk("QDRV: Error setting L2 external filter port: no pcie0 device\n");
			return -ENODEV;
		}
		tqe_port = pcie_dev->if_port;
		dev_put(pcie_dev);
		break;
	default:
		printk("QDRV: Error setting L2 external filter port: port %d is invalid\n", port);
		return -EINVAL;
	}

	if (bootcfg_get_hw_board_config(cfg, NULL, &cfg_val) < 0) {
		printk("QDRV: Error setting L2 external filter port: error getting board config\n");
		return -ENODEV;
	}

	if (!(cfg_val & cfg_mask)) {
		printk("QDRV: Error setting L2 external filter port: no such port\n");
		return -ENODEV;
	}

	g_l2_ext_filter_port = tqe_port;

	qdrv_wlan_80211_setparam(vap->iv_bss, IEEE80211_PARAM_L2_EXT_FILTER_PORT,
				 g_l2_ext_filter_port, NULL, 0);
	return 0;
}

static int qdrv_wlan_set_l2_ext_filter(struct ieee80211vap *vap, int enable)
{
	int ret;

	if (enable && (g_l2_ext_filter_port == TOPAZ_TQE_NUM_PORTS)) {
		ret = qdrv_wlan_set_l2_ext_filter_port(vap, L2_EXT_FILTER_DEF_PORT);
		if (ret < 0)
			return ret;
	}

	g_l2_ext_filter = !!enable;

	qdrv_wlan_80211_setparam(vap->iv_bss, IEEE80211_PARAM_L2_EXT_FILTER,
				 g_l2_ext_filter, NULL, 0);
	return 0;
}

static int qdrv_wlan_get_l2_ext_filter_port(void)
{
	if (g_l2_ext_filter_port == TOPAZ_TQE_NUM_PORTS) {
		return L2_EXT_FILTER_DEF_PORT;
	} else if (g_l2_ext_filter_port == TOPAZ_TQE_EMAC_0_PORT) {
		return L2_EXT_FILTER_EMAC_0_PORT;
	} else if (g_l2_ext_filter_port == TOPAZ_TQE_EMAC_1_PORT) {
		return L2_EXT_FILTER_EMAC_1_PORT;
	} else {
		return L2_EXT_FILTER_PCIE_PORT;
	}
}

static __sram_text void
qdrv_wlan_stats_prot_ip(struct qdrv_wlan *qw, uint8_t is_tx, uint8_t ip_proto)
{
	switch (ip_proto) {
	case IPPROTO_UDP:
		QDRV_STAT(qw, is_tx, prot_ip_udp);
		break;
	case IPPROTO_TCP:
		QDRV_STAT(qw, is_tx, prot_ip_tcp);
		break;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		QDRV_STAT(qw, is_tx, prot_ip_icmp);
		break;
	case IPPROTO_IGMP:
		QDRV_STAT(qw, is_tx, prot_ip_igmp);
		break;
	default:
		DBGPRINTF(DBG_LL_NOTICE, is_tx ? QDRV_LF_PKT_TX : QDRV_LF_PKT_RX,
			"%s ip pkt type %u\n",
			is_tx ? "tx" : "rx", ip_proto);
		QDRV_STAT(qw, is_tx, prot_ip_other);
		break;
	}
}

__sram_text void
qdrv_wlan_stats_prot(struct qdrv_wlan *qw, uint8_t is_tx, uint16_t ether_type, uint8_t ip_proto)
{
	switch (ether_type) {
	case 0:
		break;
	case __constant_htons(ETH_P_IP):
		qdrv_wlan_stats_prot_ip(qw, is_tx, ip_proto);
		break;
	case __constant_htons(ETH_P_IPV6):
		QDRV_STAT(qw, is_tx, prot_ipv6);
		qdrv_wlan_stats_prot_ip(qw, is_tx, ip_proto);
		break;
	case __constant_htons(ETH_P_ARP):
		QDRV_STAT(qw, is_tx, prot_arp);
		break;
	case __constant_htons(ETH_P_PAE):
		QDRV_STAT(qw, is_tx, prot_pae);
		break;
	default:
		DBGPRINTF(DBG_LL_NOTICE, is_tx ? QDRV_LF_PKT_TX : QDRV_LF_PKT_RX,
			"%s pkt type 0x%04x\n",
			is_tx ? "tx" : "rx", ether_type);
		QDRV_STAT(qw, is_tx, prot_other);
		break;
	}
}

/*
 * This function performs proxy ARP for 3-address stations.  It is intended for use
 * with HS 2.0 vaps, which do not support 4-address stations.
 */
int qdrv_proxy_arp(struct ieee80211vap *iv,
		struct qdrv_wlan *qw,
		struct ieee80211_node *ni_rx,
		uint8_t *data_start)
{
	struct ieee80211_node *ni_target;
	struct ether_arp *arp = (struct ether_arp *)data_start;
	uint32_t s_ipaddr = get_unaligned((u32 *)&arp->arp_spa);
	uint32_t t_ipaddr = get_unaligned((u32 *)&arp->arp_tpa);
	int gratuitous_arp = (s_ipaddr == t_ipaddr);
	uint8_t macaddr[IEEE80211_ADDR_LEN];

	if (gratuitous_arp) {
		/*
		 * If the ARP announcement came from an associated station,
		 * update the node's IP address.
		 */
		if (ni_rx && (arp->ea_hdr.ar_op == __constant_htons(ARPOP_REQUEST))
				&& IEEE80211_ADDR_EQ(ni_rx->ni_macaddr, arp->arp_sha)) {
			if (ni_rx->ni_ip_addr == t_ipaddr)
				return 1;

			ni_target = ieee80211_find_node_by_ip_addr(iv, t_ipaddr);
			if (ni_target) {
				ni_target->ni_ip_addr = 0;
				ieee80211_free_node(ni_target);
			}
			ni_rx->ni_ip_addr = t_ipaddr;
		}

		return 1;
	}

	if (arp->ea_hdr.ar_op == __constant_htons(ARPOP_REQUEST)) {
		if (ipv4_is_loopback(t_ipaddr) || ipv4_is_multicast(t_ipaddr) ||
				ipv4_is_zeronet(t_ipaddr)) {
			return 0;
		}

		ni_target = ieee80211_find_node_by_ip_addr(iv, t_ipaddr);
		if (ni_target) {
			IEEE80211_ADDR_COPY(macaddr, ni_target->ni_macaddr);
			ieee80211_free_node(ni_target);
			arp_send(ARPOP_REPLY, ETH_P_ARP, s_ipaddr, qw->br_dev, t_ipaddr,
					arp->arp_sha, macaddr, arp->arp_sha);
			return 1;
		}
	}

	return 0;
}

#ifdef CONFIG_IPV6
static struct sk_buff * qdrv_build_neigh_adv_skb(struct net_device *dev,
			const struct in6_addr *daddr, const struct in6_addr *saddr,
			uint8_t *src_mac, uint8_t *dest_mac, struct icmp6hdr *icmp6h,
			const struct in6_addr *target, int llinfo)
{
	struct net *net = dev_net(dev);
	struct sock *sk = net->ipv6.ndisc_sk;
	struct sk_buff *skb;
	struct icmp6hdr *hdr;
	struct ether_header *eh;
	uint8_t *opt;
	int len;
	int err;

	len = sizeof(struct icmp6hdr) + (target ? sizeof(*target) : 0);
	if (llinfo) {
		len += NDISC_OPT_SPACE(IEEE80211_ADDR_LEN + ndisc_addr_option_pad(dev->type));
		/* type(1byte) + len(1byte) + Dev addr len + pad */
	}

	skb = sock_alloc_send_skb(sk, (MAX_HEADER + sizeof(struct ipv6hdr) +
				len + LL_ALLOCATED_SPACE(dev)), 1, &err);
	if (!skb) {
		DBGPRINTF_LIMIT_E("%s: failed to allocate an skb, err=%d\n",
							__func__, err);
		return NULL;
	}

	skb->dev = dev;
	skb->priority = WME_AC_VO;

	eh = (struct ether_header *)skb->tail;
	IEEE80211_ADDR_COPY(eh->ether_dhost, dest_mac);
	IEEE80211_ADDR_COPY(eh->ether_shost, src_mac);
	eh->ether_type = htons(ETH_P_IPV6);
	skb_put(skb, sizeof(*eh));
	skb->data += sizeof(*eh);

	ip6_nd_hdr(sk, skb, dev, saddr, daddr, IPPROTO_ICMPV6, len);
	skb->data -= sizeof(*eh);

	skb->transport_header = skb->tail;
	skb_put(skb, len);

	hdr = (struct icmp6hdr *)skb_transport_header(skb);
	memcpy(hdr, icmp6h, sizeof(*hdr));

	opt = skb_transport_header(skb) + sizeof(struct icmp6hdr);
	if (target) {
		ipv6_addr_copy((struct in6_addr *)opt, target);
		opt += sizeof(*target);
	}

	if (llinfo) {
		ndisc_fill_addr_option(opt, llinfo, src_mac,
					IEEE80211_ADDR_LEN, dev->type);
	}

	hdr->icmp6_cksum = csum_ipv6_magic(saddr, daddr, len,
					IPPROTO_ICMPV6,
					csum_partial(hdr,len, 0));

	return skb;
}

static int qdrv_send_neigh_adv(struct net_device *dev, const struct in6_addr *daddr,
			const struct in6_addr *solicited_addr, uint8_t *src_mac,
			uint8_t *dest_mac, int router, int solicited, int override, int llinfo)
{
	struct sk_buff *skb;
	struct icmp6hdr icmp6h = {
		.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT,
	};

	icmp6h.icmp6_router = router;
	icmp6h.icmp6_solicited = solicited;
	icmp6h.icmp6_override = override;

	skb = qdrv_build_neigh_adv_skb(dev, daddr, solicited_addr, src_mac, dest_mac,
			&icmp6h, solicited_addr, llinfo ? ND_OPT_TARGET_LL_ADDR : 0);
	if (!skb)
		return 1;

	dev_queue_xmit(skb);
	return 0;
}
#endif

#ifdef CONFIG_IPV6
static int qdrv_wlan_handle_neigh_sol(struct ieee80211vap *vap, struct qdrv_wlan *qw, void *proto_data,
			uint8_t *data_start, struct ether_header *eh, uint8_t in_tx)
{
	struct ipv6hdr *ipv6 = (struct ipv6hdr *)data_start;
	struct nd_msg *msg = (struct nd_msg *)proto_data;
	const struct in6_addr *saddr = &ipv6->saddr;
	const struct in6_addr *daddr = &ipv6->daddr;
	int dup_addr_detect = ipv6_addr_any(saddr);
	const struct in6_addr qdrv_in6addr_linklocal_allnodes = IN6ADDR_LINKLOCAL_ALLNODES_INIT;
	uint8_t all_node_mc_mac_addr[] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01};
	struct ieee80211_node_table *nt = &qw->ic.ic_sta;
	struct ieee80211_node *ni;
	uint8_t *dest_mac;
	uint8_t src_mac[IEEE80211_ADDR_LEN];
	int llinfo;

	if (!iputil_ipv6_is_neigh_sol_msg(dup_addr_detect, msg, ipv6))
		return 1;

	if (!qw->br_dev)
		return 1;

	if (dup_addr_detect) {
		/* Duplicate address detection */
		ni = ieee80211_find_node_by_ipv6_addr(vap, &msg->target);
		if (ni && !IEEE80211_ADDR_EQ(ni->ni_macaddr, eh->ether_shost)) {
			if (in_tx) {
				/* send multicast neighbour advertisement frame to back end only */
				dest_mac = all_node_mc_mac_addr;
			} else {
				/* send unicast neighbour advertisement frame to STA */
				dest_mac = eh->ether_shost;
			}
			IEEE80211_ADDR_COPY(src_mac, ni->ni_macaddr);
			ieee80211_free_node(ni);
			qdrv_send_neigh_adv(qw->br_dev, &qdrv_in6addr_linklocal_allnodes,
					&msg->target, src_mac, dest_mac,
					false, false, true, true);
			return 1;
		} else if (ni) {
			ieee80211_free_node(ni);
			return 1;
		}

		ni = ieee80211_find_node(nt, eh->ether_shost);
		if (ni && (IEEE80211_AID(ni->ni_associd) != 0)) {
			ipv6_addr_copy(&ni->ipv6_llocal, &msg->target);
			ieee80211_free_node(ni);
		} else if (ni) {
			ieee80211_free_node(ni);
		}

		return 1;
	}

	ni = ieee80211_find_node_by_ipv6_addr(vap, &msg->target);
	if (ni && IEEE80211_AID(ni->ni_associd) != 0) {
		IEEE80211_ADDR_COPY(src_mac, ni->ni_macaddr);
		ieee80211_free_node(ni);
		llinfo = ipv6_addr_is_multicast(daddr);
		qdrv_send_neigh_adv(qw->br_dev, saddr, &msg->target, src_mac,
				eh->ether_shost, false, true, llinfo, llinfo);
		return 1;
	} else if (ni) {
		ieee80211_free_node(ni);
		return 1;
	}

	return 0;
}
#endif

#ifdef CONFIG_IPV6
int qdrv_wlan_handle_neigh_msg(struct ieee80211vap *vap, struct qdrv_wlan *qw,
			uint8_t *data_start, uint8_t in_tx, struct sk_buff *skb,
			uint8_t ip_proto, void *proto_data)
{
	struct ipv6hdr *ipv6;
	struct icmp6hdr *icmpv6;
	struct iphdr *p_iphdr = (struct iphdr *)data_start;
	struct ether_header *eh = (struct ether_header *) skb->data;

	if (ip_proto == IPPROTO_ICMPV6) {
		ipv6 = (struct ipv6hdr *)data_start;
		icmpv6 = (struct icmp6hdr *)proto_data;

		switch(icmpv6->icmp6_type) {
		case NDISC_NEIGHBOUR_ADVERTISEMENT:
		case NDISC_NEIGHBOUR_SOLICITATION:

			if (!iputil_ipv6_is_neigh_msg(ipv6, icmpv6))
				return 1;

			if (icmpv6->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT) {
				/* Verify unsolicited neighbour advertisement */
				if (iputil_ipv6_is_ll_all_nodes_mc(eh->ether_dhost, p_iphdr) &&
						!icmpv6->icmp6_solicited && in_tx) {
					return 1;
				}
			}

			if (icmpv6->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) {
				if (qdrv_wlan_handle_neigh_sol(vap, qw, proto_data,
						data_start, eh, in_tx) || in_tx)
					return 1;
			}
			break;
		default:
			return 0;
		}
	}

	return 0;
}
#endif

static int qdrv_wlan_80211_get_phy_stats(struct net_device *dev,
					struct ieee80211com *ic,
					struct ieee80211_phy_stats *ps,
					uint8_t all_stats)
{
	struct qdrv_wlan *qw;
	struct qdrv_mac *mac;

	qw = container_of(ic, struct qdrv_wlan, ic);
	mac = qw->mac;

	return qdrv_muc_get_last_phy_stats(mac, ic, ps, all_stats);
}

static int qdrv_wlan_80211_get_ldpc(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	return (ic->ldpc_enabled);
}

static int qdrv_wlan_80211_get_stbc(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	return (ic->stbc_enabled);
}

static int qdrv_wlan_80211_get_rts_cts(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	return (ic->rts_cts_prot);
}

static int qdrv_wlan_80211_get_peer_rts_mode(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	return (ic->ic_peer_rts_mode);
}

static int qdrv_wlan_80211_get_11n40_only_mode(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	return (ic->ic_11n_40_only_mode);
}

static int qdrv_wlan_80211_get_legacy_retry_limit(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;

	return (ic->ic_legacy_retry_limit);
}

static int qdrv_wlan_80211_get_retry_count(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;

	return (ic->ic_retry_count);
}

static int qdrv_wlan_80211_get_cca_stats(struct net_device *dev,
					struct ieee80211com *ic,
					struct qtn_exp_cca_stats *cs)
{
	struct qdrv_wlan *qw;
	struct qdrv_mac *mac;

	qw = container_of(ic, struct qdrv_wlan, ic);
	mac = qw->mac;

	return qdrv_muc_get_last_cca_stats(mac, ic, cs);
}

static int qdrv_wlan_80211_get_cca_trfc(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw;
	struct qdrv_mac *mac;

	qw = container_of(ic, struct qdrv_wlan, ic);
	mac = qw->mac;

	return qdrv_muc_get_last_cca_trfc(mac);
}

int qdrv_wlan_get_hw_tx_chains(int mac_unit)
{
	int tx_chains;

	if (ieee80211_swfeat_is_supported(SWFEAT_ID_4X4, 0))
		tx_chains = 4;
	else if (ieee80211_swfeat_is_supported(SWFEAT_ID_3X3, 0))
		tx_chains = 3;
	else if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X4, 0))
		tx_chains = 2;
	else if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X2, 0))
		tx_chains = 2;
	else
		tx_chains = 1;

	return tx_chains;
}
EXPORT_SYMBOL(qdrv_wlan_get_hw_tx_chains);

int qdrv_wlan_get_hw_rx_chains(int mac_unit)
{
	int rx_chains;

	if (ieee80211_swfeat_is_supported(SWFEAT_ID_4X4, 0))
		rx_chains = 4;
	else if (ieee80211_swfeat_is_supported(SWFEAT_ID_3X3, 0))
		rx_chains = 3;
	else if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X4, 0))
		rx_chains = 4;
	else if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X2, 0))
		rx_chains = 2;
	else
		rx_chains = 1;

	return rx_chains;
}

int qdrv_wlan_get_max_tx_power(int mac_unit, enum qdrv_wifi_band band)
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();

	switch (band) {
	case QDRV_BAND_5G:
		return sp->max_tx_power;
	case QDRV_BAND_2G:
		return sp->max_tx_power_2g;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(qdrv_wlan_get_max_tx_power);

int qdrv_wlan_get_max_tx_power_by_chan(int mac_unit, unsigned int ieee)
{
	enum qdrv_wifi_band band;

	if (QTN_CHAN_IS_2G(ieee))
		band = QDRV_BAND_2G;
	else if (QTN_CHAN_IS_5G(ieee))
		band = QDRV_BAND_5G;
	else
		return 0;

	return qdrv_wlan_get_max_tx_power(mac_unit, band);
}
EXPORT_SYMBOL(qdrv_wlan_get_max_tx_power_by_chan);

int8_t qdrv_wlan_get_tx_antenna_gain(int mac_unit)
{
	char tmpbuf[QDRV_BOOTCFG_BUF_LEN];
	char *varstart;
	int value;
	int antenna_gain = 0;

	varstart = bootcfg_get_var("antenna_gain", tmpbuf);
	if (varstart != NULL && sscanf(varstart, "=%d", &value) == 1)
		antenna_gain = value / 4096;

	return antenna_gain;	/* dBi */
}
EXPORT_SYMBOL(qdrv_wlan_get_tx_antenna_gain);

static int qdrv_is_gain_low(void)
{

	struct device *dev = qdrv_soc_get_addr_dev();
	uint32_t mixval;
	uint32_t pgaval;
	uint8_t lowgain_mixer;
	uint8_t lowgain_pga;
#define RFMIX_LOAD_S	14
#define RFMIX_LOAD_M	0x1c000
#define RFMIX_PGA_S	2
#define RFMIX_PGA_M	0xc

	mixval = qdrv_command_read_rf_reg(dev, 166);
	pgaval = qdrv_command_read_rf_reg(dev, 168);

	lowgain_mixer = (((mixval & RFMIX_LOAD_M) >> RFMIX_LOAD_S) == 0x7);
	lowgain_pga = (((pgaval & RFMIX_PGA_M) >> RFMIX_PGA_S) == 0);

	if (lowgain_mixer && lowgain_pga)
		return 1;
	else
		return 0;
}

static int
qdrv_wlan_get_congestion_index(struct qdrv_wlan *qw)
{
#define QDRV_CONGEST_IX_ROUNDED(_pc)	(((_pc) + 5) / 10)
	struct muc_tx_stats *tx_stats = NULL;
	int congest_idx;

	if (!qw)
		return -EFAULT;

	qdrv_pktlogger_flush_data(qw);

	tx_stats = (struct muc_tx_stats *)qw->pktlogger.stats_uc_tx_ptr;
	if (!tx_stats)
		return -EFAULT;

	congest_idx = QDRV_CONGEST_IX_ROUNDED(tx_stats->cca_fat);
	if ((congest_idx < 0) || (congest_idx > 10))
		return -EFAULT;

	return congest_idx;
}

static uint32_t qdrv_wlan_get_rx_frame_addressed_to_wrong_bss(struct qdrv_wlan *qw)
{
	struct muc_rx_stats *rx_stats = NULL;

	if (!qw)
		return 0;

	qdrv_pktlogger_flush_data(qw);

	rx_stats = (struct muc_rx_stats *)qw->pktlogger.stats_uc_rx_ptr;
	if (!rx_stats)
		return 0;

	return rx_stats->rx_frame_addressed_to_wrong_bss;
}

static uint32_t qdrv_wlan_get_rx_node_not_found(struct qdrv_wlan *qw)
{
	struct muc_rx_stats *rx_stats = NULL;

	if (!qw)
		return 0;

	qdrv_pktlogger_flush_data(qw);

	rx_stats = (struct muc_rx_stats *)qw->pktlogger.stats_uc_rx_ptr;
	if (!rx_stats)
		return 0;

	return rx_stats->rx_node_not_found;
}

static uint32_t qdrv_wlan_get_tx_status_xretry(struct qdrv_wlan *qw)
{
	if (!qw)
                return 0;

	if (!qw->pktlogger.stats_auc_dbg_p)
                qdrv_pktlogger_set_auc_status_ptr(qw);

	qdrv_pktlogger_flush_data(qw);

	if (qw->pktlogger.stats_auc_dbg_p)
                return qw->pktlogger.stats_auc_dbg_p->tx_done_status_xretry;
        else
                return 0;
}

static uint32_t qdrv_wlan_get_michael_errcnt(struct qdrv_wlan *qw)
{
	struct muc_rx_stats *rx_stats = NULL;

	if (!qw)
		return 0;

	qdrv_pktlogger_flush_data(qw);

	rx_stats = (struct muc_rx_stats *)qw->pktlogger.stats_uc_rx_ptr;
	if (!rx_stats)
		return 0;

	return rx_stats->rx_tkip_mic_err;
}

/*
 * FCSErrorCount is the number of frames for which the Frame Check Sequence (FCS) at
 * the end of the MAC frame was in error
 *
 * return:
 *    crc_errs collected from MuC stats
 */
static uint32_t qdrv_wlan_get_fcs_errcnt(struct qdrv_wlan *qw)
{
	struct muc_rx_stats *rx_stats = NULL;

	if (!qw)
		return 0;

	qdrv_pktlogger_flush_data(qw);

	rx_stats = (struct muc_rx_stats *)qw->pktlogger.stats_uc_rx_ptr;
	if (!rx_stats)
		return 0;

	return rx_stats->crc_errs;
}

/*
 * PLCPErrorCount is the number of errors in the PLCP headers of the received MPDUs,
 * which is the number of frames for which the parity check of the PLCP header failed
 *
 * input:
 *    qw: qdrv_wlan object
 *
 * return:
 *    plcp_errs collected from MuC stats
 */
static uint32_t qdrv_wlan_get_plcp_errcnt(struct qdrv_wlan *qw)
{
	struct muc_rx_stats *rx_stats = NULL;

	if (!qw)
		return 0;

	qdrv_pktlogger_flush_data(qw);

	rx_stats = (struct muc_rx_stats *)qw->pktlogger.stats_uc_rx_ptr;
	if (!rx_stats)
		return 0;

	return rx_stats->plcp_errs;
}

static void qdrv_get_mu_grp(struct ieee80211_node *ni,
	struct qtn_mu_grp_args *mu_grp_tbl_cpy)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma;

	struct qtn_mu_grp_args *mu_grp_tbl;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(mu_grp_tbl = qdrv_hostlink_alloc_coherent(NULL,
				sizeof(*mu_grp_tbl)*IEEE80211_MU_GRP_NUM_MAX,
				&dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate DISASSOC message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)mu_grp_tbl);

	ioctl->ioctl_command = IOCTL_DEV_GET_MU_GRP;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl)) {
		memcpy(mu_grp_tbl_cpy, mu_grp_tbl, sizeof(*mu_grp_tbl)*IEEE80211_MU_GRP_NUM_MAX);
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*mu_grp_tbl)*IEEE80211_MU_GRP_NUM_MAX,
				mu_grp_tbl, dma);
}

static int32_t qdrv_get_mu_enable(struct ieee80211_node *ni)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma;

	int32_t *mu_enable_ptr;
	int32_t mu_enable = -1;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(mu_enable_ptr = qdrv_hostlink_alloc_coherent(NULL,
				sizeof(*mu_enable_ptr),
				&dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate DISASSOC message\n");
		vnet_free_ioctl(ioctl);
		goto exit;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)mu_enable_ptr);

	ioctl->ioctl_command = IOCTL_DEV_GET_MU_ENABLE;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl)) {
		mu_enable = *mu_enable_ptr;
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*mu_enable_ptr), mu_enable_ptr, dma);
exit:
	return mu_enable;
}

static int32_t qdrv_get_mu_grp_qmat(struct ieee80211_node *ni, uint8_t grp)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma;

	int32_t *prec_enable_ptr;
	int32_t prec_enable = -1;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(prec_enable_ptr = qdrv_hostlink_alloc_coherent(NULL,
				sizeof(*prec_enable_ptr),
				&dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate DISASSOC message\n");
		vnet_free_ioctl(ioctl);
		goto exit;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)prec_enable_ptr);

	ioctl->ioctl_command = IOCTL_DEV_GET_PRECODE_ENABLE;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_arg2 = grp;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl)) {
		prec_enable = *prec_enable_ptr;
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*prec_enable_ptr), prec_enable_ptr, dma);
exit:
	return prec_enable;
}

static int32_t qdrv_get_mu_use_eq(struct ieee80211_node *ni)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma;

	int32_t *eq_enable_ptr;
	int32_t eq_enable = -1;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(eq_enable_ptr = qdrv_hostlink_alloc_coherent(NULL,
				sizeof(*eq_enable_ptr),
				&dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate DISASSOC message\n");
		vnet_free_ioctl(ioctl);
		goto exit;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)eq_enable_ptr);

	ioctl->ioctl_command = IOCTL_DEV_GET_MU_USE_EQ;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl)) {
		eq_enable = *eq_enable_ptr;
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*eq_enable_ptr), eq_enable_ptr, dma);
exit:
	return eq_enable;
}

static void qdrv_get_param_from_muc(struct ieee80211_node *ni, int param, int *value,
							unsigned char *data, int *len)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct qtn_setparams_args *args = NULL;
	struct host_ioctl *ioctl;
	dma_addr_t args_dma;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
							&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate get param message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	switch (param) {
	case IEEE80211_PARAM_TX_AGG_TIMEOUT:
		args->ni_value = *value >> 16;
		break;
	case IEEE80211_PARAM_NODE_BW_CAP:
		memcpy(args->ni_data, data, *len);
		break;
	default:
		args->ni_value = *value;
		break;
	}

	ioctl->ioctl_command = IOCTL_DEV_GETPARAMS;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = args_dma;

	args->ni_param = param;

	*value = -1;
	if (vnet_send_ioctl(qv, ioctl)) {
		switch(param) {
		case IEEE80211_PARAM_KEY_RSC:
			if (data && len && *len && *len >= args->ni_len) {
				memset(data, 0, *len);
				memcpy(data, args->ni_data, *len);
			} else {
				DBGPRINTF_E("Failed to assign key rsc value\n");
			}
			break;
		default:
			*value = args->ni_value;
		}
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static int32_t qdrv_get_int32_by_ioctl_command(struct ieee80211_node *ni, uint32_t ioctl_command)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl = NULL;
	dma_addr_t dma;

	int32_t *value_ptr;
	int32_t value = -1;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(value_ptr = qdrv_hostlink_alloc_coherent(NULL,
				sizeof(*value_ptr),
				&dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate BB TXTDUF message\n");
		if (ioctl)
			vnet_free_ioctl(ioctl);
		goto exit;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)value_ptr);

	ioctl->ioctl_command = ioctl_command;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl)) {
		value = *value_ptr;
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*value_ptr), value_ptr, dma);
exit:
	return value;
}

static int32_t qdrv_get_cbf_ng(struct ieee80211_node *ni)
{
	return qdrv_get_int32_by_ioctl_command(ni, IOCTL_DEV_GET_CBF_NG);
}

static void qdrv_get_timestamp(struct ieee80211_node *ni, uint8_t *data, int *len)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl = NULL;
	dma_addr_t dma = 0;
	uint64_t *value_ptr;
	if (*len < sizeof(uint64_t)) {
		*len = 0;
		return;
	}

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(value_ptr = qdrv_hostlink_alloc_coherent(NULL,
				sizeof(*value_ptr),
				&dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate BB TXTDUF message\n");
		if (ioctl)
			vnet_free_ioctl(ioctl);
		*len = 0;
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)value_ptr);

	ioctl->ioctl_command = IOCTL_DEV_GET_TIMESTAMP;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl))
		*(uint64_t *)data = *value_ptr;
	else
		*len = 0;

	qdrv_hostlink_free_coherent(NULL, sizeof(*value_ptr), value_ptr, dma);
	return;
}

static int qdrv_wlan_80211_getparam(struct ieee80211_node *ni, int param,
	int *value, unsigned char *data, int *len)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct qdrv_wlan *qw = qv->parent;
	struct ieee80211com *ic = &qw->ic;
	struct qtn_stats_log *iw_stats_log = qw->mac->mac_sys_stats;

	if (!ic->ic_muc_tx_stats) {
		ic->ic_muc_tx_stats = (struct muc_tx_stats *) ioremap_nocache(
				muc_to_lhost((u32)iw_stats_log->tx_muc_stats),
				sizeof(struct muc_tx_stats));
	}

	static uint32_t keep_alive_cnt;
	uint32_t val;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	switch (param) {
	case IEEE80211_PARAM_TXBF_CTRL:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_TXBF_CTRL (%d)\n", param);
		qdrv_txbf_config_get(qw, &val);
		*value = val;
		break;
	case IEEE80211_PARAM_TXBF_PERIOD:
		*value = ic->ic_txbf_period;
		break;
	case IEEE80211_PARAM_GET_RFCHIP_ID:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_GET_RFCHIP_ID (%d)\n", param);
		*value = qw->rf_chipid;
		break;
	case IEEE80211_PARAM_GET_RFCHIP_VERID:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_GET_RFCHIP_VERID (%d)\n", param);
		*value = qw->rf_chip_verid;
		break;
	case IEEE80211_PARAM_BW_SEL_MUC:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_BW_SEL_MUC (%d)\n", param);
		*value = qdrv_wlan_80211_get_cap_bw(ni->ni_ic);
		break;
	case IEEE80211_PARAM_LDPC:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_LDPC (%d)\n", param);
		*value = qdrv_wlan_80211_get_ldpc(ni);
		break;
	case IEEE80211_PARAM_STBC:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_STBC (%d)\n", param);
		*value = qdrv_wlan_80211_get_stbc(ni);
		break;
	case IEEE80211_PARAM_RTS_CTS:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_RTS_CTS (%d)\n", param);
		*value = qdrv_wlan_80211_get_rts_cts(ni);
		break;
	case IEEE80211_PARAM_TX_QOS_SCHED:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_TX_QOS_SCHED (%d)\n", param);
		*value = ni->ni_ic->ic_tx_qos_sched;
		break;
	case IEEE80211_PARAM_PEER_RTS_MODE:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_PEER_RTS_MODE (%d)\n", param);
		*value = qdrv_wlan_80211_get_peer_rts_mode(ni);
		break;
	case IEEE80211_PARAM_DYN_WMM:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_DYN_WMM (%d)\n", param);
		*value = ic->ic_dyn_wmm;
		break;
	case IEEE80211_PARAM_11N_40_ONLY_MODE:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"IEEE80211_PARAM_11N_40_ONLY_MODE (%d)\n", param);
		*value = qdrv_wlan_80211_get_11n40_only_mode(ni);
		break;
	case IEEE80211_PARAM_MAX_MGMT_FRAMES:
		*value = qw->tx_if.txdesc_cnt[QDRV_TXDESC_MGMT];
		break;
	case IEEE80211_PARAM_MCS_ODD_EVEN:
		*value = qw->mcs_odd_even;
		break;
	case IEEE80211_PARAM_RESTRICTED_MODE:
		*value = qw->tx_restrict;
		break;
	case IEEE80211_PARAM_RESTRICT_RTS:
		*value = qw->tx_restrict_rts;
		break;
	case IEEE80211_PARAM_RESTRICT_LIMIT:
		*value = qw->tx_restrict_limit;
		break;
	case IEEE80211_PARAM_RESTRICT_RATE:
		*value = qw->tx_restrict_rate;
		break;
	case IEEE80211_PARAM_CONFIG_TXPOWER:
		*value = qdrv_is_gain_low();
		break;
	case IEEE80211_PARAM_LEGACY_RETRY_LIMIT:
		*value = qdrv_wlan_80211_get_legacy_retry_limit(ni);
		break;
	case IEEE80211_PARAM_MIMOMODE:
		*value = qw->tx_mimomode;
		break;
	case IEEE80211_PARAM_SHORT_RETRY_LIMIT:
	case IEEE80211_PARAM_LONG_RETRY_LIMIT:
		//Just return max software retry for aggregation.
		*value = QTN_TX_SW_ATTEMPTS_AGG_MAX;
		break;
	case IEEE80211_PARAM_RETRY_COUNT:
		*value = qdrv_wlan_80211_get_retry_count(ni);
		break;
	case IEEE80211_PARAM_BR_IP_ADDR:
		qdrv_get_br_ipaddr(qw, (__be32 *)value);
		break;
	case IEEE80211_PARAM_CACSTATUS:
		*value = (qw->sm_stats.sm_state & QDRV_WLAN_SM_STATE_CAC_ACTIVE) ? 1 : 0;
		break;
	case IEEE80211_PARAM_CARRIER_ID:
		*value = g_carrier_id;
		break;
	case IEEE80211_PARAM_PLATFORM_ID:
		*value = ic->ic_ver_platform_id;
		break;
	case IEEE80211_PARAM_MODE:
		*value = qdrv_wlan_80211_get_11ac_mode(ic);
		break;
	case IEEE80211_PARAM_CONGEST_IDX:
		*value = qdrv_wlan_get_congestion_index(qw);
		break;
	case IEEE80211_PARAM_MICHAEL_ERR_CNT:
		*value = qdrv_wlan_get_michael_errcnt(qw);
		break;
	case IEEE80211_PARAM_TX_ACK_FAILURE_COUNT:
		*value = qdrv_wlan_get_tx_status_xretry(qw);
		break;
	case IEEE80211_PARAM_RX_PKT_NON_ASSOC:
		*value = qdrv_wlan_get_rx_frame_addressed_to_wrong_bss(qw)
                                + qdrv_wlan_get_rx_node_not_found(qw);
                break;
	case IEEE80211_PARAM_RX_FCS_ERR_CNT:
		*value = qdrv_wlan_get_fcs_errcnt(qw);
		break;
	case IEEE80211_PARAM_RX_PLCP_ERR_CNT:
		*value = qdrv_wlan_get_plcp_errcnt(qw);
		break;
	case IEEE80211_PARAM_MAX_AGG_SIZE:
		*value = ic->ic_tx_max_ampdu_size;
		break;
	case IEEE80211_PARAM_GET_MU_GRP:
		qdrv_get_mu_grp(ni, (void*)data);
		break;
	case IEEE80211_PARAM_MU_ENABLE:
		*value = qdrv_get_mu_enable(ni);
		break;
	case IEEE80211_PARAM_GET_MU_GRP_QMAT:
		*value = qdrv_get_mu_grp_qmat(ni, (*value) >> 16);
		break;
	case IEEE80211_PARAM_MU_USE_EQ:
		*value = qdrv_get_mu_use_eq(ni);
		break;
	case IEEE80211_PARAM_RESTRICT_WLAN_IP:
		*value = qw->restrict_wlan_ip;
		break;
	case IEEE80211_PARAM_EP_STATUS:
		*value = keep_alive_cnt++;
		break;
	case IEEE80211_PARAM_CCA_FIXED:
		*value = ic->cca_fix_disable;
		break;
	case IEEE80211_PARAM_AUTO_CCA_ENABLE:
		*value = ic->auto_cca_enable;
		break;
	case IEEE80211_PARAM_KEY_RSC:
		qdrv_get_param_from_muc(ni, param, value, data, len);
		break;
	case IEEE80211_PARAM_GET_CCA_STATS:
		/* 4-byte value field can accomodate more CCA stats if required */
		*value = ic->ic_muc_tx_stats->cca_sec40;
		break;
	case IEEE80211_PARAM_BEACON_HANG_TIMEOUT:
		*value = ic->ic_bcn_hang_timeout;
		break;
	case IEEE80211_PARAM_BB_DEAFNESS_WAR_EN:
		*value = !!ic->bb_deafness_war_disable;
		break;
	case IEEE80211_PARAM_MU_QMAT_BYPASS_MODE_EN:
		*value = ic->ic_mu_qmat_bypass_mode_enable;
		break;
	case IEEE80211_PARAM_TX_AGG_TIMEOUT:
	case IEEE80211_PARAM_RX_AGG_TIMEOUT:
	case IEEE80211_PARAM_MAX_AGG_SUBFRM:
	case IEEE80211_PARAM_TX_AMSDU:
	case IEEE80211_PARAM_DYNAMIC_AC:
	case IEEE80211_PARAM_DUR_POW_DET_CCA:
#if defined(QBMPS_ENABLE)
	case IEEE80211_PARAM_BMPS_FEATURES:
	case IEEE80211_PARAM_BMPS_WAKEUP_OFFSET_MIN:
	case IEEE80211_PARAM_BMPS_WAKEUP_OFFSET_MAX:
#endif
		qdrv_get_param_from_muc(ni, param, value, NULL, NULL);
		break;
	case IEEE80211_PARAM_CBF_NG:
		*value = qdrv_get_cbf_ng(ni);
		break;
	case IEEE80211_PARAM_UNKNOWN_DEST_DISCOVER_INTVAL:
		*value = qw->discover_intval;
		break;
	case IEEE80211_PARAM_TIMESTAMP:
		qdrv_get_timestamp(ni, data, len);
		break;
	case IEEE80211_PARAM_NODE_BW_CAP:
	case IEEE80211_PARAM_BSS_BW:
	case IEEE80211_PARAM_CUSTOM_PHY_MODE:
	case IEEE80211_PARAM_SLOT_TIME:
	case IEEE80211_PARAM_ACK_TIMEOUT:
	case IEEE80211_PARAM_ADJUST_DEFAULT_RF_TX_GAIN_2G:
	case IEEE80211_PARAM_SEC_CCA_RESET_COUNT:
	case IEEE80211_PARAM_SEC_CCA_RESET_THR:
	case IEEE80211_PARAM_ENABLECOUNTERMEASURES:
	case IEEE80211_PARAM_RXFILTER_BCAST_ALLOW:
		qdrv_get_param_from_muc(ni, param, value, data, len);
		break;
	default:
		DBGPRINTF(DBG_LL_CRIT, QDRV_LF_WLAN,
			"<0x%08x> (%d)\n", param, param);
		break;
	}

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
	return 0;
}

void qdrv_wlan_80211_stats(struct ieee80211com *ic, struct iw_statistics *is)
{
	struct qdrv_wlan *qw;
	struct qtn_phy_stats *phy_stats = NULL;
	int curr_index;
	int rssi;
	int noise;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	/*
	 * NB: the referenced node pointer is in the
	 * control block of the sk_buff.  This is
	 * placed there by ieee80211_mgmt_output because
	 * we need to hold the reference with the frame.
	 */

	qw = container_of(ic, struct qdrv_wlan, ic);
	phy_stats = (struct qtn_phy_stats *)qw->pktlogger.stats_uc_phy_stats_ptr;

	if (!phy_stats) {
		/* No stats available from MuC, mark all as invalid */
		is->qual.qual  = 0;
		is->qual.noise = 0;
		is->qual.level = 0;
		is->qual.updated = IW_QUAL_ALL_INVALID;
		return;
	}

	curr_index = phy_stats->curr_buff;

	/* Take the previous value */
	curr_index = (curr_index - 1 + NUM_LOG_BUFFS)%NUM_LOG_BUFFS;

	/* Collect error stats */
	is->discard.misc += phy_stats->stat_buffs[curr_index].rx_phy_stats.cnt_mac_crc;
	is->discard.retries += phy_stats->stat_buffs[curr_index].tx_phy_stats.num_swretries +
			phy_stats->stat_buffs[curr_index].tx_phy_stats.num_hwretries;

	/*
	 * Collect PHY Stats
	 *
	 * The RSSI values reported in the TX/RX descriptors in the driver are the SNR
	 * expressed in dBm. Thus 'rssi' is signal level above the noise floor in dBm.
	 *
	 * Noise is measured in dBm and is negative unless there is an unimaginable
	 * level of RF noise.
	 *
	 * The signal level is noise + rssi.
	 *
	 * Note that the iw_quality values are 1 byte, and can be signed, unsigned or
	 * negative depending on context.
         *
         * Note iwconfig's quality parameter is a relative value while rssi here is a
         * absolute/dBm value.
         * Convert rssi from absolute/dBm value to a relative one.
         * The convertion logic is reused from qcsapi
	 */
	rssi = phy_stats->stat_buffs[curr_index].rx_phy_stats.last_rssi_all;
	noise = phy_stats->stat_buffs[curr_index].rx_phy_stats.hw_noise;

	if (rssi < 0)
		is->qual.level = (rssi - 5) / 10;
	else
		is->qual.level = (rssi + 5) / 10;

	rssi += RSSI_OFFSET_FROM_10THS_DBM;
	if (rssi < 0 || rssi >= RSSI_OFFSET_FROM_10THS_DBM)
		is->qual.qual = 0;
	else
		is->qual.qual = (rssi + 5) / 10;

	if (noise < 0)
		is->qual.noise = (noise - 5) / 10;
	else
		is->qual.noise = (noise + 5) / 10;

	is->qual.updated = IW_QUAL_ALL_UPDATED;
	is->qual.updated |= IW_QUAL_DBM;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return;
}

/*
 * MIMO power save mode change.
 */
static void qdrv_wlan_80211_smps(struct ieee80211_node *ni, int new_mode)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;

	DBGPRINTF(DBG_LL_CRIT, QDRV_LF_TRACE,
			"Node %02x:%02x:%02x:%02x:%02x:%02x MIMO PS change to %02X"
			 " for BSSID %02x:%02x:%02x:%02x:%02x:%02x\n",
			 DBGMACFMT(ni->ni_macaddr),
			(u_int8_t)new_mode,
			 DBGMACFMT(ni->ni_bssid));

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
			!(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
			&args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate SMPS message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	memset(args, 0, sizeof(*args));

	/* BSSID for the node and it's MAC address */
	memcpy(args->ni_bssid, ni->ni_bssid, IEEE80211_ADDR_LEN);
	memcpy(args->ni_macaddr, ni->ni_macaddr, IEEE80211_ADDR_LEN);

	ioctl->ioctl_command = IOCTL_DEV_SMPS;
	ioctl->ioctl_arg1 = qv->devid;
	/* new_mode is one of the enumerations starting 'IEEE80211_HTCAP_C_MIMOPWRSAVE_...' */
	ioctl->ioctl_arg2 = new_mode;
	ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

static void qdrv_qvsp_node_auth_state_change(struct ieee80211_node *ni, int auth)
{
#ifdef CONFIG_QVSP
	struct ieee80211com *ic = ni->ni_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	if (!auth) {
		qvsp_node_del(qw->qvsp, ni);
	}
#endif
}

static void qdrv_wlan_new_assoc(struct ieee80211_node *ni)
{
#ifdef CONFIG_QVSP
	struct ieee80211com *ic = ni->ni_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	qvsp_reset(qw->qvsp);
#endif
}

static void qdrv_wlan_bridge_add_node(struct qdrv_vap *qv, struct ieee80211_node *ni)
{
	qv->ni_bridge_cnt++;
	ni->ni_ext_flags |= IEEE80211_NODE_QTN_BR;
}

static void qdrv_wlan_bridge_delete_node(struct qdrv_vap *qv, struct ieee80211_node *ni)
{
	qv->ni_bridge_cnt--;
	ni->ni_ext_flags &= ~IEEE80211_NODE_QTN_BR;
	KASSERT((qv->ni_bridge_cnt >= 0), ("Negative bridge station count"));
}

static void qdrv_wlan_lncb_add_node(struct qdrv_vap *qv, struct ieee80211_node *ni)
{
	qv->ni_lncb_cnt++;
	ni->ni_ext_flags |= IEEE80211_NODE_LNCB_4ADDR_AUTH;
}

static void qdrv_wlan_lncb_delete_node(struct qdrv_vap *qv, struct ieee80211_node *ni)
{
	qv->ni_lncb_cnt--;
	ni->ni_ext_flags &= ~IEEE80211_NODE_LNCB_4ADDR_AUTH;
	KASSERT((qv->ni_lncb_cnt >= 0), ("Negative lncb station count"));
}

static void qdrv_wlan_bridge_lncb_list_update(struct qdrv_vap *qv,
		struct ieee80211_node *ni, int auth)
{
	int is_ap = 0;
	int removed_from_lncb_list = 0;

	is_ap = (ni->ni_vap->iv_opmode == IEEE80211_M_HOSTAP);

	if (auth) {
		/* Update list of Quantenna bridge clients */
		if (ni->ni_qtn_assoc_ie && !(ni->ni_ext_flags & IEEE80211_NODE_QTN_BR))
			qdrv_wlan_bridge_add_node(qv, ni);
		else if (!ni->ni_qtn_assoc_ie && (ni->ni_ext_flags & IEEE80211_NODE_QTN_BR))
			qdrv_wlan_bridge_delete_node(qv, ni);

		/* Update list of Quantenna bridge clients that support 4-addr LNCB reception */
		if ((ni->ni_ext_flags & (IEEE80211_NODE_LNCB_4ADDR |
					IEEE80211_NODE_LNCB_4ADDR_AUTH))
				== IEEE80211_NODE_LNCB_4ADDR) {
			qdrv_wlan_lncb_add_node(qv, ni);
		} else {
			if ((ni->ni_ext_flags & (IEEE80211_NODE_LNCB_4ADDR |
						IEEE80211_NODE_LNCB_4ADDR_AUTH))
					== IEEE80211_NODE_LNCB_4ADDR_AUTH) {
				qdrv_wlan_lncb_delete_node(qv, ni);
				removed_from_lncb_list = 1;
			}

			/* Don't count this STA more than once. Can happen when reauthenticating */
			if ((!(ni->ni_ext_flags & IEEE80211_NODE_IN_AUTH_STATE) && is_ap)
					|| removed_from_lncb_list) {
				qv->iv_3addr_count++;
				DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN,
					"3 address STA auth'd (count %d)\n",
					qv->iv_3addr_count);
			}

		}
	} else {
		/* Update list of Quantenna bridge clients */
		if (ni->ni_ext_flags & IEEE80211_NODE_QTN_BR)
			qdrv_wlan_bridge_delete_node(qv, ni);

		/* Update list of Quantenna bridge clients that support 4-addr LNCB reception */
		if (ni->ni_ext_flags & IEEE80211_NODE_LNCB_4ADDR_AUTH) {
			qdrv_wlan_lncb_delete_node(qv, ni);
		} else {
			if ((ni->ni_ext_flags & IEEE80211_NODE_IN_AUTH_STATE) && is_ap) {
				qv->iv_3addr_count--;
				DBGPRINTF(DBG_LL_NOTICE, QDRV_LF_WLAN,
					"3 address STA deauth'd (count %d)\n",
					qv->iv_3addr_count);
				KASSERT((qv->iv_3addr_count >= 0),
					("Negative 3 address count"));
			}
		}
	}
}

static void qdrv_wlan_auth_state_change(struct ieee80211_node *ni, int auth)
{
	unsigned long flags;
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct ieee80211com *ic = ni->ni_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	int is_ap = 0;

	if (IEEE80211_NODE_AID(ni) == 0)
		return;

	qdrv_qvsp_node_auth_state_change(ni, auth);

	is_ap = (ni->ni_vap->iv_opmode == IEEE80211_M_HOSTAP);

	spin_lock_irqsave(&qv->ni_lst_lock, flags);
	qdrv_wlan_bridge_lncb_list_update(qv, ni, auth);
	spin_unlock_irqrestore(&qv->ni_lst_lock, flags);

	if (auth) {
		qw->sm_stats.sm_nd_auth++;
		qw->sm_stats.sm_nd_auth_tot++;
		ni->ni_ext_flags |= IEEE80211_NODE_IN_AUTH_STATE;
	} else {
		if ((ni->ni_ext_flags & IEEE80211_NODE_IN_AUTH_STATE) && is_ap) {
			qw->sm_stats.sm_nd_unauth++;
			qw->sm_stats.sm_nd_auth_tot--;
		}
		if (ni->ni_node_idx)
			qdrv_remove_invalid_sub_port(ni->ni_vap, ni->ni_node_idx);
		ni->ni_ext_flags &= ~IEEE80211_NODE_IN_AUTH_STATE;
	}
}

enum hw_opt_t get_bootcfg_bond_opt(void)
{
	uint32_t bond_opt;
	char buf[QDRV_BOOTCFG_BUF_LEN];
	char *s;
	int rc;

	s = bootcfg_get_var("bond_opt", buf);
	if (s) {
		rc = sscanf(s, "=%d", &bond_opt);
		if (rc == 1)
			return (bond_opt | HW_OPTION_BONDING_TOPAZ_PROD);
	}

	return HW_OPTION_BONDING_NOT_SET;
}

static int qdrv_wlan_80211_mark_dfs(struct ieee80211com *ic)
{
	int i;
	int chan_sec_ieee;
	int sec_index;
	struct ieee80211_channel *chan;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	uint8_t *chan_list;
	dma_addr_t dma_addr;

	chan_list = qdrv_hostlink_alloc_coherent(NULL,
			sizeof(ic->ic_chan_dfs_required), &dma_addr, GFP_KERNEL);
	if (!chan_list) {
		DBGPRINTF_E("Failed to mark DFS\n");
		return -ENOMEM;
	}

	memcpy(chan_list, ic->ic_chan_dfs_required, sizeof(ic->ic_chan_dfs_required));

	/* check if channel requires DFS */
	for (i = 0; i < ic->ic_nchans; ++i) {
		chan = &ic->ic_channels[i];

		chan->ic_radardetected = 0;

		if (isset(ic->ic_chan_dfs_required, chan->ic_ieee)) {
			chan->ic_flags |= IEEE80211_CHAN_DFS;
			/* active scan not allowed on DFS channel */
			chan->ic_flags |= IEEE80211_CHAN_PASSIVE;
			chan->ic_ext_flags |= IEEE80211_CHAN_DFS_40M;
			chan->ic_ext_flags |= IEEE80211_CHAN_DFS_80M;
			continue;
		}

		chan->ic_flags &= ~IEEE80211_CHAN_DFS;
		chan->ic_flags &= ~IEEE80211_CHAN_PASSIVE;
		chan->ic_ext_flags &= ~IEEE80211_CHAN_DFS_40M;
		chan->ic_ext_flags &= ~IEEE80211_CHAN_DFS_80M;

		chan_sec_ieee = ieee80211_find_sec_chan(chan);
		if (chan_sec_ieee && isset(ic->ic_chan_dfs_required, chan_sec_ieee))
			chan->ic_ext_flags |= IEEE80211_CHAN_DFS_40M;

		for (sec_index = 0; sec_index < QTN_VHT80_SEC40_CHANS_TOTAL; sec_index++) {
			chan_sec_ieee = ieee80211_find_sec40_chan(chan, sec_index);
			if (chan_sec_ieee && isset(ic->ic_chan_dfs_required, chan_sec_ieee)) {
				chan->ic_ext_flags |= IEEE80211_CHAN_DFS_80M;
				break;
			}
		}
	}

	qdrv_hostlink_radar_set_param(qw, RADAR_PARAM_CHAN_DFS, dma_addr);
	qdrv_hostlink_free_coherent(NULL, sizeof(ic->ic_chan_dfs_required), chan_list, dma_addr);

	return 0;
}

static int qdrv_wlan_80211_mark_weather_radar(struct ieee80211com *ic)
{
	int i;
	int chan_sec_ieee;
	int sec_index;
	struct ieee80211_channel *chan;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	uint8_t *chan_list;
	dma_addr_t dma_addr;

	if (!qdrv_dfs_is_eu_region())
		return 0;

	chan_list = qdrv_hostlink_alloc_coherent(NULL,
			sizeof(ic->ic_chan_weather_radar), &dma_addr, GFP_KERNEL);
	if (!chan_list) {
		DBGPRINTF_E("Failed to mark weather channels\n");
		return -ENOMEM;
	}

	memcpy(chan_list, ic->ic_chan_weather_radar, sizeof(ic->ic_chan_weather_radar));

	for (i = 0; i < ic->ic_nchans; i++) {
		chan = &ic->ic_channels[i];

		if (isset(ic->ic_chan_weather_radar, chan->ic_ieee)) {
			chan->ic_flags |= IEEE80211_CHAN_WEATHER;
			chan->ic_flags |= IEEE80211_CHAN_WEATHER_40M;
			chan->ic_flags |= IEEE80211_CHAN_WEATHER_80M;
			continue;
		}

		chan->ic_flags &= ~IEEE80211_CHAN_WEATHER;
		chan->ic_flags &= ~IEEE80211_CHAN_WEATHER_40M;
		chan->ic_flags &= ~IEEE80211_CHAN_WEATHER_80M;

		chan_sec_ieee = ieee80211_find_sec_chan(chan);
		if (chan_sec_ieee && isset(ic->ic_chan_weather_radar, chan_sec_ieee))
			chan->ic_flags |= IEEE80211_CHAN_WEATHER_40M;

		for (sec_index = 0; sec_index < QTN_VHT80_SEC40_CHANS_TOTAL; sec_index++) {
			chan_sec_ieee = ieee80211_find_sec40_chan(chan, sec_index);
			if (chan_sec_ieee && isset(ic->ic_chan_weather_radar, chan_sec_ieee)) {
				chan->ic_flags |= IEEE80211_CHAN_WEATHER_80M;
				break;
			}
		}
	}

	qdrv_hostlink_radar_set_param(qw, RADAR_PARAM_CHAN_DFS_WEATHER, dma_addr);
	qdrv_hostlink_free_coherent(NULL, sizeof(ic->ic_chan_weather_radar), chan_list, dma_addr);

	return 0;
}

#if defined(CONFIG_QTN_BSA_SUPPORT)
int ieee80211_bsa_powersave_event_send(struct ieee80211vap *vap, int pm_state,
		int pm_qos_class, const char *name)
{
	struct ieee80211_qrpe_event_data *p_data = NULL;
	struct ieee80211_qrpe_event_ps_state *pevent = NULL;
	int ret = 0;
	uint8_t *event_data = NULL;

	if (!vap)
		return -EINVAL;

	event_data = kmalloc(sizeof(*p_data) + sizeof(*pevent), GFP_KERNEL);
	if (!event_data) {
		DBGPRINTF_E("Failed to allocate buffer message\n");
		return -ENOMEM;
	}

	memset(event_data, 0, sizeof(*p_data) + sizeof(*pevent));
	p_data = (struct ieee80211_qrpe_event_data *)event_data;
	pevent = (struct ieee80211_qrpe_event_ps_state *)(p_data->event);
	pevent->pm_qos_class = pm_qos_class;
	if (name)
		strncpy(pevent->pm_name, name, PM_MAX_NAME_LEN - 1);
	pevent->pm_state = pm_state;
	ieee80211_build_qrpe_event_head(vap, p_data,
			IEEE80211_QRPE_EVENT_INTF_POWERSAVE_STATE, sizeof(*pevent));
	ret = ieee80211_send_qrpe_event(BSA_MCGRP_DRV_EVENT, (uint8_t *)p_data,
			sizeof(*pevent) + sizeof(*p_data));
	kfree(event_data);

	return ret;
}
EXPORT_SYMBOL(ieee80211_bsa_powersave_event_send);
#endif

static int qdrv_pm_notify(struct notifier_block *b, unsigned long level, void *v)
{
	int retval = NOTIFY_OK;
	static int pm_prev_level = BOARD_PM_LEVEL_NO;
	const int switch_level = BOARD_PM_LEVEL_DUTY;
	u_int16_t new_beacon_interval;
	struct qdrv_wlan *qw = container_of(b, struct qdrv_wlan, pm_notifier);
	struct ieee80211com *ic = &qw->ic;
	u_int8_t switched = 0;
	int pm_state;

	ic->ic_pm_state[QTN_PM_CURRENT_LEVEL] = level;

#ifdef CONFIG_QVSP
	qdrv_wlan_notify_qvsp_coc_state_changed(qw->qvsp, ic);
#endif
	qdrv_wlan_notify_pm_state_changed(ic, pm_prev_level);

	if ((pm_prev_level < switch_level) && (level >= switch_level)) {

#if defined(QBMPS_ENABLE)
		/* qdrv_pm_notify is registered in qos_pm framework */
		/* it could be triggered from modules besides wlan qdrv: e.g. qpm */
		/* so we need to make sure BMPS is enabled */
		/* before going into power-saving in STA mode */
		if ((ic->ic_opmode == IEEE80211_M_STA) &&
			!(ic->ic_flags_qtn & IEEE80211_QTN_BMPS))
			return retval;
#endif

#if defined(QBMPS_ENABLE)
		if (ic->ic_opmode != IEEE80211_M_STA) {
#endif
			/* BMPS power-saving is used for STA */
			/* this is only needed for CoC power-saving in non-STA mode */
			new_beacon_interval = ieee80211_pm_period_tu(ic);
			if (ic->ic_lintval != new_beacon_interval) {
				/* Configure beacon interval to power duty interval */
				ieee80211_beacon_interval_set(ic, new_beacon_interval);
			}

			ic->ic_pm_period_change.expires = jiffies +
				ic->ic_pm_state[QTN_PM_PERIOD_CHANGE_INTERVAL] * HZ;
			if (&ic->ic_pm_period_change)
				mod_timer(&ic->ic_pm_period_change, ic->ic_pm_period_change.expires);
#if defined(QBMPS_ENABLE)
		}
#endif
		retval = ((qdrv_hostlink_power_save(qw, QTN_PM_CURRENT_LEVEL, level) < 0) ?
				NOTIFY_STOP : NOTIFY_OK);

		if (retval == NOTIFY_OK) {
			switched = 1;
			pm_state = PM_PS_ON;
		} else {
#if defined(QBMPS_ENABLE)
			if (ic->ic_opmode != IEEE80211_M_STA) {
#endif
				del_timer(&ic->ic_pm_period_change);
				ieee80211_beacon_interval_set(ic, ic->ic_lintval_backup);
#if defined(QBMPS_ENABLE)
			}
#endif
		}
	} else if ((pm_prev_level >= switch_level) && (level < switch_level)) {
		if (ic->ic_lintval != ic->ic_lintval_backup) {
			/* Recovering beacon setting */
			ieee80211_beacon_interval_set(ic, ic->ic_lintval_backup);
		}

		ic->ic_pm_enabled = 1;
		retval = ((qdrv_hostlink_power_save(qw, QTN_PM_CURRENT_LEVEL, level) < 0) ?
				NOTIFY_STOP : NOTIFY_OK);
		ic->ic_pm_enabled = 0;

		if (retval == NOTIFY_OK) {
			switched = 1;
			pm_state = PM_PS_OFF;
#if defined(QBMPS_ENABLE)
			if (ic->ic_opmode != IEEE80211_M_STA) {
#endif
				if (&ic->ic_pm_period_change)
					del_timer(&ic->ic_pm_period_change);
#if defined(QBMPS_ENABLE)
			}
#endif
		}

	}

#if defined(CONFIG_QTN_BSA_SUPPORT)
	if (switched) {
		struct ieee80211vap *pri_vap = ieee80211_get_primary_vap(ic, 0);

		if (pri_vap)
			ieee80211_bsa_powersave_event_send(pri_vap,
							pm_state,
							PM_QOS_POWER_SAVE,
							BOARD_PM_GOVERNOR_WLAN);
	}
#endif

	pm_prev_level = level;

	if ((!level) || (BOARD_PM_LEVEL_IDLE == level)) {
		ic->ic_pm_reason = IEEE80211_PM_LEVEL_RESET_CHANNEL_STATE;
		ieee80211_pm_queue_work_custom(ic, BOARD_PM_WLAN_DEFAULT_TIMEOUT);
	}

	return retval;
}

static void qdrv_wlan_80211_fill_channel_data(struct ieee80211_channel *ch,
		const struct qdrv_channel_info *qtn_ch)
{
	ch->ic_ieee = qtn_ch->chnum;
	ch->cchan_40 = qtn_ch->cchan_40;
	ch->cchan_80 = qtn_ch->cchan_80;
	ch->cchan_160 = qtn_ch->cchan_160;
	ch->ic_freq = qtn_ch->freq;
	ch->ic_flags = qtn_ch->flags;
	ch->ic_ext_flags = qtn_ch->ext_flags;
	ch->ic_maxregpower = QDRV_DFLT_MAX_TXPOW;
	ch->ic_maxpower = QDRV_DFLT_MAX_TXPOW;
	ch->ic_minpower = QDRV_DFLT_MIN_TXPOW;
	ch->ic_maxpower_normal = QDRV_DFLT_MAX_TXPOW;
	ch->ic_minpower_normal = QDRV_DFLT_MIN_TXPOW;

	memset(ch->maxpwr, -1, sizeof(ch->maxpwr));
}

static int qdrv_wlan_80211_attach_channels(struct ieee80211com *ic)
{
	int i;
	unsigned chan_idx = 0;
	unsigned chnum;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	ic->ic_nchans = 0;

	for (i = 0; i < ARRAY_SIZE(qdrv_channels_init_info); ++i) {
		chnum = qdrv_channels_init_info[i].chnum;

		if ((QTN_CHAN_IS_2G(chnum) &&
			(ic->ic_rf_chipid == CHIPID_2_4_GHZ || ic->ic_rf_chipid == CHIPID_DUAL)) ||
			(QTN_CHAN_IS_5G(chnum) &&
			(ic->ic_rf_chipid == CHIPID_5_GHZ || ic->ic_rf_chipid == CHIPID_DUAL)))
			++ic->ic_nchans;
	}

	ic->ic_channels = kmalloc(ic->ic_nchans * sizeof(*ic->ic_channels), GFP_KERNEL);
	if (!ic->ic_channels)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(qdrv_channels_init_info); ++i) {
		chnum = qdrv_channels_init_info[i].chnum;

		if (!((QTN_CHAN_IS_2G(chnum) &&
			(ic->ic_rf_chipid == CHIPID_2_4_GHZ || ic->ic_rf_chipid == CHIPID_DUAL)) ||
			(QTN_CHAN_IS_5G(chnum) &&
			(ic->ic_rf_chipid == CHIPID_5_GHZ || ic->ic_rf_chipid == CHIPID_DUAL))))
			continue;

		qdrv_wlan_80211_fill_channel_data(&ic->ic_channels[chan_idx++],
			&qdrv_channels_init_info[i]);
	}

	BUG_ON(chan_idx != ic->ic_nchans);

	ic->ic_pwr_constraint = 0;

	return 0;
}

#ifdef CONFIG_QVSP

static void
qdrv_wlan_vsp_strm_state_set(struct ieee80211com *ic, uint8_t strm_state,
				const struct ieee80211_qvsp_strm_id *strm_id,
				struct ieee80211_qvsp_strm_dis_attr *attr)
{
#if !TOPAZ_QTM /* Disable STA side control for QTM-Lite */
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qvsp_c *qvsp = qw->qvsp;

	if (qvsp == NULL) {
		return;
	}

	qvsp_cmd_strm_state_set(qvsp, strm_state, strm_id, attr);
#endif
}

static void
qdrv_wlan_vsp_change_stamode(struct ieee80211com *ic, uint8_t stamode)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qvsp_c *qvsp = qw->qvsp;

	if (qvsp == NULL) {
		return;
	}

	qvsp_change_stamode(qvsp, stamode);
}

static void
qdrv_wlan_vsp_set(struct ieee80211com *ic, uint32_t index, uint32_t value)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qvsp_c *qvsp = qw->qvsp;

	if (qvsp == NULL)
		return;

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "configuring VSP %u:%u\n",
		index, value);

	qvsp_cmd_vsp_cfg_set(qvsp, index, value);
}

static int
qdrv_wlan_vsp_get(struct ieee80211com *ic, uint32_t index, uint32_t *value)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qvsp_c *qvsp = qw->qvsp;
	int ret;

	if (qvsp == NULL)
		return -EINVAL;

	ret = qvsp_cmd_vsp_cfg_get(qvsp, index, value);
	if (!ret) {
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "VSP configuration %u:%u\n",
			index, *value);
	} else {
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "reading VSP failed\n");
	}

	return ret;
}

int
qdrv_wlan_query_wds(struct ieee80211com *ic)
{
	struct ieee80211vap *vap;

	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		if (vap->iv_opmode == IEEE80211_M_WDS)
			return 1;
	}

	return 0;
}

/*
 * VSP config callback to send a configuration updates to peer stations
 */
void
qdrv_wlan_vsp_cb_cfg(void *token, uint32_t index, uint32_t value)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *)token;
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211_qvsp_act_cfg qvsp_ac;
	struct ieee80211_action_data act;
	uint8_t *oui;

	memset(&act, 0, sizeof(act));
	act.cat = IEEE80211_ACTION_CAT_VENDOR;

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "VSP: send config to stations - %d:%d\n",
		index, value);

	memset(&qvsp_ac, 0, sizeof(qvsp_ac));
	act.params = (void *)&qvsp_ac;
	oui = qvsp_ac.header.oui;
	ieee80211_oui_add_qtn(oui);
	qvsp_ac.header.type = QVSP_ACTION_VSP_CTRL;
	qvsp_ac.count = 1;
	qvsp_ac.cfg_items[0].index = index;
	qvsp_ac.cfg_items[0].value = value;

	ieee80211_iterate_nodes(&ic->ic_sta, ieee80211_node_vsp_send_action, &act, 1);

	/* Store config for sending to new stations when they associate */
	ic->vsp_cfg[index].value = value;
	ic->vsp_cfg[index].set = 1;
}

/*
 * VSP config callback to send stream state changes to peer stations
 */
void
qdrv_wlan_vsp_cb_strm_ctrl(void *token, struct ieee80211_node *ni, uint8_t strm_state,
		struct ieee80211_qvsp_strm_id *strm_id, struct ieee80211_qvsp_strm_dis_attr *attr)
{
	struct ieee80211_qvsp_act_strm_ctrl qvsp_ac;
	struct ieee80211_action_data act;
	uint8_t *oui;

	memset(&act, 0, sizeof(act));
	act.cat = IEEE80211_ACTION_CAT_VENDOR;

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP,
		"VSP: send stream state change (%u) to " DBGMACVAR "\n",
		strm_state, DBGMACFMT(ni->ni_macaddr));

	memset(&qvsp_ac, 0, sizeof(qvsp_ac));
	act.params = (void *)&qvsp_ac;
	oui = qvsp_ac.header.oui;
	ieee80211_oui_add_qtn(oui);
	qvsp_ac.header.type = QVSP_ACTION_STRM_CTRL;
	qvsp_ac.strm_state = strm_state;
	memcpy(&qvsp_ac.dis_attr, attr, sizeof(qvsp_ac.dis_attr));
	qvsp_ac.count = 1;

	qvsp_ac.strm_items[0] = *strm_id;

	ieee80211_node_vsp_send_action(&act, ni);
}

void qdrv_wlan_vsp_reset(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	if (qw->qvsp)
		qvsp_reset(qw->qvsp);
}

#if TOPAZ_QTM
static void __sram_text
qdrv_wlan_vsp_sync_node(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct qtn_vsp_stats *vsp_stats = qw->vsp_stats;
	struct ieee80211vap *vap = ni->ni_vap;
	struct qtn_per_tid_stats *stats;
	struct qtn_per_tid_stats *prev_stats;
	const uint8_t tids[] = QTN_VSP_TIDS;
	uint8_t tid_idx;
	uint8_t tid;
	const int8_t tid2statsidx[] = QTN_VSP_STATS_TID2IDX;
	int8_t stats_idx;
	uint8_t node;
	uint32_t sent_bytes;
	uint32_t sent_pkts;
	uint32_t throt_bytes;
	uint32_t throt_pkts;

	if ((vap->iv_opmode == IEEE80211_M_HOSTAP ||
				vap->iv_opmode == IEEE80211_M_WDS) &&
			ni->ni_associd == 0)
		return;

	node = IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx);

	/*
	 * Start composing an update message for QVSP server.
	 * Add node-specific info
	 */
	qvsp_stats_node_update_begin(qw->qvsp, ni);

	if (!qw->vsp_enabling) {
		for (tid_idx = 0; tid_idx < ARRAY_SIZE(tids); ++tid_idx) {
			tid = tids[tid_idx];
			stats_idx = tid2statsidx[tid];

			stats = &vsp_stats->per_node_stats[node].per_tid_stats[stats_idx];
			prev_stats = &ni->ni_prev_vsp_stats.per_tid_stats[stats_idx];

			/*
			 * There is a race condition that when lhost is reading these counters from
			 * shared memory, MuC is updating it. However, this won't hurt VSP. Because
			 * the key information here is whether throt_pkts is zero or not. We rely
			 * on this when reenabling streams. The small error in sent_pkts doesn't
			 * matter. So we don't need to use ping-pong buffer to solve this race condtion.
			 */
			throt_pkts = stats->tx_throt_pkts - prev_stats->tx_throt_pkts;
			throt_bytes = stats->tx_throt_bytes - prev_stats->tx_throt_bytes;
			sent_pkts = stats->tx_sent_pkts - prev_stats->tx_sent_pkts;
			sent_bytes = stats->tx_sent_bytes - prev_stats->tx_sent_bytes;
			qvsp_strm_tid_check_add(qw->qvsp, ni,
				IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx), tid,
				sent_pkts + throt_pkts,
				sent_bytes + throt_bytes,
				sent_pkts, sent_bytes);
		}
	}

	/*
	 * Finish composing an update message for QVSP server for the current node.
	 * Send the message to the server
	 */
	qvsp_stats_node_update_end(qw->qvsp, ni);

	memcpy(&ni->ni_prev_vsp_stats, &vsp_stats->per_node_stats[node], sizeof(ni->ni_prev_vsp_stats));
}

static void __sram_text
qdrv_wlan_vsp_sync(struct qdrv_wlan *qw)
{
	ieee80211_iterate_nodes(&qw->ic.ic_sta, qdrv_wlan_vsp_sync_node, 0, 1);

	if (qw->vsp_sync_sched_remain) {
		schedule_delayed_work(&qw->vsp_sync_work, HZ);
		qw->vsp_sync_sched_remain--;
	}
}

static void __sram_text
qdrv_wlan_vsp_sync_work(struct work_struct *work)
{
	struct delayed_work *dwork = (struct delayed_work *)work;
	struct qdrv_wlan *qw = container_of(dwork, struct qdrv_wlan, vsp_sync_work);

	qdrv_wlan_vsp_sync(qw);
}
#endif

static void __sram_text
qdrv_wlan_vsp_tasklet(unsigned long _qw)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *) _qw;
	if (likely(qvsp_is_active(qw->qvsp))) {
#if TOPAZ_QTM
		/* sched work one time less than interval number because here we already do one */
		qw->vsp_sync_sched_remain = qw->vsp_check_intvl - 1;
		qdrv_wlan_vsp_sync(qw);

		if (qw->vsp_enabling) {
			/* warming up, stats not ready yet */
			qw->vsp_enabling--;
			return;
		}
#endif

		qvsp_fat_set(qw->qvsp,
				qw->vsp_stats->fat, qw->vsp_stats->intf_ms,
				qw->ic.ic_curchan->ic_ieee);
	}
}

static void __sram_text
qdrv_wlan_vsp_irq_handler(void *_qw, void *_unused)
{
	struct qdrv_wlan *qw = _qw;

	if (likely(qvsp_is_active(qw->qvsp))) {
		tasklet_schedule(&qw->vsp_tasklet);
	}
}

void qdrv_wlan_vsp_cb_strm_ext_throttler(void *token, struct ieee80211_node *ni,
			uint8_t strm_state, const struct ieee80211_qvsp_strm_id *strm_id,
			struct ieee80211_qvsp_strm_dis_attr *attr, uint32_t throt_intvl)
{
#if TOPAZ_QTM
	uint8_t node;
	uint8_t tid;
	uint32_t value;
	uint32_t intvl;
	uint32_t quota;

	qvsp_fake_ip2nodetid((uint32_t*)(&strm_id->daddr.ipv4), &node, &tid);

	if (strm_state == QVSP_STRM_STATE_DISABLED) {
		/* default interval */
		intvl = throt_intvl;	/* ms */
	        quota = (attr->throt_rate / 8) * intvl; /* bytes */
		if (quota < QTN_AUC_THROT_QUOTA_UNIT) {
			quota = QTN_AUC_THROT_QUOTA_UNIT;
		} else if (quota > (QTN_AUC_THROT_QUOTA_MAX * QTN_AUC_THROT_QUOTA_UNIT)) {
			quota = QTN_AUC_THROT_QUOTA_MAX * QTN_AUC_THROT_QUOTA_UNIT;
		}
		intvl = quota / (attr->throt_rate / 8);
		if ((intvl < QTN_AUC_THROT_INTVL_UNIT) ||
				(intvl > QTN_AUC_THROT_INTVL_MAX * QTN_AUC_THROT_INTVL_UNIT)) {
			printk("VSP: throttling rate %u exceeds ioctl range: intvl %u quota %u\n",
				attr->throt_rate, intvl, quota);
			return;
		}
		intvl /= QTN_AUC_THROT_INTVL_UNIT;
		quota /= QTN_AUC_THROT_QUOTA_UNIT;
	} else {
		intvl = 0;
		quota = 0;
	}

	value = SM(AUC_QOS_SCH_PARAM_TID_THROT, AUC_QOS_SCH_PARAM) |
		SM(node, QTN_AUC_THROT_NODE) |
		SM(tid, QTN_AUC_THROT_TID) |
		SM(intvl, QTN_AUC_THROT_INTVL) |
		SM(quota, QTN_AUC_THROT_QUOTA);

	qdrv_wlan_80211_setparam(ni, IEEE80211_PARAM_AUC_QOS_SCH, value, NULL, 0);
#endif
}

static int
qdrv_wlan_vsp_irq_init(struct qdrv_wlan *qw, unsigned long hi_vsp_stats_phys)
{
	struct qdrv_mac *mac = qw->mac;
	struct int_handler int_handler;
	int ret;

	qw->vsp_stats = ioremap_nocache(muc_to_lhost(hi_vsp_stats_phys),
					sizeof(*qw->vsp_stats));
	if (qw->vsp_stats == NULL) {
		return -ENOMEM;
	}

	tasklet_init(&qw->vsp_tasklet, &qdrv_wlan_vsp_tasklet, (unsigned long) qw);

#if TOPAZ_QTM
	INIT_DELAYED_WORK(&qw->vsp_sync_work, qdrv_wlan_vsp_sync_work);
#endif

	int_handler.handler = &qdrv_wlan_vsp_irq_handler;
	int_handler.arg1 = qw;
	int_handler.arg2 = NULL;
	ret = qdrv_mac_set_handler(mac, RUBY_M2L_IRQ_LO_VSP, &int_handler);
	if (ret == 0) {
		qdrv_mac_enable_irq(mac, RUBY_M2L_IRQ_LO_VSP);
	} else {
		DBGPRINTF_E("Could not initialize VSP update irq handler\n");
		iounmap(qw->vsp_stats);
	}

	return ret;
}

static void
qdrv_wlan_vsp_irq_exit(struct qdrv_wlan *qw)
{
	struct qdrv_mac *mac = qw->mac;

	iounmap(qw->vsp_stats);
	qdrv_mac_disable_irq(mac, RUBY_M2L_IRQ_LO_VSP);
	qdrv_mac_clear_handler(mac, RUBY_M2L_IRQ_LO_VSP);
	tasklet_kill(&qw->vsp_tasklet);

#if TOPAZ_QTM
	cancel_delayed_work_sync(&qw->vsp_sync_work);
#endif
}

int
qdrv_wlan_vsp_ba_throt(struct ieee80211_node *ni, int32_t tid, int intv, int dur, int win_size)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	struct ieee80211_ba_throt *ba_throt;
	int start_timer = 0;

	ba_throt = &ni->ni_ba_rx[tid].ba_throt;
	if (ba_throt->throt_dur && !dur) {
		ic->ic_vsp_ba_throt_num--;
		ni->ni_vsp_ba_throt_bm &= ~BIT(tid);
	} else if (!ba_throt->throt_dur && dur) {
		if (!ic->ic_vsp_ba_throt_num) {
			start_timer = 1;
		}
		ic->ic_vsp_ba_throt_num++;
		ni->ni_vsp_ba_throt_bm |= BIT(tid);
	}
	ba_throt->throt_intv = intv;
	ba_throt->throt_dur = dur;
	ba_throt->throt_win_size = win_size;

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "set node %u tid %d ba throt: intv=%u dur=%u win_size=%u\n",
			IEEE80211_AID(ni->ni_associd), tid,
			ba_throt->throt_intv,
			ba_throt->throt_dur,
			ba_throt->throt_win_size);

	qdrv_wlan_drop_ba(ni, tid, 0, IEEE80211_REASON_UNSPECIFIED);

	if (start_timer) {
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "add vsp ba throt timer with intv %u ms\n",
				QVSP_BA_THROT_TIMER_INTV);
		qw->vsp_ba_throt.expires = jiffies + msecs_to_jiffies(QVSP_BA_THROT_TIMER_INTV);
		add_timer(&qw->vsp_ba_throt);
	}

	return 0;
}

enum qdrv_manual_ba_throt_subcmd {
	QDRV_MANUAL_BA_THROT_SUBCMD_SET_PARAM = 0,
	QDRV_MANUAL_BA_THROT_SUBCMD_APPLY_THROT = 1,
	QDRV_MANUAL_BA_THROT_SUBCMD_DUMP_BA = 2,
};

#define QDRV_MANUAL_BA_THROT_SUBCMD		0xC0000000
#define QDRV_MANUAL_BA_THROT_VALUE		0x3FFFFFFF

#define QDRV_MANUAL_BA_THROT_INTV		0x3FFF0000
#define QDRV_MANUAL_BA_THROT_DUR		0x0000FF00
#define QDRV_MANUAL_BA_THROT_WINSIZE		0x000000FF

#define QDRV_MANUAL_BA_THROT_ENABLE		0x3FFF0000
#define QDRV_MANUAL_BA_THROT_NCIDX		0x0000FF00
#define QDRV_MANUAL_BA_THROT_TID		0x000000FF

#define QDRV_MANUAL_BA_THROT_DUMP_NCIDX		0x000000FF

#define QDRV_MANUAL_BA_THROT_IDX		0x3F000000
#define QDRV_MANUAL_BA_THROT_VENDOR		0x00F00000
#define QDRV_MANUAL_BA_THROT_USE_DUR		0x000F0000
#define QDRV_MANUAL_BA_THROT_USE_WINSIZE	0x0000F000

/*
 * Manually control BA throttling instead of VSP automatic control
 */
static void qdrv_wlan_manual_ba_throt(struct qdrv_wlan *qw, struct qdrv_vap *qv, unsigned int value)
{
	uint32_t subcmd;
	struct ieee80211_node *ni = NULL;
	static uint32_t manual_ba_throt_intv;
	static uint32_t manual_ba_throt_dur;
	static uint32_t manual_ba_throt_winsize;
	uint32_t enable;
	uint32_t ncidx;
	int32_t tid;

	subcmd = MS_OP(value, QDRV_MANUAL_BA_THROT_SUBCMD);
	value = MS_OP(value, QDRV_MANUAL_BA_THROT_VALUE);
	DBGPRINTF(DBG_LL_DEBUG, QDRV_LF_VSP, "manual ba throt: subcmd=%u, value=0x%x\n", subcmd, value);

	switch (subcmd) {
	case QDRV_MANUAL_BA_THROT_SUBCMD_SET_PARAM:
		manual_ba_throt_intv = MS_OP(value, QDRV_MANUAL_BA_THROT_INTV);
		manual_ba_throt_dur = MS_OP(value, QDRV_MANUAL_BA_THROT_DUR);
		manual_ba_throt_winsize = MS_OP(value, QDRV_MANUAL_BA_THROT_WINSIZE);
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "set manual ba throt intv=%u dur=%u win_size=%u\n",
				manual_ba_throt_intv, manual_ba_throt_dur, manual_ba_throt_winsize);
		break;
	case QDRV_MANUAL_BA_THROT_SUBCMD_APPLY_THROT:
		enable = MS_OP(value, QDRV_MANUAL_BA_THROT_ENABLE);
		ncidx = MS_OP(value, QDRV_MANUAL_BA_THROT_NCIDX);
		tid = MS_OP(value, QDRV_MANUAL_BA_THROT_TID);
		ni = ieee80211_find_node_by_node_idx(&qv->iv, ncidx);
		if (!ni) {
			DBGPRINTF(DBG_LL_CRIT, QDRV_LF_VSP, "node %u not found\n", ncidx);
			break;
		}
		if (enable) {
			qdrv_wlan_vsp_ba_throt(ni, tid,	manual_ba_throt_intv, manual_ba_throt_dur,
					manual_ba_throt_winsize);
		} else {
			qdrv_wlan_vsp_ba_throt(ni, tid, 0, 0, 0);
		}
		ieee80211_free_node(ni);
		break;
	case QDRV_MANUAL_BA_THROT_SUBCMD_DUMP_BA:
		ncidx = MS_OP(value, QDRV_MANUAL_BA_THROT_DUMP_NCIDX);
		ni = ieee80211_find_node_by_node_idx(&qv->iv, ncidx);
		if (!ni) {
			DBGPRINTF(DBG_LL_CRIT, QDRV_LF_VSP, "node %u not found\n", ncidx);
			break;
		}
		qdrv_wlan_dump_ba(ni);
		ieee80211_free_node(ni);
		break;
	default:
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "unknown subcmd %u\n", subcmd);
		break;
	}
}

int qdrv_wlan_vsp_wme_throt(void *token, uint32_t ac, uint32_t enable,
		uint32_t aifsn, uint32_t ecwmin, uint32_t ecwmax, uint32_t txoplimit,
		uint32_t add_qwme_ie)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *)token;
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211vap *vap;
	struct ieee80211_wme_state *wme = &qw->ic.ic_wme;
	struct chanAccParams *acc_params;
	struct qtn_wmm_params *params;

	acc_params = &wme->wme_throt_bssChanParams;
	params = &acc_params->cap_wmeParams[ac];
	if (enable) {
		wme->wme_throt_bm |= BIT(ac);
		wme->wme_throt_add_qwme_ie = add_qwme_ie;
		memcpy(params, &wme->wme_bssChanParams.cap_wmeParams[ac], sizeof(struct qtn_wmm_params));
		params->wmm_aifsn = aifsn;
		params->wmm_logcwmin = ecwmin;
		params->wmm_logcwmax = ecwmax;
		params->wmm_txopLimit = txoplimit;
	} else {
		wme->wme_throt_bm &= ~BIT(ac);
		if (!wme->wme_throt_bm) {
			wme->wme_throt_add_qwme_ie = 0;
		}
		memset(params, 0x0, sizeof(struct qtn_wmm_params));
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "set ac %u wme throt: enable=%u aifsn=%u"
			" ecwmin=%u ecwmax=%u txoplimit=%u add_qwme_ie=%u\n",
			ac, enable, params->wmm_aifsn, params->wmm_logcwmin, params->wmm_logcwmax,
			params->wmm_txopLimit, wme->wme_throt_add_qwme_ie);

	wme->wme_wmeBssChanParams.cap_info_count++;
	/* apply it to all vap as we don't support per-vap wme params now */
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		ieee80211_wme_updateparams(vap, 0);
	}

	return 0;
}

static void qdrv_wlan_vsp_wme_throt_dump(struct qdrv_wlan *qw)
{
	struct ieee80211_wme_state *wme = &qw->ic.ic_wme;
	struct chanAccParams *acc_params;
	struct qtn_wmm_params *params;
	uint32_t ac;

	acc_params = &wme->wme_throt_bssChanParams;

	printk("VSP wme throt state: throt_bm=0x%x, add_qwme_ie=%u\n", wme->wme_throt_bm,
			wme->wme_throt_add_qwme_ie);
	printk("ac enable aifsn ecwmin ecwmax txoplimit\n");
	for (ac = 0; ac < WME_NUM_AC; ac++) {
		params = &acc_params->cap_wmeParams[ac];
		printk("%2u %6u %5u %6u %6u %9u\n",
			ac, !!(wme->wme_throt_bm & BIT(ac)),
			params->wmm_aifsn, params->wmm_logcwmin, params->wmm_logcwmax,
			params->wmm_txopLimit);
	}
}

enum qdrv_manual_wme_throt_subcmd {
	QDRV_MANUAL_WME_THROT_SUBCMD_SET_PARAM = 0,
	QDRV_MANUAL_WME_THROT_SUBCMD_APPLY_THROT = 1,
	QDRV_MANUAL_WME_THROT_SUBCMD_DUMP = 2,
};

#define QDRV_MANUAL_WME_THROT_SUBCMD	0xC0000000
#define QDRV_MANUAL_WME_THROT_VALUE	0x3FFFFFFF

#define QDRV_MANUAL_WME_THROT_AIFSN	0x3F000000
#define QDRV_MANUAL_WME_THROT_ECWMIN	0x00F00000
#define QDRV_MANUAL_WME_THROT_ECWMAX	0x000F0000
#define QDRV_MANUAL_WME_THROT_TXOPLIMIT	0x0000FFFF
#define QDRV_MANUAL_WME_THROT_ENABLE	0x3F000000
#define QDRV_MANUAL_WME_THROT_AC	0x00F00000

/*
 * Manually control WME throttling instead of VSP automatic control
 */
static void qdrv_wlan_manual_wme_throt(struct qdrv_wlan *qw, struct qdrv_vap *qv, unsigned int value)
{
	uint32_t subcmd;
	static uint32_t manual_wme_throt_aifsn;
	static uint32_t manual_wme_throt_ecwmin;
	static uint32_t manual_wme_throt_ecwmax;
	static uint32_t manual_wme_throt_txoplimit;
	uint32_t enable;
	uint32_t ac;

	subcmd = MS_OP(value, QDRV_MANUAL_WME_THROT_SUBCMD);
	value = MS_OP(value, QDRV_MANUAL_WME_THROT_VALUE);
	DBGPRINTF(DBG_LL_DEBUG, QDRV_LF_VSP, "manual wme throt: subcmd=%u, value=0x%x\n", subcmd, value);

	switch (subcmd) {
	case QDRV_MANUAL_WME_THROT_SUBCMD_SET_PARAM:
		manual_wme_throt_aifsn = MS_OP(value, QDRV_MANUAL_WME_THROT_AIFSN);
		manual_wme_throt_ecwmin = MS_OP(value, QDRV_MANUAL_WME_THROT_ECWMIN);
		manual_wme_throt_ecwmax = MS_OP(value, QDRV_MANUAL_WME_THROT_ECWMAX);
		manual_wme_throt_txoplimit = MS_OP(value, QDRV_MANUAL_WME_THROT_TXOPLIMIT);
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP,
				"set manual wme throt aifsn=%u ecwmin=%u ecwmax=%u txoplimit=%u\n",
				manual_wme_throt_aifsn, manual_wme_throt_ecwmin, manual_wme_throt_ecwmax,
				manual_wme_throt_txoplimit);
		break;
	case QDRV_MANUAL_WME_THROT_SUBCMD_APPLY_THROT:
		enable = MS_OP(value, QDRV_MANUAL_WME_THROT_ENABLE);
		ac = MS_OP(value, QDRV_MANUAL_WME_THROT_AC);
		if (enable) {
			qdrv_wlan_vsp_wme_throt(qw, ac, enable,
				manual_wme_throt_aifsn, manual_wme_throt_ecwmin,
				manual_wme_throt_ecwmax, manual_wme_throt_txoplimit, 1);
		} else {
			qdrv_wlan_vsp_wme_throt(qw, ac, 0, 0, 0, 0, 0, 0);
		}
		break;
	case QDRV_MANUAL_WME_THROT_SUBCMD_DUMP:
		qdrv_wlan_vsp_wme_throt_dump(qw);
		break;
	default:
		DBGPRINTF(DBG_LL_INFO, QDRV_LF_VSP, "unknown subcmd %u\n", subcmd);
		break;
	}
}

#endif	/* CONFIG_QVSP */

extern void dfs_reentry_chan_switch_notify(struct net_device *dev, struct ieee80211_channel *new_chan);
extern struct ieee80211_channel* qdrv_radar_select_newchan(u_int8_t new_ieee);

static void qdrv_wlan_send_csa_frame(struct ieee80211vap *vap,
		u_int8_t csa_mode,
		u_int8_t csa_chan,
		u_int8_t csa_count,
		u_int64_t tsf)
{
#ifndef ARTSMNG_SUPPORT
	if (vap->iv_bss == NULL) {
		DBGPRINTF_E("CSA sending frame for NULL BSS\n");
		return;
	}
#endif
	/* allow sending CSA frames to WDS links */
	ieee80211_send_csa_frame(vap, csa_mode, csa_chan, csa_count, tsf);
}

static void qdrv_wlan_pm_state_init(struct ieee80211com *ic)
{
	const static int defaults[QTN_PM_IOCTL_MAX] = QTN_PM_PARAM_DEFAULTS;
	memcpy(ic->ic_pm_state, defaults, sizeof(ic->ic_pm_state));
}

void qdrv_wlan_coex_stats_update(struct ieee80211com *ic, uint32_t value)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	switch (value) {
		case WLAN_COEX_STATS_BW_ACTION:
			RXSTAT(qw, rx_coex_bw_action);
			break;
		case WLAN_COEX_STATS_BW_ASSOC:
			RXSTAT(qw, rx_coex_bw_assoc);
			break;
		case WLAN_COEX_STATS_BW_SCAN:
			RXSTAT(qw, rx_coex_bw_scan);
			break;
	}
}

int ieee80211_get_cca_adjusting_status(void)
{
	volatile struct shared_params *sp = qtn_mproc_sync_shared_params_get();

	return sp->cca_adjusting_flag;
}

int qdrv_wlan_80211_cfg_ht(const struct ieee80211com *ic, struct ieee80211_htcap *htcap,
			   enum ieee80211_ht_nss *ht_nss_cap, struct ieee80211_htinfo *htinfo)
{
#ifdef QDRV_FEATURE_HT
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	int has_ueqm = ic->ic_caps & IEEE80211_C_UEQM;
	enum ieee80211_ht_nss nss_cap;

	htcap->maxmsdu = IEEE80211_MSDU_SIZE_3839;
	htcap->cap |= (IEEE80211_HTCAP_C_CHWIDTH40 |
				IEEE80211_HTCAP_C_SHORTGI40 |
				IEEE80211_HTCAP_C_SHORTGI20);

	htcap->numrxstbcstr = IEEE80211_MAX_TX_STBC_SS;
	htcap->cap |= (IEEE80211_HTCAP_C_TXSTBC |
				IEEE80211_HTCAP_C_RXSTBC |
				IEEE80211_HTCAP_C_MAXAMSDUSIZE_4K);
	htcap->pwrsave = IEEE80211_HTCAP_C_MIMOPWRSAVE_NONE;

	/*
	 * Workaround for transfer across slow ethernet interfaces (100Mbps or less)
	 * Reduce advertised RX MAX AMPDU to reduce sender hold time
	 * Reduce TX aggr hold time (done in MuC)
	 */
	if (board_slow_ethernet()) {
		htcap->maxampdu = IEEE80211_HTCAP_MAXRXAMPDU_8191;
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE,
				"Slow Ethernet WAR: RXAMPDU %d bytes\n", ic->ic_htcap.maxampdu);
	} else {
		htcap->maxampdu = IEEE80211_HTCAP_MAXRXAMPDU_65535;
	}

	if (sp->lh_chip_id >= QTN_BBIC_11AC)
		htcap->mpduspacing = IEEE80211_HTCAP_MPDUSPACING_4;
	else
		htcap->mpduspacing = IEEE80211_HTCAP_MPDUSPACING_8;

	htcap->maxdatarate = 0;	/* Highest advertised rate is supported */

	IEEE80211_HTCAP_SET_TXBF_CAPABILITIES(htcap,
						(IEEE80211_HTCAP_B_NDP_RX |
						IEEE80211_HTCAP_B_NDP_TX |
						IEEE80211_HTCAP_B_EXP_NCOMP_STEER |
						IEEE80211_HTCAP_B_EXP_COMP_STEER));
	IEEE80211_HTCAP_SET_EXP_NCOMP_TXBF(htcap, IEEE80211_HTCAP_B_CAPABLE_BOTH);
	IEEE80211_HTCAP_SET_EXP_COMP_TXBF(htcap, IEEE80211_HTCAP_B_CAPABLE_BOTH);
	IEEE80211_HTCAP_SET_GROUPING(htcap, IEEE80211_HTCAP_B_GROUPING_ONE_TWO_FOUR);

	if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X2, 0)) {
		nss_cap = QTN_2X2_GLOBAL_RATE_NSS_MAX;
		IEEE80211_HTCAP_SET_NCOMP_NUM_BF(htcap, IEEE80211_HTCAP_B_ANTENNAS_TWO);
		IEEE80211_HTCAP_SET_COMP_NUM_BF(htcap, IEEE80211_HTCAP_B_ANTENNAS_TWO);
		IEEE80211_HTCAP_SET_CHAN_EST(htcap, IEEE80211_HTCAP_B_ST_STREAM_TWO);
	} else if (ieee80211_swfeat_is_supported(SWFEAT_ID_3X3, 0)) {
		nss_cap = QTN_3X3_GLOBAL_RATE_NSS_MAX;
		IEEE80211_HTCAP_SET_NCOMP_NUM_BF(htcap, IEEE80211_HTCAP_B_ANTENNAS_THREE);
		IEEE80211_HTCAP_SET_COMP_NUM_BF(htcap, IEEE80211_HTCAP_B_ANTENNAS_THREE);
		IEEE80211_HTCAP_SET_CHAN_EST(htcap, IEEE80211_HTCAP_B_ST_STREAM_THREE);
	} else {
		nss_cap = QTN_GLOBAL_RATE_NSS_MAX;
		IEEE80211_HTCAP_SET_NCOMP_NUM_BF(htcap, IEEE80211_HTCAP_B_ANTENNAS_FOUR);
		IEEE80211_HTCAP_SET_COMP_NUM_BF(htcap, IEEE80211_HTCAP_B_ANTENNAS_FOUR);
		IEEE80211_HTCAP_SET_CHAN_EST(htcap, IEEE80211_HTCAP_B_ST_STREAM_FOUR);
	}

	qdrv_wlan_80211_set_ht_mcsset(nss_cap, has_ueqm, htcap);
	qdrv_wlan_80211_set_ht_mcsparams(nss_cap, has_ueqm, htcap);

	if (ht_nss_cap)
		*ht_nss_cap = nss_cap;

	if (htinfo) {
		htinfo->sigranularity = IEEE80211_HTINFO_SIGRANULARITY_5;
		htinfo->basicmcsset[IEEE80211_HT_MCSSET_20_40_NSS1] = 0;
		htinfo->basicmcsset[IEEE80211_HT_MCSSET_20_40_NSS2] = 0;
	}
#endif

	return 0;
}
EXPORT_SYMBOL(qdrv_wlan_80211_cfg_ht);

int qdrv_wlan_80211_cfg_vht(const struct ieee80211com *ic, struct ieee80211_vhtcap *vhtcap,
			    struct ieee80211_vhtop *vhtop, enum ieee80211_vht_nss *vht_nss_cap,
			    enum ieee80211_vht_nss *vht_rx_nss_cap, int band_24g,
			    enum ieee80211_opmode opmode, uint8_t mu_enable)
{
#ifdef QDRV_FEATURE_VHT
	enum ieee80211_vht_nss rx_nss_cap;
	enum ieee80211_vht_nss nss_cap;

	/*
	 * Not yet supported:
	 *   IEEE80211_VHTCAP_C_SHORT_GI_160
	 *   IEEE80211_VHTCAP_C_VHT_TXOP_PS
	 */
	vhtcap->cap_flags = IEEE80211_VHTCAP_C_RX_LDPC |
					IEEE80211_VHTCAP_C_SHORT_GI_80 |
					IEEE80211_VHTCAP_C_TX_STBC |
					IEEE80211_VHTCAP_C_PLUS_HTC_MINUS_VHT_CAP |
					IEEE80211_VHTCAP_C_RX_ATN_PATTERN_CONSISTNCY |
					IEEE80211_VHTCAP_C_TX_ATN_PATTERN_CONSISTNCY;

	if (!band_24g && (ic->ic_flags_ext2 & IEEE80211_FEXT2_TXBF_SUPPORT_IN_5G)) {
		vhtcap->cap_flags |= IEEE80211_VHTCAP_C_SU_BEAM_FORMER_CAP |
					IEEE80211_VHTCAP_C_SU_BEAM_FORMEE_CAP;

		if (mu_enable) {
			if (opmode == IEEE80211_M_STA) {
				vhtcap->cap_flags |= IEEE80211_VHTCAP_C_MU_BEAM_FORMEE_CAP;
			} else if (opmode == IEEE80211_M_HOSTAP) {
				vhtcap->cap_flags |= IEEE80211_VHTCAP_C_MU_BEAM_FORMER_CAP;
			}
		}
	}

	vhtcap->maxmpdu = IEEE80211_VHTCAP_MAX_MPDU_11454;
	vhtcap->chanwidth = IEEE80211_VHTCAP_CW_80M_ONLY ;
	vhtcap->rxstbc = IEEE80211_VHTCAP_RX_STBC_UPTO_1;
	vhtcap->maxampduexp = (band_24g ? IEEE80211_VHTCAP_MAX_A_MPDU_65535 : IEEE80211_VHTCAP_MAX_A_MPDU_1048575); /* revisit */
	vhtcap->lnkadptcap = IEEE80211_VHTCAP_LNKADAPTCAP_BOTH;

	if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X2, 0)) {
		nss_cap = IEEE80211_VHT_NSS2;
		rx_nss_cap = IEEE80211_VHT_NSS2;
		vhtcap->bfstscap = IEEE80211_VHTCAP_RX_STS_2;
		vhtcap->numsounding = IEEE80211_VHTCAP_SNDDIM_2;
	} else if (ieee80211_swfeat_is_supported(SWFEAT_ID_2X4, 0)) {
		nss_cap = IEEE80211_VHT_NSS2;
		rx_nss_cap = IEEE80211_VHT_NSS4;
		vhtcap->bfstscap = IEEE80211_VHTCAP_RX_STS_4;
		vhtcap->numsounding = IEEE80211_VHTCAP_SNDDIM_2;
	} else if (ieee80211_swfeat_is_supported(SWFEAT_ID_3X3, 0)) {
		nss_cap = IEEE80211_VHT_NSS3;
		rx_nss_cap = IEEE80211_VHT_NSS3;
		vhtcap->bfstscap = IEEE80211_VHTCAP_RX_STS_3;
		vhtcap->numsounding = IEEE80211_VHTCAP_SNDDIM_3;
	} else if (ieee80211_swfeat_is_supported(SWFEAT_ID_4X4, 0)) {
		nss_cap = IEEE80211_VHT_NSS4;
		rx_nss_cap = IEEE80211_VHT_NSS4;
		vhtcap->bfstscap = IEEE80211_VHTCAP_RX_STS_4;
		vhtcap->numsounding = IEEE80211_VHTCAP_SNDDIM_4;
	} else {
		DBGPRINTF_E("%s: stream mode is not valid\n", __func__);
		return -1;
	}

	vhtcap->bfstscap_save = IEEE80211_VHTCAP_RX_STS_INVALID;

	qdrv_wlan_80211_set_vht_mcsset(vhtcap, nss_cap, rx_nss_cap, IEEE80211_VHT_MCS_0_9);

	vhtcap->rxlgimaxrate = 0;	/* revisit */
	vhtcap->txlgimaxrate = 0;	/* revisit */

	if (vhtop) {
		vhtop->chanwidth = (band_24g ?
					IEEE80211_VHTOP_CHAN_WIDTH_20_40MHZ :
					IEEE80211_VHTOP_CHAN_WIDTH_80MHZ);
		vhtop->centerfreq0 = 0;	/* revisit */
		vhtop->centerfreq1 = 0;	/* Not supported in current BBIC4 hardware */

		vhtop->basicvhtmcsnssset = htons(
			qdrv_wlan_80211_get_vhtmcs_map(IEEE80211_VHT_NSS1, IEEE80211_VHT_MCS_0_7));
	}

	if (vht_nss_cap)
		*vht_nss_cap = nss_cap;

	if (vht_rx_nss_cap)
		*vht_rx_nss_cap = rx_nss_cap;

#endif /* QDRV_FEATURE_VHT */
	return 0;
}
EXPORT_SYMBOL(qdrv_wlan_80211_cfg_vht);

static void qdrv_wlan_init_dm_factors(struct ieee80211com *ic)
{
	char tmpbuf[QDRV_BOOTCFG_BUF_LEN];
	char *varstart;
	int value = 0;

	ic->ic_dm_factor.flags = 0;

	varstart = bootcfg_get_var("dm_txpower_factor", tmpbuf);
	if (varstart != NULL &&
		sscanf(varstart, "=%d", &value) == 1) {
		if (value >= DM_TXPOWER_FACTOR_MIN &&
				value <= DM_TXPOWER_FACTOR_MAX) {
			ic->ic_dm_factor.flags |= DM_FLAG_TXPOWER_FACTOR_PRESENT;
			ic->ic_dm_factor.txpower_factor = value;
		}
	}

	varstart = bootcfg_get_var("dm_aci_factor", tmpbuf);
	if (varstart != NULL &&
		sscanf(varstart, "=%d", &value) == 1) {
		if (value >= DM_ACI_FACTOR_MIN &&
				value <= DM_ACI_FACTOR_MAX) {
			ic->ic_dm_factor.flags |= DM_FLAG_ACI_FACTOR_PRESENT;
			ic->ic_dm_factor.aci_factor = value;
		}
	}

	varstart = bootcfg_get_var("dm_cci_factor", tmpbuf);
	if (varstart != NULL &&
		sscanf(varstart, "=%d", &value) == 1) {
		if (value >= DM_CCI_FACTOR_MIN &&
				value <= DM_CCI_FACTOR_MAX) {
			ic->ic_dm_factor.flags |= DM_FLAG_CCI_FACTOR_PRESENT;
			ic->ic_dm_factor.cci_factor = value;
		}
	}

	varstart = bootcfg_get_var("dm_dfs_factor", tmpbuf);
	if (varstart != NULL &&
		sscanf(varstart, "=%d", &value) == 1) {
		if (value >= DM_DFS_FACTOR_MIN &&
				value <= DM_DFS_FACTOR_MAX) {
			ic->ic_dm_factor.flags |= DM_FLAG_DFS_FACTOR_PRESENT;
			ic->ic_dm_factor.dfs_factor = value;
		}
	}

	varstart = bootcfg_get_var("dm_beacon_factor", tmpbuf);
	if (varstart != NULL &&
		sscanf(varstart, "=%d", &value) == 1) {
		if (value >= DM_BEACON_FACTOR_MIN &&
				value <= DM_BEACON_FACTOR_MAX) {
			ic->ic_dm_factor.flags |= DM_FLAG_BEACON_FACTOR_PRESENT;
			ic->ic_dm_factor.beacon_factor = value;
		}
	}
}

void qdrv_ic_dump_chan_availability_status(struct ieee80211com *ic)
{
	int i;
	struct ieee80211_channel * chan = NULL;
	const char * str[] = QTN_CHAN_AVAIL_STATUS_TO_STR;

	DBGPRINTF_N("Channel   Status   Status_string\n");

	for (i = 1; i < IEEE80211_CHAN_MAX; i++) {
		chan = ieee80211_find_channel_by_ieee(ic, i);
		if (chan == NULL) {
			continue;
		}

		DBGPRINTF_N("%7d   %6d   %s\n",
				chan->ic_ieee, ic->ic_chan_availability_status[chan->ic_ieee],
				str[ic->ic_chan_availability_status[chan->ic_ieee]]);
	}
}

void qdrv_ic_report_chan_availability_status(struct ieee80211com *ic, uint8_t usable)
{
	int i;
	struct ieee80211_channel * chan = NULL;
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	if (!vap)
		return;

	for (i = 1; i < IEEE80211_CHAN_MAX; i++) {
		chan = ieee80211_find_channel_by_ieee(ic, i);
		if (!is_ieee80211_chan_valid(chan))
			continue;

		if ((ic->ic_chan_availability_status[chan->ic_ieee] == usable) &&
				(usable == IEEE80211_CHANNEL_STATUS_NOT_AVAILABLE_RADAR_DETECTED)) {
			qdrv_eventf(vap->iv_dev, "RADAR: radar detected, not available channel %d", chan->ic_ieee);
		}
	}
}

static int qdrv_get_chan_availability_status_by_chan_num(struct ieee80211com *ic, struct ieee80211_channel *chan)
{
	struct ieee80211_channel *channel = NULL;

	if (is_ieee80211_chan_valid(chan) &&
			(channel = ieee80211_find_channel_by_ieee(ic, chan->ic_ieee)))
		return ic->ic_chan_availability_status[channel->ic_ieee];

	return 0;
}

static void qdrv_mark_channel_availability_status(struct ieee80211com *ic,
				struct ieee80211_channel *chan, uint8_t usable)
{
	struct ieee80211_channel *chan_list[IEEE80211_MAX_CHANNELS_IN_BW(BW_HT80)] = { NULL };
	int bw = BW_INVALID;
	int i = 0;

	if (chan == NULL)
		return;

	if ((chan->ic_flags & IEEE80211_CHAN_RADAR) &&
			(usable != IEEE80211_CHANNEL_STATUS_NOT_AVAILABLE_RADAR_DETECTED))
		return;

	if (!(chan->ic_flags & IEEE80211_CHAN_DFS))
		return;

	bw = qdrv_wlan_80211_get_cap_bw(ic);

	if (ic->ic_opmode == IEEE80211_M_STA)
		bw = MIN(bw, ic->ic_bss_bw);

	ic->ic_get_full_bss_chan(ic, chan, chan_list, bw);

	for (i = 0; i < IEEE80211_MAX_CHANNELS_IN_BW(bw); i++) {
		if (chan_list[i]) {
			ic->ic_chan_availability_status[chan_list[i]->ic_ieee] = usable;
			if ((usable == IEEE80211_CHANNEL_STATUS_NOT_AVAILABLE_RADAR_DETECTED) &&
					(chan->ic_flags & IEEE80211_CHAN_RADAR)) {
				chan_list[i]->ic_flags |= IEEE80211_CHAN_RADAR;
				chan_list[i]->ic_flags &= ~IEEE80211_CHAN_DFS_CAC_DONE;
				chan_list[i]->ic_flags &= ~IEEE80211_CHAN_DFS_CAC_IN_PROGRESS;
			} else if (usable == IEEE80211_CHANNEL_STATUS_AVAILABLE) {
				chan_list[i]->ic_flags |= IEEE80211_CHAN_DFS_CAC_DONE;
				chan_list[i]->ic_flags &= ~IEEE80211_CHAN_DFS_CAC_IN_PROGRESS;
			} else if (usable == IEEE80211_CHANNEL_STATUS_NOT_AVAILABLE_CAC_REQUIRED) {
				chan_list[i]->ic_flags &= ~IEEE80211_CHAN_RADAR;
				chan_list[i]->ic_flags &= ~IEEE80211_CHAN_DFS_CAC_DONE;
			}
		}
	}


	if (ic->ic_report_chan_availability_status) {
		ic->ic_report_chan_availability_status(ic, usable);
	}
	return;
}

static void qdrv_mark_channel_dfs_cac_status(struct ieee80211com *ic,
		struct ieee80211_channel *chan, u_int32_t cac_flag, bool set)
{
	struct ieee80211_channel *chan_list[IEEE80211_MAX_CHANNELS_IN_BW(BW_HT80)] = { NULL };
	int bw = BW_INVALID;
	int i = 0;

	if (chan == NULL)
		return;

	bw = qdrv_wlan_80211_get_cap_bw(ic);

	ic->ic_get_full_bss_chan(ic, chan, chan_list, bw);

	for (i = 0; i < IEEE80211_MAX_CHANNELS_IN_BW(bw); i++) {
		if (chan_list[i])
			set ? (chan_list[i]->ic_flags |= cac_flag):
					(chan_list[i]->ic_flags &= ~cac_flag);
	}
}

static int qdrv_is_dfs_chans_available_dfs_reentry(struct ieee80211com *ic,
	struct ieee80211vap *vap)
{
	struct ieee80211_scan_state *ss = ic->ic_scan;

	/*
	 * Current scan is initiated from sta scan module (STA interface bgscan) which doesn't
	 * support scan_pickchan(), we return here and let ieee80211_start_scan() pick channels.
	 */
	if (ieee80211_is_repeater_sta_down(ic) && ss && ss->ss_vap &&
			ss->ss_vap->iv_opmode == IEEE80211_M_STA)
		return 1;

	ieee80211_scan_refresh_scan_module_chan_list(ic, vap);

	/** Select any DFS channel from {CAC_REQUIRED, AVAILABLE} set */
	if (ieee80211_scan_pickchannel(ic, IEEE80211_SCAN_PICK_ANY_DFS)) {
		return 1;
	}

	return -EOPNOTSUPP;
}

static inline bool qdrv_chan_available_for_icac(struct ieee80211com *ic,
	struct ieee80211_channel *chan, int cac_type)
{
	if ((cac_type & IEEE80211_ICAC) && ic->ic_dfs_is_eu_region() &&
			ieee80211_is_on_weather_channel(ic, chan))
		return false;
	else
		return true;
}

/**
 * @function : qdrv_dfs_chans_available_for_cac
 * @param    : ieee80211_channel [ch]: check if this channel is ready for CAC.
 *		if [ch] is NULL, function returns true if any one channel is ready for CAC
 * @param    : int [cac_type]: indicate CAC type.
 * @brief    : returns true if atleast one DFS channel is found for which
 *		cac not yet done
 */
static bool qdrv_dfs_chans_available_for_cac(struct ieee80211com *ic,
	struct ieee80211_channel *ch, int cac_type)
{
	int i;
	int chan = 0;
	struct ieee80211_channel *channel = NULL;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	if (!vap)
		return false;

	ieee80211_scan_refresh_scan_module_chan_list(ic, vap);

	/* Check channel ch is ready for CAC */
	if (ch) {
		if((ch->ic_flags & IEEE80211_CHAN_DFS) &&
				ieee80211_is_chan_cac_required(ch)) {
			return qdrv_chan_available_for_icac(ic, ch, cac_type);
		} else {
			return false;
		}
	}

	if (ss) {
		for (i = 0; i < ss->ss_last; i++) {
			chan = ieee80211_chan2ieee(ic, ss->ss_chans[i]);
			if (!is_channel_valid(chan)) {
				continue;
			}

			channel = ieee80211_find_channel_by_ieee(ic, chan);
			if (channel == NULL) {
				continue;
			}

			if ((channel->ic_flags & IEEE80211_CHAN_DFS)
					&& ieee80211_is_chan_cac_required(channel)) {
				return qdrv_chan_available_for_icac(ic, channel, cac_type);
			}
		}
	}
	return false;
}


static int qdrv_get_init_cac_duration(struct ieee80211com *ic)
{
	return ic->ic_max_boot_cac_duration;
}

static void qdrv_set_init_cac_duration(struct ieee80211com *ic, int val)
{
	ic->ic_max_boot_cac_duration = val;
}

static void qdrv_icac_timer_func(unsigned long arg)
{
	struct ieee80211com *ic = (struct ieee80211com *)arg;
	if (ic->ic_stop_icac_procedure) {
		ic->ic_stop_icac_procedure(ic);
	}
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "%s: Timer expired\n", __func__);
}

static void qdrv_start_icac_procedure(struct ieee80211com *ic)
{
	if (ic->ic_get_init_cac_duration) {
		if (!ic->ic_boot_cac_end_jiffy && (ic->ic_get_init_cac_duration(ic) > 0)) {
			/* Update the boot time CAC timestamp only when ICAC is actually in-progress */
			init_timer(&ic->icac_timer);
			ic->icac_timer.function = qdrv_icac_timer_func;
			ic->icac_timer.data = (unsigned long) ic;
			ic->ic_boot_cac_end_jiffy = jiffies + (ic->ic_get_init_cac_duration(ic) * HZ);
			ic->icac_timer.expires = ic->ic_boot_cac_end_jiffy;
			add_timer(&ic->icac_timer);
			DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "%s: Add init CAC timer\n", __func__);
		}
	}
}

static void qdrv_stop_icac_procedure(struct ieee80211com *ic)
{
	/* set the max_boot_cac_duration to -1 */
	if (ic->ic_set_init_cac_duration) {
		ic->ic_set_init_cac_duration(ic, -1);
	}

	/* Stop on-going ICAC timer if any */
	del_timer(&ic->icac_timer);
	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "%s: Init CAC completed\n", __func__);
}

static void qdrv_init_cac_completion_event(struct ieee80211com *ic, struct ieee80211vap *vap)
{
	struct ieee80211_channel *bestchan = NULL;

	if (ic->ic_get_init_cac_duration(ic) > 0) {
		if (ic->ic_stop_icac_procedure) {
			ic->ic_stop_icac_procedure(ic);
		}

		bestchan = ieee80211_find_channel_by_ieee(ic,
					ic->ic_ocac.ocac_cfg.ocac_chan_ieee);
		if (bestchan && ic->ic_ocac.ocac_cfg.ocac_enable && ic->ic_ocac.ocac_running == 0 &&
					ieee80211_is_on_weather_channel(ic, bestchan)) {
			bestchan = ieee80211_scan_pickchannel(ic, IEEE80211_SCAN_NO_DFS);
		} else if (ic->ic_des_chan_after_init_cac == 0) {
			bestchan = ieee80211_scan_pickchannel(ic,
					IEEE80211_SCAN_PICK_AVAILABLE_ANY_CHANNEL);
		} else {
			/*
			 * AP operation and we already have a channel;
			 * bypass the scan and startup immediately.
			 * But under repeater mode, initiate the AP scan anyway
			 */
			if ((vap->iv_opmode == IEEE80211_M_HOSTAP ||
						vap->iv_opmode == IEEE80211_M_IBSS ||
						vap->iv_opmode == IEEE80211_M_WDS ||
						vap->iv_opmode == IEEE80211_M_AHDEMO) &&
					!(ic->ic_flags_ext & IEEE80211_FEXT_REPEATER)) {
				struct ieee80211_channel *chan;

				bestchan = ieee80211_find_channel_by_ieee(ic,
						ic->ic_des_chan_after_init_cac);

				/* to update the fast-switch alternate channel */
				chan = ieee80211_scan_pickchannel(ic,
						IEEE80211_SCAN_PICK_AVAILABLE_ANY_CHANNEL);

				if (ic->ic_ignore_init_scan_icac ||
						NULL == bestchan ||
						!ic->ic_check_channel(ic, bestchan,
							IEEE80211_CHANNEL_CHECK_ALLOW_CURRENT)) {
					if (chan) {
						bestchan = chan;
					}
				}
			}
			ic->ic_des_chan_after_init_cac = 0;
		}

		if (bestchan) {
			struct ieee80211_scan_state *ss = ic->ic_scan;

			ic->ic_des_chan = bestchan;
			if (ss && ss->ss_ops) {
				struct ap_state *as = ss->ss_priv;
				struct ieee80211_scan_entry se;

				memset(&se, 0, sizeof(se));
				se.se_chan = bestchan;
				as->as_selbss = se;
				as->as_action = ss->ss_ops->scan_default;
				IEEE80211_SCHEDULE_TQUEUE(&as->as_actiontq);
				ic->ic_pm_reason = IEEE80211_PM_LEVEL_ICAC_COMPLETE_ACTION;
				ieee80211_pm_queue_work_custom(ic, BOARD_PM_WLAN_IDLE_TIMEOUT);
			}
		} else {
			DBGPRINTF_N("%s: failed to select an available channel\n", __func__);
		}
	}
}

static int qdrv_ap_next_cac(struct ieee80211com *ic, struct ieee80211vap *vap,
				unsigned long cac_period,
				struct ieee80211_channel **qdrv_radar_cb_cac_chan,
				u_int32_t scan_pick_flags)
{
	/* ICAC is dependent on the scan module and availability of channels to perform initial CAC */
	if ((ic->ic_scan->ss_ops == NULL)
			|| (ic->ic_scan->ss_last == 0)
			|| (ic->ic_get_init_cac_duration(ic) <= 0))
		return -1;

	if ((!ieee80211_scan_pickchannel(ic, scan_pick_flags))
			|| (!(ic->ic_boot_cac_end_jiffy
			&& time_before(jiffies + cac_period, ic->ic_boot_cac_end_jiffy)))) {
		if (qdrv_radar_cb_cac_chan) {
			*qdrv_radar_cb_cac_chan = NULL;
		}
		qdrv_init_cac_completion_event(ic, vap);
	} else {
		if (ic->ic_scan->ss_ops->scan_end) {
			/*
			 * When max_boot_cac timer is too large value, and no channels left for CAC
			 * stop the ICAC procedure
			 */
			if (0 == ic->ic_scan->ss_ops->scan_end(ic->ic_scan, vap, NULL, scan_pick_flags)) {
				qdrv_init_cac_completion_event(ic, vap);
			}
		}
	}
	return 0;
}

static void qdrv_enable_xmit(struct ieee80211com *ic, const char *msg)
{
	sys_enable_xmit(msg);
}

static void qdrv_disable_xmit(struct ieee80211com *ic, const char *msg)
{
	sys_disable_xmit(msg);
}

static void qdrv_set_wlan_phy_param(struct ieee80211vap *vap, uint32_t value)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;

	ioctl = vnet_alloc_ioctl(qv);
	if (!ioctl) {
		DBGPRINTF_E("Failed to allocate set_pta message\n");
		return;
	}

	ioctl->ioctl_command = IOCTL_DEV_SET_WLAN_PHY_PARAM;
	ioctl->ioctl_arg1 = (value & PHY_CMD_PARAM_M) >> PHY_CMD_PARAM_S;
	ioctl->ioctl_arg2 = (value & PHY_CMD_VALUE_M);

	vnet_send_ioctl(qv, ioctl);
}

static void qdrv_get_wlan_phy_param(struct ieee80211vap *vap, uint32_t cmd, unsigned char *data)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma = 0;
	uint32_t *value_ptr = NULL;

	ioctl = vnet_alloc_ioctl(qv);
	value_ptr = qdrv_hostlink_alloc_coherent(NULL, sizeof(*value_ptr),
					&dma, GFP_DMA | GFP_ATOMIC);
	if (!ioctl || !value_ptr) {
		DBGPRINTF_E("Failed to allocate buffer message\n");
		if (ioctl)
			vnet_free_ioctl(ioctl);
		if (value_ptr)
			qdrv_hostlink_free_coherent(NULL, sizeof(*value_ptr), value_ptr, dma);
		*data = -1;
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)value_ptr);

	ioctl->ioctl_command = IOCTL_DEV_GET_WLAN_PHY_PARAM;
	ioctl->ioctl_arg1 = (cmd & 0xff00) >> PHY_CMD_PARAM_S;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl))
		*(uint32_t *)data = *value_ptr;
	else
		*data = -1;
	qdrv_hostlink_free_coherent(NULL, sizeof(*value_ptr), value_ptr, dma);
}

static void qdrv_set_pta(struct ieee80211vap *vap, uint32_t value)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;

	ioctl = vnet_alloc_ioctl(qv);
	if (!ioctl) {
		DBGPRINTF_E("Failed to allocate set_pta message\n");
		return;
	}

	ioctl->ioctl_command = IOCTL_DEV_SET_PTA_PARAM;
	ioctl->ioctl_arg1 = (value & PTA_CMD_PARAM_M) >> PTA_CMD_PARAM_S;
	ioctl->ioctl_arg2 = (value & PTA_CMD_VALUE_M);

	vnet_send_ioctl(qv, ioctl);
}

static void qdrv_get_pta(struct ieee80211vap *vap, uint32_t cmd, unsigned char *data)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma = 0;
	uint32_t *value_ptr = NULL;

	ioctl = vnet_alloc_ioctl(qv);
	value_ptr = qdrv_hostlink_alloc_coherent(NULL, sizeof(*value_ptr),
					&dma, GFP_DMA | GFP_ATOMIC);
	if (!ioctl || !value_ptr) {
		DBGPRINTF_E("Failed to allocate buffer message\n");
		if (ioctl)
			vnet_free_ioctl(ioctl);
		if (value_ptr)
			qdrv_hostlink_free_coherent(NULL, sizeof(*value_ptr), value_ptr, dma);
		*data = -1;
		return;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)value_ptr);

	ioctl->ioctl_command = IOCTL_DEV_GET_PTA_PARAM;
	ioctl->ioctl_arg1 = (cmd & 0xff00) >> 8;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl))
		*(uint32_t *)data = *value_ptr;
	else
		*data = -1;
	qdrv_hostlink_free_coherent(NULL, sizeof(*value_ptr), value_ptr, dma);
}

static int qdrv_set_sec_cca_thr(struct ieee80211vap *vap, unsigned char *data)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma = 0;
	struct ieee80211_sec_cca_param *sec_cca_param;
	struct ieee80211_sec_cca_param *sec_cca_param2 = (void *)data;
	uint32_t *value_ptr = NULL;
	int ret;

	if ((sec_cca_param2->scson_sec_thr == 0) || (sec_cca_param2->scsoff_sec_thr == 0))
		return 0;

	ioctl = vnet_alloc_ioctl(qv);
	sec_cca_param = qdrv_hostlink_alloc_coherent(NULL, sizeof(*sec_cca_param),
					&dma, GFP_DMA | GFP_ATOMIC);
	if (!ioctl || !sec_cca_param) {
		DBGPRINTF_E("Failed to allocate buffer message\n");
		if (ioctl)
			vnet_free_ioctl(ioctl);
		if (sec_cca_param)
			qdrv_hostlink_free_coherent(NULL, sizeof(*sec_cca_param), sec_cca_param, dma);
		return -1;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)value_ptr);

	sec_cca_param->scson_sec_thr = sec_cca_param2->scson_sec_thr;
	sec_cca_param->scsoff_sec_thr = sec_cca_param2->scsoff_sec_thr;

	ioctl->ioctl_command = IOCTL_DEV_SET_SECCCA_PARAM;
	ioctl->ioctl_argp = dma;

	ret = vnet_send_ioctl(qv, ioctl);
	qdrv_hostlink_free_coherent(NULL, sizeof(*sec_cca_param), sec_cca_param, dma);

	return ret;
}

static int qdrv_get_sec_cca_thr(struct ieee80211vap *vap, unsigned char *data)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	dma_addr_t dma = 0;
	struct ieee80211_sec_cca_param *sec_cca_param;
	struct ieee80211_sec_cca_param *sec_cca_param2 = (void *)data;
	uint32_t *value_ptr = NULL;
	int ret;

	ioctl = vnet_alloc_ioctl(qv);
	sec_cca_param = qdrv_hostlink_alloc_coherent(NULL, sizeof(*sec_cca_param),
					&dma, GFP_DMA | GFP_ATOMIC);
	if (!ioctl || !sec_cca_param) {
		DBGPRINTF_E("Failed to allocate buffer message\n");
		if (ioctl)
			vnet_free_ioctl(ioctl);
		if (sec_cca_param)
			qdrv_hostlink_free_coherent(NULL, sizeof(*sec_cca_param), sec_cca_param, dma);
		return -1;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN, "(1)ioctl %p dma ptr %p\n", ioctl, (void *)value_ptr);


	ioctl->ioctl_command = IOCTL_DEV_GET_SECCCA_PARAM;
	ioctl->ioctl_argp = dma;

	if (vnet_send_ioctl(qv, ioctl)) {
		sec_cca_param2->scson_sec_thr = sec_cca_param->scson_sec_thr;
		sec_cca_param2->scsoff_sec_thr = sec_cca_param->scsoff_sec_thr;
		sec_cca_param2->scson_sec40_thr = sec_cca_param->scson_sec40_thr;
		sec_cca_param2->scsoff_sec40_thr = sec_cca_param->scsoff_sec40_thr;
		ret = 0;
	} else {
		ret = -1;
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(*sec_cca_param), sec_cca_param, dma);
	return ret;
}

/*
 * Based on the discussion in BBIC4-13120, for old version APs are still used
 * on some customer side, and customer cannot support customsize the default value
 * on the specific platform STAs in that environment. To satisfy those old version AP
 * getting the correct RSSI dbm, instead, driver will use big endian for formatting
 * by default when below conditions are matched:
 * 1) platform ID is n415 or n425
 * 2) STA mode only
 */
static int is_rssi_big_endian_needed(struct ieee80211com *ic)
{
	return (ic->ic_opmode == IEEE80211_M_STA &&
			(ic->ic_ver_platform_id == MSMR_PLATFORM ||
				ic->ic_ver_platform_id == MSFT_PLATFORM));
}

static int qdrv_wlan_80211_captbl_init(struct ieee80211com *ic)
{
	int non_occupy_p;
	int cac_wea_dur;

	non_occupy_p = (ic->ic_non_occupancy_period) ? ic->ic_non_occupancy_period / HZ :
		QDRV_RADAR_DFLT_NONOCCUPY_PERIOD;
	cac_wea_dur = (ic->ic_dfs_is_eu_region()) ? CAC_WEATHER_PERIOD_EU : CAC_PERIOD;

	ic->ic_cap_ctx = (struct ieee80211_cap_ctx) {
		.scan_ctx = {
			{0, IEEE80211_CAP_SCAN_IMPACT_UNAVAIL,
				IEEE80211_CAP_MIN_SCAN_INTV},
			{0, IEEE80211_CAP_SCAN_IMPACT_TIME_SLICED,
				IEEE80211_CAP_MIN_SCAN_INTV},
			{0, IEEE80211_CAP_SCAN_IMPACT_MAX, 0},
			{0, IEEE80211_CAP_SCAN_IMPACT_MAX, 0},
		},
		.cac_ctx = {
			{IEEE80211_CAP_CAC_TYPE_CONTINUOUS,
				CAC_PERIOD / HZ,
				cac_wea_dur / HZ,
				non_occupy_p,
				non_occupy_p},
			{IEEE80211_CAP_CAC_TYPE_TIME_SLICED,
				ic->ic_ocac.ocac_cfg.ocac_params.duration_secs,
				ic->ic_ocac.ocac_cfg.ocac_params.wea_duration_secs,
				non_occupy_p,
				non_occupy_p},
			{IEEE80211_CAP_CAC_TYPE_MAX, 0, 0, 0, 0},
			{IEEE80211_CAP_CAC_TYPE_MAX, 0, 0, 0, 0},
		},
	};

	return 0;
}

static int qdrv_wlan_tag_default_vlan(struct ieee80211vap *vap, uint16_t *vlanid)
{
	struct qdrv_vap *qv;
	struct qtn_vlan_dev *vdev;

	if (!vlan_enabled)
		return 0;

	qv = container_of(vap, struct qdrv_vap, iv);
	vdev = vdev_tbl_lhost[QDRV_WLANID_FROM_DEVID(qv->devid)];

	*vlanid = vdev->pvid;

	return qtn_vlan_is_tagged_member(vdev, vdev->pvid);
}

static int qdrv_wlan_80211_init(struct ieee80211com *ic, u8 *mac_addr, u8 rf_chipid)
{
	int i;

	ic->ic_ver_sw = QDRV_BLD_VER;
	ic->ic_ver_hw = get_hardware_revision();

	ic->ic_ver_platform_id = QDRV_CFG_PLATFORM_ID;
	ic->ic_ver_timestamp = QDRV_BUILDDATE;
	/* Features advertised for QHop, even when not yet enabled */
	ic->ic_ver_flags = QTN_IE_VER_FLAG_SW_WPA3_SAE | QTN_IE_VER_FLAG_SW_WPA3_OWE |
		QTN_IE_VER_FLAG_SW_WPA3_DPP;

	/* Initialize the ieee80211com structure */
	ic->ic_rf_chipid = rf_chipid;
	ic->ic_newassoc = qdrv_wlan_80211_newassoc;
	ic->ic_disassoc = qdrv_wlan_80211_disassoc;
	ic->ic_node_update = qdrv_wlan_80211_node_update;

	/* These are called without protection from 802.11 layer */
	ic->ic_updateslot = qdrv_wlan_80211_updateslot;
	ic->ic_reset = qdrv_wlan_80211_reset;
	ic->ic_init = qdrv_wlan_80211_start;
	ic->ic_queue_reset = qdrv_wlan_80211_resetmaxqueue;

	ic->ic_send_80211 = qdrv_wlan_80211_send;
	ic->ic_get_wlanstats = qdrv_wlan_80211_stats;

	/* Hook up our code */
	ic->ic_join_bss = qdrv_wlan_80211_join_bss;
	ic->ic_beacon_update = qdrv_wlan_80211_beacon_update;
	ic->ic_beacon_stop = qdrv_wlan_80211_beacon_stop;

	ic->ic_set_l2_ext_filter = qdrv_wlan_set_l2_ext_filter;
	ic->ic_set_l2_ext_filter_port = qdrv_wlan_set_l2_ext_filter_port;
	ic->ic_get_l2_ext_filter_port = qdrv_wlan_get_l2_ext_filter_port;

	ic->ic_send_to_l2_ext_filter = qdrv_send_to_l2_ext_filter;
	ic->ic_mac_reserved = qdrv_mac_reserved;
	ic->ic_setparam = qdrv_wlan_80211_setparam;
	ic->ic_getparam = qdrv_wlan_80211_getparam;
	ic->ic_register_node = qdrv_wlan_register_node;
	ic->ic_unregister_node = qdrv_wlan_unregister_node;
	ic->ic_get_phy_stats = qdrv_wlan_80211_get_phy_stats;
	ic->ic_get_cca_stats = qdrv_wlan_80211_get_cca_stats;
	ic->ic_get_cca_trfc = qdrv_wlan_80211_get_cca_trfc;
	ic->ic_set_vap_macaddr = qdrv_wlan_80211_set_vap_macaddr;
	ic->ic_get_spdia_buf = qdrv_spdia_get_buf;

	/* Hook up our Block ack code */
	ic->ic_htaddba = qdrv_wlan_80211_process_addba;
	ic->ic_htdelba = qdrv_wlan_80211_process_delba;

	/* Hook up our Security code */
	ic->ic_setkey = qdrv_wlan_80211_setkey;
	ic->ic_delkey = qdrv_wlan_80211_delkey;

	/* Hook up the MIMO power save mode change */
	ic->ic_smps = qdrv_wlan_80211_smps;

	/* Function to authorize/deauthorize an STA */
	ic->ic_node_auth_state_change = qdrv_wlan_auth_state_change;

	/* Station has joined or rejoined a BSS */
	ic->ic_new_assoc = qdrv_wlan_new_assoc;

	ic->ic_wmm_params_update = qdrv_wlan_update_wmm_params;
	ic->ic_vap_pri_wme = 1;
	ic->ic_airfair = QTN_AUC_AIRFAIR_DFT;

	ic->ic_power_table_update = qdrv_wlan_update_chan_power_table;

	ic->ic_power_save = qdrv_wlan_80211_power_save;
	ic->ic_remain_on_channel = qdrv_remain_on_channel;

	/* Hoop up to set the TDLS parameters */
	ic->ic_set_tdls_param = qdrv_wlan_80211_tdls_set_params;
	ic->ic_get_tdls_param = qdrv_wlan_80211_tdls_get_params;

	ic->ic_peer_rts_mode = IEEE80211_PEER_RTS_DEFAULT;
	ic->ic_dyn_wmm = IEEE80211_DYN_WMM_DEFAULT;

	/* Should set real opmode here - not just a placeholder */
	ic->ic_opmode = IEEE80211_M_STA;

	ic->ic_country_code = CTRY_DEFAULT;
	ic->ic_spec_country_code = CTRY_DEFAULT;
	INIT_WORK(&ic->region_work, qdrv_update_region_work);
	ic->wlan_workqueue = create_workqueue("wlan_workqueue");

	ic->ic_beaconing_scheme = QTN_BEACONING_SCHEME_0;
	ic->ic_set_beaconing_scheme = qdrv_wlan_80211_set_bcn_scheme;

	ic->ic_country_env = IEEE80211_COUNTRY_ENV_ANY;

	ic->ic_caps = 0;
	ic->ic_caps |= IEEE80211_C_IBSS			/* ibss, nee adhoc, mode */
			| IEEE80211_C_HOSTAP		/* hostap mode */
			| IEEE80211_C_MONITOR		/* monitor mode */
			| IEEE80211_C_AHDEMO		/* adhoc demo mode */
			| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
			| IEEE80211_C_SHSLOT		/* short slot time supported */
			| IEEE80211_C_WPA		/* capable of WPA1 + WPA2 */
			| IEEE80211_C_WME		/* WMM/WME */
			| IEEE80211_C_11N
			| IEEE80211_C_TXPMGT		/* Capable of Tx Power Management */
			| IEEE80211_C_UEQM		/* Capable of unequal modulation */
			| IEEE80211_C_BGSCAN		/* Capable of background scan */
			| IEEE80211_C_UAPSD;		/* Capable of WMM power save*/

	ic->ic_mode_get_phy_stats = MUC_PHY_STATS_ALTERNATE;
	ic->ic_legacy_retry_limit = QTN_DEFAULT_LEGACY_RETRY_COUNT;
	ic->ic_mu_enable = QTN_GLOBAL_MU_INITIAL_STATE;
	ic->ic_mu_qmat_bypass_mode_enable = QTN_GLOBAL_MU_QM_BPASS_MODE_INIT_STATE;
	ic->ic_vht_mcs_cap = IEEE80211_VHT_MCS_0_9;
	/* for WFA testbed */
	ic->ic_vht_opmode_notif = IEEE80211_VHT_OPMODE_NOTIF_DEFAULT;
	ic->use_non_ht_duplicate_for_mu = 0;
	ic->rx_bws_support_for_mu_ndpa = 0;

	qdrv_wlan_80211_attach_channels(ic);
	qdrv_wlan_80211_set_11ac_mode(ic, 1);
	qdrv_wlan_80211_set_txbf_config_flags(ic);

	if (qdrv_wlan_80211_cfg_ht(ic, &ic->ic_htcap,
		&ic->ic_ht_nss_cap, &ic->ic_htinfo) != 0)
		return -1;

	if (qdrv_wlan_80211_cfg_vht(ic, &ic->ic_vhtcap, &ic->ic_vhtop,
		&ic->ic_vht_nss_cap, &ic->ic_vht_rx_nss_cap, 0,
			ic->ic_opmode, ic->ic_mu_enable) != 0)
		return -1;

	if (qdrv_wlan_80211_cfg_vht(ic, &ic->ic_vhtcap_24g, &ic->ic_vhtop_24g,
		&ic->ic_vht_nss_cap_24g, &ic->ic_vht_rx_nss_cap_24g, 1,
			ic->ic_opmode, 0) != 0)
		return -1;

	/* Assign the mac address */
	IEEE80211_ADDR_COPY(ic->ic_myaddr, mac_addr);

	/* Call MI attach routine. */
	ieee80211_ifattach(ic);
	ic->ic_node_alloc = qdrv_node_alloc;
	ic->ic_qdrv_node_free = qdrv_node_free;
	ic->ic_scan_start = qtn_scan_start;
	ic->ic_scan_end = qtn_scan_end;
	ic->ic_check_channel = qdrv_check_channel;
	ic->ic_set_channel = qdrv_set_channel;
	ic->ic_get_tsf = hal_get_tsf;
	ic->ic_set_channel_deferred = qdrv_set_channel_deferred;
	ic->ic_set_start_cca_measurement = qdrv_async_cca_read;
	ic->ic_do_measurement = qtn_do_measurement;
	ic->ic_finish_measurement = ieee80211_action_finish_measurement;
	ic->ic_send_csa_frame = qdrv_wlan_send_csa_frame;
	ic->ic_findchannel = findchannel;
	ic->ic_cca_token = CCA_TOKEN_INIT_VAL;
	ic->ic_set_coverageclass = qtn_set_coverageclass;
	ic->ic_mhz2ieee = qtn_mhz2ieee;
	ic->ic_vap_create = qtn_vap_create;
	ic->ic_vap_delete = qtn_vap_delete;
	ic->ic_get_vap_idx = qdrv_get_vap_idx;
	ic->ic_radar_is_enabled = qdrv_radar_is_enabled;
	ic->ic_radar_detected = qdrv_radar_detected;
	ic->ic_select_channel = qdrv_radar_select_newchan;
	ic->ic_dfs_action_scan_done = qdrv_dfs_action_scan_done;
	ic->ic_dfs_is_eu_region = qdrv_dfs_is_eu_region;
	ic->ic_dfs_is_us_region = qdrv_dfs_is_us_region;
	ic->ic_dfs_is_status_save_region = qdrv_dfs_is_status_save_region;
	ic->ic_dfs_is_icac_supp_region = qdrv_dfs_is_icac_supp_region;
	ic->ic_require_sta_slient = qdrv_radar_require_sta_slient;
	ic->ic_mark_dfs_channels = qdrv_wlan_80211_mark_dfs;
	ic->ic_mark_weather_radar_chans = qdrv_wlan_80211_mark_weather_radar;
	ic->ic_radar_test_mode_enabled = qdrv_radar_test_mode_enabled;
	ic->ic_use_rtscts = qdrv_use_rts_cts;
	ic->ic_sta_set_xmit = qdrv_sta_set_xmit;
	ic->ic_set_radar = qdrv_set_radar;
	ic->ic_enable_sta_dfs = qdrv_sta_dfs_enable;
	ic->ic_radar_detections_num = qdrv_radar_detections_num;
	ic->ic_run_dfs_action = qdrv_radar_run_dfs_action;
	ic->ic_complete_cac = qdrv_cac_instant_completed;
	ic->ic_get_cac_duration_jiffies = qdrv_get_cac_duration_jiffies;
	ic->ic_set_txctl = qdrv_set_txctl;
	ic->ic_robust_csa_release_frame = qdrv_robust_csa_release_frame;
	ic->ic_robust_csa_send_frame = qdrv_robust_csa_send_frame;

	ic->ic_sta_assoc_limit = QTN_ASSOC_LIMIT;
	for (i = 0; i < IEEE80211_MAX_BSS_GROUP; i++) {
		ic->ic_ssid_grp[i].limit = ic->ic_sta_assoc_limit;
		ic->ic_ssid_grp[i].reserve = 0;
		ic->ic_ssid_grp[i].assocs = 0;
	}
	ic->ic_emi_power_switch_enable = QTN_EMI_POWER_SWITCH_ENABLE;
#if defined(QBMPS_ENABLE)
	ic->ic_bmps_set_frame = qdrv_bmps_set_frame;
	ic->ic_bmps_release_frame = qdrv_bmps_release_frame;
#endif
#ifdef QSCS_ENABLED
	ic->ic_scs_update_scan_stats = qdrv_scs_update_scan_stats;
	ic->ic_sample_channel = qdrv_sample_channel;
	ic->ic_sample_channel_cancel = qdrv_sample_channel_cancel;

	ic->ic_mark_channel_availability_status = qdrv_mark_channel_availability_status;
	ic->ic_get_chan_availability_status_by_chan_num =
			qdrv_get_chan_availability_status_by_chan_num;
	ic->ic_mark_channel_dfs_cac_status = qdrv_mark_channel_dfs_cac_status;
	ic->ic_ap_next_cac = qdrv_ap_next_cac;
	ic->ic_dump_chan_availability_status = qdrv_ic_dump_chan_availability_status;
	ic->ic_report_chan_availability_status = qdrv_ic_report_chan_availability_status;
	ic->ic_dfs_chans_available_for_cac = qdrv_dfs_chans_available_for_cac;
	ic->ic_is_dfs_chans_available_for_dfs_reentry =
			qdrv_is_dfs_chans_available_dfs_reentry;
	ic->ic_get_init_cac_duration = qdrv_get_init_cac_duration;
	ic->ic_set_init_cac_duration = qdrv_set_init_cac_duration;
	ic->ic_start_icac_procedure = qdrv_start_icac_procedure;
	ic->ic_stop_icac_procedure = qdrv_stop_icac_procedure;
	ic->ic_chan_compare_equality = qdrv_chan_compare_equality;
	ic->ic_checkset_dfs_channel_availability_status =
			ieee80211_checkset_dfs_channel_availability_status;
	ic->ic_get_full_bss_chan = ieee80211_get_full_bss_chan;

	ic->ic_enable_xmit = qdrv_enable_xmit;
	ic->ic_disable_xmit = qdrv_disable_xmit;

	ic->ic_set_pta = qdrv_set_pta;
	ic->ic_get_pta = qdrv_get_pta;

	ic->ic_set_sec_cca_thr = qdrv_set_sec_cca_thr;
	ic->ic_get_sec_cca_thr = qdrv_get_sec_cca_thr;

	ic->ic_set_wlan_phy_param = qdrv_set_wlan_phy_param;
	ic->ic_get_wlan_phy_param = qdrv_get_wlan_phy_param;

	/* defaults for SCS */
	ic->ic_scs.scs_enable = 0;
	ic->ic_scs.scs_smpl_enable = 0;
	ic->ic_scs.scs_stats_on = 0;
	ic->ic_scs.scs_debug_enable = 0;
	ic->ic_scs.scs_atten_sw_enable = 0;
	ic->ic_scs.scs_sample_intv = IEEE80211_SCS_SMPL_INTV_DEFAULT;
	ic->ic_scs.scs_sample_type = QTN_OFF_CHAN_FLAG_PASSIVE_SLOW;
	ic->ic_scs.scs_smpl_dwell_time = IEEE80211_SCS_SMPL_DWELL_TIME_DEFAULT;
	ic->ic_scs.scs_thrshld_smpl_pktnum = IEEE80211_SCS_THRSHLD_SMPL_PKTNUM_DEFAULT;
	ic->ic_scs.scs_thrshld_smpl_airtime = IEEE80211_SCS_THRSHLD_SMPL_AIRTIME_DEFAULT;
	ic->ic_scs.scs_thrshld_atten_inc = IEEE80211_SCS_THRSHLD_ATTEN_INC_DFT;
	ic->ic_scs.scs_thrshld_dfs_reentry = IEEE80211_SCS_THRSHLD_DFS_REENTRY_DFT;
	ic->ic_scs.scs_thrshld_dfs_reentry_intf = IEEE80211_SCS_THRSHLD_DFS_REENTRY_INTF_DFT;
	ic->ic_scs.scs_thrshld_aging_nor = IEEE80211_SCS_THRSHLD_AGING_NOR_DFT;
	ic->ic_scs.scs_thrshld_aging_dfsreent = IEEE80211_SCS_THRSHLD_AGING_DFSREENT_DFT;
	ic->ic_scs.scs_cca_idle_thrshld = IEEE80211_CCA_IDLE_THRSHLD;
	ic->ic_scs.scs_cca_intf_lo_thrshld = IEEE80211_CCA_INTFR_LOW_THRSHLD;
	ic->ic_scs.scs_cca_intf_hi_thrshld = IEEE80211_CCA_INTFR_HIGH_THRSHLD;
	ic->ic_scs.scs_cca_intf_abs_thrshld = IEEE80211_CCA_INTFR_ABSOLUTE_THRSHLD;
	ic->ic_scs.scs_cca_intf_ratio = IEEE80211_CCA_INTFR_RATIO;
	ic->ic_scs.scs_cca_intf_dfs_margin = IEEE80211_CCA_INTFR_DFS_MARGIN;
	ic->ic_scs.scs_pmbl_err_thrshld = IEEE80211_PMBL_ERR_THRSHLD;
	ic->ic_scs.scs_cca_intf_smth_fctr[SCS_CCA_INTF_SMTH_FCTR_NOXP] =
			IEEE80211_CCA_INTF_SMTH_FCTR_NOXP_DFT;
	ic->ic_scs.scs_cca_intf_smth_fctr[SCS_CCA_INTF_SMTH_FCTR_XPED] =
			IEEE80211_CCA_INTF_SMTH_FCTR_XPED_DFT;
	ic->ic_scs.scs_rssi_smth_fctr[SCS_RSSI_SMTH_FCTR_UP] = IEEE80211_SCS_RSSI_SMTH_FCTR_UP_DFT;
	ic->ic_scs.scs_rssi_smth_fctr[SCS_RSSI_SMTH_FCTR_DOWN] = IEEE80211_SCS_RSSI_SMTH_FCTR_DOWN_DFT;
	ic->ic_scs.scs_chan_mtrc_mrgn = IEEE80211_SCS_CHAN_MTRC_MRGN_DFT;
	ic->ic_scs.scs_inband_chan_mtrc_mrgn = IEEE80211_SCS_INBAND_CHAN_MTRC_MRGN_DFT;
	ic->ic_scs.scs_leavedfs_chan_mtrc_mrgn = IEEE80211_SCS_LEAVE_DFS_CHAN_MTRC_MRGN_DFT;
	ic->ic_scs.scs_out_of_band_mrgn = IEEE80211_SCS_OUT_OF_BAND_MRGN_DFT;
	ic->ic_scs.scs_atten_adjust = IEEE80211_SCS_ATTEN_ADJUST_DFT;
	ic->ic_scs.scs_cca_sample_dur = IEEE80211_CCA_SAMPLE_DUR;
	ic->ic_scs.scs_last_smpl_chan = -1;
	ic->ic_scs.scs_brcm_rxglitch_thrshlds_scale = IEEE80211_SCS_BRCM_RXGLITCH_THRSHLD_SCALE_DFT;
	ic->ic_scs.scs_pmbl_err_smth_fctr = IEEE80211_SCS_PMBL_ERR_SMTH_FCTR_DFT;
	ic->ic_scs.scs_pmbl_err_smth_winsize = IEEE80211_SCS_PMBL_ERR_SMTH_WINSIZE_DFT;
	ic->ic_scs.scs_pmbl_err_range = IEEE80211_SCS_PMBL_ERR_RANGE_DFT;
	ic->ic_scs.scs_pmbl_err_mapped_intf_range = IEEE80211_SCS_PMBL_ERR_MAPPED_INTF_RANGE_DFT;
	ic->ic_scs.scs_sp_wf = IEEE80211_SCS_PMBL_SHORT_WF_DFT;
	ic->ic_scs.scs_lp_wf = IEEE80211_SCS_PMBL_LONG_WF_DFT;
	ic->ic_scs.scs_thrshld_loaded = IEEE80211_SCS_THRSHLD_LOADED_DFT;
	ic->ic_scs.scs_pmp_rpt_cca_smth_fctr = IEEE80211_SCS_PMP_RPT_CCA_SMTH_FCTR_DFT;
	ic->ic_scs.scs_pmp_rx_time_smth_fctr = IEEE80211_SCS_PMP_RX_TIME_SMTH_FCTR_DFT;
	ic->ic_scs.scs_pmp_tx_time_smth_fctr = IEEE80211_SCS_PMP_TX_TIME_SMTH_FCTR_DFT;
	ic->ic_scs.scs_pmp_stats_stable_percent = IEEE80211_SCS_PMP_STATS_STABLE_PERCENT_DFT;
	ic->ic_scs.scs_pmp_stats_stable_range = IEEE80211_SCS_PMP_STATS_STABLE_RANGE_DFT;
	ic->ic_scs.scs_pmp_stats_clear_interval = IEEE80211_SCS_PMP_STATS_CLEAR_INTERVAL_DFT;
	ic->ic_scs.scs_as_rx_time_smth_fctr = IEEE80211_SCS_AS_RX_TIME_SMTH_FCTR_DFT;
	ic->ic_scs.scs_as_tx_time_smth_fctr = IEEE80211_SCS_AS_TX_TIME_SMTH_FCTR_DFT;
	ic->ic_scs.scs_cca_idle_smth_fctr = IEEE80211_SCS_CCA_IDLE_SMTH_FCTR_DFT;
	spin_lock_init(&ic->ic_scs.scs_tdls_lock);
	atomic_set(&ic->ic_scs.scs_pause_cnt, 0);
	ic->ic_scs.scs_burst_enable = IEEE80211_SCS_BURST_ENABLE_DEFAULT;
	ic->ic_scs.scs_burst_window = IEEE80211_SCS_BURST_WINDOW_DEFAULT * 60;
	ic->ic_scs.scs_burst_thresh = IEEE80211_SCS_BURST_THRESH_DEFAULT;
	ic->ic_scs.scs_burst_pause_time = IEEE80211_SCS_BURST_PAUSE_DEFAULT * 60;
	ic->ic_scs.scs_burst_force_switch = IEEE80211_SCS_BURST_SWITCH_DEFAULT;
	ic->ic_scs.scs_burst_is_paused = 0;
#if defined(CONFIG_QTN_BSA_SUPPORT)
	ic->ic_scs.scs_qrpe_report_intvl = IEEE80211_SCS_QRPE_REPORT_INTVL_DFT;
#endif
	ic->ic_scs_override_mode = qdrv_scs_override_mode;
#endif /* QSCS_ENABLED */

	ic->ic_ocac.ocac_cfg.ocac_enable = 0;
	ic->ic_ocac.ocac_cfg.ocac_chan_ieee = 0;
	ic->ic_ocac.ocac_cfg.ocac_debug_level = 0;
	ic->ic_ocac.ocac_cfg.ocac_report_only = 0;
	strncpy(ic->ic_ocac.ocac_cfg.ocac_region, CTRY_STR_DFLT,
			sizeof(ic->ic_ocac.ocac_cfg.ocac_region));
	ic->ic_ocac.ocac_cfg.ocac_timer_expire_init = IEEE80211_OCAC_TIMER_EXPIRE_INIT_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.timer_interval = IEEE80211_OCAC_TIMER_INTERVAL_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.secure_dwell_ms = IEEE80211_OCAC_SECURE_DWELL_TIME_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.dwell_time_ms = IEEE80211_OCAC_DWELL_TIME_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.duration_secs = IEEE80211_OCAC_DURATION_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.cac_time_secs = IEEE80211_OCAC_CAC_TIME_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.wea_dwell_time_ms = IEEE80211_OCAC_WEA_DWELL_TIME_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.wea_duration_secs = IEEE80211_OCAC_WEA_DURATION_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.wea_cac_time_secs = IEEE80211_OCAC_WEA_CAC_TIME_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.thresh_fat = IEEE80211_OCAC_THRESHOLD_FAT_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.thresh_traffic = IEEE80211_OCAC_THRESHOLD_TRAFFIC_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.thresh_cca_intf = IEEE80211_OCAC_THRESHOLD_CCA_INTF_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.thresh_fat_dec = IEEE80211_OCAC_THRESHOLD_FAT_DEC_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.traffic_ctrl = IEEE80211_OCAC_TRAFFIC_CTRL_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.offset_txhalt = IEEE80211_OCAC_OFFSET_TXHALT_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.offset_offchan = IEEE80211_OCAC_OFFSET_OFFCHAN_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.beacon_interval = IEEE80211_OCAC_BEACON_INTERVAL_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.auto_first_dfs_channel = 0;
	ic->ic_ocac.ocac_cfg.ocac_params.suspend_time = IEEE80211_OCAC_SUSPEND_TIME_DEFAULT;
	ic->ic_ocac.ocac_cfg.ocac_params.thresh_video_frames =
		IEEE80211_OCAC_THRESHOLD_VIDEO_FRAMES_DEFAULT;
	ic->ic_set_ocac = qdrv_set_ocac;
	ic->ic_ocac_release_frame = qdrv_ocac_release_frame;
	ic->ic_dump_ocac_tsf_log = qdrv_hostlink_ocac_dump_tsf_log;

	ic->ic_rxtx_phy_rate = qdrv_muc_stats_rxtx_phy_rate;
	ic->ic_rssi_get = qdrv_muc_stats_rssi_get;
	ic->ic_rssi = qdrv_muc_stats_rssi;
	ic->ic_smoothed_rssi = qdrv_muc_stats_smoothed_rssi;
	ic->ic_snr = qdrv_muc_stats_snr;
	ic->ic_hw_noise = qdrv_muc_stats_hw_noise;
	ic->ic_max_queue = qdrv_muc_stats_max_queue;
	ic->ic_mcs_to_phyrate = qdrv_muc_stats_mcs_to_phyrate;
	ic->ic_tx_failed = qdrv_muc_stats_tx_failed;
	ic->ic_chan_switch_record = qdrv_channel_switch_record;
	ic->ic_disassoc_reason_record = qdrv_disassoc_reason_record;
	ic->ic_chan_switch_reason_record = qdrv_channel_switch_reason_record;
	ic->ic_dfs_chan_switch_notify = dfs_reentry_chan_switch_notify;
	ic->ic_set_11g_erp = qdrv_wlan_set_11g_erp;
	/* shared CSA framework */
	init_completion(&ic->csa_completion);
	INIT_WORK(&ic->csa_work, ieee80211_csa_finish);
	ic->csa_work_queue = create_workqueue("csa_work_queue");
	ic->finish_csa = NULL;
	ic->ic_20_40_coex_enable  = 1;
	ic->ic_obss_scan_enable = 1;
	ic->ic_obss_scan_count = 0;
	init_timer(&ic->ic_obss_timer);
	ic->ic_obss_ie.obss_passive_dwell = IEEE80211_OBSS_PASSIVE_DWELL_DEFAULT;
	ic->ic_obss_ie.obss_active_dwell = IEEE80211_OBSS_ACTIVE_DWELL_DEFAULT;
	ic->ic_obss_ie.obss_trigger_interval = IEEE80211_OBSS_TRIGGER_INTERVAL_DEFAULT;
	ic->ic_obss_ie.obss_passive_total = IEEE80211_OBSS_PASSIVE_TOTAL_DEFAULT;
	ic->ic_obss_ie.obss_active_total = IEEE80211_OBSS_ACTIVE_TOTAL_DEFAULT;
	ic->ic_obss_ie.obss_channel_width_delay = IEEE80211_OBSS_CHANNEL_WIDTH_DELAY_DEFAULT;
	ic->ic_obss_ie.obss_activity_threshold = IEEE80211_OBSS_ACTIVITY_THRESHOLD_DEFAULT;
	ic->ic_coex_stats_update = qdrv_wlan_coex_stats_update;
	ic->ic_neighbor_count = -1;
	ic->ic_neighbor_cnt_sparse = IEEE80211_NEIGHBORHOOD_TYPE_SPARSE_DFT_THRSHLD;
	ic->ic_neighbor_cnt_dense = IEEE80211_NEIGHBORHOOD_TYPE_DENSE_DFT_THRSHLD;

#ifdef CONFIG_QVSP
	ic->ic_vsp_strm_state_set = qdrv_wlan_vsp_strm_state_set;
	ic->ic_vsp_change_stamode = qdrv_wlan_vsp_change_stamode;
	ic->ic_vsp_set = qdrv_wlan_vsp_set;
	ic->ic_vsp_get = qdrv_wlan_vsp_get;
	ic->ic_vsp_cb_strm_ctrl = qdrv_wlan_vsp_cb_strm_ctrl;
	ic->ic_vsp_cb_cfg = qdrv_wlan_vsp_cb_cfg;
	ic->ic_vsp_reset = qdrv_wlan_vsp_reset;
	ic->ic_vsp_cb_strm_ext_throttler = qdrv_wlan_vsp_cb_strm_ext_throttler;
#endif

#ifdef QTN_BG_SCAN
	ic->ic_bgscan_start = qdrv_bgscan_start;
	ic->ic_bgscan_channel = qdrv_bgscan_channel;
	ic->ic_bgscan_end = qdrv_bgscan_end;
#endif /* QTN_BG_SCAN */

	/* we don't have short range issue with Topaz */
	ic->ic_pwr_adjust_scancnt = 0;

	/* initiate data struct that record channel switch */
	memset(&ic->ic_csw_record, 0, sizeof(ic->ic_csw_record));

	memset(&ic->ic_chan_occupy_record, 0, sizeof(ic->ic_chan_occupy_record));

	ic->ic_send_notify_chan_width_action = ieee80211_send_notify_chan_width_action;
	ic->ic_send_vht_grp_id_act = ieee80211_send_vht_grp_id_mgmt_action;
	qdrv_wlan_pm_state_init(ic);

	ic->ic_get_local_txpow = qdrv_get_local_tx_power;
	ic->ic_get_local_link_margin = qdrv_get_local_link_margin;
	ic->ic_get_shared_vap_stats = qdrv_wlan_get_shared_vap_stats;
	ic->ic_reset_shared_vap_stats = qdrv_wlan_reset_shared_vap_stats;
	ic->ic_get_shared_node_stats = qdrv_wlan_get_shared_node_stats;
	ic->ic_reset_shared_node_stats = qdrv_wlan_reset_shared_node_stats;
	ic->ic_get_dscp2ac_map = qdrv_wlan_get_dscp2ac_map;
	ic->ic_set_dscp2ac_map = qdrv_wlan_set_dscp2ac_map;

	ic->ic_get_dscp2tid_map = qdrv_sch_get_dscp2tid_map;
	ic->ic_set_dscp2tid_map = qdrv_sch_set_dscp2tid_map;

	ic->ic_pco.pco_set = 0;
	ic->ic_pco.pco_pwr_constraint = 0;
	ic->ic_pco.pco_rssi_threshold = 0;
	ic->ic_pco.pco_sec_offset = 0;
	ic->ic_pco.pco_pwr_constraint_save = PWR_CONSTRAINT_SAVE_INIT;

	ic->ic_su_txbf_pkt_cnt = QTN_SU_TXBF_TX_CNT_DEF_THRSHLD;
	ic->ic_mu_txbf_pkt_cnt = QTN_MU_TXBF_TX_CNT_DEF_THRSHLD;
	ic->ic_get_cca_adjusting_status = ieee80211_get_cca_adjusting_status;

        ic->ic_flags_qtn |= QTN_NODE_11N_TXAMSDU_OFF;
	ic->ic_flags_ext |= IEEE80211_FEXT_UAPSD;

	ic->ic_extender_rssi_continue = 0;

	/*
	 * Default CSA count when Radar detected
	 * The channel closing transmition time limitaiton (260ms) requires
	 * channel must be changed ASAP while the over low CSA count, such as
	 * 1, could cause CSA frames are missed in heavy interference. Thus,
	 * set the default CSA count to 2.
	 */
	ic->ic_dfs_csa_cnt = 2;

	qdrv_wlan_init_dm_factors(ic);
	ic->ic_ics_check_margin = ICS_METRIC_CHECK_MARGIN;

	/* tx airtime callback init */
	ic->ic_tx_airtime = qdrv_muc_stats_tx_airtime;
	ic->ic_tx_accum_airtime = qdrv_muc_stats_tx_accum_airtime;
	ic->ic_tx_airtime_control = qdrv_tx_airtime_control;
	ic->ic_rx_airtime = qdrv_muc_stats_rx_airtime;
	ic->ic_rx_accum_airtime = qdrv_muc_stats_rx_accum_airtime;

	/* tx accum retries callback init */
	ic->ic_tx_accum_retries = qdrv_muc_stats_tx_accum_retries;

	/* MU group state update */
	ic->ic_mu_group_update = qdrv_mu_grp_update;

	ic->sta_dfs_info.sta_dfs_strict_mode = 0;
	ic->sta_dfs_info.sta_dfs_radar_detected_timer = 0;
	ic->sta_dfs_info.sta_dfs_radar_detected_channel = 0;
	ic->sta_dfs_info.sta_dfs_strict_msr_cac = 0;
	ic->sta_dfs_info.allow_measurement_report = 0;
	ic->sta_dfs_info.sta_dfs_tx_chan_close_time = STA_DFS_STRICT_TX_CHAN_CLOSE_TIME_DEFAULT;
#ifdef CONFIG_QHOP
	ic->rbs_mbs_dfs_info.rbs_dfs_tx_chan_close_time = RBS_DFS_TX_CHAN_CLOSE_TIME_DEFAULT;
#endif
	ic->auto_cca_enable = 0x1;
	ic->cca_fix_disable = 0;
	ic->ic_opmode_bw_switch_en = 0;
	ic->ic_ifreset = 1;

	spin_lock_init(&ic->ic_ocac.ocac_lock);
	memset(&ic->ic_ocac.ocac_rx_state, 0, sizeof(ic->ic_ocac.ocac_rx_state));

	ic->ic_update_ocac_state_ie = qdrv_update_ocac_state_ie;

	ic->rssi_dbm_endian = is_rssi_big_endian_needed(ic) ? DBM_ENDIAN_BIG : DBM_ENDIAN_AUTO;

	/* U-Repeater init */
	ic->rep_max_level_cfg = IEEE80211_REPEATER_INIT_MAX_LEVEL;
	ic->rep_max_level = ic->rep_max_level_cfg;
	ic->rep_curr_level = IEEE80211_REPEATER_FIRST_LEVEL;
	ic->rep_tweaks = QTN_REP_TWEAK_DEFAULT;

	ic->ic_rp_info.flags = QTN_RP_FLAG_TYPE | QTN_RP_FLAG_THROUGHPUT;
	ic->ic_rp_info.type = QTN_RP_TYPE_HD;
	ic->ic_rp_info.throughput = 0;

	ic->ic_tag_default_vlan = qdrv_wlan_tag_default_vlan;
#define AP_SCAN_RESULTS_MAX_BUFF_LEN (32 * 1024)
	ic->scan_buf_max_size = AP_SCAN_RESULTS_MAX_BUFF_LEN;

	INIT_WORK(&ic->ic_scs_monitor_work, ieee80211_scs_repeater_monitor);

	qdrv_wlan_80211_captbl_init(ic);

	return 0;
}

void qdrv_wlan_get_dscp2ac_map(const uint8_t vapid, uint8_t *dscp2ac)
{
	qdrv_sch_get_dscp2ac_map(vapid, dscp2ac);
	return;
}

void qdrv_wlan_set_dscp2ac_map(const uint8_t vapid, uint8_t *ip_dscp, uint8_t listlen, uint8_t ac)
{
	qdrv_sch_set_dscp2ac_map(vapid, ip_dscp, listlen, ac);
	return;
}

static int qdrv_wlan_80211_exit(struct ieee80211com *ic)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	if (ic->wlan_workqueue) {
		flush_workqueue(ic->wlan_workqueue);
		destroy_workqueue(ic->wlan_workqueue);
		ic->wlan_workqueue = NULL;
	}

	ic->finish_csa = NULL;
	del_timer_sync(&ic->ic_obss_timer);

	pm_qos_remove_notifier(PM_QOS_POWER_SAVE, &qw->pm_notifier);
	del_timer(&ic->ic_pm_period_change);

	ieee80211_ifdetach(ic);

#ifdef QTN_BG_SCAN
	qdrv_bgscan_release_frame(ic, IEEE80211_SCAN_FRAME_ALL, 1);
#endif /* QTN_BG_SCAN */
	qdrv_scs_release_frame(ic, 1);
	qdrv_ocac_release_frame(ic, 1);

	ic->ic_nchans = 0;
	kfree(ic->ic_channels);
	ic->ic_channels = NULL;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

static void qdrv_show_wlan_stats(struct seq_file *s, void *data, u32 num)
{
	struct qdrv_mac *mac = (struct qdrv_mac *) data;
	struct qdrv_wlan *qw = (struct qdrv_wlan *) mac->data;
	struct qtn_skb_recycle_list *recycle_list = qtn_get_shared_recycle_list();
	int i;

	DBGPRINTF(DBG_LL_ERR, QDRV_LF_TRACE, "-->Enter %d\n", num);

	seq_printf(s, "TX Statistics\n");
	seq_printf(s, "  tx_enqueue_mgmt           : %d\n", qw->tx_stats.tx_enqueue_mgmt);
	seq_printf(s, "  tx_enqueue_80211_data     : %d\n", qw->tx_stats.tx_enqueue_80211_data);
	seq_printf(s, "  tx_enqueue_data           : %d\n", qw->tx_stats.tx_enqueue_data);
	seq_printf(s, "  tx_muc_enqueue            : %d\n", qw->tx_stats.tx_muc_enqueue);
	seq_printf(s, "  tx_muc_enqueue_mbox       : %d\n", qw->tx_stats.tx_muc_enqueue_mbox);
	seq_printf(s, "  tx_null_data              : %d\n", qw->tx_stats.tx_null_data);
	seq_printf(s, "  tx_done_success           : %d\n", qw->tx_stats.tx_done_success);
	seq_printf(s, "  tx_done_muc_ready_err     : %d\n", qw->tx_stats.tx_done_muc_ready_err);
	seq_printf(s, "  tx_done_enable_queues     : %d\n", qw->tx_stats.tx_done_enable_queues);
	seq_printf(s, "  tx_queue_stop             : %d\n", qw->tx_stats.tx_queue_stop);
	seq_printf(s, "  tx_requeue                : %d\n", qw->tx_stats.tx_requeue);
	seq_printf(s, "  tx_requeue_err            : %d\n", qw->tx_stats.tx_requeue_err);
	seq_printf(s, "  tx_hardstart              : %d\n", qw->tx_stats.tx_hardstart);
	seq_printf(s, "  tx_complete               : %d\n", qw->tx_stats.tx_complete);
	seq_printf(s, "  tx_min_cl_cnt             : %d\n", qw->tx_stats.tx_min_cl_cnt);
	seq_printf(s, "  txdesc_data               : %d\n", qw->tx_if.txdesc_cnt[QDRV_TXDESC_DATA]);
	seq_printf(s, "  txdesc_mgmt               : %d\n", qw->tx_if.txdesc_cnt[QDRV_TXDESC_MGMT]);
	seq_printf(s, "  tx_dropped_mac_dead       : %d\n", qw->tx_stats.tx_dropped_mac_dead);

	seq_printf(s, "  tx_igmp                   : %d\n", qw->tx_stats.tx_igmp);
	seq_printf(s, "  tx_unknown                : %d\n", qw->tx_stats.tx_unknown);
	seq_printf(s, "  tx_arp_req                : %d\n", qw->tx_stats.tx_arp_req);

	seq_printf(s, "  tx_copy4_mc               : %d\n", qw->tx_stats.tx_copy4_mc);
	seq_printf(s, "  tx_copy4_igmp             : %d\n", qw->tx_stats.tx_copy4_igmp);
	seq_printf(s, "  tx_copy4_unknown          : %d\n", qw->tx_stats.tx_copy4_unknown);
	seq_printf(s, "  tx_copy4                  : %d\n", qw->tx_stats.tx_copy4);
	seq_printf(s, "  tx_copy_fail              : %d\n", qw->tx_stats.tx_copy_fail);
	seq_printf(s, "  tx_copy4_busy             : %d\n", qw->tx_stats.tx_copy4_busy);
	seq_printf(s, "  tx_copy3_mc               : %d\n", qw->tx_stats.tx_copy3_mc);
	seq_printf(s, "  tx_copy3_igmp             : %d\n", qw->tx_stats.tx_copy3_igmp);
	seq_printf(s, "  tx_copy_uc                : %d\n", qw->tx_stats.tx_copy_uc);
	seq_printf(s, "  tx_copy_mc                : %d\n", qw->tx_stats.tx_copy_mc);
	seq_printf(s, "  tx_copy_mc_to_uc          : %d\n", qw->tx_stats.tx_copy_mc_to_uc);
	seq_printf(s, "  tx_copy_ssdp              : %d\n", qw->tx_stats.tx_copy_ssdp);
	seq_printf(s, "  tx_copy3                  : %d\n", qw->tx_stats.tx_copy3);

	seq_printf(s, "  tx_drop_auth              : %d\n", qw->tx_stats.tx_drop_auth);
	seq_printf(s, "  tx_drop_aid               : %d\n", qw->tx_stats.tx_drop_aid);
	seq_printf(s, "  tx_drop_nodesc            : %d\n", qw->tx_stats.tx_drop_nodesc);
	seq_printf(s, "  tx_drop_wds               : %d\n", qw->tx_stats.tx_drop_wds);
	seq_printf(s, "  tx_drop_3addr             : %d\n", qw->tx_stats.tx_drop_3addr);
	seq_printf(s, "  tx_drop_vsp               : %d\n", qw->tx_stats.tx_drop_vsp);
	seq_printf(s, "  tx_dropped_config         : %d\n", qw->tx_stats.tx_dropped_config);
	seq_printf(s, "  tx_drop_total             : %d\n", qw->tx_stats.tx_drop_total);
	seq_printf(s, "  tx_channel                : %d\n", qw->tx_stats.tx_channel);
	seq_printf(s, "  tx_l2_ext_filter          : %d\n", qw->tx_stats.tx_l2_ext_filter);
	seq_printf(s, "  tx_drop_l2_ext_filter     : %d\n", qw->tx_stats.tx_drop_l2_ext_filter);

	seq_printf(s, "  tx_prot_arp               : %u\n", qw->tx_stats.prot_arp);
	seq_printf(s, "  tx_prot_pae               : %u\n", qw->tx_stats.prot_pae);
	seq_printf(s, "  tx_prot_ip_udp            : %u\n", qw->tx_stats.prot_ip_udp);
	seq_printf(s, "  tx_prot_ip_tcp            : %u\n", qw->tx_stats.prot_ip_tcp);
	seq_printf(s, "  tx_prot_ip_icmp           : %u\n", qw->tx_stats.prot_ip_icmp);
	seq_printf(s, "  tx_prot_ip_igmp           : %u\n", qw->tx_stats.prot_ip_igmp);
	seq_printf(s, "  tx_prot_ip_other          : %u\n", qw->tx_stats.prot_ip_other);
	seq_printf(s, "  tx_prot_ipv6              : %u\n", qw->tx_stats.prot_ipv6);
	seq_printf(s, "  tx_prot_other             : %u\n", qw->tx_stats.prot_other);

	seq_printf(s, "RX Statistics\n");
	seq_printf(s, "  rx_irq                    : %d\n", qw->rx_stats.rx_irq);
	seq_printf(s, "  rx_irq_schedule           : %d\n", qw->rx_stats.rx_irq_schedule);
	seq_printf(s, "  rx_beacon                 : %d\n", qw->rx_stats.rx_beacon);
	seq_printf(s, "  rx_non_beacon             : %d\n", qw->rx_stats.rx_non_beacon);
	seq_printf(s, "  rx_input_all              : %d\n", qw->rx_stats.rx_input_all);
	seq_printf(s, "  rx_input_node             : %d\n", qw->rx_stats.rx_input_node);
	seq_printf(s, "  rx_data_snap              : %d\n", qw->rx_stats.rx_data_snap);
	seq_printf(s, "  rx_data_tods              : %d\n", qw->rx_stats.rx_data_tods);
	seq_printf(s, "  rx_data_nods              : %d\n", qw->rx_stats.rx_data_nods);
	seq_printf(s, "  rx_data_fromds            : %d\n", qw->rx_stats.rx_data_fromds);
	seq_printf(s, "  rx_data_dstods            : %d\n", qw->rx_stats.rx_data_dstods);
	seq_printf(s, "  rx_data_no_node           : %d\n", qw->rx_stats.rx_data_no_node);
	seq_printf(s, "  rx_data_too_short         : %d\n", qw->rx_stats.rx_data_too_short);
	seq_printf(s, "  rx_poll                   : %d\n", qw->rx_stats.rx_poll);
	seq_printf(s, "  rx_poll_pending           : %d\n", qw->rx_stats.rx_poll_pending);
	seq_printf(s, "  rx_poll_empty             : %d\n", qw->rx_stats.rx_poll_empty);
	seq_printf(s, "  rx_poll_retrieving        : %d\n", qw->rx_stats.rx_poll_retrieving);
	seq_printf(s, "  rx_poll_buffer_err        : %d\n", qw->rx_stats.rx_poll_buffer_err);
	seq_printf(s, "  rx_poll_skballoc_err      : %d\n", qw->rx_stats.rx_poll_skballoc_err);
	seq_printf(s, "  rx_poll_stopped           : %d\n", qw->rx_stats.rx_poll_stopped);
	seq_printf(s, "  rx_df_numelems            : %d\n", qw->rx_stats.rx_df_numelems);
	seq_printf(s, "  rx_amsdu                  : %d\n", qw->rx_stats.rx_amsdu);
	seq_printf(s, "  rx_packets                : %d\n", qw->rx_stats.rx_packets);
	seq_printf(s, "  rx_bytes                  : %d\n", qw->rx_stats.rx_bytes);
	seq_printf(s, "  rx_poll_next              : %d\n", qw->rx_stats.rx_poll_next);
	seq_printf(s, "  rx_poll_complete          : %d\n", qw->rx_stats.rx_poll_complete);
	seq_printf(s, "  rx_poll_continue          : %d\n", qw->rx_stats.rx_poll_continue);
	seq_printf(s, "  rx_poll_vap_err           : %d\n", qw->rx_stats.rx_poll_vap_err);
	seq_printf(s, "  rx_frag                   : %d\n", qw->rx_stats.rx_frag);
	seq_printf(s, "  rx_lncb_4                 : %d\n", qw->rx_stats.rx_lncb_4);
	seq_printf(s, "  rx_blacklist              : %d\n", qw->rx_stats.rx_blacklist);
	seq_printf(s, "  rx_igmp                   : %d\n", qw->rx_stats.rx_igmp);
	seq_printf(s, "  rx_igmp_4                 : %d\n", qw->rx_stats.rx_igmp_4);
	seq_printf(s, "  rx_igmp_3_drop            : %d\n", qw->rx_stats.rx_igmp_3_drop);
	seq_printf(s, "  rx_mc_3_drop              : %d\n", qw->rx_stats.rx_mc_3_drop);
	seq_printf(s, "  rx_decap_snap_drop        : %u\n", qw->rx_stats.rx_decap_snap_drop);

	seq_printf(s, "  rx_prot_arp               : %u\n", qw->rx_stats.prot_arp);
	seq_printf(s, "  rx_prot_pae               : %u\n", qw->rx_stats.prot_pae);
	seq_printf(s, "  rx_prot_ip_udp            : %u\n", qw->rx_stats.prot_ip_udp);
	seq_printf(s, "  rx_prot_ip_tcp            : %u\n", qw->rx_stats.prot_ip_tcp);
	seq_printf(s, "  rx_prot_ip_icmp           : %u\n", qw->rx_stats.prot_ip_icmp);
	seq_printf(s, "  rx_prot_ip_igmp           : %u\n", qw->rx_stats.prot_ip_igmp);
	seq_printf(s, "  rx_prot_ip_other          : %u\n", qw->rx_stats.prot_ip_other);
	seq_printf(s, "  rx_prot_ipv6              : %u\n", qw->rx_stats.prot_ipv6);
	seq_printf(s, "  rx_prot_other             : %u\n", qw->rx_stats.prot_other);
	seq_printf(s, "  rx_rate_train_invalid     : %u\n", qw->rx_stats.rx_rate_train_invalid);
	seq_printf(s, "  rx_mac_reserved           : %u\n", qw->rx_stats.rx_mac_reserved);
	seq_printf(s, "  rx_coex_bw_action         : %u\n", qw->rx_stats.rx_coex_bw_action);
	seq_printf(s, "  rx_coex_bw_assoc          : %u\n", qw->rx_stats.rx_coex_bw_assoc);
	seq_printf(s, "  rx_coex_bw_scan           : %u\n", qw->rx_stats.rx_coex_bw_scan);

	seq_printf(s, "Recycling Statistics\n");
	seq_printf(s, "  qdrv_free_pass            : %d\n", recycle_list->stats_qdrv.free_recycle_pass);
	seq_printf(s, "  qdrv_free_fail            : %d\n", recycle_list->stats_qdrv.free_recycle_fail);
	seq_printf(s, "  qdrv_free_fail_undersize  : %d\n", recycle_list->stats_qdrv.free_recycle_fail_undersize);
	seq_printf(s, "  qdrv_alloc_recycle        : %d\n", recycle_list->stats_qdrv.alloc_recycle);
	seq_printf(s, "  qdrv_alloc_kmalloc        : %d\n", recycle_list->stats_qdrv.alloc_kmalloc);
	seq_printf(s, "  eth_free_pass             : %d\n", recycle_list->stats_eth.free_recycle_pass);
	seq_printf(s, "  eth_free_fail             : %d\n", recycle_list->stats_eth.free_recycle_fail);
	seq_printf(s, "  eth_free_fail_undersize   : %d\n", recycle_list->stats_eth.free_recycle_fail_undersize);
	seq_printf(s, "  eth_alloc_recycle         : %d\n", recycle_list->stats_eth.alloc_recycle);
	seq_printf(s, "  eth_alloc_kmalloc         : %d\n", recycle_list->stats_eth.alloc_kmalloc);
#if defined(CONFIG_RUBY_PCIE_HOST) || defined(CONFIG_RUBY_PCIE_TARGET)
	seq_printf(s, "  pcie_free_pass             : %d\n", recycle_list->stats_pcie.free_recycle_pass);
	seq_printf(s, "  pcie_free_fail             : %d\n", recycle_list->stats_pcie.free_recycle_fail);
	seq_printf(s, "  pcie_free_fail_undersize   : %d\n", recycle_list->stats_pcie.free_recycle_fail_undersize);
	seq_printf(s, "  pcie_alloc_recycle         : %d\n", recycle_list->stats_pcie.alloc_recycle);
	seq_printf(s, "  pcie_alloc_kmalloc         : %d\n", recycle_list->stats_pcie.alloc_kmalloc);
#endif
	seq_printf(s, "  kfree_free_pass           : %d\n", recycle_list->stats_kfree.free_recycle_pass);
	seq_printf(s, "  kfree_free_fail           : %d\n", recycle_list->stats_kfree.free_recycle_fail);
	seq_printf(s, "  kfree_free_fail_undersize : %d\n", recycle_list->stats_kfree.free_recycle_fail_undersize);

	seq_printf(s, "BF Statistics\n");
	for (i = 0; i < QTN_STATS_NUM_BF_SLOTS; i++) {
		seq_printf(s, "  slot %u success            : %d\n", i, qw->rx_stats.rx_bf_success[i]);
	}
	for (i = 0; i < QTN_STATS_NUM_BF_SLOTS; i++) {
		seq_printf(s, "  slot %u rejected           : %d\n", i, qw->rx_stats.rx_bf_rejected[i]);
	}

	seq_printf(s, "QCAT state: %d\n", qw->tx_stats.qcat_state);

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return;
}

int qdrv_wlan_stats(void *data)
{
	qdrv_control_set_show(qdrv_show_wlan_stats, data, 1, 1);

	return 0;
}

/*
 * If BIT(24) is set, it stands for all nodes.
 * If not, 8 LSBs stands for the node index.
 * BIT(8)-BIT(15) stands for the control masks.
 */
void qdrv_tx_airtime_control(struct ieee80211vap *vap, uint32_t value)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct qdrv_wlan *qw = qv->parent;

	qdrv_hostlink_tx_airtime_control(qw, value);
}

void qdrv_mu_grp_update(struct ieee80211com *ic, struct qtn_mu_group_update_args *args)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);
	qdrv_hostlink_mu_group_update(qw, args);
}

static void
qdrv_wlan_show_assoc_info( struct seq_file *s, void *data, u32 num )
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *) data;
	struct ieee80211com *ic = &qw->ic;

	ic->ic_iterate_nodes(&ic->ic_sta, get_node_info, (void *)s, 1);
}

#ifdef CONFIG_NAC_MONITOR
static void
qdrv_wlan_show_nac_info( struct seq_file *s, void *data, u32 num )
{
	struct shared_params *sp = qtn_mproc_sync_shared_params_get();
	struct nac_mon_info *info = sp->nac_mon_info;
	struct nac_stats_entry *entry = &info->nac_stats[0];
	int i;
	seq_printf(s, "  Mac Address      Rssi(dB)  Timestamp  Channel  Packet Type\n");
	for (i = 0; i < MAX_NAC_STA; i++, entry++) {
		if(entry->nac_valid) {
			seq_printf(s, "%s %9d %10llu %8d   %-10s\n",
				ether_sprintf(&entry->nac_txmac[0]),
				(int8_t)entry->nac_avg_rssi,
				entry->nac_timestamp,
				entry->nac_channel,
				(entry->nac_packet_type == 1)?"Control":
					((entry->nac_packet_type == 2)?"Data":"Management"));
		}
	}
}

int
qdrv_wlan_get_nac_info(void *data)
{
	qdrv_control_set_show(qdrv_wlan_show_nac_info, data, 1, 1);

	return 0;
}
#endif

int
qdrv_wlan_get_assoc_info(void *data)
{
	qdrv_control_set_show(qdrv_wlan_show_assoc_info, data, 1, 1);

	return 0;
}

int qdrv_wlan_start_vap(struct qdrv_wlan *qw, const char *name,
	uint8_t *mac_addr, int devid, int opmode, int flags)
{
	int ret;
	struct ieee80211com *ic = &qw->ic;

	switch (opmode) {
	case IEEE80211_M_HOSTAP:
		if (!(ic->ic_flags_ext & IEEE80211_FEXT_REPEATER) &&
				!ieee80211_swfeat_is_supported(SWFEAT_ID_MODE_AP, 1)) {
			return -1;
		}
		break;
	case IEEE80211_M_WDS:
		if (!ieee80211_swfeat_is_supported(SWFEAT_ID_MODE_AP, 1))
			return -1;
		break;
	case IEEE80211_M_STA:
		if (!ieee80211_swfeat_is_supported(SWFEAT_ID_MODE_STA, 1))
			return -1;
		break;
	default:
		printk("mode %u is not supported on this device\n", opmode);
		return -1;
	}

	ret = qdrv_hostlink_msg_create_vap(qw, name, mac_addr, devid, opmode, flags);
	if (ret < 0) {
		DBGPRINTF_E("Failed to send create VAP message\n");
	}

	return ret;
}

int qdrv_wlan_stop_vap(struct qdrv_mac *mac, struct net_device *vdev)
{
	struct qdrv_wlan *qw = qdrv_mac_get_wlan(mac);

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	if (qdrv_hostlink_msg_delete_vap(qw, vdev) < 0) {
		DBGPRINTF_E("Failed to delete VAP on MuC\n");
		return -1;
	}

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

#define DEFAULT_NUM_TEMP_ZONE 15
#define MAX_NUM_TEMP_ZONE 50
#define MAX_SIZE_TEMP_PROFILE_BUFF 200
#define QDRV_TEMP_CAL_PERIOD	(5 * HZ)

#define TPROFILE_IN_TEMP "/tmp/tprofile.txt"
#define TPROFILE_IN_PROC "/proc/bootcfg/tprofile.txt"
#define PDETECTOR_IN_PROC "/proc/bootcfg/pdetector.cal"

struct _temp_info *p_muc_temp_index;
static int tpf[MAX_NUM_TEMP_ZONE] = { 0xFFFFFFF };

static inline char *qdrv_txpow_cal_strip_all_white_space(char *str)
{
	char *p;
	char *s_ptr = str;
	p = str;
	do {
		if (!isspace(*p = *str)) {
			p++;
		}
	} while (*str++);

	return s_ptr;
}

static int qdrv_txpow_cal_tzone_get(char *temperature_profile)
{
	char *from;
	char *value;
	int  *d_ptr;
	int num_of_temp_zone = 0;

	from = qdrv_txpow_cal_strip_all_white_space(temperature_profile);
	d_ptr = &tpf[0];

	while (from) {
		value = strsep(&from, ",");
		*d_ptr = simple_strtoul(value, NULL, 0);
		d_ptr++;
		num_of_temp_zone++;
	}
	return num_of_temp_zone;
}

static int qdrv_txpow_cal_convert_temp_index(struct qdrv_wlan *qw, int temp)
{
	int temp_index = 0;
	int i = 0;
	int findit = 0;

	for (i = 0; i < qw->tx_power_cal_data.temp_info.num_zone; i++) {
		if (temp >= tpf[i] && temp < tpf[i + 1]) {
			temp_index = i + 1;
			findit = 1;
			break;
		}
	}

	if (findit != 1) {
		if (temp < tpf[0])
			temp_index = 0;
		else if (temp > tpf[qw->tx_power_cal_data.temp_info.num_zone - 1])
			temp_index = qw->tx_power_cal_data.temp_info.num_zone;
	}
	return temp_index;
}

static void qdrv_init_tsensor(struct qdrv_wlan *qw)
{
	struct i2c_board_info se95_info = {
		I2C_BOARD_INFO("se95", SE95_DEVICE_ADDR),
	};
	int temp;
	struct i2c_adapter *adapter;

	adapter = i2c_get_adapter(RUBY_I2C_ADAPTER_NUM);
	if (!adapter) {
		qw->se95_temp_sensor = NULL;
		printk("QDRV: I2C dapter not found\n");
		return;
	}

	qw->se95_temp_sensor = i2c_new_device(adapter, &se95_info);
	if (!qw->se95_temp_sensor) {
		i2c_put_adapter(adapter);
		DBGPRINTF_E("Failed to instantiate temperature sensor device\n");
		return;
	}

	/*
	 * i2c_new_device will return successfully even if i2c device's ->probe()
	 * callback failed, so check that temperature sensor is functional.
	 */
	if (qtn_tsensor_get_temperature(qw->se95_temp_sensor, &temp) < 0) {
		i2c_unregister_device(qw->se95_temp_sensor);
		qw->se95_temp_sensor = NULL;
		i2c_put_adapter(adapter);
		DBGPRINTF_N("QDRV: no external temperature sensor found\n");
	}
}

static void qdrv_txpow_cal_execute(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, tx_power_cal_data.bbrf_cal_work.work);
	int temp = 0;

	qtn_tsensor_get_temperature(qw->se95_temp_sensor, &temp);
	qw->tx_power_cal_data.temp_info.temp_index = qdrv_txpow_cal_convert_temp_index(qw, temp);
	qw->tx_power_cal_data.temp_info.real_temp = temp;
	if (p_muc_temp_index) {
		memcpy(p_muc_temp_index, &qw->tx_power_cal_data.temp_info, sizeof(*p_muc_temp_index));
	}

	schedule_delayed_work(&qw->tx_power_cal_data.bbrf_cal_work, QDRV_TEMP_CAL_PERIOD);
}

static void qdrv_get_internal_temp(struct work_struct *work)
{
	struct qdrv_wlan *qw = container_of(work, struct qdrv_wlan, tx_power_cal_data.bbrf_cal_work.work);
	int temp = 0;

	qw->tx_power_cal_data.temp_info.temp_index = topaz_read_internal_temp_sens(&temp);
	qw->tx_power_cal_data.temp_info.real_temp = temp;
	if (p_muc_temp_index) {
		memcpy(p_muc_temp_index, &qw->tx_power_cal_data.temp_info, sizeof(*p_muc_temp_index));
	}
	schedule_delayed_work(&qw->tx_power_cal_data.bbrf_cal_work, QDRV_TEMP_CAL_PERIOD);
}

static void qdrv_txpow_cal_init(struct qdrv_wlan *qw)
{
	int i;
	int num_temp_zone = 0;
	int default_tpf[DEFAULT_NUM_TEMP_ZONE] = {
			40, 46, 52, 57, 63, 67, 70, 74, 77, 81, 85, 89, 92, 96, 100
	};
	char temperature_profile[MAX_SIZE_TEMP_PROFILE_BUFF] = {0};
	int fd_1 = sys_open(TPROFILE_IN_TEMP, O_RDONLY, 0);
	int fd_2 = sys_open(TPROFILE_IN_PROC, O_RDONLY, 0);
	int fd_3 = sys_open(PDETECTOR_IN_PROC, O_RDONLY, 0);

	if (fd_1 < 0 && fd_2 < 0) {
		DBGPRINTF(DBG_LL_DEBUG, QDRV_LF_WLAN,
				"QDRV: using default temperature profile\n");
		num_temp_zone = DEFAULT_NUM_TEMP_ZONE;
		memcpy(tpf, default_tpf, sizeof(int) * DEFAULT_NUM_TEMP_ZONE);
	} else if (fd_1 >= 0 && fd_2 < 0) {
		sys_read(fd_1, temperature_profile, MAX_SIZE_TEMP_PROFILE_BUFF);
		sys_close(fd_1);
		num_temp_zone = qdrv_txpow_cal_tzone_get(temperature_profile);
	} else if (fd_1 < 0 && fd_2 >= 0) {
		sys_read(fd_2, temperature_profile, MAX_SIZE_TEMP_PROFILE_BUFF);
		sys_close(fd_2);
		num_temp_zone = qdrv_txpow_cal_tzone_get(temperature_profile);
	} else {
		sys_read(fd_2, temperature_profile, MAX_SIZE_TEMP_PROFILE_BUFF);
		sys_close(fd_1);
		sys_close(fd_2);
		num_temp_zone = qdrv_txpow_cal_tzone_get(temperature_profile);
	}

	for (i = 0; i < num_temp_zone; i ++) {
		tpf[i] *= 100000;
	}

	if (fd_3 < 0) {
		if (qw->se95_temp_sensor) {
			qw->tx_power_cal_data.temp_info.num_zone = num_temp_zone;
			DBGPRINTF(DBG_LL_DEBUG, QDRV_LF_WLAN,
					"QDRV: temperature sensor zone=%d, <%d, %d, %d, %d>\n",
					num_temp_zone, tpf[0], tpf[1], tpf[num_temp_zone - 2],
					tpf[num_temp_zone - 1]);

			INIT_DELAYED_WORK(&qw->tx_power_cal_data.bbrf_cal_work, qdrv_txpow_cal_execute);
			schedule_delayed_work(&qw->tx_power_cal_data.bbrf_cal_work,
					QDRV_TEMP_CAL_PERIOD);
		} else {
			DBGPRINTF_W("QDRV: failed to initialize power calibration\n");
		}
	} else {
		sys_close(fd_3);

		DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN,
				"QDRV: using %s for Tx gain calibration\n",	PDETECTOR_IN_PROC);
		/*
		 * If no external temp sensor is found then rx_stats sys_temp
		 * would be updated from internal temp sensor
		 */
		INIT_DELAYED_WORK(&qw->tx_power_cal_data.bbrf_cal_work, qdrv_get_internal_temp);
		schedule_delayed_work(&qw->tx_power_cal_data.bbrf_cal_work,
				QDRV_TEMP_CAL_PERIOD);
	}
}

static void qdrv_txpow_cal_stop(struct qdrv_wlan *qw)
{
	cancel_delayed_work_sync(&qw->tx_power_cal_data.bbrf_cal_work);
}
#if !TOPAZ_FPGA_PLATFORM
static void qdrv_wlan_enable_hr(unsigned long data)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan*)data;
	qdrv_hostlink_set_hrflags(qw, 1);
}
#endif
/* Enable the one-shot timer to enable hang detection/recovery */
static void qdrv_wlan_hr_oneshot_enable(struct qdrv_wlan *qw)
{
#if !TOPAZ_FPGA_PLATFORM
	init_timer(&qw->hr_timer);
	qw->hr_timer.function = qdrv_wlan_enable_hr;
	qw->hr_timer.data = (unsigned long)qw;
	qw->hr_timer.expires = jiffies + QDRV_WLAN_HR_DELAY_SECS * HZ;
	add_timer(&qw->hr_timer);
#endif
}

static void
qdrv_wlan_hr_oneshot_disable(struct qdrv_wlan *qw)
{
	del_timer(&qw->hr_timer);
}

static struct sk_buff *ip4_multicast_alloc_query(struct net_device *qbr_dev)
{
	struct sk_buff *skb;
	struct igmphdr *ih;
	struct ethhdr *eth;
	struct iphdr *iph;
	struct in_device *in_dev;
	__be32 srcip = 0;

	in_dev = in_dev_get(qbr_dev);
	srcip = qdrv_dev_ipaddr_get(qbr_dev);
	if (!in_dev || !srcip) {
		DBGPRINTF_LIMIT_E("could not get inet device\n");
		return NULL;
	}

	skb = netdev_alloc_skb_ip_align(qbr_dev, sizeof(*eth) + sizeof(*iph) +
						 sizeof(*ih) + 4);
	if (!skb)
		goto out;

	skb->protocol = htons(ETH_P_IP);

	skb_reset_mac_header(skb);
	eth = eth_hdr(skb);

	memcpy(eth->h_source, qbr_dev->dev_addr, 6);
	eth->h_dest[0] = 1;
	eth->h_dest[1] = 0;
	eth->h_dest[2] = 0x5e;
	eth->h_dest[3] = 0;
	eth->h_dest[4] = 0;
	eth->h_dest[5] = 1;

	eth->h_proto = htons(ETH_P_IP);
	skb_put(skb, sizeof(*eth));

	skb_set_network_header(skb, skb->len);
	iph = ip_hdr(skb);

	iph->version = 4;
	iph->ihl = 6;
	iph->tos = 0xc0;
	iph->tot_len = htons(sizeof(*iph) + sizeof(*ih) + 4);
	iph->id = 0x0;
	iph->frag_off = htons(IP_DF);
	iph->ttl = 1;
	iph->protocol = IPPROTO_IGMP;
	iph->saddr = srcip;
	iph->daddr = htonl(INADDR_ALLHOSTS_GROUP);
	((u8 *)&iph[1])[0] = IPOPT_RA;
	((u8 *)&iph[1])[1] = 4;
	((u8 *)&iph[1])[2] = 0;
	((u8 *)&iph[1])[3] = 0;
	ip_send_check(iph);
	skb_put(skb, 24);

	skb_set_transport_header(skb, skb->len);
	ih = igmp_hdr(skb);
	ih->type = IGMP_HOST_MEMBERSHIP_QUERY;
	ih->code = 0xa;
	ih->group = 0;
	ih->csum = 0;
	ih->csum = ip_compute_csum((void *)ih, sizeof(struct igmphdr));
	skb_put(skb, sizeof(*ih));

out:
	return skb;
}

static void qdrv_wlan_send_to_node(struct ieee80211vap *vap, struct sk_buff *skb)
{
	struct qdrv_vap *qv;
	struct net_device *vdev;

	qv = container_of(vap, struct qdrv_vap, iv);
	vdev = qv->ndev;

	skb->dev = vdev;
	skb->priority = WME_AC_VO;

	QTN_SKB_ENCAP(skb) = QTN_SKB_ENCAP_ETH;

	M_FLAG_SET(skb, M_NO_AMSDU);

	dev_queue_xmit(skb);
}

static void qdrv_wlan_igmp_query_send(struct qdrv_wlan *qw, struct ieee80211vap *vap)
{
	struct sk_buff *skb;

	if (!qw->br_dev)
		return;

	skb = ip4_multicast_alloc_query(qw->br_dev);

	if (!skb) {
		DBGPRINTF_LIMIT_E("could not alloc igmp query skb\n");
		return;
	}

	qdrv_wlan_send_to_node(vap, skb);
}

static void qdrv_wlan_igmp_query_timer_handler(unsigned long data)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *)data;
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211vap *vap;

	if ((ic->ic_vendor_fix & VENDOR_FIX_BRCM_AP_GEN_IGMPQUERY) &&
			ic->ic_nonqtn_sta) {
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			if (vap->iv_opmode != IEEE80211_M_HOSTAP)
				continue;
			if (vap->iv_state != IEEE80211_S_RUN)
				continue;
			if (vap->iv_non_qtn_sta_assoc == 0)
				continue;

			qdrv_wlan_igmp_query_send(qw, vap);
		}
	}

	mod_timer(&qw->igmp_query_timer, jiffies + QDRV_WLAN_IGMP_QUERY_INTERVAL * HZ);
}

void qdrv_wlan_igmp_query_timer_start(struct qdrv_wlan *qw)
{
	init_timer(&qw->igmp_query_timer);
	qw->igmp_query_timer.function = qdrv_wlan_igmp_query_timer_handler;
	qw->igmp_query_timer.data = (unsigned long)qw;
	qw->igmp_query_timer.expires = jiffies + QDRV_WLAN_IGMP_QUERY_INTERVAL * HZ;
	add_timer(&qw->igmp_query_timer);
}

void qdrv_wlan_igmp_timer_stop(struct qdrv_wlan *qw)
{
	del_timer(&qw->igmp_query_timer);
}

static int
qdrv_troubleshoot_start_cb(void *in_ctx)
{
	struct qdrv_wlan *qw = in_ctx;
	struct timespec ts;

	/* Stop the MuC so we can borrow its stack */
	if (qw) {
		struct qdrv_mac *mac = qw->mac;
		mac->dead = 1;
		mdelay(100);
		hal_disable_muc();
	}
	/* Add timestamp to crash dump */
	getnstimeofday(&ts);
	printk_once(KERN_WARNING "\ntimestamp = %lu s\n", ts.tv_sec);
	return 0;
}

static void
qdrv_wlan_debug_init(struct qdrv_wlan *qw)
{
	/* Hook into the troubleshoot functions */
	arc_set_sram_safe_area(ARC_SRAM_SAFE_AREA_BASE, ARC_SRAM_SAFE_AREA_END);
	arc_set_troubleshoot_start_hook(qdrv_troubleshoot_start_cb, qw);
}

void qdrv_update_cgq_stats(void *ctx, uint32_t type, uint8_t index, uint32_t value)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *)ctx;

	switch (type) {
	case TOPAZ_CONGEST_QUEUE_STATS_QLEN:
		qw->cgq_stats.congest_qlen[index] = value;
		break;
	case TOPAZ_CONGEST_QUEUE_STATS_ENQFAIL:
		qw->cgq_stats.congest_enq_fail[index] = value;
		break;
	default:
		break;
	}
}

extern void fwt_if_register_node_idx_hook(
	int (*func)(const char *iface_name, const uint8_t *mac_addr));

int qdrv_wlan_get_node_idx(const char *iface_name, const uint8_t *mac_addr)
{
	struct qdrv_cb *qcb;
	struct qdrv_wlan *qw;
	struct ieee80211com *ic;
	struct device *dev = qdrv_soc_get_addr_dev();
	struct ieee80211_node_table *nt;
	struct ieee80211_node *ni;
	int ni_node_idx = 0;

	qcb = dev_get_drvdata(dev);
	qw = (struct qdrv_wlan *)qcb->macs[0].data;
	ic = &qw->ic;
	nt = &ic->ic_sta;

	if (iface_name == NULL) {
		ni = ieee80211_find_node(nt, mac_addr);

		if (ni != NULL) {
			ni_node_idx = IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx);
			ieee80211_free_node(ni);
			return ni_node_idx;
		} else {
			return 0;
		}
	}

	IEEE80211_NODE_LOCK_IRQ(nt);
	TAILQ_FOREACH(ni, &nt->nt_node, ni_list) {
		if (!strcmp(ni->ni_vap->iv_dev->name, iface_name)) {
			ni_node_idx = IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx);
			break;
		}
	}
	IEEE80211_NODE_UNLOCK_IRQ(nt);

	return ni_node_idx;
}

int qdrv_wlan_init(struct qdrv_mac *mac, struct host_ioctl_hifinfo *hifinfo,
	u32 arg1, u32 arg2)
{
	struct qdrv_wlan *qw;	/* Old struct qnet_priv */
	int i;

	if (!TOPAZ_HBM_SKB_ALLOCATOR_DEFAULT) {
		printk(KERN_ERR "%s: wlan rx accelerate should be used with topaz hbm"
				"skb allocator only\n", __FUNCTION__);
		return -1;
	}

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	/* Allocate a wlan structure */
	qw = kmalloc(sizeof(*qw), GFP_KERNEL);
	if (qw == NULL) {
		DBGPRINTF_E("Failed to allocate wlan structure\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -ENOMEM;
	}

	DBGPRINTF(DBG_LL_INFO, QDRV_LF_WLAN,
		"qw 0x%08x ic 0x%08x\n",
		(unsigned int) qw, (unsigned int) &qw->ic);

	/* Clean it out */
	memset(qw, 0, sizeof(struct qdrv_wlan));

	/* Store it in the mac structure as opaque private data */
	mac->data = (void *) qw;

	qw->flags_ext |= QDRV_WLAN_FLAG_UNKNOWN_ARP;

	reg_congest_queue_stats(qdrv_update_cgq_stats, qw);

	/* We need the back pointer so we can control interrupts */
	qw->mac = mac;

	/* Initialize the wlan data structure */
	qw->unit = mac->unit;
	qw->ic.ic_unit = mac->unit;
	qw->flags = arg1 & IOCTL_DEVATTACH_DEVFLAG_MASK;
	qw->rf_chipid = (arg1 & IOCTL_DEVATTACH_DEV_RFCHIP_FREQID_MASK) >>
					IOCTL_DEVATTACH_DEV_RFCHIP_FREQID_MASK_S;
	qw->rf_chip_verid = (arg1 & IOCTL_DEVATTACH_DEV_RFCHIP_VERID_MASK) >>
					IOCTL_DEVATTACH_DEV_RFCHIP_VERID_MASK_S;
	qw->host_sem = (u32)&mac->ruby_sysctrl->l2m_sem;
	soc_shared_params->rf_chip_id = qw->rf_chipid;

	if ((strcmp(QDRV_CFG_TYPE, "qtm710_rgmii_config") == 0) ||
			(strcmp(QDRV_CFG_TYPE, "topaz_rgmii_config") == 0) ||
			(strcmp(QDRV_CFG_TYPE, "topaz_vzn_config") == 0) ||
			(strcmp(QDRV_CFG_TYPE, "topaz_pcie_config") == 0))
		qw->br_isolate = QDRV_BR_ISOLATE_NORMAL;
	else
		qw->br_isolate = 0;
	qw->br_isolate_vid = 0;
#ifdef CONFIG_QUANTENNA_RESTRICT_WLAN_IP
	qw->restrict_wlan_ip = 1;
#else
	qw->restrict_wlan_ip = 0;
#endif
	qw->br_dev = dev_get_by_name(&init_net, "br0");
	if (!qw->br_dev)
		DBGPRINTF_E("Could not get bridge device\n");

	spin_lock_init(&qw->lock);
	spin_lock_init(&qw->flowlock);

	qdrv_br_create(&qw->bridge_table, mac->mac_addr);
	qw->mcs_odd_even = 0;
	qw->tx_restrict = 0;
	qw->tx_restrict_rts = IEEE80211_TX_RESTRICT_RTS_DEF;
	qw->tx_restrict_limit = IEEE80211_TX_RESTRICT_LIMIT_DEF;
	qw->tx_restrict_rate = IEEE80211_TX_RESTRICT_RATE;

	qw->arp_last_sent = jiffies;
	qw->discover_intval = MAX_QDRV_UNKNOWN_DEST_DISCOVER_INTVAL;

	/* init csa workqueues and irq handlers */
	qdrv_init_csa_irqhandler(qw);
	INIT_WORK(&qw->csa_wq, csa_work);
	spin_lock_init(&qw->csa_lock);
	INIT_WORK(&qw->remain_chan_wq, remain_channel_work);
	INIT_WORK(&qw->channel_work_wq, channel_work);

	qdrv_init_cca_irqhandler(qw);
	INIT_WORK(&qw->cca_wq, cca_work);
	spin_lock_init(&qw->cca_lock);

	qdrv_init_meas_irqhandler(qw);
	INIT_WORK(&qw->meas_wq, meas_work);

#ifdef QTN_BG_SCAN
	qdrv_init_scan_irqhandler(qw);
	INIT_WORK(&qw->scan_wq, scan_work);
	spin_lock_init(&qw->scan_lock);
#endif /* QTN_BG_SCAN */

#ifdef CONFIG_QVSP
	if (qdrv_wlan_vsp_irq_init(qw, hifinfo->hi_vsp_stats_phys)) {
		panic("Could not initialize VSP stats IRQ");
	}
#endif

	qw->shared_pernode_stats_pool = dma_alloc_coherent(NULL,
			sizeof(struct qtn_node_shared_stats_list) * QTN_PER_NODE_STATS_POOL_SIZE,
			&qw->shared_pernode_stats_phys, GFP_KERNEL | GFP_DMA | __GFP_ZERO);
	if (qw->shared_pernode_stats_pool == NULL) {
		DBGPRINTF_E("Failed to allocate per node stats pool");
		return -ENOMEM;
	}
	TAILQ_INIT(&qw->shared_pernode_stats_head);
	for (i = 0; i < QTN_PER_NODE_STATS_POOL_SIZE; i++) {
		TAILQ_INSERT_TAIL(&qw->shared_pernode_stats_head,
				&qw->shared_pernode_stats_pool[i], next);
	}

	/* Initialize the TX interface */
	if (qdrv_tx_init(mac, hifinfo, arg2) < 0) {
		DBGPRINTF_E("Failed to initialize TX\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Initialize the RX interface */
	if (qdrv_rx_init(qw, hifinfo) < 0) {
		DBGPRINTF_E("Failed to initialize RX\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Initialize the Scan interface */
	if (qdrv_scan_init(qw, hifinfo) < 0) {
		DBGPRINTF_E("Failed to initialize Scan\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Initialize the hostlink interface */
	if (qdrv_hostlink_init(qw, hifinfo) < 0) {
		DBGPRINTF_E("Failed to initialize hostlink\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Initialize the beamforming support */
	if (qdrv_txbf_init(qw) < 0) {
		DBGPRINTF_E("Failed to initialize beamforming\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Start the RX interface */
	if (qdrv_rx_start(mac) < 0) {
		DBGPRINTF_E("Failed to start RX\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Start the TX interface */
	if (qdrv_tx_start(mac) < 0) {
		DBGPRINTF_E("Failed to start TX\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Start the Scan interface */
	if (qdrv_scan_start(mac) < 0) {
		DBGPRINTF_E("Failed to start Scan\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Start the hostlink interface */
	if (qdrv_hostlink_start(mac) < 0) {
		DBGPRINTF_E("Failed to start hostlink\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Set up rate tables for all potential media types. */
	if (set_rates(qw, IEEE80211_MODE_11A) < 0) {
		DBGPRINTF_E("Failed to set A rates\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11B) < 0) {
		DBGPRINTF_E("Failed to set B rates\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11G) < 0) {
		DBGPRINTF_E("Failed to set G rates\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11NG) < 0) {
		DBGPRINTF_E("Failed to set NG rates\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11NG_HT40PM) < 0) {
		DBGPRINTF_E("Failed to set NG rates\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11AC_NG_VHT20PM) < 0) {
		DBGPRINTF_E("Failed to set AC-NG rates VHT20\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11AC_NG_VHT40PM) < 0) {
		DBGPRINTF_E("Failed to set AC-NG rates VHT40\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11NA) < 0) {
		DBGPRINTF_E("Failed to set NA rates\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11NA_HT40PM) < 0) {
		DBGPRINTF_E("Failed to set NA rates\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11AC_VHT20PM) < 0) {
		DBGPRINTF_E("Failed to set AC rates VHT20\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11AC_VHT40PM) < 0) {
		DBGPRINTF_E("Failed to set AC rates VHT40\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_rates(qw, IEEE80211_MODE_11AC_VHT80PM) < 0) {
		DBGPRINTF_E("Failed to set AC rates VHT80\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Initialize the 802.11 layer (old qtn_attach()) */
	if (qdrv_wlan_80211_init(&qw->ic, mac->mac_addr, qw->rf_chipid) < 0) {
		DBGPRINTF_E("Failed to initialize 802.11 (ieee80211com)\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_mode(qw, IEEE80211_MODE_11G) < 0) {
		DBGPRINTF_E("Failed to set mode\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	if (set_mode(qw, IEEE80211_MODE_11NG) < 0) {
		DBGPRINTF_E("Failed to set mode\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	qdrv_init_tsensor(qw);
	qdrv_txpow_cal_init(qw);

	/* start DFS function */
	qdrv_radar_init(mac);

	qdrv_pktlogger_init(qw);

	if (qdrv_spdia_init(qw)) {
		DBGPRINTF_E("Failed to initialize QSPDIA\n");
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");
		return -1;
	}

	/* Timer to enable hang recovery. We delay this as the intial channel
	 * change can take a long time to complete, causing false hangs to be
	 * detected.
	 */
	qdrv_wlan_hr_oneshot_enable(qw);

	/* Subscribe to PM notifications */
	qw->pm_notifier.notifier_call = qdrv_pm_notify;
	pm_qos_add_notifier(PM_QOS_POWER_SAVE, &qw->pm_notifier);

	qdrv_wlan_debug_init(qw);

#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
	br_fdb_get_active_sub_port_hook = qdrv_get_active_sub_port;
	br_fdb_check_active_sub_port_hook = qdrv_check_active_sub_port;
#endif

	qw->sp = qtn_mproc_sync_shared_params_get();

	fwt_if_register_node_idx_hook(qdrv_wlan_get_node_idx);

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

/* WPS Polling programs */
static u8	qdrv_wps_gpio_polling_pin = 255;
static u32	qdrv_wps_button_last_level;
static u32	qdrv_wps_button_active_level;

int qdrv_wlan_exit(struct qdrv_mac *mac)
{
	struct qdrv_wlan *qw = (struct qdrv_wlan *) mac->data;

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "-->Enter\n");

	fwt_if_register_node_idx_hook(NULL);

	qdrv_wlan_80211_exit(&qw->ic);

	qdrv_wlan_hr_oneshot_disable(qw);
	qdrv_txpow_cal_stop(qw);
	if (qw->se95_temp_sensor) {
		i2c_unregister_device(qw->se95_temp_sensor);
		qw->se95_temp_sensor = NULL;
	}

	qdrv_txbf_exit(qw);
	qdrv_spdia_exit(qw);
	qdrv_hostlink_exit(qw);
	qdrv_tx_stop(mac);
	qdrv_scan_exit(qw);
	qdrv_rx_exit(qw);
	qdrv_tx_exit(qw);

	qdrv_pktlogger_exit(qw);

	qdrv_br_exit(&qw->bridge_table);

	if (qw->br_dev != NULL) {
		dev_put(qw->br_dev);
	}

	if (qw->pktlogger.dev != NULL) {
		dev_put(qw->pktlogger.dev);
	}

	dma_free_coherent(NULL, sizeof(struct qtn_node_shared_stats_list) * QTN_PER_NODE_STATS_POOL_SIZE,
			qw->shared_pernode_stats_pool, qw->shared_pernode_stats_phys);

#ifdef CONFIG_QVSP
	qdrv_wlan_vsp_irq_exit(qw);
#endif
	qdrv_genpcap_exit(qw);

	kfree(qw);

	/* Reset the MAC data structure */
	mac->data = NULL;

	if (qdrv_wps_gpio_polling_pin != 255)
		gpio_free(qdrv_wps_gpio_polling_pin);

#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
	br_fdb_get_active_sub_port_hook = NULL;
	br_fdb_check_active_sub_port_hook = NULL;
#endif

	DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "<--Exit\n");

	return 0;
}

/*
* Queue of processes who access wps_button file
*/
DECLARE_WAIT_QUEUE_HEAD(WPS_Button_WaitQ);

/* WPS button event reported to user space process */
typedef enum {
	WPS_BUTTON_NONE_EVENT = 0,
	WPS_BUTTON_WIRELESS_EVENT,
	WPS_BUTTON_DBGDUMP_EVENT,
	WPS_BUTTON_INVALIDE_EVENT
} WPS_Button_Event;
#define WPS_BUTTON_VALID(e) (WPS_BUTTON_NONE_EVENT < (e) && (e) < WPS_BUTTON_INVALIDE_EVENT)
static WPS_Button_Event wps_button_event = WPS_BUTTON_NONE_EVENT;

static void qdrv_wps_button_event_wakeup(WPS_Button_Event event)
{
	if (!WPS_BUTTON_VALID(event))
		return;

	wps_button_event = event;
	wake_up_all(&WPS_Button_WaitQ);
}

static ssize_t qdrv_wps_button_read(struct device *dev,
				    struct device_attribute *attr,
				    char *buff)
{
	int i = 0;

	/* As usual, this read is always blocked untill wps button is pressed
	 * so increase the module reference to prevent it being unload during
	 * blocking read
	 */
	if (!try_module_get(THIS_MODULE))
		return 0;

	/* wait for valid WPS button event */
	wait_event_interruptible(WPS_Button_WaitQ, WPS_BUTTON_VALID(wps_button_event));

	/* read back empty string in signal wakeup case */
	for (i = 0; i < _NSIG_WORDS; i++) {
		if (current->pending.signal.sig[i] & ~current->blocked.sig[i]) {
			module_put(THIS_MODULE);
			return 0;
		}
	}

	sprintf(buff, "%d\n", wps_button_event);

	/* after new event been handled, reset to none event */
	wps_button_event = WPS_BUTTON_NONE_EVENT;

	module_put(THIS_MODULE);

	return strlen(buff);
}

DEVICE_ATTR(wps_button, S_IWUSR | S_IRUSR, qdrv_wps_button_read, NULL); /* dev_attr_wps_button */

static inline void qdrv_wps_button_device_file_create(struct net_device *ndev)
{
	if (device_create_file(&(ndev->dev), &dev_attr_wps_button)) {
		DBGPRINTF_W("QDRV: failed to create wps device file\n");
	}
}

static inline void qdrv_wps_button_device_file_remove(struct net_device *ndev)
{
	device_remove_file(&ndev->dev, &dev_attr_wps_button);
}

/* records the jiffies when button down, back to 0 after button released */
static u32 qdrv_wps_button_down_jiffies = 0;
static int interrupt_mode = 0;

#define WPS_BUTTON_TIMER_INTERVAL ((3 * HZ) / 10) /* timer internal */

static void qdrv_wps_polling_button_notifier(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	u32 current_level;

	current_level = gpio_get_value(qdrv_wps_gpio_polling_pin);

	/* records the falling edge jiffies */
	if ((current_level == qdrv_wps_button_active_level)
	    && (qdrv_wps_button_last_level != qdrv_wps_button_active_level)) {

		qdrv_wps_button_down_jiffies = jiffies;
	}

	/* at rising edge */
	if ((current_level != qdrv_wps_button_active_level)
	    && (qdrv_wps_button_last_level == qdrv_wps_button_active_level)) {

		/* WPS button event is rising triggered -- when button
		 * being changed from active to inactive level.
		 *
		 * Different press time trigger different event
		 */
		if ((jiffies - qdrv_wps_button_down_jiffies) >= 10 * HZ) {

			/* wakeup the event waiting processes */
			qdrv_wps_button_event_wakeup(WPS_BUTTON_DBGDUMP_EVENT);
			DBGPRINTF_N("WPS: button long press polling at %u\n", (unsigned int) jiffies);
		} else {
			/* wakeup the event waiting processes */
			qdrv_wps_button_event_wakeup(WPS_BUTTON_WIRELESS_EVENT);
			qdrv_eventf(dev, "WPS-BUTTON.indication");

			DBGPRINTF_N("WPS: button short press polling at %u\n", (unsigned int) jiffies);
		}

		/* back to 0 after rising edge */
		qdrv_wps_button_down_jiffies = 0;

		if (interrupt_mode)
			goto interrupt_end;
	}

	/* Restart the timer */
	mod_timer(&qdrv_wps_button_timer, jiffies + WPS_BUTTON_TIMER_INTERVAL);

interrupt_end:
	qdrv_wps_button_last_level = current_level;

	return;
}

static int qdrv_polling_wps_button_init(struct net_device *dev, u8 wps_gpio_pin, u8 active_logic, int mode)
{
	interrupt_mode = mode;

	if (wps_gpio_pin > MAX_GPIO_PIN) {
		DBGPRINTF_E("WPS polling GPIO pin %d is invalid\n", wps_gpio_pin);
		return -1;
	}

	/*
	 * Set up timer to poll the button.
	 * Request the GPIO resource and export it for userspace
	 */
	if (gpio_request(wps_gpio_pin, dev->name) < 0)
		DBGPRINTF_E("%s: Failed to request GPIO%d for GPIO reset\n",
				dev->name, wps_gpio_pin);
	else
		gpio_export(wps_gpio_pin, true);

	qdrv_wps_gpio_polling_pin = wps_gpio_pin;
	qdrv_wps_button_active_level = (active_logic) ? 1 : 0;
	qdrv_wps_button_last_level = ~qdrv_wps_button_active_level;

	init_timer(&qdrv_wps_button_timer);
	qdrv_wps_button_timer.function = qdrv_wps_polling_button_notifier;
	qdrv_wps_button_timer.data = (unsigned long)dev;

	/* creeate the device file for user space use */
	qdrv_wps_button_device_file_create(dev);

	return 0;
}

static void qdrv_polling_wps_button_begin(void)
{
	qdrv_wps_button_timer.expires = jiffies + WPS_BUTTON_TIMER_INTERVAL;
	if (!timer_pending(&qdrv_wps_button_timer))
		add_timer(&qdrv_wps_button_timer);
}

static void qdrv_polling_wps_button_exit(uint8_t wps_gpio_pin)
{
	struct net_device *ndev = (struct net_device *)(qdrv_wps_button_timer.data);

	qdrv_wps_button_device_file_remove(ndev);
	del_timer_sync(&qdrv_wps_button_timer);
	qdrv_wps_button_timer.data = (unsigned long)(NULL);
	gpio_free(wps_gpio_pin);
}

static irqreturn_t qdrv_wps_button_handler(int irq, void *dev_id)
{
	qdrv_polling_wps_button_begin();

	return IRQ_HANDLED;
}

static int
qdrv_interrupt_wps_button_init(struct net_device *dev, u8 wps_gpio_pin)
{
	u8 active_logic;

	/* current wps button is in released state, so its value can determine active_logic */
	active_logic = (~gpio_get_value(wps_gpio_pin) & 0x01) ? 1 : 0;

	if (request_irq(GPIO2IRQ(wps_gpio_pin), qdrv_wps_button_handler, IRQF_TRIGGER_FALLING | IRQF_SHARED,
			"qwps_btn", dev)) {
		DBGPRINTF_E("WPS: push button IRQ %d is not free for register falling edge irq\n", GPIO2IRQ(wps_gpio_pin));
		return -1;
	}

	DBGPRINTF_N("WPS: push button IRQ initialised\n");

	return qdrv_polling_wps_button_init(dev, wps_gpio_pin, active_logic, 1);
}

static void qdrv_interrupt_wps_button_exit(struct net_device *dev, u8 wps_gpio_pin)
{
	qdrv_polling_wps_button_exit(wps_gpio_pin);
	free_irq(GPIO2IRQ(wps_gpio_pin), dev);
}

int qdrv_wps_button_init(struct net_device *dev)
{
	u8	wps_gpio_pin = 0;
	u8	use_interrupt = 0;
	u8	active_logic = 0;
	int	retval;

	if (qdrv_get_wps_push_button_config(&wps_gpio_pin, &use_interrupt, &active_logic) != 0) {
		DBGPRINTF_N("WPS: push button is not configured\n");
		return 0;
	}

	DBGPRINTF_N("WPS: push button GPIO pin %d\n", wps_gpio_pin);
	DBGPRINTF_N("WPS: monitored using %s\n", use_interrupt ? "interrupt" : "polling");
	if (use_interrupt) {
		DBGPRINTF_N("WPS: interrupt on line %d\n", GPIO2IRQ(wps_gpio_pin));
	} else {
		DBGPRINTF_N("WPS: active logic is %s\n", active_logic ? "high" : "low");
	}

	set_wps_push_button_enabled();

	if (use_interrupt) {
		retval = qdrv_interrupt_wps_button_init(dev, wps_gpio_pin);
	} else {
		retval = qdrv_polling_wps_button_init(dev, wps_gpio_pin, active_logic, 0);
		qdrv_polling_wps_button_begin();
	}

	return retval;
}

void qdrv_wps_button_exit(void)
{
	struct net_device *dev = (struct net_device *)(qdrv_wps_button_timer.data);
	u8	wps_gpio_pin = 0;
	u8	use_interrupt = 0;
	u8	active_logic = 0;

	if (!dev)
		return;

	if (qdrv_get_wps_push_button_config(&wps_gpio_pin, &use_interrupt, &active_logic) != 0) {
		return;
	}

	if (use_interrupt) {
		qdrv_interrupt_wps_button_exit(dev, wps_gpio_pin);
	} else {
		qdrv_polling_wps_button_exit(wps_gpio_pin);
	}
}

struct net_device *qdrv_wps_button_get_dev(void)
{
	return (struct net_device *)(qdrv_wps_button_timer.data);
}

/**
 * Intermediate point between MUC and WLAN driver. Layer to hide 802.11 specific structures
 * from the MUC comm layer.
 *
 * @return 1 if the MIC failure was reported to the WLAN driver, 0 otherwise.
 */
int qdrv_wlan_tkip_mic_error(struct qdrv_mac *mac, int devid, int count)
{
	struct net_device *dev = mac->vnet[QDRV_WLANID_FROM_DEVID(devid)];

	struct ieee80211vap *vap;
	struct ieee80211com *ic;

	if (dev) {
		vap = netdev_priv(dev);
		ic = vap->iv_ic;
		/*
		 * FIXME
		 * TKIP MIC errors report from MUC when STA is not associated, indicating a
		 * mismatch that "vap->iv_bss" has been disassociated in driver, but not in MUC.
		 * Suppress report in this case.
		 * Refer to qdrv_wlan_80211_disassoc() and qtn_rx_should_process_mic_failure()
		 */
		if (vap->iv_opmode == IEEE80211_M_STA && vap->iv_bss->ni_associd == 0 &&
				(!ieee80211_is_repeater(ic) || ic->ic_sta_assoc == 0))
			return 0;
		ic->ic_tkip_mic_failure(vap, count);
		return 1;
	}

	return 0;
}

void qdrv_disassoc_reason_record(struct ieee80211com *ic, const char *macaddr,
			uint32_t reason)
{
	struct ieee80211_disassoc_records *records = &ic->ic_disassoc_records;
	int i;
	int free_slot = -1;
	int oldest_slot = 0;
	time_t oldest_time = records->timestamp[0];
	uint32_t uptime = (jiffies - INITIAL_JIFFIES) / HZ;

	for (i = 0; i < IEEE80211_MAX_DISASSOC_RECORDS; i++) {
		if (IEEE80211_ADDR_EQ(records->sta_macaddr[i], macaddr)) {
			records->reason[i] = reason;
			records->timestamp[i] = uptime;
			records->disassoc_num[i]++;
			return;
		}

		if (free_slot < 0 && !records->timestamp[i]) {
			free_slot = i;
			break;
		}

		if (records->timestamp[i] < oldest_time) {
			oldest_time = records->timestamp[i];
			oldest_slot = i;
		}
	}

	if (free_slot >= 0)
		i = free_slot;
	else
		i = oldest_slot;

	IEEE80211_ADDR_COPY(records->sta_macaddr[i], macaddr);
	records->reason[i] = reason;
	records->timestamp[i] = uptime;
	records->disassoc_num[i]++;
}
EXPORT_SYMBOL(qdrv_disassoc_reason_record);

/* record channel change event */
void qdrv_channel_switch_record(struct ieee80211com *ic,
				struct ieee80211_channel *new_chan,
				uint32_t reason)
{
	struct ieee80211req_csw_record *records = &ic->ic_csw_record;
	int index = records->index;
	struct ieee80211vap *vap = ieee80211_get_primary_vap(ic, 1);

	qdrv_chan_occupy_record_start(ic, new_chan->ic_ieee);

	/* if the new_chan is same with last channel, do not record it */
	if (new_chan->ic_ieee == records->channel[index]) {
		return;
	}

	if (vap)
		qdrv_eventf(vap->iv_dev,
			    QEVT_TAG_CHAN_CHANGED" to %d (reason %u)",
			    new_chan->ic_ieee, reason);

	if (records->cnt < CSW_MAX_RECORDS_MAX) {
		records->cnt++;
	}

	if (records->cnt == 1) {
		records->index = 0;
	} else {
		if (records->index == (CSW_MAX_RECORDS_MAX - 1))
			records->index = 0;
		else
			records->index++;
	}

	index = records->index;
	records->channel[index] = new_chan->ic_ieee;
	records->timestamp[index] = (jiffies - INITIAL_JIFFIES) / HZ;
	if (ic->ic_opmode != IEEE80211_M_STA) {
		records->reason[index] = reason;

		if ((reason & CSW_REASON_MASK) == IEEE80211_CSW_REASON_SCS) {
			records->reason[index] = ic->ic_csw_reason;
			memcpy(records->csw_record_mac[index], ic->ic_csw_mac, IEEE80211_ADDR_LEN);
		}
	} else {
		records->reason[index] = IEEE80211_CSW_REASON_UNKNOWN;
	}
}
EXPORT_SYMBOL(qdrv_channel_switch_record);

void qdrv_channel_switch_reason_record(struct ieee80211com *ic, int reason_flag)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	switch (reason_flag & CSW_REASON_MASK) {
	case IEEE80211_CSW_REASON_SCS:
		qw->csw_stats.csw_by_scs++;
		break;
	case IEEE80211_CSW_REASON_DFS:
		qw->csw_stats.csw_by_dfs++;
		break;
	case IEEE80211_CSW_REASON_MANUAL:
		qw->csw_stats.csw_by_user++;
		break;
	case IEEE80211_CSW_REASON_SAMPLING:
		qw->csw_stats.csw_by_sampling++;
		break;
	case IEEE80211_CSW_REASON_TDLS_CS:
		qw->csw_stats.csw_by_tdls++;
		break;
	case IEEE80211_CSW_REASON_BGSCAN:
		qw->csw_stats.csw_by_bgscan++;
		break;
	case IEEE80211_CSW_REASON_OCAC:
		qw->csw_stats.csw_by_ocac++;
		break;
	case IEEE80211_CSW_REASON_OCAC_RUN:
		qw->csw_stats.csw_by_ocac_run++;
		break;
	case IEEE80211_CSW_REASON_CSA:
		qw->csw_stats.csw_by_csa++;
		break;
	case IEEE80211_CSW_REASON_SCAN:
		qw->csw_stats.csw_by_scan++;
		break;
	case IEEE80211_CSW_REASON_COC:
		qw->csw_stats.csw_by_coc++;
		break;
	default:
		DBGPRINTF_E("unexpected event\n");
	}
}

void qdrv_wlan_drop_ba(struct ieee80211_node *ni, int tid, int tx, int reason)
{
	ieee80211_send_delba(ni, tid, !tx, reason);
	ieee80211_node_ba_del(ni, tid, tx, reason);
}

void qdrv_wlan_dump_ba(struct ieee80211_node *ni)
{
	int32_t tid;
	int istx;
	struct ieee80211_ba_tid *ba_tid;

	printk("Node %u %pM BA table:\n", IEEE80211_AID(ni->ni_associd), ni->ni_macaddr);
	printk("tx tid state type win timeout\n");
	for (istx = 0; istx <= 1; istx++) {
		for (tid = 0; tid < WME_NUM_TID; tid++) {
			ba_tid = istx ? &ni->ni_ba_tx[tid] : &ni->ni_ba_rx[tid];
			printk("%2d %3d %5d %4d %3d %7d\n",
				istx, tid, ba_tid->state,
				ba_tid->type, ba_tid->buff_size, ba_tid->timeout);
		}
	}
}

static void qdrv_wlan_set_11g_erp(struct ieee80211vap *vap, int on)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	struct qtn_node_args *args = NULL;
	dma_addr_t args_dma;

	if (!(ioctl = vnet_alloc_ioctl(qv)) ||
	    !(args = qdrv_hostlink_alloc_coherent(NULL, sizeof(*args),
						  &args_dma, GFP_DMA | GFP_ATOMIC))) {
		DBGPRINTF_E("Failed to allocate set_11g_erp message\n");
		vnet_free_ioctl(ioctl);
		return;
	}

	memset(args, 0, sizeof(*args));

	ioctl->ioctl_command = IOCTL_DEV_SET_11G_ERP;
        ioctl->ioctl_arg1 = qv->devid;
        ioctl->ioctl_arg2 = on;
        ioctl->ioctl_argp = args_dma;

	vnet_send_ioctl(qv, ioctl);
        qdrv_hostlink_free_coherent(NULL, sizeof(*args), args, args_dma);
}

void qdrv_wlan_cleanup_before_reload(struct ieee80211com *ic)
{
	ic->ic_chan_is_set = 0;
	ic->ic_des_chan_after_init_scan = 0;
	ic->ic_des_chan_after_init_cac = 0;
	ic->ic_max_boot_cac_duration = -1;
}

int8_t qdrv_get_local_tx_power(struct ieee80211com *ic)
{
#define QTN_TXPOW_TOTAL_OFFSET	6
	if (qdrv_is_gain_low()) {
		return IEEE80211_LOWGAIN_TXPOW_MAX + QTN_TXPOW_TOTAL_OFFSET;
	} else {
		return ic->ic_curchan->ic_maxpower_normal + QTN_TXPOW_TOTAL_OFFSET;
	}
#undef QTN_TXPOW_TOTAL_OFFSET
}

static int8_t min_rssi_40MHZ_perchain_mcstbl[] = {
	-82,
	-82,
	-81,
	-78,
	-74,
	-71,
	-70,
	-68,
	-82,
	-80,
	-77,
	-74,
	-71,
	-67,
	-65,
	-63,
	-80,
	-77,
	-73,
	-71,
	-66,
	-63,
	-61,
	-58,
	-72,
	-69,
	-63,
	-61,
	-55,
	-47,
	-47,
	-47,
	-76,
	-71,
	-72,
	-70,
	-65,
	-65,
	-74,
	-72,
	-70,
	-70,
	-69,
	-67,
	-67,
	-68,
	-68,
	-62,
	-61,
	-63,
	-62,
	-62,
	-66,
	-64,
	-62,
	-61,
	-61,
	-60,
	-59,
	-55,
	-57,
	-47,
	-47,
	-47,
	-58,
	-57,
	-57,
	-47,
	-47,
	-47,
	-47,
	-47,
	-47,
	-47,
	-47,
	-47
};

char *link_margin_info_err_msg[] = {
	"Link Margin ERROR:No such node in macfw\n"				/* QTN_LINK_MARGIN_REASON_NOSUCHNODE*/
};

int qdrv_get_local_link_margin(struct ieee80211_node *ni, int8_t *result)
{
	struct qdrv_vap *qv = container_of(ni->ni_vap, struct qdrv_vap, iv);
	struct host_ioctl *ioctl;
	struct qtn_link_margin_info *lm_info;
	dma_addr_t lm_dma;

	if ((!(ioctl = vnet_alloc_ioctl(qv))) ||
			(!(lm_info = (struct qtn_link_margin_info *)qdrv_hostlink_alloc_coherent(NULL,
												 sizeof(struct qtn_link_margin_info),
												 &lm_dma,
												 GFP_DMA | GFP_ATOMIC)))) {
		DBGPRINTF_E("Failed to allocate LINKMARGIN message\n");
		vnet_free_ioctl(ioctl);
		return -EINVAL;
	}

	memset(lm_info, 0, sizeof(*lm_info));
	memcpy(lm_info->mac_addr, ni->ni_macaddr, 6);
	ioctl->ioctl_command = IOCTL_DEV_GET_LINK_MARGIN_INFO;
	ioctl->ioctl_arg1 = qv->devid;
	ioctl->ioctl_argp = lm_dma;

	vnet_send_ioctl(qv, ioctl);

	if (lm_info->reason == QTN_LINK_MARGIN_REASON_SUCC) {
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "get link margin info success:bw=%d,mcs=%d,rssi_avg=%d\n",
				lm_info->bw,
				lm_info->mcs,
				lm_info->rssi_avg / 10);

		/* protect invalid mcs */
		if (lm_info->mcs >= ARRAY_SIZE(min_rssi_40MHZ_perchain_mcstbl))
			lm_info->mcs = 0;

		/* it seems bw=0 appears usually */
		if (!lm_info->bw)
			*result = (lm_info->rssi_avg / 10) - RSSI_40M_TO_20M_DBM(min_rssi_40MHZ_perchain_mcstbl[lm_info->mcs]);
		else
			*result = (lm_info->rssi_avg / 10) - min_rssi_40MHZ_perchain_mcstbl[lm_info->mcs];
	} else {
		DBGPRINTF(DBG_LL_ALL, QDRV_LF_TRACE, "%s", link_margin_info_err_msg[lm_info->reason - QTN_LINK_MARGIN_REASON_NOSUCHNODE]);
		*result = LINK_MARGIN_INVALID;
	}

	qdrv_hostlink_free_coherent(NULL, sizeof(struct qtn_link_margin_info), lm_info, lm_dma);
	return 0;
}

int qdrv_wlan_get_shared_vap_stats(struct ieee80211vap *vap)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	uint8_t vapid = QDRV_WLANID_FROM_DEVID(qv->devid);
	qtn_shared_vap_stats_t* shared_stats = qdrv_auc_get_vap_stats(vapid);

	if (!shared_stats)
		return -EINVAL;

	vap->iv_devstats.rx_packets = shared_stats->qtn_rx_pkts;
	vap->iv_devstats.rx_bytes = shared_stats->qtn_rx_bytes;
	vap->iv_devstats.rx_unicast_packets = shared_stats->qtn_rx_pkts -
			(shared_stats->qtn_rx_bcast + shared_stats->qtn_rx_mcast);
	vap->iv_devstats.rx_broadcast_packets = shared_stats->qtn_rx_bcast;
	vap->iv_devstats.multicast = shared_stats->qtn_rx_mcast;
	vap->iv_devstats.rx_dropped = shared_stats->qtn_rx_dropped;

	vap->iv_devstats.tx_packets = shared_stats->qtn_tx_pkts;
	vap->iv_devstats.tx_bytes = shared_stats->qtn_tx_bytes;
	vap->iv_devstats.tx_multicast_packets += shared_stats->qtn_tx_mcast +
			shared_stats->qtn_muc_tx_mcast;
	vap->iv_devstats.tx_unicast_packets = shared_stats->qtn_tx_pkts -
			vap->iv_devstats.tx_multicast_packets -
			vap->iv_devstats.tx_broadcast_packets;

	vap->iv_devstats.tx_dropped = shared_stats->qtn_tx_dropped;

	return 0;
}

int qdrv_wlan_reset_shared_vap_stats(struct ieee80211vap *vap)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	uint8_t vapid = QDRV_WLANID_FROM_DEVID(qv->devid);
	qtn_shared_vap_stats_t* shared_stats = qdrv_auc_get_vap_stats(vapid);

	if (!shared_stats)
		return -EINVAL;

	memset(shared_stats, 0, sizeof(*shared_stats));

	return 0;
}

int qdrv_wlan_get_shared_node_stats(struct ieee80211_node *ni)
{
	uint8_t node_idx = IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx);
	qtn_shared_node_stats_t* shared_stats = qdrv_auc_get_node_stats(node_idx);
	qtn_shared_node_stats_ext_t *shared_stats_ext = qtn_get_shared_node_stats(node_idx);
	uint8_t i;
	uint32_t total_msdu_drops = 0;

	if (!shared_stats)
		return -EINVAL;

	ni->ni_stats.ns_rx_data = shared_stats->qtn_rx_pkts;
	ni->ni_stats.ns_rx_bytes = shared_stats->qtn_rx_bytes;
	ni->ni_stats.ns_rx_ucast = shared_stats->qtn_rx_pkts -
			(shared_stats->qtn_rx_mcast + shared_stats->qtn_rx_bcast);
	ni->ni_stats.ns_rx_mcast = shared_stats->qtn_rx_mcast;
	ni->ni_stats.ns_rx_bcast = shared_stats->qtn_rx_bcast;
	ni->ni_stats.ns_rx_vlan_pkts = shared_stats->qtn_rx_vlan_pkts;
	if (shared_stats_ext)
		ni->ni_stats.ns_rx_errors = shared_stats_ext->qtn_rx_error;

	ni->ni_stats.ns_tx_data = shared_stats->qtn_tx_pkts;
	ni->ni_stats.ns_tx_bytes = shared_stats->qtn_tx_bytes;
	ni->ni_stats.ns_tx_mcast = shared_stats->qtn_tx_mcast +
			shared_stats->qtn_muc_tx_mcast;
	ni->ni_stats.ns_tx_ucast = shared_stats->qtn_tx_pkts -
			ni->ni_stats.ns_tx_mcast -
			ni->ni_stats.ns_tx_bcast;
	for (i = 0; i < WMM_AC_NUM; i++) {
		ni->ni_stats.ns_tx_wifi_drop[i] = shared_stats->qtn_tx_drop_data_msdu[i];
		total_msdu_drops += ni->ni_stats.ns_tx_wifi_drop[i];
		ni->ni_stats.ns_tx_wifi_drop_xattempts[i] =
						shared_stats->qtn_tx_drop_xattempts[i];
	}
	ni->ni_stats.ns_tx_dropped = total_msdu_drops;
	ni->ni_stats.ns_tx_allretries = shared_stats->qtn_tx_allretries;

	return 0;
}

int qdrv_wlan_reset_shared_node_stats(struct ieee80211_node *ni)
{
	uint8_t node_idx = IEEE80211_NODE_IDX_UNMAP(ni->ni_node_idx);
	qtn_shared_node_stats_t* shared_stats = qdrv_auc_get_node_stats(node_idx);

	if (!shared_stats)
		return -EINVAL;

	memset(shared_stats, 0, sizeof(*shared_stats));

	return 0;
}

int qdrv_rxgain_params(struct ieee80211com *ic, int index, struct qtn_rf_rxgain_params *params)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	qdrv_hostlink_rxgain_params(qw, index, params);

	return 0;
}

void
qdrv_wlan_vlan_enable(struct ieee80211com *ic, int enable)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	qdrv_hostlink_vlan_enable(qw, enable);
}

void
qdrv_wlan_vlan_drop_stag(struct ieee80211com *ic, int drop_stag)
{
	struct qdrv_wlan *qw = container_of(ic, struct qdrv_wlan, ic);

	qdrv_hostlink_vlan_drop_stag(qw, drop_stag);
}

int qdrv_wlan_80211_set_bcn_scheme(struct ieee80211vap *vap, int param, int value)
{
	struct qdrv_vap *qv = container_of(vap, struct qdrv_vap, iv);
	struct ieee80211com *ic = vap->iv_ic;
	int ret = 0;

	ret = qdrv_hostlink_change_bcn_scheme(qv, param, value);
	if ((ret < 0) || (ret & (QTN_HLINK_RC_ERR)))
		return -1;

	ic->ic_beaconing_scheme = value;
	return 0;
}

#if defined(CONFIG_QTN_BSA_SUPPORT)
static void qdrv_wlan_output_erw_entry(struct seq_file *s, struct ieee80211vap *vap, struct ieee80211_bsa_erw_entry *erw_entry, int is_first)
{
	char select[32];
	char mode[16];
	int left_len;

	memset(select, 0, sizeof(select));
	memset(mode, 0, sizeof(mode));

	if (!s || !vap || !erw_entry)
		return;

	left_len = sizeof(select);
	if (erw_entry->frame_select & IEEE80211_ERW_PROBE_RESP) {
		strncpy(select, "Pr", left_len);
		left_len -= strlen("Pr");
	}

	if (erw_entry->frame_select & IEEE80211_ERW_AUTH_RESP) {
		if (left_len < sizeof(select)) {
			strncat(select, "|", left_len);
			left_len -= 1;
		}
		strncat(select, "Au", left_len - 1);
		left_len -= strlen("Au");
	}

	if (erw_entry->frame_select & IEEE80211_ERW_ASSOC_RESP) {
		if (left_len < sizeof(select)) {
			strncat(select, "|", left_len - 1);
			left_len -= 1;
		}
		strncat(select, "As", left_len - 1);
		left_len -= strlen("As");
	}

	if (erw_entry->frame_select & IEEE80211_ERW_REASSOC_RESP) {
		if (left_len < sizeof(select)) {
			strncat(select, "|", left_len - 1);
			left_len -= 1;
		}
		strncat(select, "Reas", left_len - 1);
		left_len -= strlen("Reas");
	}

	if ((erw_entry->frame_select & IEEE80211_ERW_SELECT_ALL) == IEEE80211_ERW_SELECT_ALL)
		strncpy(select, "All", sizeof(select));

	left_len = sizeof(mode);
	if (erw_entry->rssi_mode & IEEE80211_ERW_RSSI_MODE_MAX) {
		strncpy(mode, "Max", left_len);
		left_len -= strlen("Max");
	}
	if (erw_entry->rssi_mode & IEEE80211_ERW_RSSI_MODE_MIN) {
		if (left_len < sizeof(mode)) {
			strncat(mode, "|", left_len - 1);
			left_len -= 1;
		}
		strncat(mode, "Min", left_len - 1);
		left_len -= strlen("Min");
	}

	if (!erw_entry->rssi_mode)
		strncpy(mode, "None", sizeof(mode));

	seq_printf(s, "%-7s %pM %11s %7s %8d %8d %7d %15d %15d %12u %12u %12u %12u %8d %8x\n",
		is_first ? vap->iv_dev->name : " ", erw_entry->mac_addr, select, mode,
		erw_entry->rssi_thrd_min, erw_entry->rssi_thrd_max,
		erw_entry->rssi_mode == IEEE80211_ERW_CONTENT_BASE ? 1 : 0,
		erw_entry->ie_missing_num, erw_entry->ie_present_num,
		erw_entry->probe_cnt, erw_entry->auth_cnt, erw_entry->assoc_cnt,
		erw_entry->reassoc_cnt, erw_entry->reject_mode, erw_entry->idx_mask);
}

static void qdrv_wlan_output_erw_nr_table(struct seq_file *s, struct ieee80211vap *vap)
{
	int i, j, title_output = 1;
	struct ieee80211_bsa_erw_nr_ie *erw_nr = NULL;

	if (!s || !vap)
		return;

	for (i = 0; i < IEEE80211_MAX_NEIGH_BSS; i++) {
		erw_nr = (struct ieee80211_bsa_erw_nr_ie *)vap->bsa_erw_nr_tbl[i];
		if (!erw_nr)
			continue;

		if (title_output) {
			seq_printf(s, "\n%-7s %-4s %-10s %s\n", "NR_IE", "ID", "IE length", "IE");
			title_output = 0;
		}

		seq_printf(s, "       %-5d %-10d", erw_nr->id, erw_nr->nr_ie_len);
		for (j = 0; j < erw_nr->nr_ie_len; j++)
			seq_printf(s, " %2.2x", erw_nr->nr_ie[j]);
		seq_printf(s, "\n");
	}
	seq_printf(s, "\n");
}

static void qdrv_show_erw_content_ie(struct seq_file *s, struct ieee80211vap *vap,
						struct ieee80211_bsa_erw_content_ie *ie)
{
	int i;

	if (!s || !vap || !ie)
		return;

	seq_printf(s, "IE(%2.2x):  mode=%2.2x offset=%u present=%u subie_offset=%u num_subie=%u idx_mask=%8.8x match_len=%u",
			ie->ie_id, ie->reject_mode, ie->match_offset,
			ie->ie_present, ie->subel_offset, ie->num_subie, ie->idx_mask,
			ie->match_len);

	if (ie->match_len > 0) {
		seq_printf(s, "%s", " match={");
		for (i = 0; i < ie->match_len; i++) {
			seq_printf(s, "%#x", ie->match[i]);
			if (i < (ie->match_len - 1))
				seq_puts(s, ",");
		}
		seq_puts(s, "}");
	}
	seq_puts(s, "\n");
}

static void qdrv_show_erw_content_subie(struct seq_file *s, struct ieee80211vap *vap,
						struct ieee80211_bsa_erw_content_subie *subie)
{
	int i;

	if (!s || !vap || !subie)
		return;

	seq_printf(s, "SUBIE(%2.2x): mode=%2.2x offset=%u present=%u match_type=%u idx_mask=%8.8x match_len=%u",
		subie->subie_id, subie->reject_mode, subie->match_offset,
		subie->subie_present, subie->match_type, subie->idx_mask, subie->match_len);

	if (subie->match_len > 0) {
		seq_printf(s, "%s", " match={");
		for (i = 0; i < subie->match_len; i++) {
			seq_printf(s, "%#x", subie->match[i]);
			if (i < (subie->match_len - 1))
				seq_puts(s, ",");
		}
		seq_puts(s, "}");
	}
	seq_puts(s, "\n");
}

static void qdrv_wlan_show_erw_entry_content(struct seq_file *s, struct ieee80211vap *vap,
						struct ieee80211_bsa_erw_entry *erw_entry)
{
	struct ieee80211_bsa_erw_content_ie *erw_ie = NULL;
	struct ieee80211_bsa_erw_content_subie *erw_subie = NULL;

	if (!erw_entry)
		return;
	if (TAILQ_EMPTY(&erw_entry->ie_missing_list) &&
			TAILQ_EMPTY(&erw_entry->ie_present_list))
		return;

	seq_printf(s, "Entry %pM\n", erw_entry->mac_addr);

	if (!TAILQ_EMPTY(&erw_entry->ie_missing_list)) {
		seq_puts(s, "IEs_missing[]:\n");
		TAILQ_FOREACH(erw_ie, &erw_entry->ie_missing_list, ie_entry) {
			qdrv_show_erw_content_ie(s, vap, erw_ie);
			if (!TAILQ_EMPTY(&erw_ie->subie_missing_list)) {
				seq_puts(s, "SubEls_missing[]:\n");
				TAILQ_FOREACH(erw_subie, &erw_ie->subie_missing_list, subie_entry)
					qdrv_show_erw_content_subie(s, vap, erw_subie);
			}
			if (!TAILQ_EMPTY(&erw_ie->subie_present_list)) {
				seq_puts(s, "SubEls_present[]:\n");
				TAILQ_FOREACH(erw_subie, &erw_ie->subie_present_list, subie_entry)
					qdrv_show_erw_content_subie(s, vap, erw_subie);
			}
		}
	}

	if (!TAILQ_EMPTY(&erw_entry->ie_present_list)) {
		seq_puts(s, "IEs_present[]:\n");
		TAILQ_FOREACH(erw_ie, &erw_entry->ie_present_list, ie_entry) {
			qdrv_show_erw_content_ie(s, vap, erw_ie);
			if (!TAILQ_EMPTY(&erw_ie->subie_missing_list)) {
				seq_puts(s, "SubEls_missing[]:\n");
				TAILQ_FOREACH(erw_subie, &erw_ie->subie_missing_list, subie_entry)
					qdrv_show_erw_content_subie(s, vap, erw_subie);
			}
			if (!TAILQ_EMPTY(&erw_ie->subie_present_list)) {
				seq_puts(s, "SubEls_present[]:\n");
				TAILQ_FOREACH(erw_subie, &erw_ie->subie_present_list, subie_entry)
					qdrv_show_erw_content_subie(s, vap, erw_subie);
			}
		}
	}
}

static void qdrv_wlan_show_erw_content(struct seq_file *s,
				struct ieee80211vap *vap, struct qdrv_wlan *qw)
{
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211_bsa_erw_entry_list *erw_list = NULL;
	struct ieee80211_bsa_erw_table *erw_table = NULL;

	seq_printf(s, "%s:\n", "ERW CONTENT");
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		erw_table = (struct ieee80211_bsa_erw_table *)vap->bsa_erw_sta_tbl;
		if (!erw_table || (TAILQ_EMPTY(&erw_table->erw_list) &&
						!erw_table->erw_wildcard_status))
			continue;

		TAILQ_FOREACH(erw_list, &erw_table->erw_list, erw_entry_list)
			qdrv_wlan_show_erw_entry_content(s, vap, &erw_list->erw_entry);
		if (erw_table->erw_wildcard_status)
			qdrv_wlan_show_erw_entry_content(s, vap, &erw_table->erw_wildcard_entry);
	}
}

static void qdrv_wlan_show_bsa_erw_stats(struct seq_file *s, void *data, u32 num)
{
	struct qdrv_wlan *qw = data;
	struct ieee80211com *ic = &qw->ic;
	struct ieee80211vap *vap;
	struct ieee80211_bsa_erw_entry_list *erw_list = NULL;
	struct ieee80211_bsa_erw_table *erw_table = NULL;
	int is_first;

	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		spin_lock(&vap->bsa_erw_sta_tbl_lock);

		erw_table = (struct ieee80211_bsa_erw_table *)vap->bsa_erw_sta_tbl;
		if (!erw_table || (TAILQ_EMPTY(&erw_table->erw_list) && !erw_table->erw_wildcard_status)) {
			spin_unlock(&vap->bsa_erw_sta_tbl_lock);
			continue;
		}

		seq_printf(s, "%-7s %-17s %11s %7s %8s %8s %7s %15s %15s %12s %12s %12s %12s %8s %8s\n",
			"VAP", "MAC", "Select", "Mode", "RSSI_MIN", "RSSI_MAX", "CONTENT",
			"IEs_missing_num", "IEs_present_num",
			"Probe_ERW", "AUTH_ERW", "ASSOC_ERW", "REASSOC_ERW", "REJ_MODE", "NR_MASK");

		is_first = 1;

		TAILQ_FOREACH(erw_list, &erw_table->erw_list, erw_entry_list) {
			qdrv_wlan_output_erw_entry(s, vap, &erw_list->erw_entry, is_first);
			is_first = 0;
		}

		if (erw_table->erw_wildcard_status) {
			qdrv_wlan_output_erw_entry(s, vap, &erw_table->erw_wildcard_entry, is_first);
		}

		qdrv_wlan_output_erw_nr_table(s, vap);

		qdrv_wlan_show_erw_content(s, vap, qw);

		spin_unlock(&vap->bsa_erw_sta_tbl_lock);
	}
}

int qdrv_wlan_get_bsa_erw_stats(void *data)
{
	qdrv_control_set_show(qdrv_wlan_show_bsa_erw_stats, data, 1, 1);

	return 0;
}

#endif
