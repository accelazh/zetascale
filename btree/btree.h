/************************************************************************
 * 
 *  btree.h  Jan. 21, 2013   Brian O'Krafka
 * 
 *  btree code for a multi-threaded b-tree implementation that uses 
 *  multiple partitions implemented using the btree_raw single-threaded
 *  btree.
 * 
 * xxxzzz NOTES:
 *     - check all uses of "ret"
 *     - make sure that all btree updates are logged!
 *     - add doxygen comments for all functions
 *     - make asserts a compile time option
 *     - make sure that left/right node pointers are properly maintained
 *     - check insert_ptr arithmetic
 *     - optimize key search within a node?
 * 
 ************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include "btree_raw.h"


#ifndef __BTREE_H
#define __BTREE_H

struct btree;

extern struct btree *btree_init(uint32_t n_partitions, uint32_t flags, uint32_t max_key_size, uint32_t min_keys_per_node, uint32_t nodesize, uint32_t n_l1cache_buckets, create_node_cb_t *create_node_cb, void *create_node_data, read_node_cb_t *read_node_cb, void *read_node_cb_data, write_node_cb_t *write_node_cb, void *write_node_cb_data, freebuf_cb_t *freebuf_cb, void *freebuf_cb_data, delete_node_cb_t *delete_node_cb, void *delete_node_data, log_cb_t *log_cb, void *log_cb_data, msg_cb_t *msg_cb, void *msg_cb_data, cmp_cb_t *cmp_cb, void * cmp_cb_data, txn_cmd_cb_t *txn_cmd_cb, void *txn_cmd_data);

extern int btree_destroy(struct btree *btree);

extern int btree_get(struct btree *btree, char *key, uint32_t keylen, char **data, uint64_t *datalen, btree_metadata_t *meta);

extern int btree_insert(struct btree *btree, char *key, uint32_t keylen, char *data, uint64_t datalen, btree_metadata_t *meta);

extern int btree_update(struct btree *btree, char *key, uint32_t keylen, char *data, uint64_t datalen, btree_metadata_t *meta);

extern int btree_set(struct btree *btree, char *key, uint32_t keylen, char *data, uint64_t datalen, btree_metadata_t *meta);

/*   delete a key
 *
 *   returns 0: success
 *   returns 1: key not found
 */
extern int btree_delete(struct btree *btree, char *key, uint32_t keylen, btree_metadata_t *meta);

/* Like btree_get, but gets next n_in keys after a specified key.
 * Use key=NULL and keylen=0 for first call in enumeration.
 */
extern int btree_get_next_n(uint32_t n_in, uint32_t *n_out, struct btree *btree, char *key_in, uint32_t keylen_in, char **keys_out, uint32_t *keylens_out, char **data_out, uint64_t datalens_out, btree_metadata_t *meta);

extern int btree_fast_build(struct btree *btree);

extern void btree_dump(FILE *f, struct btree *btree);

extern void btree_check(struct btree *btree);

extern void btree_test(struct btree *btree);

extern int btree_snapshot(struct btree *btree, uint64_t *seqno);
extern int btree_delete_snapshot(struct btree *btree, uint64_t seqno);
extern int btree_get_snapshots(struct btree *btree, uint32_t *n_snapshots, uint64_t *seqnos);
extern int btree_free_buffer(struct btree *btree, char *key, uint32_t keylen, char *buf);

extern void btree_get_stats(struct btree *btree, btree_stats_t *stats);

#endif // __BTREE_H