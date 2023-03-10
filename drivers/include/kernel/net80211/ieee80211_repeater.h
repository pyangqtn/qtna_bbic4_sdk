/*-
 * Copyright (c) 2017 - Quantenna Communications, Inc.
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
 */
#ifndef _NET80211_IEEE80211_REPEATER_H_
#define _NET80211_IEEE80211_REPEATER_H_

#define IEEE80211_REPEATER_INIT_MAX_LEVEL	1
#define IEEE80211_REPEATER_FIRST_LEVEL		1
#define IEEE80211_REPEATER_SCS_ALLOW_MAX_LEVEL	1

void ieee80211_repeater_process_cascade_ie(struct ieee80211com *ic,
	struct ieee80211_node *ni, const void *ie);

void ieee80211_repeater_level_update(struct ieee80211com *ic,
	uint8_t new_curr_leve, uint8_t new_max_level);

int ieee80211_repeater_sta_pre_join(struct ieee80211com *ic,
		struct ieee80211vap *vap, const struct ieee80211_scan_entry *se);
void ieee80211_repeater_sta_post_join(struct ieee80211com *ic, const void *ie);
void ieee80211_repeater_sta_leave(struct ieee80211com *ic);

void ieee80211_repeater_process_rp_info_ie(struct ieee80211com *ic,
	struct ieee80211_node *ni, const void *ie);
#endif
