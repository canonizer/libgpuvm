/** @file region.c implementation of region_t */

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "gpuvm.h"
#include "region.h"
#include "subreg.h"
#include "util.h"

/** single node of the region tree */
typedef struct region_node_struct {
	/** region stored at this node */
	region_t *region;
	/** right subtree (regions with greater addresses) */
	struct region_node_struct *right;
	/** left subtree (regions with smaller addresses) */
	struct region_node_struct *left;
} region_node_t;

/** root of the region tree */
region_node_t *region_tree_g = 0;

/** recursive function adding a region to a tree node
		@param pnode [inout] the node to which the region is being added. *pnode may be 0 in which case a new
		node is allocated
		@param region the region being added to the tree
		@returns new node if successful and 0 if 
 */
static int tree_add_to_node(region_node_t **pnode, region_t *region) {
	if(*pnode) {
		switch(memrange_cmp(&region->range, &(*pnode)->region->range)) {
		case MR_CMP_LT: 
			return tree_add_to_node(&(*pnode)->left, region);
		case MR_CMP_GT:
			return tree_add_to_node(&(*pnode)->right, region);
		case MR_CMP_EQ: case MR_CMP_INT:
			fprintf(stderr, "tree_add_to_node: same or intersecting region exists\n");
			return GPUVM_ERANGE;
		}
	} else {
		// a new node is needed
		*pnode = (region_node_t*)smalloc(sizeof(region_node_t));
		if(!*pnode)
			return GPUVM_ESALLOC;
		(*pnode)->left = (*pnode)->right = 0;
		(*pnode)->region = region;
		return 0;
	}	
}  // tree_add_to_node()

/** adds a newly allocated region to a tree 
		@param region new region being added
		@returns 0 if successful and negative error code if not. Possible errors include
		allocation errors and adding a region whose range intersects that of some other region
		in the tree
 */
static int tree_add(region_t *region) {
	return tree_add_to_node(&region_tree_g, region);
}

/** finds a region in the region tree by pointer 
		@param node the tree in which to find the pointer
		@param ptr the pointer to find
		@returns pointer to the region if found and 0 if not
 */
static region_t *tree_find_region(const region_node_t *node, const void *ptr) {
	// node is 0 - not found
	if(!node)
		return 0;
	// non-zero node - further search
	switch(memrange_pos_ptr(&node->region->range, ptr)) {
	case MR_CMP_LT:
		return tree_find_region(node->left, ptr);
	case MR_CMP_INT:
		return node->region;
	case MR_CMP_GT:
		return tree_find_region(node->right, ptr);
	default:
		// shall not happen
		assert(0);
	}
}  // tree_find_region

/** finds address of loca*/
static region_node_t **tree_min_pnode(region_node_t **pnode) {
	if((*pnode)->left)
		tree_min_pnode(&(*pnode)->left);
	else
		pnode;
}

/** removes the tree from the node, if any 
		@param pnode *pnode is the node the region is being removed from. Passed by reference
		as there may be a different node
		@param region the region to be removed
 */
static void tree_remove_from_node(region_node_t **pnode, const region_t *region) {
	if(!*pnode)
		return;
	if((*pnode)->region == region) {
		// remove from this node
		region_node_t *node = *pnode;
		if(!node->left && !node->right) {
			*pnode = 0;
		} else if(!node->left || !node->right) {
			if(!node->left) {
				*pnode = node->right;
			  sfree(node);
			} else {
				*pnode = node->left;
				sfree(node);
			}
		} else {
			// both nodes are non-null find min in right subtree
			// TODO: add implementations which alternate between max in left subtree and min in
			// right subtree
			region_node_t **pmin_node = tree_min_pnode(&node->right);
			node->region = (*pmin_node)->region;
			sfree(*pmin_node);
			*pmin_node = 0;
		}
	} else {
		// remove from one of subnodes
		memrange_cmp_t cmp_res = memrange_cmp(&region->range, &(*pnode)->region->range);
		if(cmp_res == MR_CMP_LT) 
			tree_remove_from_node(&(*pnode)->left, region);
		else if(cmp_res == MR_CMP_GT) 
			tree_remove_from_node(&(*pnode)->right, region);
		else {
			fprintf(stderr, "tree_remove_from_node: region intersecting node region " 
							"but not equal to it\n");
		}
	}
}  // tree_remove_from_node()

/** removes the region from the region tree
		@region the region to remove
 */
static void tree_remove(const region_t *region) {
	tree_remove_from_node(&region_tree_g, region);
}

int region_alloc(region_t **p, subreg_t *subreg) {
	if(p)
		*p = 0;
	// allocate memory
	region_t *new_region = (region_t*)smalloc(sizeof(region_t));
	if(!new_region)
		return GPUVM_ESALLOC;
	memset(new_region, 0, sizeof(region_t));
	
	// initialize members
	new_region->range.ptr = 
		(void*)((ptrdiff_t)subreg->range.ptr / GPUVM_PAGE_SIZE * GPUVM_PAGE_SIZE);
	new_region->range.nbytes = 
		(((ptrdiff_t)subreg->range.ptr + subreg->range.nbytes - 1)
		 / GPUVM_PAGE_SIZE + 1) * GPUVM_PAGE_SIZE - (ptrdiff_t)new_region->range.ptr;
	new_region->prot_status = PROT_READ | PROT_WRITE;
	new_region->nsubregs = 1;
	if(sem_init(&new_region->unprot_sem, 0, 0)) {
		fprintf(stderr, "region_alloc: can\'t initialize unrpot semaphore\n");
		sfree(new_region);
		return GPUVM_ERROR;
	}
	
	// initialize subregion list
	new_region->subreg_list = (subreg_list_t*)smalloc(sizeof(subreg_list_t));
	if(!new_region->subreg_list) {
		sem_destroy(&new_region->unprot_sem);
		sfree(new_region);
		return GPUVM_ESALLOC;
	}
	new_region->subreg_list->subreg = subreg;
	new_region->subreg_list->next = 0;
	
	// insert region into tree
	int err = tree_add(new_region);
	if(err) {
		sfree(new_region->subreg_list->subreg);
		sem_destroy(&new_region->unprot_sem);
		sfree(new_region);
		return err;
	}
	subreg->region = new_region;
	if(p)
		*p = new_region;
	return 0;
}  // region_alloc

int region_protect(region_t *region) {
	if(mprotect(region->range.ptr, region->range.nbytes, PROT_NONE)) {
		fprintf(stderr, "region_protect: can\'t set memory protection\n");
		return GPUVM_EPROT;
	}
	region->prot_status = PROT_NONE;
	return 0;
}

int region_is_protected(const region_t *region) {
	return region->prot_status != (PROT_READ | PROT_WRITE);
}

int region_unprotect(region_t *region) {
	if(mprotect(region->range.ptr, region->range.nbytes, PROT_READ | PROT_WRITE)) {
		fprintf(stderr, "region_unprotect: can\'t remove memory protection\n");
		return GPUVM_EPROT;
	}
	region->prot_status = PROT_READ | PROT_WRITE;
	return 0;
}

int region_wait_unprotect(region_t *region) {
	if(sem_wait(&region->unprot_sem)) {
		fprintf(stderr, "region_wait_unprotect: can\'t wait for semaphore\n");
		return -1;
	}
	return 0;
}

int region_post_unprotect(region_t *region) {
	if(sem_post(&region->unprot_sem)) {
		fprintf(stderr, "region_post_unprotect: can\'t post to semaphore\n");
		return -1;
	}
	return 0;
}

void region_free(region_t *region) {
	tree_remove(region);
	if(region->subreg_list)
		fprintf(stderr, "region_free: removing region with subregions\n");
	sem_destroy(&region->unprot_sem);
	if(region->prot_status != (PROT_READ | PROT_WRITE))
		region_unprotect(region);
	sfree(region);
}

int region_add_subreg(region_t *region, subreg_t *subreg) {
	// check if subreg belongs to the region
	if(!memrange_is_inside(&region->range, &subreg->range))
		return GPUVM_ERROR;
	subreg_list_t *new_list = (subreg_list_t*)smalloc(sizeof(subreg_list_t));
	if(!new_list) 
		return GPUVM_ESALLOC;
	new_list->subreg = subreg;
	new_list->next = 0;
	// find insertion point
	subreg_list_t **plist = 0;
	memrange_t range = subreg->range;
	for(plist = &region->subreg_list; *plist; plist = &(*plist)->next) {
		memrange_cmp_t cmp_res = memrange_cmp(&range, &(*plist)->subreg->range);
		if(cmp_res == MR_CMP_LT) {
			// insert position found
			break;
		} else if(cmp_res == MR_CMP_INT || cmp_res == MR_CMP_INT) {
			// error - ranges mustn't intersect
			sfree(new_list);
			return GPUVM_ERANGE;
		}
		// MR_CMP_GT - continue search
	}  // for(plist)
	
	// do insertion
	new_list->next = *plist;
	*plist = new_list;
	subreg->region = region;
	region->nsubregs++;
	return 0;
}  // region_add_subreg

int region_remove_subreg(region_t *region, subreg_t *subreg) {
	subreg_list_t **plist;
	for(plist = &region->subreg_list; *plist; plist = &(*plist)->next) {
		if((*plist)->subreg == subreg) {
			// remove subregion
			subreg_list_t *next = (*plist)->next;
			sfree(*plist);
			(*plist) = next;
			region->nsubregs--;
			break;
		}
	}
	return 0;
}

region_t *region_find_region(const void *ptr) {
	return tree_find_region(region_tree_g, ptr);
}

subreg_t *region_find_subreg(const region_t *region, const void *ptr) {
	if(memrange_pos_ptr(&region->range, ptr) != MR_CMP_INT) 
		return 0;
	subreg_list_t *list;
	for(list = region->subreg_list; list; list = list->next) 
		if(memrange_pos_ptr(&list->subreg->range, ptr) == MR_CMP_INT)
			return list->subreg;
	return 0;
}

int region_lock(region_t *region) {
	// no-op - due to global lock 
	// if(pthread_mutex_lock(&region->mutex)) {
	//	fprintf(stderr, "region_lock: can\'t lock mutex\n");
	//	return GPUVM_ERROR;
	//}
	return 0;
}

int region_unlock(region_t *region) {
	// no-op - due to global lock
	// if(pthread_mutex_unlock(&region->mutex)) {
	//	fprintf(stderr, "region_unlock: can\'t unlock mutex\n");
	//	return GPUVM_ERROR;
	//}
	return 0;
}
