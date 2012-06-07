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

static void tree_dump(const region_node_t *node, int depth);

/** recursive function adding a region to a tree node
		@param pnode [inout] the node to which the region is being added. *pnode may be 0 in which case a new
		node is allocated
		@param region the region being added to the tree
		@returns new node if successful and 0 if 
 */
static int tree_add_to_node(region_node_t **pnode, region_t *region) {
	//fprintf(stderr, "region = %x, node = %x\n", region, *pnode);
	if(*pnode) {
		region_t *node_region = (*pnode)->region;
		//fprintf(stderr, "node->region = %x\n", node_region);
		//fprintf(stderr, "node->left = %x\n", (*pnode)->left);
		//fprintf(stderr, "node->right = %x\n", (*pnode)->right);*/
		int err;
		if(!node_region) {
			fprintf(stderr, "tree_add_to_node: suddenly, region is NULL\n");
			return GPUVM_ERROR;
		}
		switch(memrange_cmp(&region->range, &node_region->range)) {
		case MR_CMP_LT: 
			//fprintf(stderr, "going to left node\n");
			err = tree_add_to_node(&(*pnode)->left, region);
			break;
		case MR_CMP_GT:
			//fprintf(stderr, "going to right node\n");
			err = tree_add_to_node(&(*pnode)->right, region);
			break;
		case MR_CMP_EQ: case MR_CMP_INT:
			fprintf(stderr, "tree_add_to_node: same or intersecting region exists\n");
			return GPUVM_ERANGE;
		}
		//fprintf(stderr, "node->region = %x\n", (*pnode)->region);
		//fprintf(stderr, "node->left = %x\n", (*pnode)->left);
		//fprintf(stderr, "node->right = %x\n", (*pnode)->right);
		return err;
	} else {
		// a new node is needed
		*pnode = (region_node_t*)smalloc(sizeof(region_node_t));
		//fprintf(stderr, "new node = %x\n", *pnode);
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
	//fprintf(stderr, "adding region to the tree\n");
	//fprintf(stderr, "region tree dump before adding:\n");
  //tree_dump(region_tree_g, 0);
	int err = tree_add_to_node(&region_tree_g, region);
	//fprintf(stderr, "region tree dump after adding:\n");
  //tree_dump(region_tree_g, 0);
	//fprintf(stderr, "region added to the tree\n");
	return err;
}

/** dumps the tree to stdout; this function must be used for debugging only */
static void tree_dump(const region_node_t *node, int depth) {
	if(!node)
		return;
	tree_dump(node->left, depth + 1);
	char pref[128];
	int i;
	for(i = 0; i < depth; i++)
		pref[i] = ' ';
	pref[depth] = '\0';
	if(node->region)
		fprintf(stderr, "%sregion = {%p, %zd}\n", pref, node->region->range.ptr, 
						node->region->range.nbytes);
	else
		fprintf(stderr, "%sregion = NULL\n", pref);
	tree_dump(node->right, depth + 1);
}  // tree_dump()

/** finds a region in the region tree by pointer 
		@param node the tree in which to find the pointer
		@param ptr the pointer to find
		@returns pointer to the region if found and 0 if not
 */
static region_t *tree_find_region(const region_node_t *node, const void *ptr) {
	//fprintf(stderr, "node = %x\n", node);
	// node is 0 - not found
	if(!node)
		return 0;
	//fprintf(stderr, "node->region = %x\n", node->region);
	//fprintf(stderr, "node->left = %x\n", node->left);
	//fprintf(stderr, "node->right = %x\n", node->right);
	//fprintf(stderr, "region = {%p, %zd}\n", node->region->range.ptr, 
	//				node->region->range.nbytes);

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
		return 0;
	}
}  // tree_find_region

/** finds a subregion which contains one of the addresses in range; if multiple
		such subregions exist, any one may be returned
		@param node the region tree node in which to search for the subregion
		@param ptr the pointer to the start of the range
		@param nbytes the size of the range, in bytes
		@returns the subregion if found and 0 if none
 */
static subreg_t *tree_find_region_subreg_in_range
(const region_node_t *node, void *ptr, size_t nbytes) {
	if(!node)
		return 0;
	memrange_t range = {ptr, nbytes}, node_range = node->region->range;
	subreg_t *subreg;
	switch(memrange_cmp(&range, &node_range)) {
	case MR_CMP_LT:
		return tree_find_region_subreg_in_range(node->left, ptr, nbytes);
	case MR_CMP_GT:
		return tree_find_region_subreg_in_range(node->right, ptr, nbytes);
	case MR_CMP_EQ:
		return region_find_subreg_in_range(node->region, ptr, nbytes);
	case MR_CMP_INT:
		subreg = region_find_subreg_in_range(node->region, ptr, nbytes);
		if(subreg)
			return subreg;
		subreg = tree_find_region_subreg_in_range(node->left, ptr, nbytes);
		if(subreg)
			return subreg;
		subreg = tree_find_region_subreg_in_range(node->right, ptr, nbytes);
		return subreg;
	default:
		// shall not happen
		assert(0);
		return 0;
	}
}  // tree_find_region_in_range

/** finds the minimum (leftmost) node in the tree; this may be the node itself */
static region_node_t **tree_min_pnode(region_node_t **pnode) {
	region_node_t *left = (*pnode)->left;
	//fprintf(stderr, "left subnode=%p\n", left);
	if(left != NULL)
		return tree_min_pnode(&(*pnode)->left);
	else
		return pnode;
}

/** removes the region, if any, from the tree
		@param pnode *pnode is the node the region is being removed from. Passed by reference
		as there may be a different node
		@param region the region to be removed
 */
static void tree_remove_from_node
(region_node_t **pnode, const region_t *region)
{
	//fprintf(stderr, "node = %p, region = %p\n", *pnode, region);
	//fprintf(stderr, "node->region = %p\n", (*pnode)->region);
	if(!*pnode || !(*pnode)->region) {
		fprintf(stderr, "tree_remove_from_node: invalid region\n");
		return;
	}
	if((*pnode)->region == region) {
		// remove from this node
		region_node_t *node = *pnode;
		if(!node->left && !node->right) {
			//fprintf(stderr, "removing from leaf\n");
			*pnode = 0;
			sfree(node);			
		} else if(!node->left || !node->right) {
			//fprintf(stderr, "removing from line\n");
			if(!node->left) {
				*pnode = node->right;
			} else {
				*pnode = node->left;
			}
			sfree(node);
		} else {
			//fprintf(stderr, "removing from inner\n");
			// both nodes are non-null find min in right subtree
			// TODO: add implementations which alternate between max in left subtree and min in
			// right subtree
			//fprintf(stderr, "right subnode=%p\n", node->right);
			region_node_t **pmin_node = tree_min_pnode(&node->right);
			region_node_t *min_node = *pmin_node;
			*pmin_node = min_node->right;
			min_node->left = node->left;
			min_node->right = node->right;
			*pnode = min_node;
			//region_node_t *min_node = *pmin_node;
			//fprintf(stderr, "minimum subnode found, min_node=%p\n", *pmin_node);
			//node->region = min_node->region;
			//*pmin_node = min_node->right;
			//sfree(min_node);
			sfree(node);
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
	//fprintf(stderr, "removing region from tree\n");
	tree_remove_from_node(&region_tree_g, region);
	//fprintf(stderr, "removed region from tree\n");
}

int region_alloc(region_t **p, subreg_t *subreg) {
	if(p)
		*p = 0;
	// allocate memory
	region_t *new_region = (region_t*)smalloc(sizeof(region_t));
	if(!new_region)
		return GPUVM_ESALLOC;
	memset(new_region, 0, sizeof(region_t));
	//fprintf(stderr, "memory for region allocated\n");
	
	// initialize members
	new_region->range.ptr = 
		(void*)((ptrdiff_t)subreg->range.ptr / GPUVM_PAGE_SIZE * GPUVM_PAGE_SIZE);
	new_region->range.nbytes = 
		(((ptrdiff_t)subreg->range.ptr + subreg->range.nbytes - 1)
		 / GPUVM_PAGE_SIZE + 1) * GPUVM_PAGE_SIZE - (ptrdiff_t)new_region->range.ptr;
	new_region->prot_status = PROT_READ | PROT_WRITE;
	new_region->nsubregs = 1;
	if(semaph_init(&new_region->unprot_sem, 0)) {
		sfree(new_region);
		return GPUVM_ERROR;
	}
	
	// initialize subregion list
	new_region->subreg_list = (subreg_list_t*)smalloc(sizeof(subreg_list_t));
	if(!new_region->subreg_list) {
		semaph_destroy(&new_region->unprot_sem);
		sfree(new_region);
		return GPUVM_ESALLOC;
	}
	//fprintf(stderr, "memory for subregion list allocated\n");
	new_region->subreg_list->subreg = subreg;
	new_region->subreg_list->next = 0;
	
	// insert region into tree
	int err = tree_add(new_region);
	if(err) {
		sfree(new_region->subreg_list->subreg);
		semaph_destroy(&new_region->unprot_sem);
		sfree(new_region);
		return err;
	}
	//fprintf(stderr, "region added to tree\n");
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

int region_protect_after(region_t *region, int flags) {
	flags &= GPUVM_READ_WRITE;
	int new_prot_status;
	if(flags == GPUVM_READ_WRITE)
		new_prot_status = PROT_NONE;
	else if(flags == GPUVM_READ_ONLY)
		new_prot_status = PROT_READ;
	if(new_prot_status != region->prot_status) {
		if(mprotect(region->range.ptr, region->range.nbytes, new_prot_status)) {
			fprintf(stderr, "region_protect: can\'t set memory protection\n");
			return GPUVM_EPROT;
		}
		region->prot_status = new_prot_status;
	}
	return 0;
}  // region_protect_after

int region_unprotect(region_t *region) {
	if(mprotect(region->range.ptr, region->range.nbytes, PROT_READ | PROT_WRITE)) {
		fprintf(stderr, "region_unprotect: can\'t remove memory protection\n");
		return GPUVM_EPROT;
	}
	region->prot_status = PROT_READ | PROT_WRITE;
	return 0;
}

int region_wait_unprotect(region_t *region) {
	if(semaph_wait(&region->unprot_sem)) {
		fprintf(stderr, "region_wait_unprotect: can\'t wait for semaphore\n");
		return -1;
	}
	return 0;
}

int region_post_unprotect(region_t *region) {
	if(semaph_post(&region->unprot_sem)) {
		fprintf(stderr, "region_post_unprotect: can\'t post to semaphore\n");
		return -1;
	}
	return 0;
}

void region_free(region_t *region) {
	if(!region)
		return;
	//fprintf(stderr, "removing region protection\n");
	if(region->prot_status != (PROT_READ | PROT_WRITE))
		region_unprotect(region);
	//fprintf(stderr, "removing region from tree\n");
	//fprintf(stderr, "region tree dump before removal:\n");
  //tree_dump(region_tree_g, 0);
	tree_remove(region);
	//fprintf(stderr, "region tree dump after removal:\n");
  //tree_dump(region_tree_g, 0);
	if(region->subreg_list)
		fprintf(stderr, "region_free: removing region with subregions\n");
	//fprintf(stderr, "removing region semaphore\n");
	semaph_destroy(&region->unprot_sem);
	sfree(region);
	//fprintf(stderr, "region freed\n");
}

int region_add_subreg(region_t *region, subreg_t *subreg) {
	// check if subreg belongs to the region
	if(!memrange_is_inside(&region->range, &subreg->range)) {
		fprintf(stderr, "subregion is not completely inside region\n");
		return GPUVM_ERROR;
	}
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
		} else if(cmp_res == MR_CMP_EQ || cmp_res == MR_CMP_INT) {
			// error - ranges mustn't intersect
			fprintf(stderr, "region_add_subreg: subregion intersects with one of " 
							"subregions of the region");
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
			*plist = next;
			region->nsubregs--;
			break;
		}
	}
	return 0;
}

region_t *region_find_region(const void *ptr) {
	//if((size_t)ptr == 0x2aaac1825020)
	//fprintf(stderr, "region tree dump:\n");
  //tree_dump(region_tree_g, 0);
	//fprintf(stderr, "ptr = %p\n", ptr);
	region_t *region = tree_find_region(region_tree_g, ptr);
	//fprintf(stderr, "region = %p\n", region);
	return region;
}

subreg_t *region_find_region_subreg_in_range(void *ptr, size_t nbytes) {
	return tree_find_region_subreg_in_range(region_tree_g, ptr, nbytes);
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

subreg_t *region_find_subreg_in_range
(const region_t *region, void *ptr, size_t nbytes) {
	memrange_t range = {ptr, nbytes};
	int comp_res = memrange_cmp(&range, &region->range);
	if(comp_res != MR_CMP_INT && comp_res != MR_CMP_EQ)
		return 0;
	subreg_list_t *list;
	for(list = region->subreg_list; list; list = list->next) {
		comp_res = memrange_cmp(&range, &list->subreg->range);
		if(comp_res == MR_CMP_INT || comp_res == MR_CMP_EQ)
			return list->subreg;
	}
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
