/**
 * Copyright (c) 2013 - 2017 Quantenna Communications Inc
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#ifndef _AUC_SHARE_DEF_H_
#define _AUC_SHARE_DEF_H_

/* Define how many TIDs have and which is timer TID */
#define AUC_TID_FIRST			0
#define AUC_TID_NUM			20
#define AUC_TID_TIMER			19

#ifndef __ASSEMBLY__
/* WMAC parameters */
#define AUC_FW_WMAC_TX_QNUM		4
#define AUC_FW_WMAC_TX_QDEEP		4

#define AUC_FW_WMAC_RX_Q_MGMT		0
#define AUC_FW_WMAC_RX_Q_CTRL		1
#define AUC_FW_WMAC_RX_Q_DATA		2
#define AUC_FW_WMAC_RX_QNUM		3
#define AUC_FW_WMAC_RX_QDEEP_MGMT		8
#define AUC_FW_WMAC_RX_QDEEP_CTRL		8
#define AUC_FW_WMAC_RX_QDEEP_DATA		64
#define AUC_FW_WMAC_RX_DESC_NUM	(AUC_FW_WMAC_RX_QDEEP_MGMT + \
	AUC_FW_WMAC_RX_QDEEP_CTRL + AUC_FW_WMAC_RX_QDEEP_DATA)
#endif

/* Used to define 'state' field of qtn_auc_per_tid_data */
#define QTN_AUC_TID_TX_STATE_IDLE			0		/* idle state, this is init state, please keep zero value */
#define QTN_AUC_TID_TX_STATE_RUN			1		/* sending state */
#define QTN_AUC_TID_TX_STATE_WAIT_TIMER_AGG		2		/* waiting on agg timer firing */
#define QTN_AUC_TID_TX_STATE_WAIT_TX_DONE		3		/* waiting on tx done firing */
#define QTN_AUC_TID_TX_STATE_WAIT_TX_RESUME		4		/* waiting on ocs tx resume */
#define QTN_AUC_TID_TX_STATE_MAX			4		/* maximum value of tx states */

/* Used to define 'tqew_state' field of qtn_auc_per_tid_data */
#define QTN_AUC_TID_TQEW_STATE_RUN			0
#define QTN_AUC_TID_TQEW_STATE_WAIT_TX_DONE		1
#define QTN_AUC_TID_TQEW_STATE_MAX			1

#endif // #ifndef _AUC_SHARE_DEF_H_

