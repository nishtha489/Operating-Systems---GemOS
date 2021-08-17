#include<types.h>
#include<mmap.h>

// Helper function to create a new vm_area
struct vm_area* create_vm_area(u64 start_addr, u64 end_addr, u32 flags, u32 mapping_type)
{
	struct vm_area *new_vm_area = alloc_vm_area();
	new_vm_area-> vm_start = start_addr;
	new_vm_area-> vm_end = end_addr;
	new_vm_area-> access_flags = flags;
	new_vm_area->mapping_type = mapping_type;
	return new_vm_area;
}

/**
 * Function will invoked whenever there is page fault. (Lazy allocation)
 * 
 * For valid access. Map the physical page 
 * Return 0
 * 
 * For invalid access, i.e Access which is not matching the vm_area access rights (Writing on ReadOnly pages)
 * return -1. 
 */
int vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
	u64 *vaddr_base = (u64 *)osmap(current -> pgd);
	u64 *entry;
	u64 pfn;

	u64 ac_flags = 0x5 | (error_code & 0x2);
	
	entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
	if(*entry & 0x1) {
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
	if(*entry & 0x1) {
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
	if(*entry & 0x1) {
		pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
		vaddr_base = (u64 *)osmap(pfn);
	}else{
		pfn = os_pfn_alloc(OS_PT_REG);
		*entry = (pfn << PTE_SHIFT) | ac_flags;
		vaddr_base = osmap(pfn);
	}

	entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
	pfn = os_pfn_alloc(USER_REG);
	*entry = (pfn << PTE_SHIFT) | ac_flags;

	return 1;
}

/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
	if(!current)
		return -1;

	if(addr + length > MMAP_AREA_END)
		return -1;
		
	if(flags == MAP_FIXED && (addr == NULL || addr % 4096 != 0))
		return -1;

	if(flags == MAP_FIXED && length % 4096 != 0)
		return -1;

	//if(flags = MAP_FIXED && addr % 4096 != 0)
	//	return -1;
	if(length % 4096 != NULL){
		length = (length / 4096) + 1;
		length = length * 4096;
	}
	
	if(current -> vm_area == NULL){
		struct vm_area* head = create_vm_area(MMAP_AREA_START,  MMAP_AREA_START + 4096, 0x4, NORMAL_PAGE_MAPPING);
		head -> vm_next = NULL;
		current -> vm_area = head;
	}

	if(addr != NULL){
		if(addr % 4096 != 0){
			addr = (addr / 4096) + 1;
			addr = addr * 4096;
		}

		struct vm_area * curr = current -> vm_area;

		while(curr -> vm_next != NULL){
			if((curr -> vm_end <= addr) && (curr -> vm_next -> vm_start >= addr + length))
				break;
			
			if(flags == MAP_FIXED)
				if((curr -> vm_start <= addr) && (curr -> vm_end > addr))			
					return -1;
			
			curr = curr -> vm_next;
		}

		if((curr -> vm_start <= addr) && (curr -> vm_end > addr))
			if(flags == MAP_FIXED)
				return -1;

		if(addr == curr -> vm_end){
			if((curr -> mapping_type != HUGE_PAGE_MAPPING) && (curr -> access_flags == prot)){
				curr -> vm_end  = curr -> vm_end + length;
				
				if(curr -> vm_next != NULL){
					if(addr == curr -> vm_end && addr + length == curr -> vm_next -> vm_start){
						curr -> vm_end = curr -> vm_next -> vm_end;
						dealloc_vm_area(curr -> vm_next);
					}
				}
			}
			return addr;
		}

		if((addr < curr -> vm_end)){
			if(flags == 0){
				struct vm_area* curr = current -> vm_area;
				while(curr -> vm_next != NULL){
					if(curr -> vm_next -> vm_start >= curr -> vm_end + length)
						break;
					curr = curr -> vm_next;
				}

				if((curr -> mapping_type != HUGE_PAGE_MAPPING) && (curr -> access_flags == prot)){
					curr -> vm_end  = curr -> vm_end + length;
					if(curr -> vm_next != NULL){
						if((curr -> vm_end == curr -> vm_next -> vm_start) && (prot == curr -> vm_next -> access_flags)){
							curr -> vm_end = curr -> vm_next -> vm_end;
							curr -> vm_next = curr -> vm_next -> vm_next;
							dealloc_vm_area(curr -> vm_next);
						}
					}
					return curr -> vm_next -> vm_start;
				}

				struct vm_area *node = create_vm_area(curr -> vm_end, curr -> vm_end + length, prot, NORMAL_PAGE_MAPPING);
				if((curr -> vm_next != NULL)){
					if((curr -> vm_end + length == curr -> vm_next -> vm_start) && (prot == curr -> vm_next -> access_flags)){
						node -> vm_end = curr -> vm_next -> vm_end;
						
						struct vm__area* del;
						del = curr -> vm_next;
						dealloc_vm_area(del);
						
						curr -> vm_next = node;
						return node -> vm_start;
					}
				}
				node -> vm_next = curr -> vm_next;
				curr -> vm_next = node;
				return node -> vm_start;
			}
		}
		
		struct vm_area *node = create_vm_area(addr, addr + length, prot, NORMAL_PAGE_MAPPING);
		if((curr -> vm_next -> access_flags == prot) && (addr + length == curr -> vm_next -> vm_start)){
			node -> vm_end = curr -> vm_next -> vm_end;
			dealloc_vm_area(curr -> vm_next);
			
		}
		node -> vm_next = curr -> vm_next;
		curr -> vm_next = node;
		return node -> vm_start;
	}	

	if(addr == NULL){
		struct vm_area *curr = current -> vm_area;
		while(curr -> vm_next != NULL){
			if(curr -> vm_next -> vm_start >= curr -> vm_end + length)
				break;

			curr = curr -> vm_next;
		}

		if((curr -> mapping_type != HUGE_PAGE_MAPPING) && (curr -> access_flags == prot)){
			curr -> vm_end  = curr -> vm_end + length;
			if(curr -> vm_next != NULL){
				if((curr -> vm_end == curr -> vm_next -> vm_start) && (prot == curr -> vm_next -> access_flags)){
					curr -> vm_end = curr -> vm_next -> vm_end;
					curr -> vm_next = curr -> vm_next -> vm_next;
					dealloc_vm_area(curr -> vm_next);
				}
			}
			return curr -> vm_next -> vm_start;
		}

		struct vm_area *node = create_vm_area(curr -> vm_end, curr -> vm_end + length, prot, NORMAL_PAGE_MAPPING);
		if((curr -> vm_next != NULL)){
			if((curr -> vm_end + length == curr -> vm_next -> vm_start) && (prot == curr -> vm_next -> access_flags)){
				node -> vm_end = curr -> vm_next -> vm_end;
				
				struct vm__area* del;
				del = curr -> vm_next;
				dealloc_vm_area(del);
				
				curr -> vm_next = node;
				return node -> vm_start;
			}
		}
		node -> vm_next = curr -> vm_next;
		curr -> vm_next = node;
		return node -> vm_start;
	}
	return 0;
}


/**
 * munmap system call implemenations
 */
int vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
	if(addr % 4096 != 0)
		return -1;
	if(length <= 0) 
		return -1;

	long start = addr;		
	long end = addr + length;
	struct vm_area *curr = current -> vm_area -> vm_next;
	struct vm_area *prev = current -> vm_area;

	int temp = 0;
	while(curr != NULL){
		if((start < curr -> vm_end) && (curr -> vm_end - curr -> vm_start == 4096) && (end <= curr -> vm_end) && (start >= curr -> vm_start)){
			prev -> vm_next = curr -> vm_next;
			dealloc_vm_area(curr);
			return 0;
		}

		if((start < curr -> vm_end) && (curr -> vm_end - curr -> vm_start > 4096) && (end <= curr -> vm_end) && (start >= curr -> vm_start)){
			if((curr -> vm_start == start) && (curr -> vm_end >= end + 4096)){
				if(end % 4096 == 0){
					curr -> vm_start = end;
				}else{
					curr -> vm_start = ((end / 4096) + 1) * 4096;
				}
				return 0;
			}

			if((curr -> vm_start == start) && (curr -> vm_end < end + 4096)){
				prev -> vm_next = curr -> vm_next;
				dealloc_vm_area(curr);
				return 0;
			}

			if(end + 4096 > curr -> vm_end){
				curr -> vm_start = start;
				return 0;
			}
			
			if(end % 4096){
				curr -> vm_start = ((end / 4096) + 1) * 4096;
			}

			struct vm_area *node = create_vm_area(end, curr -> vm_end, curr -> access_flags, curr -> mapping_type);
			node -> vm_next = curr -> vm_next;
			curr -> vm_end = start;
			curr -> vm_next = node;
			return 0;
		}
		else if((start < curr -> vm_end) && (end > curr -> vm_end) && (start >= curr -> vm_start)){
			if(curr == NULL)
				return 0;
			if(curr)
				temp = 0;
		}

		if(!temp){
			if(end >= curr -> vm_end){
				prev -> vm_next = curr -> vm_next;
				dealloc_vm_area(curr);
				curr = prev -> vm_next;
				continue;
			}else{
				if(end % 4096) 
					curr -> vm_start = ((end / 4096) + 1) * 4096;
				curr -> vm_start = end;
			}
		}

		if((start < curr -> vm_start) && (start > prev->vm_end)){
			if(curr -> vm_end <= end){
				prev -> vm_next = curr -> vm_next;
				dealloc_vm_area(curr);
				curr = prev -> vm_next;
				continue;
			}else{
				if(end % 4096) 
					curr -> vm_start = ((end / 4096) + 1) * 4096;
				curr -> vm_start = end;
			}
		}
		prev = curr;
		curr = curr -> vm_next;
	}
	return 0;
}


/**
 * make_hugepage system call implemenation
 */
long vm_area_make_hugepage(struct exec_context *current, void *addr, u32 length, u32 prot, u32 force_prot)
{
	if(addr == NULL)
		return -EINVAL;
	
	if(length <= 0)
		return -EINVAL;

	if(!current)
		return -1;

	return 0;
}


/**
 * break_system call implemenation
 */
int vm_area_break_hugepage(struct exec_context *current, void *addr, u32 length)
{
	if(addr == NULL)
		return -EINVAL;
	
	if(length <= 0)
		return -EINVAL;
	
	if(!current)
		return -1;
	return 0;
}
