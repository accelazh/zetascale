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
 * File:   cmap.c
 * Author: Brian O'Krafka
 *
 * Created on September 11, 2008
 *
 * SanDisk Proprietary Material, © Copyright 2012 SanDisk, all rights reserved.
 * http://www.sandisk.com
 *
 * IMPORTANT NOTES:
 *    - Unlike tlmap in the zs directory, zs_tlmap does NOT
 *      automatically malloc and free the key and contest of
 *      a hashtable entry!
 *
 * $Id: tlmap.c 308 2008-02-20 22:34:58Z tomr $
 */
#define _CMAP_C

#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <execinfo.h>
#include "cmap.h"
#include "cmap_internal.h"
#include "cmap_hash.h"
#include <assert.h>

//  Define this to turn on detailed list checking
// #define LISTCHECK

//disable messages in debug trace by default
#ifdef DBG_PRINT
#undef DBG_PRINT
#endif

#ifdef DBG_PRINT
#define dbg_print(msg, ...) do { fprintf(stderr, "%x %s:%d " msg, (int)pthread_self(), __FUNCTION__, __LINE__, ##__VA_ARGS__); } while(0)
#else
#define dbg_print(msg, ...)
#endif

static void _map_assert(int x) {
    if (x) {
        fprintf(stderr, "Assertion failure in cmap!\n");
	    assert(0);
    }
}
#define map_assert(x) _map_assert((x) == 0)

//#define do_lock(x)  {if (pm->use_locks) { pthread_mutex_lock(x); }}
//#define do_unlock(x) {if (pm->use_locks) { pthread_mutex_unlock(x); }}

#define atomic_inc(x) (void)__sync_fetch_and_add(&(x), 1);
#define atomic_dec(x) (void)__sync_fetch_and_sub(&(x), 1);

#define do_lock_read(x)  {if (pm->use_locks) { pthread_rwlock_rdlock(&(x)->lock); }}
#define do_lock_write(x)  {if (pm->use_locks) { pthread_rwlock_wrlock(&(x)->lock); }}
#define do_unlock(x) {if (pm->use_locks) { pthread_rwlock_unlock(&(x)->lock); }}

//  Predeclarations
#ifdef LISTCHECK
static void check_list(CMapBucket_t *pb, CMapEntry_t *pme);
#endif /* LISTCHECK */
static void insert_lru(struct CMap *pm, CMapEntry_t *pme);
static void remove_lru(struct CMap *pm, CMapEntry_t *pme);
static void update_lru(struct CMap *pm, CMapEntry_t *pme);
static void replace_lru(struct CMap *pm, CMapEntry_t *pme);
static CMapEntry_t *find_pme(struct CMap *pm, char *pkey, uint32_t keylen, CMapBucket_t **pb);
static CMapEntry_t *create_pme(CMap_t *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen);
// static CMapEntry_t *copy_pme(CMap_t *pm, CMapEntry_t *pme);
// static CMapEntry_t *copy_pme_list(CMap_t *pm, CMapEntry_t *pme);
static void free_pme(CMap_t *pm, CMapEntry_t *pme);
static void free_pme_list(CMap_t *pm, CMapEntry_t *pme);


static CMapEntry_t *get_entry(CMap_t *pm)
{
    int                i;
    CMapEntry_t  *e;

    if (pm->FreeEntries == NULL) {
        e = (CMapEntry_t *) malloc(N_ENTRIES_TO_MALLOC*sizeof(CMapEntry_t));
	map_assert(e);

	for (i=0; i<N_ENTRIES_TO_MALLOC; i++) {
	    e[i].next = pm->FreeEntries;
	    pm->FreeEntries = &(e[i]);
	}
        pm->NEntries += N_ENTRIES_TO_MALLOC;
    }
    e = pm->FreeEntries;
    pm->FreeEntries = e->next;

    pm->NUsedEntries++;
    return(e);
}

static void free_entry(CMap_t *pm, CMapEntry_t *e)
{
    e->next     = pm->FreeEntries;
    pm->FreeEntries = e;
    pm->NUsedEntries--;
}

static struct CMapIterator *get_iterator(CMap_t *pm)
{
    int                   i;
    struct CMapIterator *it;

    if (pm->FreeIterators == NULL) {
        it = (struct CMapIterator *) malloc(N_ITERATORS_TO_MALLOC*sizeof(struct CMapIterator));
	map_assert(it);

	for (i=0; i<N_ITERATORS_TO_MALLOC; i++) {
	    it[i].next = pm->FreeIterators;
	    pm->FreeIterators = &(it[i]);
	}
        pm->NIterators += N_ITERATORS_TO_MALLOC;
    }
    it = pm->FreeIterators;
    pm->FreeIterators = it->next;

    pm->NUsedIterators++;
    return(it);
}

static void free_iterator(CMap_t *pm, struct CMapIterator *it)
{
    it->next     = pm->FreeIterators;
    pm->FreeIterators = it;
    pm->NUsedIterators--;
}

struct CMap *CMapInit(uint64_t nbuckets, uint64_t max_entries, char use_locks, void (*replacement_callback)(void *callback_data, char *key, uint32_t keylen, char *pdata, uint64_t datalen), void *replacement_callback_data, 
						void (*delete_callback)(void *))
{
    uint64_t          i;
    CMap_t       *pm;

    pm = (CMap_t *) malloc(sizeof(CMap_t));
    map_assert(pm);
    pm->lru_head       = NULL;
    pm->lru_tail       = NULL;
    pm->clock_hand     = NULL;
    pm->replacement_callback       = replacement_callback;
    pm->replacement_callback_data  = replacement_callback_data;
    pm->delete_callback = delete_callback;
    pm->use_locks      = use_locks;
    pm->max_entries    = max_entries;
    pm->n_entries      = 0;
    pm->nbuckets       = nbuckets;
    pm->NEntries       = 0;
    pm->NUsedEntries   = 0;
    pm->FreeEntries    = NULL;
    pm->NIterators     = 0;
    pm->NUsedIterators = 0;
    pm->FreeIterators  = NULL;

    pm->buckets       = (CMapBucket_t *) malloc(nbuckets*(sizeof(CMapBucket_t)));
    map_assert(pm->buckets);

    for (i=0; i<nbuckets; i++) {
	pm->buckets[i].entry = NULL;
    }

    pthread_rwlock_init(&pm->lock, NULL);

    // xxxzzz only one enumeration can be done at a time per Map!
    pthread_mutex_init(&(pm->enum_mutex), NULL);

	dbg_print("pm=%p\n", pm);

    return(pm);
}

uint64_t CMapNEntries(struct CMap *pm)
{
    return(pm->n_entries);
}

void CMapDestroy(struct CMap *pm)
{
    uint64_t          i;

	dbg_print("pm=%p\n", pm);

    for (i=0; i<pm->nbuckets; i++) {
        free_pme_list(pm, pm->buckets[i].entry);
    }

    free(pm->buckets);
    pthread_rwlock_destroy(&(pm->lock));
    free(pm);
}

void CMapClear(struct CMap *pm)
{
    CMapEntry_t   *pme, *pme_next;

    do_lock_write(pm);

	dbg_print("pm=%p\n", pm);

    //  Go through LRU list and free entries
    //  This is much faster than examining all of the buckets!

    for (pme=pm->lru_head; pme != NULL; pme = pme_next) {
        pme_next = pme->next_lru;
	pme->bucket->entry = NULL;
        free_pme(pm, pme);
    }

    pm->n_entries     = 0;
    pm->lru_head      = NULL;
    pm->lru_tail      = NULL;
    do_unlock(pm);
}

void CMapCheckRefcnts(struct CMap *pm)
{
    uint64_t           i;
    CMapEntry_t   *pme;

    do_lock_write(pm);

	dbg_print("pm=%p\n", pm);

    for (i=0; i<pm->nbuckets; i++) {
	for (pme = pm->buckets[i].entry; pme != NULL; pme = pme->next) {
	    if (pme->refcnt != 0) {
		fprintf(stderr, "*****************   CMapCheckRefcnts: [pm=%p]: key 0x%lx refcnt=%d!\n", pm, *((uint64_t *) pme->key), pme->refcnt);
	    }
	}
    }
    do_unlock(pm);
}

//  Return non-NULL if success, NULL if object exists
struct CMapEntry *CMapCreate(struct CMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen)
{
    CMapEntry_t   *pme;
    CMapBucket_t  *pb;

    do_lock_write(pm);

	dbg_print("pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld, ", pm, *((uint64_t *) pkey), keylen, pdata, datalen);

    pme = find_pme(pm, pkey, keylen, &pb);

    if (pme != NULL) {
	    dbg_print("pme=%p\n", pme);

fprintf(stderr, "CMapCreate: entry exists!\n");

	    do_unlock(pm);
        return(NULL);
    }

    /* Create a new entry. */

    pme = create_pme(pm, pkey, keylen, pdata, datalen);

assert(pme != NULL);

    /* put myself on the bucket list */
    pme->refcnt = 1;
    pme->bucket = pb;
    pme->next = pb->entry;
    pb->entry = pme;
    #ifdef LISTCHECK
    check_list(pb, pme);
    #endif

    pm->n_entries++;

    /* put myself on the lru list */
    {
        insert_lru(pm, pme);

	    if ((pm->max_entries > 0) && (pm->n_entries > pm->max_entries)) {
	        // do an LRU replacement
	        replace_lru(pm, pme);
	    }
    }

    do_unlock(pm);

	dbg_print("pme=%p\n", pme);

    return(pme);
}

//  Return non-NULL if success, NULL if object does not exist
struct CMapEntry *CMapUpdate(struct CMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen)
{
    CMapEntry_t   *pme;

    do_lock_write(pm);

	dbg_print("pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld\n", pm, *((uint64_t *) pkey), keylen, pdata, datalen);

    pme = find_pme(pm, pkey, keylen, NULL);

    if (pme == NULL) {
	do_unlock(pm);
        return(NULL);
    }

    /* Update an existing entry. */

    // update the LRU list if necessary
    if (pm->max_entries != 0) {
	update_lru(pm, pme);
    }

    if (pm->replacement_callback) {
        //  return NULL for key so that it is not freed!
	(pm->replacement_callback)(pm->replacement_callback_data, pme->key, pme->keylen, pme->contents, pme->datalen);
    }

    pme->contents = pdata;
    pme->datalen  = datalen;

    do_unlock(pm);
    return(pme);
}

//  Return non-NULL if success, NULL if object exists
struct CMapEntry *CMapSet(struct CMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen, char **old_pdata, uint64_t *old_datalen)
{
    CMapEntry_t   *pme;
    CMapBucket_t  *pb;

    do_lock_write(pm);

	dbg_print("pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld\n", pm, *((uint64_t *) pkey), keylen, pdata, datalen);

    pme = find_pme(pm, pkey, keylen, &pb);

    if (pme != NULL) {

	/* Update an existing entry. */

	if (pm->replacement_callback) {
	    //  return NULL for key so that it is not freed!
	    (pm->replacement_callback)(pm->replacement_callback_data, NULL, 0, pme->contents, pme->datalen);
	}

	*old_pdata   = pme->contents;
	*old_datalen = pme->datalen;

	pme->contents = pdata;
	pme->datalen  = datalen;

	/* update the lru list if necessary */
	if (pm->max_entries != 0) {
	    update_lru(pm, pme);
	}

    } else {

	/* Create a new entry. */

	pme = create_pme(pm, pkey, keylen, pdata, datalen);

	*old_pdata   = NULL;
	*old_datalen = 0;

	/* put myself on the bucket list */
	pme->refcnt = 1;
	pme->bucket = pb;
	pme->next = pb->entry;
	pb->entry = pme;
	#ifdef LISTCHECK
	    check_list(pb, pme);
	#endif

	pm->n_entries++;

	/* put myself on the lru list */
	{
	    insert_lru(pm, pme);

	    if ((pm->max_entries != 0) && (pm->n_entries > pm->max_entries)) {
		// do an LRU replacement
		replace_lru(pm, pme);
	    }
	}
    }

    do_unlock(pm);
    return(pme);
}

//  Returns non-NULL if successful, NULL otherwise
struct CMapEntry *CMapGet(struct CMap *pm, char *key, uint32_t keylen, char **pdata, uint64_t *pdatalen)
{
    uint64_t           datalen;
    CMapEntry_t   *pme;
    char              *data;

    do_lock_read(pm);

    pme = find_pme(pm, key, keylen, NULL);

    if (pme != NULL) {
        data    = pme->contents;
		datalen = pme->datalen;
		assert(pme->refcnt < 10000);
		atomic_inc(pme->refcnt);

		// update the LRU list if necessary
		if (pm->max_entries != 0) {
			update_lru(pm, pme);
		}

    } else {
        data    = NULL;
		datalen = 0;
    }

	dbg_print("pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld, refcnt=%d, pme=%p\n", pm, *((uint64_t *) key), keylen, data, datalen, pme ? pme->refcnt : 0, pme);

#ifdef notdef
	void* tracePtrs[100];
	int count = backtrace( tracePtrs, 100 );

	char** funcNames = backtrace_symbols( tracePtrs, count );

	// Print the stack trace
	printf("---------------------------------------------------------------------------\n");
	for( int ii = 0; ii < count; ii++ ) {
		printf( "%s\n", funcNames[ii] );
	}
	printf("---------------------------------------------------------------------------\n");

	// Free the string pointers
	free( funcNames );
#endif

    do_unlock(pm);
    *pdatalen = datalen;
    *pdata    = data;
    return(pme);
}

//  Increment the reference count for this entry
//  rc=1 if entry is found, rc=0 otherwise
int CMapGetRefcnt(struct CMap *pm, char *key, uint32_t keylen)
{
    CMapEntry_t   *pme;
    int res;

    do_lock_read(pm);

    pme = find_pme(pm, key, keylen, NULL);

    assert(pme);
    res = (pme->refcnt);

    do_unlock(pm);
    return(res);
}
//  Increment the reference count for this entry
//  rc=1 if entry is found, rc=0 otherwise
int CMapIncrRefcnt(struct CMap *pm, char *key, uint32_t keylen)
{
    CMapEntry_t   *pme;
    int                rc = 0;

    do_lock_read(pm);

	dbg_print("pm=%p, key=0x%lx, keylen=%d", pm, *((uint64_t *) key), keylen);

    pme = find_pme(pm, key, keylen, NULL);

    if (pme != NULL) {
// fprintf(stderr, ", after incr refcnt [0x%lx] =%d\n", *((uint64_t *) key), pme->refcnt + 1);
	    dbg_print(", after incr refcnt=%d\n", pme->refcnt + 1);
        atomic_inc(pme->refcnt);
	rc = 1;
    } else {
	    dbg_print(" (KEY NOT FOUND!)\n");
    }

    do_unlock(pm);
    return(rc);
}

//  Decrement the reference count for this entry
//  rc=1 if entry is found, rc=0 otherwise
int CMapRelease(struct CMap *pm, char *key, uint32_t keylen)
{
    CMapEntry_t   *pme;
    int                rc = 0;

    do_lock_read(pm);

	dbg_print("pm=%p, key=0x%lx, keylen=%d", pm, *((uint64_t *) key), keylen);

    pme = find_pme(pm, key, keylen, NULL);

    if (pme != NULL) {
// fprintf(stderr, ", after release [0x%lx] =%d\n", *((uint64_t *) key), pme->refcnt - 1);
	    dbg_print(", after release refcnt=%d\n", pme->refcnt - 1);
        atomic_dec(pme->refcnt); //xxxzzz check this!
        //pme->refcnt = 0;
		rc = 1;
    } else {
	    dbg_print(" (KEY NOT FOUND!)\n");
    }

    do_unlock(pm);
    return(rc);
}

int CMapRelease_fix(struct CMapEntry *pme)
{
	atomic_dec(pme->refcnt);
	return (1);
}
#if 0
//  Decrement the reference count for this entry
int CMapReleaseEntry(struct CMap *pm, struct CMapEntry *pme)
{
    int rc = 0;

    do_lock_read(pm);

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "CMapReleaseEntry: pm=%p, pme=%p\n", pm, pme);
    #endif

    if (pme != NULL) {
        (pme->refcnt)--;
	rc = 1;
    }

    do_unlock(pm);
    return(rc);
}
#endif

struct CMapIterator *CMapEnum(struct CMap *pm)
{
    CMapIterator_t    *iterator;

    do_lock_read(pm);

    iterator = get_iterator(pm);
    map_assert(iterator);

	dbg_print("pm=%p, iterator=%p, ", pm, iterator);

    iterator->enum_entry = pm->lru_head;

	dbg_print("pme=%p, enum_entry=%p\n", pm, iterator->enum_entry);

    return(iterator);
}

void CMapFinishEnum(struct CMap *pm, struct CMapIterator *iterator)
{
	dbg_print("pm=%p, iterator=%p\n", pm, iterator);

    free_iterator(pm, iterator);

    do_unlock(pm);
}

//  Returns 1 if successful, 0 otherwise
//  Caller is responsible for freeing key and data
int CMapNextEnum(struct CMap *pm, struct CMapIterator *iterator, char **key, uint32_t *keylen, char **data, uint64_t *datalen) 
{
    CMapEntry_t    *pme_return;

	dbg_print("pm=%p, iterator=%p, ", pm, iterator);

    if (iterator->enum_entry == NULL) {

	dbg_print("pdata=NULL, datalen=0\n");

        *key     = NULL;
	*keylen  = 0;
        *data    = NULL;
	*datalen = 0;
        return(0);
    }
    pme_return = iterator->enum_entry;

    iterator->enum_entry = pme_return->next_lru;

    if (pme_return != NULL) {
	*key     = pme_return->key;
	*keylen  = pme_return->keylen;

	*data    = (char *) pme_return->contents;
	*datalen = pme_return->datalen;

	dbg_print("key=%p, keylen=%d, pdata=%p, datalen=%ld, pme_return=%p\n", *key, *keylen, *data, *datalen, pme_return);

	return(1);
    } else {

	dbg_print("pdata=NULL, datalen=0\n");

        *key     = NULL;
	*keylen  = 0;
        *data    = NULL;
	*datalen = 0;
	return(0);
    }
}

/*   Return 0 if succeeds, 1 if object doesn't exist.
 */
int CMapDelete(struct CMap *pm, char *key, uint32_t keylen)
{
    uint64_t            h;
    CMapEntry_t   **ppme;
    CMapEntry_t    *pme;
    CMapBucket_t   *pb;
    char               *key2;

    key2 = (char *) *((uint64_t *) key);

    do_lock_write(pm);

    dbg_print("pm=%p, key=0x%lx, keylen=%d\n", pm, *((uint64_t *) key), keylen);

    h = cmap_hash((const unsigned char *) key, sizeof(uint64_t), 0) % pm->nbuckets;
    pb = &(pm->buckets[h]);

    map_assert(keylen == 8); // remove this! xxxzzz

    for (ppme = &(pb->entry); (*ppme) != NULL; ppme = &((*ppme)->next)) {
	pme = *ppme;
	if (pme->key == key2) {

	    //  Remove from the LRU list if necessary
	    remove_lru(pm, pme);
	    pm->n_entries--;

            *ppme = pme->next;
            free_pme(pm, pme);
	    do_unlock(pm);
            return (0);
        }
    }
    do_unlock(pm);
    return (1);
}

static CMapEntry_t *create_pme(CMap_t *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen)
{
    CMapEntry_t   *pme;

    pme = get_entry(pm);

    if (keylen == 8) {
		pme->key = (char *) *((uint64_t *) pkey);
    } else {
        map_assert(0);
    }
    pme->keylen   = keylen;
    pme->refcnt   = 0;

    pme->contents  = pdata;
    pme->datalen   = datalen;

    pme->next      = NULL;

    pme->ref       = 0;

    return(pme);
}

#ifdef notdef
static CMapEntry_t *copy_pme(CMap_t *pm, CMapEntry_t *pme)
{
    CMapEntry_t  *pme2;

    pme2 = get_entry(pm);

    pme2->key    = pme->key;
    pme2->keylen = pme->keylen;

    pme2->contents = pme->contents;
    pme2->datalen  = pme->datalen;

	dbg_print("copy_pme [from pme %p to %p] key=%p, keylen=%d, data=%p, datalen=%ld\n", pme, pme2, pme->key, pme->keylen, pme->contents, pme->datalen);

    pme2->next   = NULL;
    pme2->bucket = pme->bucket;

    return(pme2);
}

static CMapEntry_t *copy_pme_list(CMap_t *pm, CMapEntry_t *pme_in)
{
    CMapEntry_t  *pme, *pme2, *pme_out;

    pme_out = NULL;
    for (pme = pme_in; pme != NULL; pme = pme->next) {
        pme2 = copy_pme(pm, pme);
	pme2->next = pme_out;
	pme_out = pme2;
    }
    return(pme_out);
}
#endif

static void free_pme(CMap_t *pm, CMapEntry_t *pme)
{
	if (pme->contents) {
		pm->delete_callback(pme->contents);
		pme->contents = NULL;
	}
    free_entry(pm, pme);
}

static void free_pme_list(CMap_t *pm, CMapEntry_t *pme_in)
{
    CMapEntry_t  *pme, *pme_next;

    for (pme = pme_in; pme != NULL; pme = pme_next) {
        pme_next = pme->next;
        free_pme(pm, pme);
    }
}

static void insert_lru(struct CMap *pm, CMapEntry_t *pme)
{
    pme->next_lru = pm->lru_head;
    if (pm->lru_head != NULL) {
        pm->lru_head->prev_lru = pme;
    }
    pme->prev_lru = NULL;
    pm->lru_head = pme;
    if (pm->lru_tail == NULL) {
	pm->lru_tail = pme;
    }
}

static void remove_lru(struct CMap *pm, CMapEntry_t *pme)
{
    if(pm->clock_hand == pme)
        pm->clock_hand = pme->next_lru;

    if (pme->next_lru == NULL) {
	map_assert(pm->lru_tail == pme); // xxxzzz remove this!
	pm->lru_tail = pme->prev_lru;
    } else {
	pme->next_lru->prev_lru = pme->prev_lru;
    }
    if (pme->prev_lru == NULL) {
	map_assert(pm->lru_head == pme); // xxxzzz remove this!
	pm->lru_head = pme->next_lru;
    } else {
	pme->prev_lru->next_lru = pme->next_lru;
    }
}

static void update_lru(struct CMap *pm, CMapEntry_t *pme)
{
    pme->ref = 1;
    //  remove pme from list
    //remove_lru(pm, pme);
    //  insert_lru(pm, pme)
    //insert_lru(pm, pme);
}

static void replace_lru(struct CMap *pm, CMapEntry_t *pme_in)
{
    CMapEntry_t **ppme;
    CMapEntry_t  *pme;
    int               found_it;

     pme = pm->clock_hand;
     if(!pme)
 	pme = pm->lru_head;
 
     while(pme->ref || pme->refcnt) {
 	pme->ref = 0;
 	pme = pme->next_lru;
 	if(!pme)
 	    pme = pm->lru_head;
    }
 
     pm->clock_hand = pme->next_lru;
 
    if (pme == NULL) {
	fprintf(stderr, "replace_lru could not find a victim!!!!\n");
	map_assert(0);
    }
	dbg_print("replace_lru found a victim: %p\n", pme);

    //  Remove from bucket list
    found_it = 0;
    for (ppme = &(pme->bucket->entry); (*ppme) != NULL; ppme = &((*ppme)->next)) {
	if (pme == (*ppme)) {
	    *ppme = pme->next;
	    found_it = 1;
	    break;
	}
    }
    map_assert(found_it);

    remove_lru(pm, pme);
    pm->n_entries--;
    (pm->replacement_callback)(pm->replacement_callback_data, pme->key, pme->keylen, pme->contents, pme->datalen);
    free_pme(pm, pme);

}

static CMapEntry_t *find_pme(struct CMap *pm, char *pkey, uint32_t keylen, CMapBucket_t **pb_out)
{
    uint64_t           h;
    CMapBucket_t  *pb;
    CMapEntry_t   *pme = NULL;
    char              *key2;
    key2 = (char *) *((uint64_t *) pkey);

    h = cmap_hash((const unsigned char *) pkey, keylen, 0) % pm->nbuckets;
    pb = &(pm->buckets[h]);

    if (keylen == 8) {
	for (pme = pb->entry; pme != NULL; pme = pme->next) {
	    if (pme->key == key2) {
		break;
	    }
	}
    } else {
        map_assert(0);
    }
    if (pb_out) {
        *pb_out = pb;
    }
    return(pme);
}

#ifdef LISTCHECK
static void check_list(CMapBucket_t *pb, CMapEntry_t *pme_in)
{
    CMapEntry_t *pme, *pme2;
    //  check for a circular list

    for (pme = pb->entry; pme != NULL; pme = pme->next) {
	for (pme2 = pme->next; pme2 != NULL; pme2 = pme2->next) {
	    if (pme2 == pme) {
                fprintf(stderr, "Circular pme list!\n");
		map_assert(0);
	    }
	}
    }
}
#endif /* LISTCHECK */
