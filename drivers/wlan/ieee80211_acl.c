/*-
 * Copyright (c) 2004-2005 Sam Leffler, Errno Consulting
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
 * $Id: ieee80211_acl.c 1721 2006-09-20 08:45:13Z mentor $
 */
#ifndef EXPORT_SYMTAB
#define	EXPORT_SYMTAB
#endif

/*
 * IEEE 802.11 MAC ACL support.
 *
 * When this module is loaded the sender address of each received
 * frame is passed to the iac_check method and the module indicates
 * if the frame should be accepted or rejected.  If the policy is
 * set to ACL_POLICY_OPEN then all frames are accepted w/o checking
 * the address.  Otherwise, the address is looked up in the database
 * and if found the frame is either accepted (ACL_POLICY_ALLOW)
 * or rejected (ACL_POLICY_DENY).
 */
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/init.h>

#include "net80211/if_media.h"

#include "net80211/ieee80211_var.h"

enum {
	ACL_POLICY_OPEN		= 0,	/* open, don't check ACLs */
	ACL_POLICY_ALLOW	= 1,	/* allow traffic from MAC */
	ACL_POLICY_DENY		= 2,	/* deny traffic from MAC */
};

#define	ACL_HASHSIZE	32

struct acl {
	TAILQ_ENTRY(acl) acl_list;
	LIST_ENTRY(acl) acl_hash;
	u_int8_t acl_macaddr[IEEE80211_ADDR_LEN];
};

struct aclstate {
	acl_lock_t as_lock;
	int as_policy;
	TAILQ_HEAD(, acl) as_mac_list;	/* list of MAC ACLs */
	ATH_LIST_HEAD(, acl) as_mac_hash[ACL_HASHSIZE];
	TAILQ_HEAD(, acl) as_oui_list;	/* list of OUI ACLs */
	ATH_LIST_HEAD(, acl) as_oui_hash[ACL_HASHSIZE];
};

/* simple hash is enough for variation of macaddr */
#define	ACL_HASH(addr)	\
	(((const u_int8_t *)(addr))[IEEE80211_ADDR_LEN - 1] % ACL_HASHSIZE)

MALLOC_DEFINE(M_80211_ACL, "acl", "802.11 station acl");

static void acl_free_all_locked(struct aclstate *);

static int
acl_attach(struct ieee80211vap *vap)
{
	struct aclstate *as;

	_MOD_INC_USE(THIS_MODULE, return 0);

	MALLOC(as, struct aclstate *, sizeof(struct aclstate),
		M_DEVBUF, M_NOWAIT | M_ZERO);
	if (as == NULL) {
		_MOD_DEC_USE(THIS_MODULE);
		return 0;
	}
	ACL_LOCK_INIT(as, "acl");
	TAILQ_INIT(&as->as_mac_list);
	TAILQ_INIT(&as->as_oui_list);
	as->as_policy = ACL_POLICY_OPEN;
	vap->iv_as = as;
	return 1;
}

static void
acl_detach(struct ieee80211vap *vap)
{
	struct aclstate *as = vap->iv_as;

	if (as == NULL)
		return;

	ACL_LOCK(as);
	acl_free_all_locked(as);
	ACL_UNLOCK(as);
	vap->iv_as = NULL;
	ACL_LOCK_DESTROY(as);
	FREE(as, M_DEVBUF);

	_MOD_DEC_USE(THIS_MODULE);
}

static __inline struct acl *
_find_mac_acl(struct aclstate *as, const u_int8_t *macaddr)
{
	struct acl *acl;
	int hash;

	hash = ACL_HASH(macaddr);
	LIST_FOREACH(acl, &as->as_mac_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, macaddr))
			return acl;
	}

	return NULL;
}

static __inline struct acl *
_find_oui_acl(struct aclstate *as, const u_int8_t *macaddr)
{
	struct acl *acl;
	int hash;

	hash = ACL_HASH(macaddr);
	LIST_FOREACH(acl, &as->as_oui_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, macaddr))
			return acl;
	}

	return NULL;
}

static void
_acl_free_mac(struct aclstate *as, struct acl *acl)
{

	TAILQ_REMOVE(&as->as_mac_list, acl, acl_list);
	LIST_REMOVE(acl, acl_hash);
	FREE(acl, M_80211_ACL);
}

static void
_acl_free_oui(struct aclstate *as, struct acl *acl)
{

	TAILQ_REMOVE(&as->as_oui_list, acl, acl_list);
	LIST_REMOVE(acl, acl_hash);
	FREE(acl, M_80211_ACL);
}

static int
acl_check(struct ieee80211vap *vap, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = vap->iv_as;

	switch (as->as_policy) {
	case ACL_POLICY_OPEN:
		return 1;
	case ACL_POLICY_ALLOW:
		if (_find_mac_acl(as, mac) != NULL)
			return 1;
		{
		u_int8_t oui[IEEE80211_ADDR_LEN] = {0};

		memcpy(oui, mac, IEEE80211_OUI_LEN);
		return _find_oui_acl(as, oui) != NULL;
		}
	case ACL_POLICY_DENY:
		return _find_mac_acl(as, mac) == NULL;
	}

	return 0;		/* should not happen */
}

static int
acl_add_mac(struct ieee80211vap *vap, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl, *new;
	int hash;

	MALLOC(new, struct acl *, sizeof(struct acl), M_80211_ACL, M_NOWAIT | M_ZERO);
	if (new == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
			"ACL: add %s failed, no memory\n", ether_sprintf(mac));
		/* XXX statistic */
		return -ENOMEM;
	}

	ACL_LOCK(as);
	hash = ACL_HASH(mac);
	LIST_FOREACH(acl, &as->as_mac_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, mac)) {
			ACL_UNLOCK(as);
			FREE(new, M_80211_ACL);
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
				"ACL: add %s failed, already present\n",
				ether_sprintf(mac));
			return -EEXIST;
		}
	}
	IEEE80211_ADDR_COPY(new->acl_macaddr, mac);
	TAILQ_INSERT_TAIL(&as->as_mac_list, new, acl_list);
	LIST_INSERT_HEAD(&as->as_mac_hash[hash], new, acl_hash);
	ACL_UNLOCK(as);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: add %s\n", ether_sprintf(mac));

	return 0;
}

static int
acl_add_oui(struct ieee80211vap *vap, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl, *new;
	int hash;

	MALLOC(new, struct acl *, sizeof(struct acl), M_80211_ACL, M_NOWAIT | M_ZERO);
	if (new == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
			"ACL: add %s failed, no memory\n", ether_sprintf(mac));
		/* XXX statistic */
		return -ENOMEM;
	}

	ACL_LOCK(as);
	hash = ACL_HASH(mac);
	LIST_FOREACH(acl, &as->as_oui_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, mac)) {
			ACL_UNLOCK(as);
			FREE(new, M_80211_ACL);
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
				"ACL: add %s failed, already present\n",
				ether_sprintf(mac));
			return -EEXIST;
		}
	}
	IEEE80211_ADDR_COPY(new->acl_macaddr, mac);
	TAILQ_INSERT_TAIL(&as->as_oui_list, new, acl_list);
	LIST_INSERT_HEAD(&as->as_oui_hash[hash], new, acl_hash);
	ACL_UNLOCK(as);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: add %s\n", ether_sprintf(mac));

	return 0;
}

static int
acl_remove_mac(struct ieee80211vap *vap, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl;

	ACL_LOCK(as);
	acl = _find_mac_acl(as, mac);
	if (acl != NULL)
		_acl_free_mac(as, acl);
	ACL_UNLOCK(as);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: remove %s%s\n", ether_sprintf(mac),
		acl == NULL ? ", not present" : "");

	return (acl == NULL ? ENOENT : 0);
}

static int
acl_remove_oui(struct ieee80211vap *vap, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl;

	ACL_LOCK(as);
	acl = _find_oui_acl(as, mac);
	if (acl != NULL)
		_acl_free_oui(as, acl);
	ACL_UNLOCK(as);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: remove %s%s\n", ether_sprintf(mac),
		acl == NULL ? ", not present" : "");

	return (acl == NULL ? ENOENT : 0);
}

static void
acl_free_all_locked(struct aclstate *as)
{
	struct acl *acl;

	ACL_LOCK_ASSERT(as);

	while ((acl = TAILQ_FIRST(&as->as_mac_list)) != NULL)
		_acl_free_mac(as, acl);
	while ((acl = TAILQ_FIRST(&as->as_oui_list)) != NULL)
		_acl_free_oui(as, acl);
}

static int
acl_free_all(struct ieee80211vap *vap)
{
	struct aclstate *as = vap->iv_as;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL, "ACL: %s\n", "free all");

	ACL_LOCK(as);
	acl_free_all_locked(vap->iv_as);
	ACL_UNLOCK(as);

	return 0;
}

static int
acl_add_mac_list(struct ieee80211vap *vap, int num_macs, struct ieee80211_mac_addr *mac_list)
{
	int i = 0;
	int ret = 0;

	while (i < num_macs) {
		ret = acl_add_mac(vap, mac_list[i].addr);
		if (ret < 0 && ret != -EEXIST)
			return ret;
		i++;
	}

	return ret;
}

static int
acl_add_oui_list(struct ieee80211vap *vap, int num_macs, struct ieee80211_mac_addr *oui_list)
{
	int i = 0;
	int ret = 0;

	while (i < num_macs) {
		ret = acl_add_oui(vap, oui_list[i].addr);
		if (ret < 0 && ret != -EEXIST)
			return ret;
		i++;
	}

	return ret;
}

static int
acl_setpolicy(struct ieee80211vap *vap, int policy)
{
	struct aclstate *as = vap->iv_as;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: set policy to %u\n", policy);

	switch (policy) {
	case IEEE80211_MACCMD_POLICY_OPEN:
		as->as_policy = ACL_POLICY_OPEN;
		/* Flush silently to reset */
		acl_free_all(vap);
		break;
	case IEEE80211_MACCMD_POLICY_ALLOW:
		as->as_policy = ACL_POLICY_ALLOW;
		break;
	case IEEE80211_MACCMD_POLICY_DENY:
		as->as_policy = ACL_POLICY_DENY;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
acl_getpolicy(struct ieee80211vap *vap)
{
	struct aclstate *as = vap->iv_as;

	return as->as_policy;
}

/*
 * Module glue.
 */

MODULE_AUTHOR("Errno Consulting, Sam Leffler");
MODULE_DESCRIPTION("802.11 wireless support: MAC-based ACL policy");
#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual BSD/GPL");
#endif

static const struct ieee80211_aclator mac = {
	.iac_name	= "mac",
	.iac_attach	= acl_attach,
	.iac_detach	= acl_detach,
	.iac_check	= acl_check,
	.iac_add_mac	= acl_add_mac,
	.iac_add_oui	= acl_add_oui,
	.iac_remove_mac	= acl_remove_mac,
	.iac_remove_oui	= acl_remove_oui,
	.iac_flush	= acl_free_all,
	.iac_setpolicy	= acl_setpolicy,
	.iac_getpolicy	= acl_getpolicy,
	.iac_add_mac_list = acl_add_mac_list,
	.iac_add_oui_list = acl_add_oui_list,
};

static int __init
init_ieee80211_acl(void)
{
	ieee80211_aclator_register(&mac);
	return 0;
}
module_init(init_ieee80211_acl);

static void __exit
exit_ieee80211_acl(void)
{
	ieee80211_aclator_unregister(&mac);
}
module_exit(exit_ieee80211_acl);
