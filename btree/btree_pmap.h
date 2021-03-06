//----------------------------------------------------------------------------
// ZetaScale
// Copyright (c) 2016, SanDisk Corp. and/or all its affiliates.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published by the Free
// Software Foundation;
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License v2.1 for more details.
//
// A copy of the GNU Lesser General Public License v2.1 is provided with this package and
// can also be found at: http://opensource.org/licenses/LGPL-2.1
// You should have received a copy of the GNU Lesser General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 59 Temple
// Place, Suite 330, Boston, MA 02111-1307 USA.
//----------------------------------------------------------------------------

/*
 * File:   btree_map.h
 * Author: Brian O'Krafka
 *
 * Created on September 11, 2008
 *
 * SanDisk Proprietary Material, © Copyright 2012 SanDisk, all rights reserved.
 * http://www.sandisk.com
 *
 * $Id: tlmap.h 308 2008-02-20 22:34:58Z tomr $
 */

#ifndef _BTREE_PMAP_H
#define _BTREE_PMAP_H

#include <stdint.h>
#include <inttypes.h>

#include "btree_map.h"

struct Iterator;
struct PMap;

extern struct PMap *PMapInit(uint32_t nparts, uint64_t nbuckets, uint64_t max_entries, char use_locks, void (*replacement_callback)(void *callback_data, char *key, uint32_t keylen, char *pdata, uint64_t datalen));
extern void PMapDestroy(struct PMap **pm);
extern void PMapClean(struct PMap **pm, uint64_t cguid, void *replacement_callback_data);
extern void PMapClear(struct PMap *pm);
extern struct MapEntry *PMapCreate(struct PMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen, uint64_t cguid, int robj, void *replacement_callback_data);
extern struct MapEntry *PMapUpdate(struct PMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen, uint64_t cguid, int robj, void *replacement_callback_data);
extern struct MapEntry *PMapSet(struct PMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen, char **old_pdata, uint64_t *old_datalen, uint64_t cguid, int robj, void *replacement_callback_data);
extern struct MapEntry *PMapGet(struct PMap *pc, char *key, uint32_t keylen, char** data, uint64_t *pdatalen, uint64_t cguid, int robj);
extern int PMapIncrRefcnt(struct PMap *pm, char *key, uint32_t keylen, uint64_t cguid, int robj);
extern int PMapGetRefcnt(struct PMap *pm, char *key, uint32_t keylen, uint64_t cguid, int robj);
extern void PMapCheckRefcnts(struct PMap *pm);
extern int PMapRelease(struct PMap *pm, char *key, uint32_t keylen, uint64_t cguid, int robj, void *replacement_callback_data);
extern int PMapReleaseAll(struct PMap *pm, char *key, uint32_t keylen, uint64_t cguid, int robj, void *replacement_callback_data);
#if 0
extern int PMapReleaseEntry(struct PMap *pm, struct MapEntry *pme);
extern struct Iterator *PMapEnum(struct PMap *pm);
extern void FinishEnum(struct PMap *pm, struct Iterator *iterator);
extern int PMapNextEnum(struct PMap *pm, struct Iterator *iterator, char **key, uint32_t *keylen, char **data, uint64_t *datalen);
#endif
extern int PMapDelete(struct PMap *pm, char *key, uint32_t keylen, uint64_t cguid, int robj, void *replacement_callback_data);
extern uint64_t PMapNEntries(struct PMap *pm);

#endif /* _BTREE_MAP_H */
