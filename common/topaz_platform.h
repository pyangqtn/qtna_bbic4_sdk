/*
 * (C) Copyright 2010 Quantenna Communications Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * Header file which describes Topaz platform.
 * Has to be used by both kernel and bootloader.
 */

#ifndef __TOPAZ_PLATFORM_H
#define __TOPAZ_PLATFORM_H

#include "ruby_platform.h"
#include "qtn_bits.h"

/* Extra reset bits */
#define TOPAZ_SYS_CTL_RESET_AUC		(RUBY_BIT(10))
#define TOPAZ_SYS_CTL_ALIAS_MAP		(RUBY_BIT(29))
#define TOPAZ_SYS_CTL_UNIFIED_MAP	(RUBY_BIT(30))

/* Extra system controller bits */
#define TOPAZ_SYS_CTL_DDRCLK_S		22
#define TOPAZ_SYS_CTL_DDRCLK		(0x7 << TOPAZ_SYS_CTL_DDRCLK_S)
#define TOPAZ_SYS_CTL_DDRCLK_400MHZ	SM(0, TOPAZ_SYS_CTL_DDRCLK)
#define TOPAZ_SYS_CTL_DDRCLK_320MHZ	SM(1, TOPAZ_SYS_CTL_DDRCLK)
#define TOPAZ_SYS_CTL_DDRCLK_250MHZ	SM(2, TOPAZ_SYS_CTL_DDRCLK)
#define TOPAZ_SYS_CTL_DDRCLK_200MHZ	SM(3, TOPAZ_SYS_CTL_DDRCLK)
#define TOPAZ_SYS_CTL_DDRCLK_160MHZ	SM(4, TOPAZ_SYS_CTL_DDRCLK)

/* Extra system controller registers */
#define TOPAZ_SYS_CTL_RESET_CAUSE	(RUBY_SYS_CTL_BASE_ADDR + 0x10)
#define TOPAZ_SYS_CTL_M2D_2_INT		(RUBY_SYS_CTL_BASE_ADDR + 0x0184)
#define TOPAZ_SYS_CTL_M2D_2_INT_MASK	(RUBY_SYS_CTL_BASE_ADDR + 0x0188)
#define TOPAZ_SYS_CTL_M2D_3_INT		(RUBY_SYS_CTL_BASE_ADDR + 0x018C)
#define TOPAZ_SYS_CTL_M2D_3_INT_MASK	(RUBY_SYS_CTL_BASE_ADDR + 0x0190)

/* Temperature control registers */
#define TOPAZ_SYS_CTL_TEMPSENS_CTL	(RUBY_SYS_CTL_BASE_ADDR + 0x0108)
#define TOPAZ_SYS_CTL_TEMPSENS_CTL_START_CONV		0x00000001
#define TOPAZ_SYS_CTL_TEMPSENS_CTL_SHUTDWN		0x00000002

#define TOPAZ_SYS_CTL_TEMP_SENS_TEST_CTL		(RUBY_SYS_CTL_BASE_ADDR + 0x010C)

#define TOPAZ_SYS_CTL_TEMP_SENS_DATA			(RUBY_SYS_CTL_BASE_ADDR + 0x0110)
#define TOPAZ_SYS_CTL_TEMP_SENS_DATA_TEMP		0x00000FFF
#define TOPAZ_SYS_CTL_TEMP_SENS_DATA_END_CONV		0x00001000
#define TOPAZ_SYS_CTL_TEMP_SENS_DATA_END_CONV_S		11

/* AuC SoC interrupt controller registers */
#define TOPAZ_AUC_INT_MASK		(RUBY_SYS_CTL_BASE_ADDR + 0x0174)
#define TOPAZ_AUC_IPC_INT		(RUBY_SYS_CTL_BASE_ADDR + 0x0178)
#define TOPAZ_AUC_IPC_INT_MASK(val)	((val & 0xFFFF) << 16)
#define TOPAZ_AUC_INT_STATUS		(RUBY_SYS_CTL_BASE_ADDR + 0x00D0)

/* Linux Host interrupt controller registers */
#define TOPAZ_LH_IPC3_INT		(RUBY_SYS_CTL_BASE_ADDR + 0x14C)
#define TOPAZ_LH_IPC3_INT_MASK		(RUBY_SYS_CTL_BASE_ADDR + 0x150)
#define TOPAZ_IPC4_INT(base)	((base) + 0x13C)
#define TOPAZ_IPC4_INT_MASK(base)	((base) + 0x140)
#define TOPAZ_LH_IPC4_INT		(TOPAZ_IPC4_INT(RUBY_SYS_CTL_BASE_ADDR))
#define TOPAZ_LH_IPC4_INT_MASK		(TOPAZ_IPC4_INT_MASK(RUBY_SYS_CTL_BASE_ADDR))

/* Multi-processor hardware semahpore */
#define TOPAZ_MPROC_SEMA		(RUBY_SYS_CTL_BASE_ADDR + 0x0170)

/* MuC SoC Interrupt controller registers */
#define TOPAZ_SYS_CTL_A2M_INT		(RUBY_SYS_CTL_BASE_ADDR + 0x0144)
#define TOPAZ_SYS_CTL_A2M_INT_MASK	(RUBY_SYS_CTL_BASE_ADDR + 0x0148)

/* PCIE SoC Interrupt controller registers */
#define TOPAZ_SYS_CTL_PCIE_INT_STATUS	(RUBY_SYS_CTL_BASE_ADDR + 0x017c)
#define TOPAZ_SYS_CTL_TQE_INT_STATS_BIT	(RUBY_BIT(10))

#define TOPAZ_SWITCH_BASE_ADDR		0xE1000000
#define TOPAZ_SWITCH_OUT_NODE_BITS	7		/* Up to 128 output nodes */
#define TOPAZ_SWITCH_OUT_NODE_MAX	(1 << TOPAZ_SWITCH_OUT_NODE_BITS)
#define TOPAZ_SWITCH_OUT_NODE_MASK	((1 << TOPAZ_SWITCH_OUT_NODE_BITS) - 1)
#define TOPAZ_SWITCH_OUT_PORT_BITS	4		/* Up to 16 output ports. 8 are used */
#define TOPAZ_SWITCH_OUT_PORT_MAX	(1 << TOPAZ_SWITCH_OUT_PORT_BITS)
#define TOPAZ_SWITCH_OUT_PORT_MASK	((1 << TOPAZ_SWITCH_OUT_PORT_BITS) - 1)

/* TQE */
#define TOPAZ_TQE_BASE_ADDR		(TOPAZ_SWITCH_BASE_ADDR + 0x30000)
#define TOPAZ_TQE_EMAC_TDES_1_CNTL	(TOPAZ_TQE_BASE_ADDR + 0x0000)
#define TOPAZ_TQE_EMAC_TDES_1_CNTL_VAL				0x000000FF
#define TOPAZ_TQE_EMAC_TDES_1_CNTL_VAL_S			0
#define TOPAZ_TQE_EMAC_TDES_1_CNTL_SHIFT			24	/* reg bits [7:0] become emac ctrl [31:24] */
#define TOPAZ_TQE_EMAC_TDES_1_CNTL_MCAST_APPEND_CNTR_EN_S	16
#define TOPAZ_TQE_EMAC_TDES_1_CNTL_MCAST_APPEND_CNTR_EN		0x00010000
#define TOPAZ_TQE_EMAC_TDES_1_CNTL_PRI_MODE			0x0F000000
#define TOPAZ_TQE_EMAC_TDES_1_CNTL_PRI_MODE_S			24
#define TOPAZ_TQE_MISC			(TOPAZ_TQE_BASE_ADDR + 0x0004)
#define TOPAZ_TQE_MISC_CLR_DONE_DLY_CYCLE_NUM		0x000003FF	/* q_avail_clr_done delay cycles */
#define TOPAZ_TQE_MISC_CLR_DONE_DLY_CYCLE_NUM_S		0
#define TOPAZ_TQE_MISC_RFLCT_OUT_PORT			0x000F0000	/* dest port for reflected pkts */
#define TOPAZ_TQE_MISC_RFLCT_OUT_PORT_S			16
#define TOPAZ_TQE_MISC_RFLCT_OUT_PORT_ENABLE		0x00100000	/* redirect emac0<->emac0 or emac1<->emac1 reflected pkts */
#define TOPAZ_TQE_MISC_RFLCT_OUT_PORT_ENABLE_S		20
#define TOPAZ_TQE_MISC_RFLCT_2_OUT_PORT_ENABLE		0x00200000	/* redirect emac0<->emac0/emac1 or emac1<->emac0/emac1 reflected pkts */
#define TOPAZ_TQE_MISC_RFLCT_2_OUT_PORT_ENABLE_S	21
#define TOPAZ_TQE_MISC_CLR_DONE_DLY_ENABLE		0x80000000	/* enable q_avail_clr_done delay */
#define TOPAZ_TQE_MISC_CLR_DONE_DLY_ENABLE_S		31
#define TOPAZ_TQE_WMAC_Q_STATUS_PTR	(TOPAZ_TQE_BASE_ADDR + 0x0008)
#define TOPAZ_TQE_CPU_SEM		(TOPAZ_TQE_BASE_ADDR + 0x000c)
#define TOPAZ_TQE_OUTPORT_EMAC0_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0010)
#define TOPAZ_TQE_OUTPORT_EMAC1_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0014)
#define TOPAZ_TQE_OUTPORT_WMAC_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0018)
#define TOPAZ_TQE_OUTPORT_LHOST_CNT	(TOPAZ_TQE_BASE_ADDR + 0x001c)
#define TOPAZ_TQE_OUTPORT_MUC_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0020)
#define TOPAZ_TQE_OUTPORT_DSP_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0024)
#define TOPAZ_TQE_OUTPORT_AUC_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0028)
#define TOPAZ_TQE_OUTPORT_PCIE_CNT	(TOPAZ_TQE_BASE_ADDR + 0x002c)
#define TOPAZ_TQE_Q_AVAIL_CLR_CNTL	(TOPAZ_TQE_BASE_ADDR + 0x0030)
#define TOPAZ_TQE_Q_AVAIL_CLR_CNTL_TID		0xF
#define TOPAZ_TQE_Q_AVAIL_CLR_CNTL_TID_S	0
#define TOPAZ_TQE_Q_AVAIL_CLR_CNTL_NODE		0x7F00
#define TOPAZ_TQE_Q_AVAIL_CLR_CNTL_NODE_S	8
#define TOPAZ_TQE_Q_AVAIL_CLR_CNTL_CLEAR	RUBY_BIT(30)
#define TOPAZ_TQE_Q_AVAIL_CLR_CNTL_CLEAR_DONE	RUBY_BIT(31)
#define TOPAZ_TQE_DROP_CNT		(TOPAZ_TQE_BASE_ADDR + 0x0034)
#define TOPAZ_TQE_DROP_EMAC0_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0040)
#define TOPAZ_TQE_DROP_EMAC1_CNT	(TOPAZ_TQE_BASE_ADDR + 0x0044)
#define TOPAZ_TQE_DROP_WMAC_CNT		(TOPAZ_TQE_BASE_ADDR + 0x0048)
#define TOPAZ_TQE_DROP_LHOST_CNT	(TOPAZ_TQE_BASE_ADDR + 0x004c)
#define TOPAZ_TQE_DROP_MUC_CNT		(TOPAZ_TQE_BASE_ADDR + 0x0050)
#define TOPAZ_TQE_DROP_DSP_CNT		(TOPAZ_TQE_BASE_ADDR + 0x0054)
#define TOPAZ_TQE_DROP_AUC_CNT		(TOPAZ_TQE_BASE_ADDR + 0x0058)
#define TOPAZ_TQE_DROP_PCIE_CNT		(TOPAZ_TQE_BASE_ADDR + 0x005c)

/* TQE-CPU interface */
#define TOPAZ_TQE_CPUIF_BASE(num)		(TOPAZ_TQE_BASE_ADDR + 0x4000 + 0x1000 * (num))	// For FPGA build 72 and earlier need to use (0xE1040000 + 0x10000 * (num))
#define TOPAZ_TQE_CPUIF_CSR(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0000)
#define TOPAZ_TQE_CPUIF_RX_RING_SIZE(num)	(TOPAZ_TQE_CPUIF_BASE(num) + 0x0004)
#define TOPAZ_TQE_CPUIF_RX_RING(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0008)
#define TOPAZ_TQE_CPUIF_RX_CURPTR(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x000c)
#define TOPAZ_TQE_CPUIF_PKT_FINISH(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0010)
#define TOPAZ_TQE_CPUIF_Q_PTR_STATUS(num)	(TOPAZ_TQE_CPUIF_BASE(num) + 0x0014)
#define TOPAZ_TQE_CPUIF_PPCTL0(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0020)
#define TOPAZ_TQE_CPUIF_PPCTL1(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0024)
#define TOPAZ_TQE_CPUIF_PPCTL2(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0028)
#define TOPAZ_TQE_CPUIF_PPCTL3(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x002c)
#define TOPAZ_TQE_CPUIF_PPCTL4(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0030)
#define TOPAZ_TQE_CPUIF_PPCTL5(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0034)
#define TOPAZ_TQE_CPUIF_TXSTART(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x0038)
#define TOPAZ_TQE_CPUIF_STATUS(num)		(TOPAZ_TQE_CPUIF_BASE(num) + 0x003C)
/* Some bits definitions */
#define TOPAZ_TQE_CPUIF_CSR_IRQ_EN		RUBY_BIT(0)
#define TOPAZ_TQE_CPUIF_CSR_IRQ_THRESHOLD(num)	(((num) & 0x7F) << 8)
#define TOPAZ_TQE_CPUIF_CSR_IRQ_THRESHOLD_EN	RUBY_BIT(15)
#define TOPAZ_TQE_CPUIF_CSR_RESET		RUBY_BIT(31)
/* Aux definitions */
#define TOPAZ_TQE_CPUIF_RXDESC_ALIGN		8	/* TQE CPU rx descriptors must be 64 bit aligned */

/**
 * Hardware Buffer Manager
 */
#define TOPAZ_HBM_BASE_ADDR		(TOPAZ_SWITCH_BASE_ADDR + 0x20000)
#define TOPAZ_HBM_END_ADDR		(TOPAZ_HBM_BASE_ADDR + 0x200)

#define TOPAZ_HBM_CSR_REG		(TOPAZ_HBM_BASE_ADDR + 0x0000)
#define TOPAZ_HBM_CSR_Q_EN(x)		(BIT(0 + (x)))
#define TOPAZ_HBM_CSR_INT_EN		(BIT(7))
#define TOPAZ_HBM_CSR_OFLOW_INT_MASK(x)	(BIT(8 + (x)))
#define TOPAZ_HBM_CSR_UFLOW_INT_MASK(x)	(BIT(12 + (x)))
#define TOPAZ_HBM_CSR_OFLOW_INT_RAW(x)	(BIT(16 + (x)))
#define TOPAZ_HBM_CSR_UFLOW_INT_RAW(x)	(BIT(20 + (x)))
#define TOPAZ_HBM_CSR_INT_MSK_RAW	(0xff << 16)
#define TOPAZ_HBM_CSR_OFLOW_INT_STATUS(x) (BIT(24 + (x)))
#define TOPAZ_HBM_CSR_UFLOW_INT_STATUS(x) (BIT(28 + (x)))

#define TOPAZ_HBM_BASE_REG(x)		(TOPAZ_HBM_BASE_ADDR + 0x0004 + ((x) * 0x10))
#define TOPAZ_HBM_LIMIT_REG(x)		(TOPAZ_HBM_BASE_ADDR + 0x0008 + ((x) * 0x10))
#define TOPAZ_HBM_WR_PTR(x)		(TOPAZ_HBM_BASE_ADDR + 0x000c + ((x) * 0x10))
#define TOPAZ_HBM_RD_PTR(x)		(TOPAZ_HBM_BASE_ADDR + 0x0010 + ((x) * 0x10))

#define TOPAZ_HBM_POOL(x)		(TOPAZ_HBM_BASE_ADDR + 0x0100 + ((x) * 0x4))
#define TOPAZ_HBM_POOL_REQ(x)		(TOPAZ_HBM_BASE_ADDR + 0x0110 + ((x) * 0x4))
#define TOPAZ_HBM_POOL_DATA(x)		(TOPAZ_HBM_BASE_ADDR + 0x0140 + ((x) * 0x4))

#define TOPAZ_HBM_OVERFLOW_CNT		(TOPAZ_HBM_BASE_ADDR + 0x0190)
#define TOPAZ_HBM_UNDERFLOW_CNT		(TOPAZ_HBM_BASE_ADDR + 0x0194)

#define TOPAZ_HBM_MASTER_COUNT				9
#define TOPAZ_HBM_POOL_COUNT				4
#define TOPAZ_HBM_POOL_REQUEST_CNT(master, pool)	(TOPAZ_HBM_BASE_ADDR + 0x0200 + (master) * 0x20 + (pool) * 0x4)
#define TOPAZ_HBM_POOL_RELEASE_CNT(master, pool)	(TOPAZ_HBM_BASE_ADDR + 0x0210 + (master) * 0x20 + (pool) * 0x4)

#define TOPAZ_HBM_RELEASE_BUF		(BIT(0))
#define TOPAZ_HBM_REQUEST_BUF		(BIT(1))
#define TOPAZ_HBM_POOL_NUM(x)		((x) << 2)
#define TOPAZ_HBM_DONE			(BIT(31))

#define TOPAZ_PACKET_MEMORY_BASE_ADDR		(0xE5030000)
#define TOPAZ_PACKET_MEMORY_END_ADDR		(TOPAZ_PACKET_MEMORY_BASE_ADDR + 0xFF)

#define TOPAZ_TCM_BASE_ADDR			(0xE5040000)
#define TOPAZ_TCM_END_ADDR			(TOPAZ_TCM_BASE_ADDR + 0x3FFF)

#define TOPAZ_GLOBAL_CONTROL_BASE_ADDR		(0xE5044000)
#define TOPAZ_GLOBAL_CONTROL_END_ADDR		(TOPAZ_GLOBAL_CONTROL_BASE_ADDR + 0xFF)

#define TOPAZ_TX_FRAME_PROC_BASE_ADDR		(0xE5050000)
#define TOPAZ_TX_FRAME_PROC_END_ADDR		(TOPAZ_TX_FRAME_PROC_BASE_ADDR + 0x1FFF)

#define TOPAZ_RX_FRAME_PROC_BASE_ADDR		(0xE5052000)
#define TOPAZ_RX_FRAME_PROC_END_ADDR		(TOPAZ_RX_FRAME_PROC_BASE_ADDR + 0xFFF)

#define TOPAZ_SHARED_FRAME_PROC_BASE_ADDR	(0xE5053000)
#define TOPAZ_SHARED_FRAME_PROC_END_ADDR	(TOPAZ_SHARED_FRAME_PROC_BASE_ADDR + 0xFFF)

#define TOPAZ_GLOBAL_CONTROL_REGS_BASE_ADDR	(0xE5090000)
#define TOPAZ_GLOBAL_CONTROL_REGS_END_ADDR	(TOPAZ_GLOBAL_CONTROL_REGS_BASE_ADDR + 0x4FF)

/* SoC interrupts */
#define TOPAZ_SYS_CTL_M2L_HI_INT	PLATFORM_REG_SWITCH(RUBY_SYS_CTL_M2L_INT, (RUBY_SYS_CTL_BASE_ADDR + 0xFC))

/**
 * Forwarding Table
 */
#define TOPAZ_FWT_BASE_ADDR		(TOPAZ_SWITCH_BASE_ADDR + 0x0)
#define TOPAZ_FWT_SIZE			(BIT(12))

#define TOPAZ_FWT_TABLE_BASE		(TOPAZ_FWT_BASE_ADDR)

#define TOPAZ_FWT_VLAN_TABLE_BASE	(TOPAZ_FWT_BASE_ADDR + 0x10000)
#define TOPAZ_FWT_VLAN_TABLE_LIMIT	(TOPAZ_FWT_BASE_ADDR + 0x14000)

#define TOPAZ_FWT_CTRL_BASE_ADDR	(TOPAZ_FWT_BASE_ADDR + 0xA000)

#define TOPAZ_FWT_CPU_ACCESS		(TOPAZ_FWT_CTRL_BASE_ADDR + 0x0000)
#define TOPAZ_FWT_CPU_ACCESS_STATE	0x0000000F
#define TOPAZ_FWT_CPU_ACCESS_STATE_S	0
#define TOPAZ_FWT_CPU_ACCESS_STATE_GRANTED	0x3
#define TOPAZ_FWT_CPU_ACCESS_REQ	BIT(31)
#define TOPAZ_FWT_TIME_STAMP_CTRL	(TOPAZ_FWT_CTRL_BASE_ADDR + 0x0004)
#define TOPAZ_FWT_TIME_STAMP_CTRL_UNIT		0x0000001F
#define TOPAZ_FWT_TIME_STAMP_CTRL_UNIT_S	0
#define TOPAZ_FWT_TIME_STAMP_CTRL_SCALE		0x000003e0
#define TOPAZ_FWT_TIME_STAMP_CTRL_SCALE_S	5
#define TOPAZ_FWT_TIME_STAMP_DIS_AUTO_UPDATE_S	(16)
#define TOPAZ_FWT_TIME_STAMP_CTRL_CLEAR		BIT(31)
#define TOPAZ_FWT_TIME_STAMP_CNT	(TOPAZ_FWT_CTRL_BASE_ADDR + 0x0008)
#define TOPAZ_FWT_HASH_CTRL		(TOPAZ_FWT_CTRL_BASE_ADDR + 0x000c)
#define TOPAZ_FWT_HASH_CTRL_ENABLE	BIT(15)

#define TOPAZ_FWT_LOOKUP_LHOST		0
#define TOPAZ_FWT_LOOKUP_MUC		1
#define TOPAZ_FWT_LOOKUP_DSP		2
#define TOPAZ_FWT_LOOKUP_AUC		3

#define	__TOPAZ_FWT_LOOKUP_REG(x)	(TOPAZ_FWT_CTRL_BASE_ADDR + 0x0010 + ((x) * 0x10))
#define	__TOPAZ_FWT_LOOKUP_MAC_LO(x)	(TOPAZ_FWT_CTRL_BASE_ADDR + 0x0014 + ((x) * 0x10))
#define	__TOPAZ_FWT_LOOKUP_MAC_HI(x)	(TOPAZ_FWT_CTRL_BASE_ADDR + 0x0018 + ((x) * 0x10))

#define TOPAZ_FWT_LOOKUP_TRIG		0x00000001
#define TOPAZ_FWT_LOOKUP_TRIG_S		0
#define TOPAZ_FWT_LOOKUP_ENTRY_ADDR	0x7FF00000
#define TOPAZ_FWT_LOOKUP_ENTRY_ADDR_S	20
#define TOPAZ_FWT_LOOKUP_HASH_ADDR	0x0003FF00
#define TOPAZ_FWT_LOOKUP_HASH_ADDR_S	8
#define TOPAZ_FWT_LOOKUP_VALID		0x80000000
#define TOPAZ_FWT_LOOKUP_VALID_S	31

#define TOPAZ_FWT_PORT_EMAC0		(0)
#define TOPAZ_FWT_PORT_EMAC1		(1)
#define TOPAZ_FWT_PORT_WMAC		(2)
#define TOPAZ_FWT_PORT_PCIE		(3)
#define TOPAZ_FWT_PORT_LH		(4)
#define TOPAZ_FWT_PORT_MUC		(5)
#define TOPAZ_FWT_PORT_DSP		(6)
#define TOPAZ_FWT_PORT_AUC		(7)

#define TOPAZ_FWT_ENTRY_NXT_ENTRY	0x0FFE0000
#define TOPAZ_FWT_ENTRY_NXT_ENTRY_S	17
#define TOPAZ_FWT_ENTRY_VALID		0x80000000
#define TOPAZ_FWT_ENTRY_VALID_S		31
#define TOPAZ_FWT_ENTRY_PORTAL		0x40000000
#define TOPAZ_FWT_ENTRY_PORTAL_S	30

#define TOPAZ_FWT_ENTRY_OUT_NODE_0		0x0000007F
#define TOPAZ_FWT_ENTRY_OUT_NODE_0_S		0
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_0		0x00000080
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_0_S	7
#define TOPAZ_FWT_ENTRY_OUT_NODE_1		0x00007F00
#define TOPAZ_FWT_ENTRY_OUT_NODE_1_S		8
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_1		0x00008000
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_1_S	15
#define TOPAZ_FWT_ENTRY_OUT_NODE_2		0x007F0000
#define TOPAZ_FWT_ENTRY_OUT_NODE_2_S		16
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_2		0x00800000
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_2_S	23
#define TOPAZ_FWT_ENTRY_OUT_NODE_3		0x7F000000
#define TOPAZ_FWT_ENTRY_OUT_NODE_3_S		24
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_3		0x80000000
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_3_S	31
#define TOPAZ_FWT_ENTRY_OUT_NODE_4		0x007F0000
#define TOPAZ_FWT_ENTRY_OUT_NODE_4_S		16
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_4		0x00800000
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_4_S	23
#define TOPAZ_FWT_ENTRY_OUT_NODE_5		0x7F000000
#define TOPAZ_FWT_ENTRY_OUT_NODE_5_S		24
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_5		0x80000000
#define TOPAZ_FWT_ENTRY_OUT_NODE_VLD_5_S	31

#define TOPAZ_FWT_ENTRY_OUT_PORT	0x00003C00
#define TOPAZ_FWT_ENTRY_OUT_PORT_S	10

#define TOPAZ_FWT_ENTRY_TIMESTAMP	0x000003FF
#define TOPAZ_FWT_ENTRY_TIMESTAMP_S	0

#define TOPAZ_FWT_HW_HASH_SHIFT		10
#define TOPAZ_FWT_HW_HASH_MASK		((1 << TOPAZ_FWT_HW_HASH_SHIFT) - 1)
#define TOPAZ_FWT_HW_LEVEL1_ENTRIES	(1 << TOPAZ_FWT_HW_HASH_SHIFT)
#define TOPAZ_FWT_HW_LEVEL2_ENTRIES	1024
#define TOPAZ_FWT_HW_TOTAL_ENTRIES	(TOPAZ_FWT_HW_LEVEL1_ENTRIES + TOPAZ_FWT_HW_LEVEL2_ENTRIES)

/*
 * VLAN table
 */
#define TOPAZ_VLAN_BASE_ADDR		(TOPAZ_SWITCH_BASE_ADDR + 0x10000)
#define TOPAZ_VLAN_ENTRIES		(1 << 12)	/* 802.1Q VLAN ID */
#define TOPAZ_VLAN_ENTRY_ADDR(x)	(TOPAZ_VLAN_BASE_ADDR + 4 * (x))
#define TOPAZ_VLAN_OUT_NODE		0x0000007F
#define TOPAZ_VLAN_OUT_NODE_S		0
#define TOPAZ_VLAN_OUT_PORT		0x00000380
#define TOPAZ_VLAN_OUT_PORT_S		7
#define TOPAZ_VLAN_VALID		0x00000400
#define TOPAZ_VLAN_VALID_S		10
#define TOPAZ_VLAN_HW_BITMASK		0x000007ff

/* TX AGG */
#define TOPAZ_TX_AGG_BASE_ADDR				0xE5090000
#define TOPAZ_TX_AGG_NODE_N_TID_Q_AVAIL(node)		(TOPAZ_TX_AGG_BASE_ADDR + 0x000 + 4 * (node))
#define TOPAZ_TX_AGG_NODE_N_TID_Q_AVAIL_MASK(val)	((val & 0xFFFF) << 16)
#define TOPAZ_TX_AGG_NODE_N_TID_Q_AVAIL_SUP(node)	(TOPAZ_TX_AGG_BASE_ADDR + 0x200 + 4 * (node))
#define TOPAZ_TX_AGG_NODE_N_TID_Q_AVAIL_SUP_MASK(val)	((val & 0xFFFF) << 16)
#define TOPAZ_TX_AGG_CSR				(TOPAZ_TX_AGG_BASE_ADDR + 0x460)
#define TOPAZ_TX_AGG_TAC_MAP_MODE_64			0
#define TOPAZ_TX_AGG_TAC_MAP_MODE_128			1
#define TOPAZ_TX_AGG_AC					0xF0000000
#define TOPAZ_TX_AGG_AC_S				28
#define TOPAZ_TX_AGG_CPU_Q_ACCESS_SEM			(TOPAZ_TX_AGG_BASE_ADDR + 0x464)
#define TOPAZ_TX_AGG_UC_Q_ACCESS_SEM			(TOPAZ_TX_AGG_BASE_ADDR + 0x468)
#define TOPAZ_TX_AGG_TAC_CNTL				(TOPAZ_TX_AGG_BASE_ADDR + 0x46C)
#ifdef TOPAZ_128_NODE_MODE
#define TOPAZ_TX_AGG_TAC_CNTL_NODE(node)		((node) & 0x7F)
#else
#define TOPAZ_TX_AGG_TAC_CNTL_NODE(node)		((node) & 0x3F)
#endif
#define TOPAZ_TX_AGG_TAC_CNTL_TID(tid)			(((tid) & 0xF) << 8)
#define TOPAZ_TX_AGG_TAC_CNTL_READ_CMD(cmd)		(((cmd) & 0x3) << 12)
#define TOPAZ_TX_AGG_TAC_CNTL_READ_DATA_VLD		RUBY_BIT(29)
#define TOPAZ_TX_AGG_TAC_CNTL_READ			RUBY_BIT(30)
#define TOPAZ_TX_AGG_TAC_CNTL_WRITE			RUBY_BIT(31)
#define TOPAZ_TX_AGG_TAC_DATA				(TOPAZ_TX_AGG_BASE_ADDR + 0x470)
#define TOPAZ_TX_AGG_TAC_DATA_AC(__ac)			((__ac) & 0x3)
#define TOPAZ_TX_AGG_TAC_DATA_PRIORITY(__pri)		(((__pri) & 0xFF) << 2)
#ifdef TOPAZ_128_NODE_MODE
#define TOPAZ_TX_AGG_TAC_DATA_AC_LO			0x00000003
#define TOPAZ_TX_AGG_TAC_DATA_AC_LO_S			0
#define TOPAZ_TX_AGG_TAC_DATA_PRIORITY_LO		0x0000001c
#define TOPAZ_TX_AGG_TAC_DATA_PRIORITY_LO_S		2
#define TOPAZ_TX_AGG_TAC_DATA_AC_HI			0x00000060
#define TOPAZ_TX_AGG_TAC_DATA_AC_HI_S			5
#define TOPAZ_TX_AGG_TAC_DATA_PRIORITY_HI		0x00000380
#define TOPAZ_TX_AGG_TAC_DATA_PRIORITY_HI_S		7
#endif
#define TOPAZ_TX_AGG_AC_N_NODE_TID(ac)			(TOPAZ_TX_AGG_BASE_ADDR + 0x478 + 4 * (ac))
#define TOPAZ_TX_AGG_AC_N_STAT_PTR(ac)			(TOPAZ_TX_AGG_BASE_ADDR + 0x488 + 4 * (ac))
#define TOPAZ_TX_AGG_Q_FULL_THRESH			(TOPAZ_TX_AGG_BASE_ADDR + 0x498)
#define TOPAZ_TX_AGG_Q_FULL_THRESH_VAL(q0, q1, q2, q3)	(((q0) & 0xF) | (((q1) & 0xF) << 4) | (((q2) & 0xF) << 8) | (((q3) & 0xF) << 12))
#define TOPAZ_TX_AGG_CPU_IRQ_CSR			(TOPAZ_TX_AGG_BASE_ADDR + 0x49C)
#define TOPAZ_TX_AGG_STATUS_IRQ				(TOPAZ_TX_AGG_BASE_ADDR + 0x4A0)
#define TOPAZ_TX_AGG_AC_N_NODE_TID_NO_SEL(ac)		(TOPAZ_TX_AGG_BASE_ADDR + 0x4A4 + 4 * (ac))
#define TOPAZ_TX_AGG_TAC_CNTL_READ_CMD_NODE_TAB		0
#define TOPAZ_TX_AGG_TAC_CNTL_READ_CMD_AVAIL_LO		1
#define TOPAZ_TX_AGG_TAC_CNTL_READ_CMD_AVAIL_HI		3
#define TOPAZ_TX_AGG_MAX_NODE_NUM			128
#define TOPAZ_TX_AGG_HALF_MAX_NODE_NUM			(TOPAZ_TX_AGG_MAX_NODE_NUM >> 1)

#define TOPAZ_MAC0_SVD_PM_BASE				(0xE5038000)

#define TOPAZ_MAC1_WMAC_PM_BASE				(0xE5130000)
#define TOPAZ_MAC1_WMAC_PM_END				(TOPAZ_MAC1_WMAC_PM_BASE + 0xFFF)

#define TOPAZ_MAC1_WMAC_TCM_BASE			(0xE5140000)
#define TOPAZ_MAC1_WMAC_TCM_END				(TOPAZ_MAC1_WMAC_TCM_BASE + 0xFFF)
/*
 * MuC/Lhost new interrupts.
 * Old interrupts (even changed number) are in ruby_platform, RUBY_IRQ_*
 */
#define	TOPAZ_IRQ_TQE					(5)
#define TOPAZ_IRQ_HDMA0					(RUBY_IRQ_DMA0)
#define TOPAZ_IRQ_HBM					(RUBY_IRQ_DMA1)
#define TOPAZ_IRQ_HDMA1					(RUBY_IRQ_DMA3)
#define TOPAZ_IRQ_PCIE					(28)
#define TOPAZ_IRQ_IPC_A2M				(18)
#define TOPAZ_IQR_TQE_DSP				(19)
#define	TOPAZ_IRQ_PCIE_DMA				(RUBY_IRQ_DMA2)
#define	TOPAZ_IRQ_IPC4					(29)
#define	TOPAZ_MUC_IRQ_BB_PER_PKT			(31)

#if defined(CONFIG_TOPAZ_FILTER_HBM_LEVEL2)
#define TOPAZ_IRQ_TIMER0_LEVEL1				(30)
#endif

#define TOPAZ_HBM_INT_EN				RUBY_BIT(31)
#define TOPAZ_PCIE_INTX_CLR_MASK			RUBY_BIT(11)
#define	TOPAZ_PCIE_INT_MASK				RUBY_PCIE_INT_MASK
#define	TOPAZ_PCIE_MSI_MASK				RUBY_PCIE_MSI_MASK
#define TOPAZ_PCIE_MSI_EN				RUBY_BIT(0)
#define TOPAZ_PCIE_MSI_BASE				0xE9000050
#define TOPAZ_PCIE_MSI_CAP				(TOPAZ_PCIE_MSI_BASE + 0x0)

#define TOPAZ_PCIE_EXP_DEVCTL				(0xE9000078)

/* MSI defines to be used in Topaz PCIe host driver */
#define	TOPAZ_PCIE_MSI_REGION				RUBY_PCIE_MSI_REGION
#define	TOPAZ_MSI_ADDR_LOWER				RUBY_MSI_ADDR_LOWER
#define	TOPAZ_MSI_ADDR_UPPER				RUBY_MSI_ADDR_UPPER
#define	TOPAZ_MSI_INT_ENABLE				RUBY_MSI_INT_ENABLE

/* AHB Bus monitors */
#define TOPAZ_BUSMON_INTR_STATUS			(RUBY_SYS_CTL_BASE_ADDR + 0x015c)
#define TOPAZ_BUSMON_INTR_MASK				(RUBY_SYS_CTL_BASE_ADDR + 0x0160)
#define TOPAZ_BUSMON_INTR_MASK_TIMEOUT_EN(master)	BIT((master) * 2 + 0)
#define TOPAZ_BUSMON_INTR_MASK_RANGE_CHECK_EN(master)	BIT((master) * 2 + 1)
#define TOPAZ_BUSMON_DEBUG_VIEW				(RUBY_SYS_CTL_BASE_ADDR + 0x0164)
#define TOPAZ_BUSMON_DEBUG_VIEW_MASTER(x)		(((x) & 0x3) << 0)
#define TOPAZ_BUSMON_DEBUG_VIEW_DATA_SEL(x)		(((x) & 0x7) << 2)
#define TOPAZ_BUSMON_DEBUG_STATUS			(RUBY_SYS_CTL_BASE_ADDR + 0x0168)
#define TOPAZ_BUSMON_CTL_BASE_ADDR			(RUBY_SYS_CTL_BASE_ADDR + 0x0200)
#define TOPAZ_BUSMON_CTL(core)				(TOPAZ_BUSMON_CTL_BASE_ADDR + ((core) * 0x40))
#define __TOPAZ_BUSMON_CTL_RANGE(core, range)		(TOPAZ_BUSMON_CTL(core) + 0x8 + ((range) * 0x8))
#define TOPAZ_BUSMON_CTL_RANGE_LOW(core, range)		(__TOPAZ_BUSMON_CTL_RANGE((core), (range)) + 0x0)
#define TOPAZ_BUSMON_CTL_RANGE_HIGH(core, range)	(__TOPAZ_BUSMON_CTL_RANGE((core), (range)) + 0x4)
#define TOPAZ_BUSMON_HREADY_EN				BIT(0)
#define TOPAZ_BUSMON_TIMER_INT_EN			BIT(1)
#define TOPAZ_BUSMON_TIMER_ERROR_EN			BIT(2)
#define TOPAZ_BUSMON_ADDR_CHECK_EN			BIT(3)
#define TOPAZ_BUSMON_REGION_VALID(x)			(((x) & 0xF) << 4)
#define TOPAZ_BUSMON_TIMEOUT(cycles)			(((cycles) & 0x3FF) << 8)
#define TOPAZ_BUSMON_BLOCK_TRANS_EN			BIT(18)
#define TOPAZ_BUSMON_OUTSIDE_ADDR_CHECK			BIT(19)

/* AHB Bus monitor masters */
#define TOPAZ_BUSMON_LHOST				0
#define TOPAZ_BUSMON_MUC				1
#define TOPAZ_BUSMON_DSP				2
#define TOPAZ_BUSMON_AUC				3
#define TOPAZ_BUSMON_WMAC				4
#define TOPAZ_BUSMON_PCIE				5
#define TOPAZ_BUSMON_SWE				6
#define TOPAZ_BUSMON_EMAC				7

#define TOPAZ_BUSMON_MASTER_NAMES	{ "lhost", "muc", "dsp", "auc", "wmac", "pcie", "swe", "emac" }

/* AHB Bus monitor debug data select */
#define TOPAZ_BUSMON_ADDR				0
#define TOPAZ_BUSMON_WR_L32				1
#define TOPAZ_BUSMON_WR_H32				2
#define TOPAZ_BUSMON_RD_L32				3
#define TOPAZ_BUSMON_RD_H32				4
#define TOPAZ_BUSMON_CTRL0				5
#define TOPAZ_BUSMON_CTRL1				6
#define TOPAZ_BUSMON_CTRL2				7
#define TOPAZ_BUSMON_DEBUG_MAX				8

/* GPIO Registers */
#define RUBY_GPIO3_PWM1					(RUBY_GPIO1_PWM0 + 4)
#define RUBY_GPIO12_PWM3				(RUBY_GPIO1_PWM0 + 12)
#define RUBY_GPIO13_PWM4				(RUBY_GPIO1_PWM0 + 16)
#define RUBY_GPIO15_PWM5				(RUBY_GPIO1_PWM0 + 20)
#define RUBY_GPIO16_PWM6				(RUBY_GPIO1_PWM0 + 24)
#define RUBY_GPIO_PWM_LOW_SHIFT				(0)
#define RUBY_GPIO_PWM_HIGH_SHIFT			(8)
#define RUBY_GPIO_PWM_ENABLE				(BIT(16))
#define RUBY_GPIO_PWM_MAX_COUNT				(255)

#ifdef TOPAZ_AMBER_IP
#define	AMBER_GPIO11_PWM0				(RUBY_GPIO_REGS_ADDR + 0x20)
#define AMBER_GPIO12_PWM1				(RUBY_GPIO_REGS_ADDR + 0x24)
#define	AMBER_GPIO13_PWM2				(RUBY_GPIO_REGS_ADDR + 0x28)
#define AMBER_GPIO14_PWM3				(RUBY_GPIO_REGS_ADDR + 0x2C)
#define AMBER_GPIO15_PWM4				(RUBY_GPIO_REGS_ADDR + 0x30)
#define AMBER_GPIO16_PWM5				(RUBY_GPIO_REGS_ADDR + 0x34)
#define AMBER_GPIO17_PWM6				(RUBY_GPIO_REGS_ADDR + 0x38)
#endif

/* Interrupt lines */
#define TOPAZ_IRQ_MISC_WDT				(57)
#define TOPAZ_IRQ_MISC_SPI1				(58)
#define TOPAZ_IRQ_MISC_AHB_MON				(61)
#define TOPAZ_IRQ_MISC_HBM				(62)
#define TOPAZ_IRQ_MISC_FWT				(63)
#define TOPAZ_IRQ_MISC_EXT_IRQ_COUNT			(8)
#define TOPAZ_IRQ_MISC_RST_CAUSE_START			(9)

/* RESET CAUSE */
#define TOPAZ_SYS_CTL_INTR_TIMER_MSK(t)		(1 << (3 + (t)))

/* HDMA registers */
#define TOPAZ_HDMA_NUM_CHANNELS		(4)
#define TOPAZ_HDMA_CH_REGS		(0x58)
#define TOPAZ_HDMA_BASE_ADDR		(0xEA000000)
#define TOPAZ_HDMA_SAR(x)		(TOPAZ_HDMA_BASE_ADDR + 0x00 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_DAR(x)		(TOPAZ_HDMA_BASE_ADDR + 0x08 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_LLP(x)		(TOPAZ_HDMA_BASE_ADDR + 0x10 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_CTL(x)		(TOPAZ_HDMA_BASE_ADDR + 0x18 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_SIZE(x)		(TOPAZ_HDMA_BASE_ADDR + 0x1c + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_SSTAT(x)		(TOPAZ_HDMA_BASE_ADDR + 0x20 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_DSTAT(x)		(TOPAZ_HDMA_BASE_ADDR + 0x28 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_SSTATAR(x)		(TOPAZ_HDMA_BASE_ADDR + 0x30 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_DSTATAR(x)		(TOPAZ_HDMA_BASE_ADDR + 0x38 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_CFG(x)		(TOPAZ_HDMA_BASE_ADDR + 0x40 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_SGR(x)		(TOPAZ_HDMA_BASE_ADDR + 0x48 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_DSR(x)		(TOPAZ_HDMA_BASE_ADDR + 0x50 + (x) * TOPAZ_HDMA_CH_REGS)
#define TOPAZ_HDMA_RAW_TFR		(TOPAZ_HDMA_BASE_ADDR + 0x2c0)
#define TOPAZ_HDMA_RAW_BLK		(TOPAZ_HDMA_BASE_ADDR + 0x2c8)
#define TOPAZ_HDMA_RAW_SRC		(TOPAZ_HDMA_BASE_ADDR + 0x2d0)
#define TOPAZ_HDMA_RAW_DST		(TOPAZ_HDMA_BASE_ADDR + 0x2d8)
#define TOPAZ_HDMA_RAW_ERR		(TOPAZ_HDMA_BASE_ADDR + 0x2e0)
#define TOPAZ_HDMA_STS_TFR		(TOPAZ_HDMA_BASE_ADDR + 0x2e8)
#define TOPAZ_HDMA_STS_BLK		(TOPAZ_HDMA_BASE_ADDR + 0x2f0)
#define TOPAZ_HDMA_STS_SRC		(TOPAZ_HDMA_BASE_ADDR + 0x2f8)
#define TOPAZ_HDMA_STS_DST		(TOPAZ_HDMA_BASE_ADDR + 0x300)
#define TOPAZ_HDMA_STS_ERR		(TOPAZ_HDMA_BASE_ADDR + 0x308)
#define TOPAZ_HDMA_MSK_TFR		(TOPAZ_HDMA_BASE_ADDR + 0x310)
#define TOPAZ_HDMA_MSK_BLK		(TOPAZ_HDMA_BASE_ADDR + 0x318)
#define TOPAZ_HDMA_MSK_SRC		(TOPAZ_HDMA_BASE_ADDR + 0x320)
#define TOPAZ_HDMA_MSK_DST		(TOPAZ_HDMA_BASE_ADDR + 0x328)
#define TOPAZ_HDMA_MSK_ERR		(TOPAZ_HDMA_BASE_ADDR + 0x330)
#define TOPAZ_HDMA_TFR_CLR		(TOPAZ_HDMA_BASE_ADDR + 0x338)
#define TOPAZ_HDMA_BLK_CLR		(TOPAZ_HDMA_BASE_ADDR + 0x340)
#define TOPAZ_HDMA_SRC_CLR		(TOPAZ_HDMA_BASE_ADDR + 0x348)
#define TOPAZ_HDMA_DST_CLR		(TOPAZ_HDMA_BASE_ADDR + 0x350)
#define TOPAZ_HDMA_ERR_CLR		(TOPAZ_HDMA_BASE_ADDR + 0x358)
#define TOPAZ_HDMA_STS_INT		(TOPAZ_HDMA_BASE_ADDR + 0x360)
#define TOPAZ_HDMA_REQ_SRC		(TOPAZ_HDMA_BASE_ADDR + 0x368)
#define TOPAZ_HDMA_REQ_DST		(TOPAZ_HDMA_BASE_ADDR + 0x370)
#define TOPAZ_HDMA_SGL_REQ_SRC		(TOPAZ_HDMA_BASE_ADDR + 0x378)
#define TOPAZ_HDMA_SGL_REQ_DST		(TOPAZ_HDMA_BASE_ADDR + 0x380)
#define TOPAZ_HDMA_LST_SRC		(TOPAZ_HDMA_BASE_ADDR + 0x388)
#define TOPAZ_HDMA_LST_DST		(TOPAZ_HDMA_BASE_ADDR + 0x390)
#define TOPAZ_HDMA_DMA_CFG		(TOPAZ_HDMA_BASE_ADDR + 0x398)
#define TOPAZ_HDMA_CH_EN		(TOPAZ_HDMA_BASE_ADDR + 0x3a0)
#define TOPAZ_HDMA_ID			(TOPAZ_HDMA_BASE_ADDR + 0x3a8)
#define TOPAZ_HDMA_TEST			(TOPAZ_HDMA_BASE_ADDR + 0x3b0)
/* TOPAZ_HDMA_STS_INT values */
#define TOPAZ_HDMA_STS_INT_ERR		(BIT(4))
#define TOPAZ_HDMA_STS_INT_DSTT		(BIT(3))
#define TOPAZ_HDMA_STS_INT_SRCT		(BIT(2))
#define TOPAZ_HDMA_STS_INT_BLOCK	(BIT(1))
#define TOPAZ_HDMA_STS_INT_TFR		(BIT(0))
/* TOPAZ_HDMA_DMA_CFG values */
#define TOPAZ_HDMA_DMA_CFG_ENABLE	(BIT(0))
/* TOPAZ_HDMA_CH_EN values */
#define TOPAZ_HDMA_CH_EN_MASK(ch)	(1 << (ch))
#define TOPAZ_HDMA_CH_EN_WR_MASK(ch)	((1 << (8 + (ch))) | (1 << (ch)))
/* TOPAZ_HDMA_CTL values */
#define TOPAZ_HDMA_CTL_LLP_SRC_EN	(BIT(28))
#define TOPAZ_HDMA_CTL_LLP_DST_EN	(BIT(27))
#define TOPAZ_HDMA_CTL_SRC_M1		(0 << 25)	// ahb master bus 1
#define TOPAZ_HDMA_CTL_SRC_M2		(1 << 25)	// ahb master bus 2
#define TOPAZ_HDMA_CTL_DST_M1		(0 << 23)
#define TOPAZ_HDMA_CTL_DST_M2		(1 << 23)
#define TOPAZ_HDMA_CTL_MEM2MEM		(0 << 20)
#define TOPAZ_HDMA_CTL_DST_SCATTER	(BIT(18))
#define TOPAZ_HDMA_CTL_SRC_SCATTER	(BIT(17))
#define TOPAZ_HDMA_CTL_SRC_MSIZE1	(0 << 14)
#define TOPAZ_HDMA_CTL_SRC_MSIZE4	(1 << 14)
#define TOPAZ_HDMA_CTL_SRC_MSIZE8	(2 << 14)
#define TOPAZ_HDMA_CTL_SRC_MSIZE16	(3 << 14)
#define TOPAZ_HDMA_CTL_SRC_MSIZE32	(4 << 14)
#define TOPAZ_HDMA_CTL_SRC_MSIZE64	(5 << 14)
#define TOPAZ_HDMA_CTL_SRC_MSIZE128	(6 << 14)
#define TOPAZ_HDMA_CTL_SRC_MSIZE256	(7 << 14)
#define TOPAZ_HDMA_CTL_DST_MSIZE1	(0 << 11)
#define TOPAZ_HDMA_CTL_DST_MSIZE4	(1 << 11)
#define TOPAZ_HDMA_CTL_DST_MSIZE8	(2 << 11)
#define TOPAZ_HDMA_CTL_DST_MSIZE16	(3 << 11)
#define TOPAZ_HDMA_CTL_DST_MSIZE32	(4 << 11)
#define TOPAZ_HDMA_CTL_DST_MSIZE64	(5 << 11)
#define TOPAZ_HDMA_CTL_DST_MSIZE128	(6 << 11)
#define TOPAZ_HDMA_CTL_DST_MSIZE256	(7 << 11)
#define TOPAZ_HDMA_CTL_SRC_SINC_INC	(0 << 9)
#define TOPAZ_HDMA_CTL_SRC_SINC_DEC	(1 << 9)
#define TOPAZ_HDMA_CTL_SRC_SINC_NONE	(2 << 9)
#define TOPAZ_HDMA_CTL_DST_DINC_INC	(0 << 7)
#define TOPAZ_HDMA_CTL_DST_DINC_DEC	(1 << 7)
#define TOPAZ_HDMA_CTL_DST_DINC_NONE	(2 << 7)
#define TOPAZ_HDMA_CTL_SRC_WIDTH8	(0 << 4)
#define TOPAZ_HDMA_CTL_SRC_WIDTH16	(1 << 4)
#define TOPAZ_HDMA_CTL_SRC_WIDTH32	(2 << 4)
#define TOPAZ_HDMA_CTL_SRC_WIDTH64	(3 << 4)
#define TOPAZ_HDMA_CTL_DST_WIDTH8	(0 << 1)
#define TOPAZ_HDMA_CTL_DST_WIDTH16	(1 << 1)
#define TOPAZ_HDMA_CTL_DST_WIDTH32	(2 << 1)
#define TOPAZ_HDMA_CTL_DST_WIDTH64	(3 << 1)
#define TOPAZ_HDMA_CTL_INT_EN		(BIT(0))

#define TOPAZ_EMAC0_BASE_ADDR		(0xED000000)
#define TOPAZ_EMAC0_END_ADDR		(TOPAZ_EMAC0_BASE_ADDR + 0x800)

#define TOPAZ_EMAC1_BASE_ADDR		(0xE8000000)
#define TOPAZ_EMAC1_END_ADDR		(TOPAZ_EMAC1_BASE_ADDR + 0x800)

#define TOPAZ_UART1_CTRL_BASE_ADDR	(0xF0000000)
#define TOPAZ_UART1_CTRL_END_ADDR	(TOPAZ_UART1_CTRL_BASE_ADDR + 0xFF)

#define TOPAZ_GPIO_BASE_ADDR		(0xF1000000)
#define TOPAZ_GPIO_END_ADDR		(TOPAZ_GPIO_BASE_ADDR + 0x3F)

#define TOPAZ_SPI1_BASE_ADDR		(0xF2000000)
#define TOPAZ_SPI1_END_ADDR		(TOPAZ_SPI1_BASE_ADDR + 0x20)

#define TOPAZ_TIMER_CTRL_BASE_ADDR	(0xF4000000)
#define TOPAZ_TIMER_CTRL_END_ADDR	(TOPAZ_TIMER_CTRL_BASE_ADDR + 0xAF)

#define TOPAZ_UART2_CTRL_BASE_ADDR	(0xF5000000)
#define TOPAZ_UART2_CTRL_END_ADDR	(TOPAZ_UART2_CTRL_BASE_ADDR + 0xFF)

#define TOPAZ_DDR_BASE_ADDR		(0xF6000000)
#define TOPAZ_DDR_END_ADDR		(TOPAZ_DDR_BASE_ADDR + 0x8FF)

/* End addresses for some sub blocks */
#define TOPAZ_BB_END_ADDR		0XE64FFFFF
#define TOPAZ_ENET1_END_ADDR		0XE8FFFFFF
#define TOPAZ_PCIE_END_ADDR		0XE9FFFFFF
#define TOPAZ_ENET0_END_ADDR		0XEDFFFFFF

/* Some bb addresses (obfuscated). See documentation for more details. */
#define TOPAZ_BB_1_BASE_ADDR		(0xE6000000)
#define TOPAZ_BB_1_END_ADDR		(TOPAZ_BB_1_BASE_ADDR + 0x3FF)

#define TOPAZ_BB_2_BASE_ADDR		(0xE6010000)
#define TOPAZ_BB_2_END_ADDR		(TOPAZ_BB_2_BASE_ADDR + 0x3FF)

#define TOPAZ_BB_3_BASE_ADDR		(0xE6020000)
#define TOPAZ_BB_3_END_ADDR		(TOPAZ_BB_3_BASE_ADDR + 0x2B)

#define TOPAZ_BB_4_BASE_ADDR		(0xE6030000)
#define TOPAZ_BB_4_END_ADDR		(TOPAZ_BB_4_BASE_ADDR + 0x37)

#define TOPAZ_BB_5_BASE_ADDR		(0xE6040000)
#define TOPAZ_BB_5_END_ADDR		(TOPAZ_BB_5_BASE_ADDR + 0xFFFF)

#define TOPAZ_BB_6_BASE_ADDR		(0xE6050000)
#define TOPAZ_BB_6_END_ADDR		(TOPAZ_BB_6_BASE_ADDR + 0x4C4)

#define TOPAZ_BB_7_BASE_ADDR		(0xE6090000)
#define TOPAZ_BB_7_END_ADDR		(TOPAZ_BB_7_BASE_ADDR + 0x6FF)

#define TOPAZ_BB_8_BASE_ADDR		(0xE6091000)
#define TOPAZ_BB_8_END_ADDR		(TOPAZ_BB_8_BASE_ADDR + 0x5FF)

#define TOPAZ_BB_9_BASE_ADDR		(0xE6092000)
#define TOPAZ_BB_9_END_ADDR		(TOPAZ_BB_9_BASE_ADDR + 0x5FF)

#define TOPAZ_BB_10_BASE_ADDR		(0xE60A1000)
#define TOPAZ_BB_10_END_ADDR		(TOPAZ_BB_10_BASE_ADDR + 0x33F)

#define TOPAZ_BB_11_BASE_ADDR		(0xE60A2000)
#define TOPAZ_BB_11_END_ADDR		(TOPAZ_BB_11_BASE_ADDR + 0x33F)

#define TOPAZ_BB_12_BASE_ADDR		(0xE60A3000)
#define TOPAZ_BB_12_END_ADDR		(TOPAZ_BB_12_BASE_ADDR + 0x33F)

#define TOPAZ_BB_13_BASE_ADDR		(0xE60A4000)
#define TOPAZ_BB_13_END_ADDR		(TOPAZ_BB_13_BASE_ADDR + 0x33F)

#define TOPAZ_BB_14_BASE_ADDR		(0xE60A5000)
#define TOPAZ_BB_14_END_ADDR		(TOPAZ_BB_14_BASE_ADDR + 0x3FF)

#define TOPAZ_BB_15_BASE_ADDR		(0xE60A6000)
#define TOPAZ_BB_15_END_ADDR		(TOPAZ_BB_15_BASE_ADDR + 0x3FF)

#define TOPAZ_BB_16_BASE_ADDR		(0xE60B1000)
#define TOPAZ_BB_16_END_ADDR		(TOPAZ_BB_16_BASE_ADDR + 0xFF)

#define TOPAZ_BB_17_BASE_ADDR		(0xE60B2000)
#define TOPAZ_BB_17_END_ADDR		(TOPAZ_BB_17_BASE_ADDR + 0x4FB)

#define TOPAZ_BB_18_BASE_ADDR		(0xE60B3000)
#define TOPAZ_BB_18_END_ADDR		(TOPAZ_BB_18_BASE_ADDR + 0x13F)

#define TOPAZ_BB_19_BASE_ADDR		(0xE60B4000)
#define TOPAZ_BB_19_END_ADDR		(TOPAZ_BB_19_BASE_ADDR + 0x63)

#define TOPAZ_BB_20_BASE_ADDR		(0xE60F0000)
#define TOPAZ_BB_20_END_ADDR		(TOPAZ_BB_20_BASE_ADDR + 0x260)

#define TOPAZ_BB_21_BASE_ADDR		(0xE6100000)
#define TOPAZ_BB_21_END_ADDR		(TOPAZ_BB_21_BASE_ADDR + 0x7FFF)

#define TOPAZ_BB_22_BASE_ADDR		(0xE6200000)
#define TOPAZ_BB_22_END_ADDR		(TOPAZ_BB_22_BASE_ADDR + 0xFFB)

#define TOPAZ_BB_23_BASE_ADDR		(0xE6201000)
#define TOPAZ_BB_23_END_ADDR		(TOPAZ_BB_23_BASE_ADDR + 0xFFB)

#define TOPAZ_BB_24_BASE_ADDR		(0xE6202000)
#define TOPAZ_BB_24_END_ADDR		(TOPAZ_BB_24_BASE_ADDR + 0xFFB)

#define TOPAZ_BB_25_BASE_ADDR		(0xE6203000)
#define TOPAZ_BB_25_END_ADDR		(TOPAZ_BB_25_BASE_ADDR + 0xFFB)

#define TOPAZ_BB_26_BASE_ADDR		(0xE6400000)
#define TOPAZ_BB_26_END_ADDR		(TOPAZ_BB_26_BASE_ADDR + 0xFFB)

#endif /* #ifndef __TOPAZ_PLATFORM_H */
