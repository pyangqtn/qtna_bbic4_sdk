/*SH0
*******************************************************************************
**                                                                           **
**         Copyright (c) 2009 - 2017 Quantenna Communications, Inc.          **
**                                                                           **
**  File        : qtn_uc_comm.h                                              **
**  Description :                                                            **
**                                                                           **
*******************************************************************************
**                                                                           **
**  Redistribution and use in source and binary forms, with or without       **
**  modification, are permitted provided that the following conditions       **
**  are met:                                                                 **
**  1. Redistributions of source code must retain the above copyright        **
**     notice, this list of conditions and the following disclaimer.         **
**  2. Redistributions in binary form must reproduce the above copyright     **
**     notice, this list of conditions and the following disclaimer in the   **
**     documentation and/or other materials provided with the distribution.  **
**  3. The name of the author may not be used to endorse or promote products **
**     derived from this software without specific prior written permission. **
**                                                                           **
**  Alternatively, this software may be distributed under the terms of the   **
**  GNU General Public License ("GPL") version 2, or (at your option) any    **
**  later version as published by the Free Software Foundation.              **
**                                                                           **
**  In the case this software is distributed under the GPL license,          **
**  you should have received a copy of the GNU General Public License        **
**  along with this software; if not, write to the Free Software             **
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA  **
**                                                                           **
**  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR       **
**  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES**
**  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  **
**  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,         **
**  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT **
**  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,**
**  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY    **
**  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT      **
**  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF **
**  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.        **
**                                                                           **
*******************************************************************************
EH0*/

#ifndef _QTN_UC_COMM_H
#define _QTN_UC_COMM_H

#define MAC_UNITS		1

#if defined(TOPAZ_128_NODE_MODE)
#define QTN_NCIDX_MAX			128
#define QTN_NODE_TBL_SIZE_LHOST		118
#define QTN_NODETID_NODE_SHIFT		7
#else
#define QTN_NCIDX_MAX			64
#define QTN_NODE_TBL_SIZE_LHOST		56
#define QTN_NODETID_NODE_SHIFT		6
#endif
#define QTN_MAX_BSS_VAPS		8
#define QTN_MAX_WDS_VAPS		8
#define QTN_MAX_VAPS			((QTN_MAX_BSS_VAPS) + (QTN_MAX_WDS_VAPS))
#define QTN_NODE_TBL_MUC_HEADRM		3 /* Allow for delayed delete on MUC */
#define QTN_NODE_TBL_SIZE_MUC		((QTN_NODE_TBL_SIZE_LHOST) + (QTN_NODE_TBL_MUC_HEADRM))
#define QTN_ASSOC_LIMIT			((QTN_NODE_TBL_SIZE_LHOST) - (QTN_MAX_VAPS))

#endif // #ifndef _QTN_UC_COMM_H

