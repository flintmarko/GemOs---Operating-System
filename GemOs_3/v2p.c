#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>

/* 
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables 
 * */


/**
 * mprotect System call Implementation.
 */
#define P_size 4096 // bytes
void merge_em_all(struct vm_area *vm){
    struct vm_area *a = vm -> vm_next, *b = vm, *to_delete = NULL;
    while(a != NULL){
        if(a -> vm_start == b -> vm_end && a -> access_flags == b -> access_flags){
            b -> vm_end = a -> vm_end;
            b -> vm_next = a -> vm_next;
            to_delete = a;
            a = a -> vm_next;
        }
        else{
            b = a;
            a = a -> vm_next;
            to_delete = NULL;
        }
        if(to_delete){
            os_free(to_delete, sizeof(struct vm_area));
            stats -> num_vm_area--;
            to_delete = NULL;
        }
    }
}

void FLUSH(){
    u64 cr3_val;

	asm volatile (
		"mov %%cr3, %0"
		: "=r" (cr3_val)
	);

	asm volatile (
		"mov %0, %%rax\n\t"
		"mov %%rax, %%cr3"
		:
		: "r" (cr3_val)
		: "eax"
	);
}
void print_mappings(struct vm_area* vm){
    int i = 1;
    while(vm != NULL){
        printk("%d - th block : \n", i);
        printk("Start = %x\n", vm -> vm_start);
        printk("End = %x\n", vm -> vm_end);
        printk("Access_flags = %d\n", vm -> access_flags);
        i++;
        vm = vm -> vm_next;
    }
}
void protect_pfns(struct exec_context *ctx, u64 addr, int prot){
        u64 pfn;
        u64 cr3 = (u64)osmap(ctx -> pgd);
        // checking pgd_t
        u64 pgd_t_offset = addr >> 39;
        u64 pgd_t_address = cr3 + 8 * pgd_t_offset;
        u64 pgd_t_pte = *((u64*)pgd_t_address);
        if((pgd_t_pte & 1) == 0){
            return;
        }
        pfn = (pgd_t_pte >> 12) << 12;
        // checking pud_t
        u64 pud_t_offset = (addr >> 30) & ((1 << 9) - 1);
        u64 pud_t_address = pfn + pud_t_offset * 8; 
        u64 pud_t_pte = *((u64*)pud_t_address);
        if((pud_t_pte & 1) == 0){
            return;
        }
        pfn = (pud_t_pte >> 12) << 12;
        // checking pmd_t
        u64 pmd_t_offset = (addr >> 21) & ((1 << 9) - 1);
        u64 pmd_t_address = pfn + 8 * pmd_t_offset;
        u64 pmd_t_pte = *((u64*)pmd_t_address);
        if((pmd_t_pte & 1) == 0){
            return;
        }
        pfn = (pmd_t_pte >> 12) << 12;
        // checking pte_t
        u64 pte_t_offset = (addr >> 12) & ((1 << 9) - 1);
        u64 pte_t_address = pfn + 8 * pte_t_offset;
        u64 pte_t_pte = *((u64*)pte_t_address);
        FLUSH();
        if(get_pfn_refcount((pte_t_pte >> 12)) > 1)return;
        if((pte_t_pte & 1) == 0){
            return;
        }
        if(prot == 1){
            if((pte_t_pte >> 3) & 1){
                pte_t_pte ^= (1 << 3);
            }
        }
        else if(prot == 3){
            if(((pte_t_pte >> 3) & 1) == 0){
                pte_t_pte ^= (1 << 3);
            }
        }
        *((u64*)pte_t_address) = pte_t_pte;
}


long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if(!(prot == 1 || prot == 3 || prot == 2)){
        return -1;
    }
    if(length > 128 * 4096 || length < 0){
        return -1;
    }
    if(((void*)addr) != NULL &&  (addr < MMAP_AREA_START || addr > MMAP_AREA_END)){
        return -1;
    }
    length = ((length - 1) / P_size + 1) * P_size;
    struct vm_area *vm = current -> vm_area, *prev = NULL;
    while(vm != NULL){
        if(addr <= vm -> vm_start && addr + length >= vm -> vm_end){
            vm -> access_flags = prot;
            prev = vm;
            vm = vm -> vm_next;
        }
        else if(addr > vm -> vm_start && addr + length < vm -> vm_end){
            struct vm_area *first = (struct vm_area*)os_alloc(sizeof(struct vm_area));       
            struct vm_area *second = (struct vm_area*)os_alloc(sizeof(struct vm_area));       
            stats -> num_vm_area += 2;
            first -> vm_start = addr;
            first -> vm_end = addr + length;
            second -> vm_start = addr + length;
            second -> vm_end = vm -> vm_end;
            vm -> vm_end = addr;
            second -> vm_next = vm -> vm_next;
            first -> vm_next = second;
            vm -> vm_next = first;
            second -> access_flags = vm -> access_flags;
            first -> access_flags = prot;
            prev = second;
            vm = second -> vm_next;
        }
        else if(addr <= vm -> vm_start && addr + length > vm -> vm_start && addr + length < vm -> vm_end){
            struct vm_area *first = (struct vm_area*)os_alloc(sizeof(struct vm_area));
            stats -> num_vm_area++;
            first -> vm_start = addr + length;
            first -> vm_end = vm -> vm_end;
            vm -> vm_end = addr + length;
            first -> vm_next = vm -> vm_next;
            vm -> vm_next = first;
            first -> access_flags = vm -> access_flags;
            vm -> access_flags = prot;
            prev = first;
            vm = first -> vm_next;
        }
        else if(addr > vm -> vm_start && addr < vm -> vm_end && addr + length >= vm -> vm_end){
            struct vm_area *first = (struct vm_area*)os_alloc(sizeof(struct vm_area));
            stats -> num_vm_area++;
            first -> vm_end = vm -> vm_end;
            first -> vm_start = addr;
            vm -> vm_end = addr;
            first -> vm_next = vm -> vm_next;
            vm -> vm_next = first;
            first -> access_flags = prot;
            prev = first;
            vm = first -> vm_next;
        }
        else{
            prev = vm;
            vm = vm -> vm_next;
        }
    }
    merge_em_all(current -> vm_area);
    for(u64 i = addr; i < addr + length; i += 4096){
        protect_pfns(current, i, prot);
    }
    FLUSH();
    return 0;
}

/**
 * mmap system call implementation.
 */

long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
    //validity check
    if(!(flags == 0 || flags == 1)){
        return -1;
    }
    if(!(prot == 1 || prot == 3 || prot == 2)){
        return -1;
    }
    if(length > 128 * 4096 || length < 0){
        return -1;
    }
    if(((void*)addr) != NULL &&  (addr < MMAP_AREA_START || addr > MMAP_AREA_END)){
        return -1;
    }
    if(current -> vm_area == NULL){
        struct vm_area* node = (struct vm_area*)os_alloc(sizeof(struct vm_area));
        stats -> num_vm_area++;
        node -> vm_start = MMAP_AREA_START;
        node -> vm_end = MMAP_AREA_START + P_size;
        node -> access_flags = 0x0;
        node -> vm_next = NULL;
        current -> vm_area = node;
    }
    long value_to_return;
    length = ((length - 1) / P_size + 1) * P_size;
    if(flags == MAP_FIXED && (void *)addr == NULL){
        value_to_return = -1;
    }
    else if(flags == MAP_FIXED || (!flags && (void *)addr != NULL) ){
        struct vm_area* vm = current -> vm_area;
        while(vm -> vm_next != NULL){
            if(vm -> vm_start <= addr && vm -> vm_next -> vm_start > addr){
                break;
            }
            vm = vm -> vm_next;
        }
        if(vm -> vm_end > addr || ((vm -> vm_next != NULL) && ((addr + length) > vm -> vm_next -> vm_start))){
            value_to_return = -1;
            if(!flags)goto there;
        }
        else{
            value_to_return = addr;
            struct vm_area* node = (struct vm_area*)os_alloc(sizeof(struct vm_area));
            stats -> num_vm_area++;
            node -> vm_start = addr;
            node -> vm_end = addr + length;
            node -> access_flags = prot;
            node -> vm_next = vm -> vm_next;
            vm -> vm_next = node;
        }
    }
    else{
        there:
        struct vm_area* vm = current -> vm_area;
        while(vm -> vm_next != NULL && vm -> vm_next -> vm_start - vm -> vm_end < length){
            vm = vm -> vm_next;
        }
        struct vm_area* node = (struct vm_area*)os_alloc(sizeof(struct vm_area));
        stats -> num_vm_area++;
        node -> vm_start = vm -> vm_end;
        node -> vm_end = vm -> vm_end + length;
        node -> access_flags = prot;
        node -> vm_next = vm -> vm_next;
        vm -> vm_next = node;
        value_to_return = node -> vm_start;
    }
    merge_em_all(current -> vm_area);
    return value_to_return;
}

/**
 * munmap system call implemenations
 */


void free_pfns(struct exec_context *ctx, u64 addr){
        u64 pfn;
        u64 cr3 = (u64)osmap(ctx -> pgd);
        // checking pgd_t
        u64 pgd_t_offset = addr >> 39;
        u64 pgd_t_address = cr3 + 8 * pgd_t_offset;
        u64 pgd_t_pte = *((u64*)pgd_t_address);
        if((pgd_t_pte & 1) == 0){
            return;
        }
        pfn = (pgd_t_pte >> 12) << 12;
        // checking pud_t
        u64 pud_t_offset = (addr >> 30) & ((1 << 9) - 1);
        u64 pud_t_address = pfn + pud_t_offset * 8; 
        u64 pud_t_pte = *((u64*)pud_t_address);
        if((pud_t_pte & 1) == 0){
            return;
        }
        pfn = (pud_t_pte >> 12) << 12;
        // checking pmd_t
        u64 pmd_t_offset = (addr >> 21) & ((1 << 9) - 1);
        u64 pmd_t_address = pfn + 8 * pmd_t_offset;
        u64 pmd_t_pte = *((u64*)pmd_t_address);
        if((pmd_t_pte & 1) == 0){
            return;
        }
        pfn = (pmd_t_pte >> 12) << 12;
        // checking pte_t
        u64 pte_t_offset = (addr >> 12) & ((1 << 9) - 1);
        u64 pte_t_address = pfn + 8 * pte_t_offset;
        u64 pte_t_pte = *((u64*)pte_t_address);
        if((pte_t_pte & 1) == 0){
            return;
        }
        pfn = (pte_t_pte >> 12) << 12;
        pte_t_pte ^= 0x1;
        *((u64*)pte_t_address) = pte_t_pte;
        if(get_pfn_refcount(pfn >> 12))put_pfn(pfn >> 12);
        if(get_pfn_refcount(pfn >> 12) == 0)os_pfn_free(USER_REG, pfn >> 12);
        *((u64*)pte_t_address) = 0x0;
        FLUSH();
}

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    if(length > 128 * 4096 || length < 0){
        return -1;
    }
    if(((void*)addr) != NULL &&  (addr < MMAP_AREA_START || addr > MMAP_AREA_END)){
        return -1;
    }
    length = ((length - 1) / P_size + 1) * P_size;
    struct vm_area *vm = current -> vm_area, *prev = NULL;
    while(vm != NULL){
        if(addr <= vm -> vm_start && addr + length >= vm -> vm_end){
            prev -> vm_next = vm -> vm_next;
            os_free(vm, sizeof(struct vm_area));
            stats -> num_vm_area--;
            vm = prev -> vm_next;
        }
        else if(addr > vm -> vm_start && addr + length < vm -> vm_end){
            struct vm_area *first = (struct vm_area*)os_alloc(sizeof(struct vm_area));
            stats -> num_vm_area++;
            first -> vm_next = vm -> vm_next;
            first -> vm_start = addr + length;
            first -> vm_end = vm -> vm_end;
            first -> access_flags = vm -> access_flags;
            vm -> vm_end = addr;
            vm -> vm_next = first;
            prev = first;
            vm = first -> vm_next;
        }
        else if(addr <= vm -> vm_start && addr + length > vm -> vm_start && addr + length < vm -> vm_end){
            vm -> vm_start = addr + length;
            prev = vm;
            vm = vm -> vm_next;
        }
        else if(addr > vm -> vm_start && addr < vm -> vm_end && addr + length >= vm -> vm_end){
            vm -> vm_end = addr;
            prev = vm;
            vm = vm -> vm_next;
        }
        else{
            prev = vm;
            vm = vm -> vm_next;
        }
    }
    merge_em_all(current -> vm_area);
    for(u64 i = addr; i < addr + length; i += 4096){
        free_pfns(current, i);
    }
    FLUSH();
    return 0;
}



/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    // checking validity of addr
    struct vm_area *vm = current -> vm_area -> vm_next; 
    struct vm_area *corresponding_vm;
    int cause_of_fault;
    // 0 - invalid (error)
    // 1 - proper access but the physical page hasnt been maaped to the vm yet(that is error_code[0] = 0) --> manipulating the page table entries 
    // 2 - cow i.e. proper access permissions but still fault is due to the protection fault -> copy on write in fork()
    while(vm != NULL){
        if(addr >= vm -> vm_start && addr < vm -> vm_end){
            if((error_code == 0x7 && vm -> access_flags == PROT_READ) || (error_code == 0x6 && vm -> access_flags == PROT_READ)){
                cause_of_fault = 0;
            }
            else if(error_code == 0x7 && vm -> access_flags == PROT_READ|PROT_WRITE){
                cause_of_fault = 2;
            }
            else{
                cause_of_fault = 1;
            }
            corresponding_vm = vm;
            break;
        }
        vm = vm -> vm_next;
    }
    if(!vm || !cause_of_fault){
        return -1;
    }
    else if(cause_of_fault == 2){
        //cow functionality
        return handle_cow_fault(current, addr, corresponding_vm -> access_flags);
    }
    else{
        u64 pfn;
        u64 cr3 = (u64)osmap(current -> pgd);
        // checking pgd_t
        u64 pgd_t_offset = addr >> 39;
        u64 pgd_t_address = cr3 + 8 * pgd_t_offset;
        u64 pgd_t_pte = *((u64*)pgd_t_address);
        if(pgd_t_pte & 1 && corresponding_vm -> access_flags == 3){
            pgd_t_pte |= 1 << 3;
        }
        else if((pgd_t_pte & 1) == 0){
            u64 pfn = os_pfn_alloc(OS_PT_REG);
            pgd_t_pte = 17;
            pgd_t_pte |= (pfn << 12);
            pgd_t_pte |= (1 << 3);
        }
        *((u64*)pgd_t_address) = pgd_t_pte;
        pfn = (pgd_t_pte >> 12) << 12;
        // checking pud_t
        u64 pud_t_offset = (addr >> 30) & ((1 << 9) - 1);
        u64 pud_t_address = pfn + pud_t_offset * 8; 
        u64 pud_t_pte = *((u64*)pud_t_address);
        if(pud_t_pte & 1){
            pud_t_pte |= 1 << 3;
        }
        else if((pud_t_pte & 1) == 0){
            u64 pfn = os_pfn_alloc(OS_PT_REG);
            pud_t_pte = 17;
            pud_t_pte |= (pfn << 12);
            pud_t_pte |= (1 << 3);
        }
        *((u64*)pud_t_address) = pud_t_pte;
        pfn = (pud_t_pte >> 12) << 12;
        // checking pmd_t
        u64 pmd_t_offset = (addr >> 21) & ((1 << 9) - 1);
        u64 pmd_t_address = pfn + 8 * pmd_t_offset;
        u64 pmd_t_pte = *((u64*)pmd_t_address);
        if(pmd_t_pte & 1){
            pmd_t_pte |= 1 << 3;
        }
        else if((pmd_t_pte & 1) == 0){
            u64 pfn = os_pfn_alloc(OS_PT_REG);
            pmd_t_pte = 17;
            pmd_t_pte |= (pfn << 12);
            pmd_t_pte |= (1 << 3);
        }
        *((u64*)pmd_t_address) = pmd_t_pte;
        pfn = (pmd_t_pte >> 12) << 12;
        // checking pte_t
        u64 pte_t_offset = (addr >> 12) & ((1 << 9) - 1);
        u64 pte_t_address = pfn + 8 * pte_t_offset;
        u64 pte_t_pte = *((u64*)pte_t_address);
        if(pte_t_pte & 1){
            pte_t_pte |= corresponding_vm -> access_flags << 2;
        }
        else if((pte_t_pte & 1) == 0){
            u64 pfn = os_pfn_alloc(USER_REG);
            pte_t_pte = 17;
            pte_t_pte |= (pfn << 12);
            pte_t_pte |= (corresponding_vm -> access_flags << 2);
        }
        *((u64*)pte_t_address) = pte_t_pte;
    }
    return 1;
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the 
 * end of this function (e.g., setup_child_context etc.)
 */

void present(u64 addr, struct exec_context *ctx, struct exec_context *ctx_c){
        u64 pfn;
        u64 cr3 = (u64)osmap(ctx -> pgd);
        // checking pgd_t
        u64 pgd_t_offset = addr >> 39;
        u64 pgd_t_address = cr3 + 8 * pgd_t_offset;
        u64 pgd_t_pte = *((u64*)pgd_t_address);
        if((pgd_t_pte & 1) == 0){
            return;
        }
        pfn = (pgd_t_pte >> 12) << 12;
        // checking pud_t
        u64 pud_t_offset = (addr >> 30) & ((1 << 9) - 1);
        u64 pud_t_address = pfn + pud_t_offset * 8; 
        u64 pud_t_pte = *((u64*)pud_t_address);
        if((pud_t_pte & 1) == 0){
            return;
        }
        pfn = (pud_t_pte >> 12) << 12;
        // checking pmd_t
        u64 pmd_t_offset = (addr >> 21) & ((1 << 9) - 1);
        u64 pmd_t_address = pfn + 8 * pmd_t_offset;
        u64 pmd_t_pte = *((u64*)pmd_t_address);
        if((pmd_t_pte & 1) == 0){
            return;
        }
        pfn = (pmd_t_pte >> 12) << 12;
        // checking pte_t
        u64 pte_t_offset = (addr >> 12) & ((1 << 9) - 1);
        u64 pte_t_address = pfn + 8 * pte_t_offset;
        u64 pte_t_pte = *((u64*)pte_t_address);
        if((pte_t_pte & 1) == 0){
            return;
        }
        //mapping is present in parent 
        //allocating page tables in child process too
        
        u64 pfn_c;
        u64 cr3_c = (u64)osmap(ctx_c -> pgd);
        // checking pgd_t
        u64 pgd_t_offset_c = addr >> 39;
        u64 pgd_t_address_c = cr3_c + 8 * pgd_t_offset;
        u64 pgd_t_pte_c = *((u64*)pgd_t_address_c);
        if((pgd_t_pte_c & 1) == 0){
            u64 pfn = os_pfn_alloc(OS_PT_REG);
            pgd_t_pte_c = 17;
            pgd_t_pte_c |= (pfn << 12);
            pgd_t_pte_c |= (1 << 3);
        }
        *((u64*)pgd_t_address_c) = pgd_t_pte_c;
        pfn_c = (pgd_t_pte_c >> 12) << 12;
        // checking pud_t
        u64 pud_t_offset_c = (addr >> 30) & ((1 << 9) - 1);
        u64 pud_t_address_c = pfn_c + pud_t_offset_c * 8; 
        u64 pud_t_pte_c = *((u64*)pud_t_address_c);
        if((pud_t_pte_c & 1) == 0){
            u64 pfn = os_pfn_alloc(OS_PT_REG);
            pud_t_pte_c = 17;
            pud_t_pte_c |= (pfn << 12);
            pud_t_pte_c |= (1 << 3);
        }
        *((u64*)pud_t_address_c) = pud_t_pte_c;
        pfn_c = (pud_t_pte_c >> 12) << 12;
        // checking pmd_t
        u64 pmd_t_offset_c = (addr >> 21) & ((1 << 9) - 1);
        u64 pmd_t_address_c = pfn_c + 8 * pmd_t_offset_c;
        u64 pmd_t_pte_c = *((u64*)pmd_t_address_c);
        if((pmd_t_pte_c & 1) == 0){
            u64 pfn = os_pfn_alloc(OS_PT_REG);
            pmd_t_pte_c = 17;
            pmd_t_pte_c |= (pfn << 12);
            pmd_t_pte_c |= (1 << 3);
        }
        *((u64*)pmd_t_address_c) = pmd_t_pte_c;
        pfn_c = (pmd_t_pte_c >> 12) << 12;
        // checking pte_t
        u64 pte_t_offset_c = (addr >> 12) & ((1 << 9) - 1);
        u64 pte_t_address_c = pfn_c + 8 * pte_t_offset_c;
        u64 pte_t_pte_c = *((u64*)pte_t_address_c);
        if((pte_t_pte >> 3) & 1){
            pte_t_pte ^= (1 << 3);
        }
        pte_t_pte_c = pte_t_pte;   //Important
        *((u64*)pte_t_address_c) = pte_t_pte_c;
        *((u64*)pte_t_address) = pte_t_pte;
        get_pfn(pte_t_pte >> 12);
        FLUSH();
}

long do_cfork(){
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
    pid = new_ctx -> pid;
    new_ctx -> ppid = ctx -> pid;
    new_ctx -> type = ctx -> type;
    new_ctx -> state = ctx -> state;
    new_ctx -> used_mem = ctx -> used_mem;
    new_ctx -> pgd = os_pfn_alloc(OS_PT_REG);
    for(u64 i = 0; i < MAX_MM_SEGS; i++){
        new_ctx -> mms[i] = ctx -> mms[i];
    }
    struct vm_area *vm_current = ctx -> vm_area -> vm_next;
    struct vm_area* node = (struct vm_area*)os_alloc(sizeof(struct vm_area));
    node -> vm_start = MMAP_AREA_START;
    node -> vm_end = MMAP_AREA_START + P_size;
    node -> access_flags = 0x0;
    node -> vm_next = NULL;
    new_ctx -> vm_area = node;
    struct vm_area *vm_new = new_ctx -> vm_area;
    while(vm_current != NULL){
        struct vm_area *first = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        first -> vm_start = vm_current -> vm_start;
        first -> vm_end = vm_current -> vm_end;
        first -> access_flags = vm_current -> access_flags;
        for(u64 i = vm_current -> vm_start; i < vm_current -> vm_end; i += 4096){
            present(i, ctx, new_ctx);
        }
        vm_new -> vm_next = first;
        vm_new = first;
        vm_current = vm_current -> vm_next;
    }
    for(u64 i = (ctx -> mms[MM_SEG_CODE]) . start; i < (ctx -> mms[MM_SEG_CODE]) . next_free; i += 4096){
        present(i, ctx, new_ctx);
    }
    for(u64 i = (ctx -> mms[MM_SEG_RODATA]) . start; i < (ctx -> mms[MM_SEG_RODATA]) . next_free; i += 4096){
        present(i, ctx, new_ctx);
    }
    for(u64 i = (ctx -> mms[MM_SEG_DATA]) . start; i < (ctx -> mms[MM_SEG_DATA]) . next_free; i += 4096){
        present(i, ctx, new_ctx);
    }
    for(u64 i = (ctx -> mms[MM_SEG_STACK]) . start; i < (ctx -> mms[MM_SEG_STACK]) . end; i += 4096){
        present(i, ctx, new_ctx);
    }
    
    for(u64 i = 0; i < CNAME_MAX; i++){
        new_ctx -> name[i] = ctx -> name[i];
    }
    new_ctx -> regs = ctx -> regs;
    new_ctx -> pending_signal_bitmap = ctx -> pending_signal_bitmap;

    for(u64 i = 0; i < MAX_SIGNALS; i++){
        new_ctx -> sighandlers[i] = ctx -> sighandlers[i];
    }

    new_ctx -> ticks_to_sleep = ctx -> ticks_to_sleep;
    new_ctx -> alarm_config_time = ctx -> alarm_config_time;
    new_ctx -> ticks_to_alarm = ctx -> ticks_to_alarm;
    for(u64 i = 0; i < MAX_OPEN_FILES; i++){
        new_ctx -> files[i] = ctx -> files[i];
    }
    
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}



/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data) 
 * it is called when there is a CoW violation in these areas. 
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *ctx, u64 addr, int access_flags)
{
        u64 pfn;
        u64 cr3 = (u64)osmap(ctx -> pgd);
        // checking pgd_t
        u64 pgd_t_offset = addr >> 39;
        u64 pgd_t_address = cr3 + 8 * pgd_t_offset;
        u64 pgd_t_pte = *((u64*)pgd_t_address);
        pfn = (pgd_t_pte >> 12) << 12;
        // checking pud_t
        u64 pud_t_offset = (addr >> 30) & ((1 << 9) - 1);
        u64 pud_t_address = pfn + pud_t_offset * 8; 
        u64 pud_t_pte = *((u64*)pud_t_address);
        pfn = (pud_t_pte >> 12) << 12;
        // checking pmd_t
        u64 pmd_t_offset = (addr >> 21) & ((1 << 9) - 1);
        u64 pmd_t_address = pfn + 8 * pmd_t_offset;
        u64 pmd_t_pte = *((u64*)pmd_t_address);
        pfn = (pmd_t_pte >> 12) << 12;
        // checking pte_t
        u64 pte_t_offset = (addr >> 12) & ((1 << 9) - 1);
        u64 pte_t_address = pfn + 8 * pte_t_offset;
        u64 pte_t_pte = *((u64*)pte_t_address);
        pfn = ((pte_t_pte >> 12) << 12);
        if((get_pfn_refcount(pfn >> 12)) > 1){
            put_pfn(pfn >> 12);
            u64 pfn_ = os_pfn_alloc(USER_REG);
            pte_t_pte = 17;
            pte_t_pte |= (pfn_ << 12);
            if(access_flags == 3){
                if(!((pte_t_pte >> 3) & 1)){
                    pte_t_pte ^= (1 << 3);
                }
            }
            else if(access_flags == PROT_READ){
                if((pte_t_pte >> 3) & 1){
                    pte_t_pte ^= (1 << 3);
                }
            }
            memcpy((void*)(pfn_ << 12),(void*)pfn, 4096);                                                                                
        }
        else{
            if(access_flags == 3){
                if(!((pte_t_pte >> 3) & 1)){
                    pte_t_pte ^= (1 << 3);
                }
            }
            else if(access_flags == PROT_READ){
                if((pte_t_pte >> 3) & 1){
                    pte_t_pte ^= (1 << 3);
                }
            }
        }
        *((u64*)pte_t_address) = pte_t_pte;
        FLUSH();
        return 1;
}
