#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit) 
{
    struct exec_context *ctx = get_current_ctx();
    unsigned long b_start = buff;
    unsigned long b_end = buff + count;
   
    if(b_start >= ctx -> mms[MM_SEG_CODE].start && b_end <= ctx -> mms[MM_SEG_CODE].next_free - 1){
        if(access_bit == 0){
            return 1;
        }
        else{
            return 0;
        }
    }
    
    else if(b_start >= ctx -> mms[MM_SEG_RODATA].start && b_end <= ctx -> mms[MM_SEG_RODATA].next_free - 1){
        if(access_bit == 0){
            return 1;
        }
        else{
            return 0;
        }
    }

    else if(b_start >= ctx -> mms[MM_SEG_DATA].start && b_end <= ctx -> mms[MM_SEG_DATA].next_free - 1){
        return 1;
    }

    else if(b_start >= ctx -> mms[MM_SEG_STACK].start && b_end <= ctx -> mms[MM_SEG_STACK].end - 1){
        return 1;
    }
//vm area
    else{
        struct vm_area *vim = ctx -> vm_area;
        while(vim != NULL){
            if(b_start >= vim -> vm_start && b_start <= vim -> vm_end - 1){
                if((vim -> access_flags) & (1 << access_bit)){
                    return 1;
                }
                else{
                    return 0;
                }
            }
            vim = vim -> vm_next;
        }
    }
    return 0;
}

long trace_buffer_close(struct file *filep)
{
    if(filep -> mode == REGULAR){
        return -EINVAL;
    }
    struct exec_context *ctx = get_current_ctx(); 
    int fd = 0;
    while(fd < MAX_OPEN_FILES){
        if(ctx -> files[fd] == filep){
            break;
        }
        fd++;
    }
    if(fd == MAX_OPEN_FILES){
        return -EINVAL;
    }
    os_page_free(USER_REG, filep -> trace_buffer -> buffer);
    os_free(filep -> trace_buffer, sizeof(struct trace_buffer_info));
    os_free(filep -> fops, sizeof(struct fileops));
    os_page_free(USER_REG, filep);    
    ctx -> files[fd] = NULL;
	return 0;	
}



int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
    if((filep -> mode != O_READ && filep -> mode != O_RDWR) || filep -> type == REGULAR){
        return -EINVAL;
    }
    if(!is_valid_mem_range((unsigned long)buff, count, 1)){
        return -EBADMEM;
    }
    u32 r_offset = filep -> trace_buffer -> read_offset;
    u32 w_offset = filep -> trace_buffer -> write_offset;
    u8 full = filep -> trace_buffer -> is_full;
    u32 new_r_offset = r_offset, bytes_read = 0;
    u8 new_is_full = full;
    char *bfr  = filep -> trace_buffer -> buffer;
    if(w_offset > r_offset){
        u32 available = w_offset - r_offset; //bytes
        if(available > count){
            for(int i = r_offset; i < r_offset + count; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + count) % 4096;
            bytes_read = count; 
        }  
        else{
            for(int i = r_offset; i < r_offset + available; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + available) % 4096;
            bytes_read = available;
        }
    }
    else if(w_offset < r_offset || (w_offset == r_offset && full)){
        u32 available = 4096 - r_offset + w_offset; //bytes
        if(available > count){
            for(int i = r_offset; i < r_offset + count; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + count) % 4096;
            bytes_read = count;
        }
        else{
            for(int i = r_offset; i < r_offset + available; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + available) % 4096;
            bytes_read = available;
        }
    }
    else if(full == 0){
         bytes_read = 0;
    }
	if(bytes_read > 0){
        new_is_full = 0;
    }
    filep -> trace_buffer -> read_offset = new_r_offset;
    filep -> trace_buffer -> is_full = new_is_full;
    return bytes_read;
}


int trace_buffer_write(struct file *filep, char *buff, u32 count){
    if((filep -> mode != O_WRITE && filep -> mode != O_RDWR) || filep -> type == REGULAR){
        return -EINVAL;
    }
    if(!is_valid_mem_range((unsigned long)buff, count, 0)){
        return -EBADMEM;
    }
    u32 r_offset = filep -> trace_buffer -> read_offset;
    u32 w_offset = filep -> trace_buffer -> write_offset;
    u8 full = filep -> trace_buffer -> is_full;
    u32 new_w_offset = w_offset, bytes_write = 0;
    u8 new_is_full = full;
    char *bfr  = filep -> trace_buffer -> buffer;
    if(w_offset < r_offset){
        u32 available = r_offset - w_offset; //bytes
        if(available > count){
            for(int i = w_offset; i < w_offset + count; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = (w_offset + count) % 4096;
            bytes_write = count; 
        }
        else{
            for(int i = w_offset; i < w_offset + available; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = r_offset;
            bytes_write = available;
        }
    }
    else if(w_offset > r_offset || (w_offset == r_offset && !full)){
        u32 available = 4096 - w_offset + r_offset; //bytes
        if(available > count){
            for(int i = w_offset; i < w_offset + count; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = (w_offset + count) % 4096;
            bytes_write = count;
        }
        else{
            for(int i = w_offset; i < w_offset + available; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = (w_offset + available) % 4096;
            bytes_write = available;
        }
    }
    else if(full == 1){
           bytes_write = 0;
    }
	if(bytes_write > 0 && r_offset == new_w_offset){
        new_is_full = 1;
    }
    filep -> trace_buffer -> write_offset = new_w_offset;
    filep -> trace_buffer -> is_full = new_is_full;
    return bytes_write;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
    if(mode != O_READ && mode != O_RDWR && mode != O_WRITE){
        return -EINVAL;
    }
    int fd = 0;
    while(fd < MAX_OPEN_FILES){
        if(current -> files[fd] == NULL){
            break;
        }
        fd++;
    }
    if(fd == MAX_OPEN_FILES){
        return -EINVAL;
    }
    struct file *fp = (struct file*)os_page_alloc(USER_REG);
    fp -> type = TRACE_BUFFER;
    fp -> mode = mode;
    fp -> offp = 0;
    fp -> ref_count = 1;
    fp -> inode = NULL;
    fp -> trace_buffer = (struct trace_buffer_info *)os_alloc(sizeof(struct trace_buffer_info));
    //fp -> trace_buffer = (struct trace_buffer_info *)os_page_alloc(USER_REG);
    fp -> trace_buffer -> read_offset = 0;
    fp -> trace_buffer -> write_offset = 0;
    fp -> trace_buffer -> buffer = (char*)os_page_alloc(USER_REG);
    fp -> trace_buffer -> is_full = 0;
    fp -> fops = (struct fileops *)os_alloc(sizeof(struct fileops));
    //fp -> fops = (struct fileops *)os_page_alloc(USER_REG);
    fp -> fops -> read = trace_buffer_read;
    fp -> fops -> write = trace_buffer_write;
    fp -> fops -> lseek = NULL;
    fp -> fops -> close = trace_buffer_close;
    current -> files[fd] = fp;
	return fd;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////
int read_inside_buffer(struct file *filep, char *buff, u32 count)
{
    u32 r_offset = filep -> trace_buffer -> read_offset;
    u32 w_offset = filep -> trace_buffer -> write_offset;
    u8 full = filep -> trace_buffer -> is_full;
    u32 new_r_offset = r_offset, bytes_read;
    u8 new_is_full;
    char *bfr  = filep -> trace_buffer -> buffer;
    if(w_offset > r_offset){
        u32 available = w_offset - r_offset; //bytes
        if(available > count){
            for(int i = r_offset; i < r_offset + count; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + count) % 4096;
            bytes_read = count; 
        }  
        else{
            for(int i = r_offset; i < r_offset + available; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + available) % 4096;
            bytes_read = available;
        }
    }
    else if(w_offset < r_offset || (w_offset == r_offset && full)){
        u32 available = 4096 - r_offset + w_offset; //bytes
        if(available > count){
            for(int i = r_offset; i < r_offset + count; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + count) % 4096;
            bytes_read = count;
        }
        else{
            for(int i = r_offset; i < r_offset + available; i++){
                buff[i - r_offset] = bfr[i % 4096];
            }
            new_r_offset = (r_offset + available) % 4096;
            bytes_read = available;
        }
    }
    else if(full == 0){
        bytes_read = 0;
    }
	if(bytes_read > 0){
        new_is_full = 0;
    }
    filep -> trace_buffer -> read_offset = new_r_offset;
    filep -> trace_buffer -> is_full = new_is_full;
    return bytes_read;
}

int write_inside_buffer(struct file *filep, char *buff, u32 count){
    u32 r_offset = filep -> trace_buffer -> read_offset;
    u32 w_offset = filep -> trace_buffer -> write_offset;
    u8 full = filep -> trace_buffer -> is_full;
    u32 new_w_offset = w_offset, bytes_write;
    u8 new_is_full;
    char *bfr  = filep -> trace_buffer -> buffer;
    if(w_offset < r_offset){
        u32 available = r_offset - w_offset; //bytes
        if(available > count){
            for(int i = w_offset; i < w_offset + count; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = (w_offset + count) % 4096;
            bytes_write = count; 
        }
        else{
            for(int i = w_offset; i < w_offset + available; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = r_offset;
            bytes_write = available;
        }
    }
    else if(w_offset > r_offset || (w_offset == r_offset && !full)){
        u32 available = 4096 - w_offset + r_offset; //bytes
        if(available > count){
            for(int i = w_offset; i < w_offset + count; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = (w_offset + count) % 4096;
            bytes_write = count;
        }
        else{
            for(int i = w_offset; i < w_offset + available; i++){
                filep -> trace_buffer -> buffer[i % 4096] = buff[i - w_offset];
            }
            new_w_offset = (w_offset + available) % 4096;
            bytes_write = available;
        }
    }
    else if(full == 1){
        bytes_write = 0;
    }
	if(bytes_write > 0 && r_offset == new_w_offset){
        new_is_full = 1;
    }
    filep -> trace_buffer -> write_offset = new_w_offset;
    filep -> trace_buffer -> is_full = new_is_full;

    return bytes_write;
}
int sys_arg(int syscall_num) {
    u64 arguments;
    if( syscall_num == SYSCALL_GETPID ||
            syscall_num == SYSCALL_GETPPID ||
            syscall_num == SYSCALL_FORK ||
            syscall_num == SYSCALL_CFORK ||
            syscall_num == SYSCALL_VFORK ||
            syscall_num == SYSCALL_PHYS_INFO ||
            syscall_num == SYSCALL_STATS ||
            syscall_num == SYSCALL_GET_USER_P ||
            syscall_num == SYSCALL_GET_COW_F ||
            syscall_num == SYSCALL_END_STRACE) {
                arguments = 0;
    } else if(syscall_num == SYSCALL_EXIT ||
            syscall_num == SYSCALL_CONFIGURE ||
            syscall_num == SYSCALL_DUMP_PTT ||
            syscall_num == SYSCALL_SLEEP ||
            syscall_num == SYSCALL_PMAP ||
            syscall_num == SYSCALL_DUP ||
            syscall_num == SYSCALL_CLOSE ||
            syscall_num == SYSCALL_TRACE_BUFFER) {
                arguments = 1;
    } else if(syscall_num == SYSCALL_SIGNAL ||
            syscall_num == SYSCALL_EXPAND ||
            syscall_num == SYSCALL_CLONE ||
            syscall_num == SYSCALL_MUNMAP ||
            syscall_num == SYSCALL_OPEN ||
            syscall_num == SYSCALL_DUP2 ||
            syscall_num == SYSCALL_START_STRACE ||
            syscall_num == SYSCALL_STRACE) {
                arguments = 2;
    } else if(syscall_num == SYSCALL_MPROTECT ||
            syscall_num == SYSCALL_READ ||
            syscall_num == SYSCALL_WRITE ||
            syscall_num == SYSCALL_LSEEK ||
            syscall_num == SYSCALL_READ_STRACE ||
            syscall_num == SYSCALL_READ_FTRACE) {
                arguments = 3;
    } else if(syscall_num == SYSCALL_MMAP ||
            syscall_num == SYSCALL_FTRACE) {
                arguments = 4;
    }
    return arguments;
}

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
    if(syscall_num == 1){
        return 0;
    }
    struct exec_context *ctx = get_current_ctx();
    if(ctx -> st_md_base == NULL){
        return 0;
    }
    int fd = ctx -> st_md_base -> strace_fd;
    int todo = 0;
    if(ctx -> st_md_base -> tracing_mode == FULL_TRACING){
        todo = 1;
    }
    else{
        struct strace_info *temp = ctx -> st_md_base -> next;
        while(temp != NULL){
            if(temp -> syscall_num == syscall_num){
                todo = 1;
                break;
            }
            temp = temp -> next;
        }
    }
    if(todo == 0 || syscall_num == SYSCALL_START_STRACE || syscall_num == SYSCALL_END_STRACE || ctx -> st_md_base -> is_traced == 0){
        return 0;
    }
    int arguments = sys_arg(syscall_num);
    u64 *buff = (u64 *)os_alloc((arguments + 1) * 8);
    buff[0] = syscall_num;
    if(arguments == 1){
        buff[1] = param1;
    }
    else if(arguments == 2){
        buff[1] = param1;
        buff[2] = param2;
    }
    else if(arguments == 3){
        buff[1] = param1;
        buff[2] = param2;
        buff[3] = param3;
    }
    else{
        buff[1] = param1;
        buff[2] = param2;
        buff[3] = param3;
        buff[4] = param4;
    }
    int wrote = write_inside_buffer(ctx -> files[fd], (char *)buff, (arguments + 1) * 8);
    return 0;
}


int sys_strace(struct exec_context *current, int syscall_num, int action)
{
    if(current -> st_md_base == NULL){
        if(action == REMOVE_STRACE){
            return -EINVAL;
        }
        current -> st_md_base = (struct strace_head *)os_alloc(sizeof(struct strace_head));
        if(current -> st_md_base == (void *)(-1)){
            return -EINVAL;
        }
        current -> st_md_base -> count = 1;
        current -> st_md_base -> is_traced = 0;
        struct strace_info *sinfo = (struct strace_info *)os_alloc(sizeof(struct strace_info));
        sinfo -> syscall_num = syscall_num;
        sinfo -> next = (struct strace_info *)NULL;
        current -> st_md_base -> next = sinfo;
        current -> st_md_base -> last = sinfo;
        return 0;
    }
    else if(current -> st_md_base -> next == NULL){
        current -> st_md_base -> count = 1;
        struct strace_info *sinfo = (struct strace_info *)os_alloc(sizeof(struct strace_info));
        sinfo -> syscall_num = syscall_num;
        sinfo -> next = (struct strace_info *)NULL;
        current -> st_md_base -> next = sinfo;
        current -> st_md_base -> last = sinfo;
        return 0;
    }
    else{
        if(action == ADD_STRACE){
            if(current -> st_md_base -> count == STRACE_MAX){
                return -EINVAL;
            }
            struct strace_info * sinfo = (struct strace_info *)os_alloc(sizeof(struct strace_info));
            sinfo -> syscall_num = syscall_num;
            sinfo -> next = (struct strace_info *)NULL;
            if(current -> st_md_base -> last == NULL){
                current -> st_md_base -> next = sinfo;
                current -> st_md_base -> last = sinfo;
            }
            else{
                current -> st_md_base -> last -> next = sinfo;
                current -> st_md_base -> last = sinfo;
            }
            current -> st_md_base -> count++;
            return 0;
        }
        else{
            struct strace_info * head = current -> st_md_base -> next;
            struct strace_info * prev = NULL;
            int found = 0;
            while(head != NULL){
                if(head -> syscall_num == syscall_num){
                    found = 1;
                    break;
                }
                prev = head;
                head = head -> next;
            }
            if(!found){
                return -EINVAL;
            }
            if(prev == NULL){
                current -> st_md_base -> next = head -> next;
                if(head -> next == NULL){
                    current -> st_md_base -> last = prev;
                }
            }
            else{
                prev -> next = head -> next;
                if(head -> next == NULL){
                    current -> st_md_base -> last = prev;
                }
            }
            current -> st_md_base -> count--;
            return 0;
        }
    }
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
    int offset = 0;
    int bytes_read = 0;
    int r_offset = filep -> trace_buffer -> read_offset;
    int w_offset = filep -> trace_buffer -> write_offset;
    for(int i = 0; i < count; i++){
        int bytes = read_inside_buffer(filep, (char *)(buff + offset), 8);
        if(bytes > 0){
            bytes_read += bytes;
        }
        int syscall_num = *(u64 *)(buff + offset);
        offset = offset + bytes;
        int arg = sys_arg(syscall_num);
        bytes = read_inside_buffer(filep, (char *)(buff + offset), 8 * arg);
        offset = offset + bytes;
        if(bytes > 0){
            bytes_read += bytes;
        }
    }
	return bytes_read;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
    if(current -> st_md_base == NULL){
        current -> st_md_base = (struct strace_head *)os_alloc(sizeof(struct strace_head));
        if(current -> st_md_base == (void *)(-1)){
            return -EINVAL;
        }
        current -> st_md_base -> count = 0;
        current -> st_md_base -> is_traced = 1;
        current -> st_md_base -> tracing_mode = tracing_mode;
        current -> st_md_base -> strace_fd = fd;
        current -> st_md_base -> next = NULL;
        current -> st_md_base -> last = NULL;
    }
    else{
        current -> st_md_base -> is_traced = 1;
        current -> st_md_base -> tracing_mode = tracing_mode;
        current -> st_md_base -> strace_fd = fd;
    }
	return 0;
}

int sys_end_strace(struct exec_context *current)
{

    struct strace_info *temp = current -> st_md_base -> next;
    struct strace_info *prev = NULL;
    while(temp != NULL){
        prev = temp;
        temp = temp -> next;
        os_free(prev, sizeof(struct strace_info));
    }
    os_free(current -> st_md_base, sizeof(struct strace_head));
    current -> st_md_base = NULL;
	return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
    if(action == ADD_FTRACE){
        if(ctx -> ft_md_base == NULL){
            ctx -> ft_md_base = (struct ftrace_head *)os_alloc(sizeof(struct ftrace_head));
            //ctx -> ft_md_base = (struct ftrace_head *)os_page_alloc(USER_REG);
            if(ctx -> ft_md_base == (void *)-1){
                return -EINVAL;
            }
            ctx -> ft_md_base -> count = 1;
            struct ftrace_info *r = (struct ftrace_info *)os_alloc(sizeof(struct ftrace_info));
            //struct ftrace_info *r = (struct ftrace_info *)os_page_alloc(USER_REG);
            if(r == (void *)-1){
                return -EINVAL;
            }
            r -> faddr = faddr;
            r -> num_args = nargs;
            r -> fd = fd_trace_buffer;
            r -> next = NULL;
            ctx -> ft_md_base -> next = r;
            ctx -> ft_md_base -> last = r;
            return 0;
        }
        else if(ctx -> ft_md_base -> next == NULL){
            ctx -> ft_md_base -> count = 1;
            struct ftrace_info *r = (struct ftrace_info *)os_alloc(sizeof(struct ftrace_info));
            //struct ftrace_info *r = (struct ftrace_info *)os_page_alloc(USER_REG);
            if(r == (void *)-1){
                return -EINVAL;
            }
            r -> faddr = faddr;
            r -> num_args = nargs;
            r -> fd = fd_trace_buffer;
            r -> next = NULL;
            ctx -> ft_md_base -> next = r;
            ctx -> ft_md_base -> last = r;
            return 0;
        }
        else{
            if(ctx -> ft_md_base -> count == FTRACE_MAX){
                return -EINVAL;
            }
            int found = 0;
            struct ftrace_info *temp = ctx -> ft_md_base -> next;
            while(temp != NULL){
                if(temp -> faddr == faddr){
                    found = 1;
                    break;
                }
                temp = temp -> next;
            }
            if(found){
                return -EINVAL;
            }
            struct ftrace_info *r = (struct ftrace_info *)os_alloc(sizeof(struct ftrace_info));
            //struct ftrace_info *r = (struct ftrace_info *)os_page_alloc(USER_REG);
            if(r == (void*)-1){
                return -EINVAL;
            }
            r -> faddr = faddr;
            r -> num_args = nargs;
            r -> fd = fd_trace_buffer;
            r -> next = NULL;
            ctx -> ft_md_base -> last -> next = r;
            ctx -> ft_md_base -> last = r;
            ctx -> ft_md_base -> count++;

            return 0;
       }
    }
    else if(action == REMOVE_FTRACE){
        if(ctx -> ft_md_base == NULL){
            return -EINVAL;
        }
        int found = 0;
        struct ftrace_info *temp = ctx -> ft_md_base -> next;
        struct ftrace_info *prev = NULL;
        while(temp != NULL){
            if(temp -> faddr == faddr){
                found = 1;
                break;
            }
            temp = temp -> next;
        }
        if(!found){
            return -EINVAL;
        }
        if(prev == NULL){
            ctx -> ft_md_base -> next = temp -> next;
            if(temp -> next == NULL){
                ctx -> ft_md_base -> last = prev;
            }
        }
        else{
            prev -> next = temp -> next;
            if(temp -> next == NULL){
                ctx -> ft_md_base -> last = prev;
            }
        }
        ctx -> ft_md_base -> count--;
        if(*((u8*)faddr) == INV_OPCODE){
            *((u8*)faddr) = temp -> code_backup[0];
            *((u8*)faddr + 1) = temp -> code_backup[1];
            *((u8*)faddr + 2) = temp -> code_backup[2];
            *((u8*)faddr + 3) = temp -> code_backup[3];
        }
        os_free(temp, sizeof(struct ftrace_info));
        return 0;
    }
    else if(action == ENABLE_FTRACE){
        if(ctx -> ft_md_base == NULL){
            return -EINVAL;
        }
        int found = 0;
        struct ftrace_info *temp = ctx -> ft_md_base -> next;
        while(temp != NULL){
            if(temp -> faddr == faddr){
                found = 1;
                break;
            }
            temp = temp -> next;
        }
        if(!found){
            return -EINVAL;
        }
        if(*((u8*)faddr) == INV_OPCODE){
            return 0;
        }
        temp -> code_backup[0] =*((u8*)faddr);
        temp -> code_backup[1] =*((u8*)faddr + 1) ;
        temp -> code_backup[2] =*((u8*)faddr + 2);
        temp -> code_backup[3] =*((u8*)faddr + 3);
        *((u8*)faddr) = INV_OPCODE;
        *((u8*)faddr + 1) = INV_OPCODE;
        *((u8*)faddr + 2) = INV_OPCODE;
        *((u8*)faddr + 3) = INV_OPCODE;
        return 0;
    }
    else if(action == DISABLE_FTRACE){
        if(ctx -> ft_md_base == NULL){
            return -EINVAL;
        }
        int found = 0;
        struct ftrace_info *temp = ctx -> ft_md_base -> next;
        while(temp != NULL){
            if(temp -> faddr == faddr){
                found = 1;
                break;
            }
            temp = temp -> next;
        }
        if(!found){
            return -EINVAL;
        }
        if(*((u8*)faddr) != INV_OPCODE){
            return 0;
        }
        *((u8*)faddr) = temp -> code_backup[0];
        *((u8*)faddr + 1) = temp -> code_backup[1];
        *((u8*)faddr + 2) = temp -> code_backup[2];
        *((u8*)faddr + 3) = temp -> code_backup[3];
        return 0;
    }
    else if(action == ENABLE_BACKTRACE){
        if(ctx -> ft_md_base == NULL){
            return -EINVAL;
        }
        int found = 0;
        struct ftrace_info *temp = ctx -> ft_md_base -> next;
        while(temp != NULL){
            if(temp -> faddr == faddr){
                    found = 1;
                    break;
            }
            temp = temp -> next;
        }
        if(!found){
            return -EINVAL;
        }
        temp -> capture_backtrace = 1;
        if(*((u8*)faddr) == INV_OPCODE){
            return 0;
        }
        temp -> code_backup[0] =*((u8*)faddr);
        temp -> code_backup[1] =*((u8*)faddr + 1) ;
        temp -> code_backup[2] =*((u8*)faddr + 2);
        temp -> code_backup[3] =*((u8*)faddr + 3);
        *((u8*)faddr) = INV_OPCODE;
        *((u8*)faddr + 1) = INV_OPCODE;
        *((u8*)faddr + 2) = INV_OPCODE;
        *((u8*)faddr + 3) = INV_OPCODE;
        return 0;
    }
    else if(action == DISABLE_BACKTRACE){
        if(ctx -> ft_md_base == NULL){
            return -EINVAL;
        }
        int found = 0;
        struct ftrace_info *temp = ctx -> ft_md_base -> next;
        while(temp != NULL){
            if(temp -> faddr == faddr){
                found = 1;
                break;
            }
            temp = temp -> next;
        }
        if(!found){
            return -EINVAL;
        }
        temp -> capture_backtrace = 0;
        if(*((u8*)faddr) != INV_OPCODE){
            return 0;
        }
        *((u8*)faddr) = temp -> code_backup[0];
        *((u8*)faddr + 1) = temp -> code_backup[1];
        *((u8*)faddr + 2) = temp -> code_backup[2];
        *((u8*)faddr + 3) = temp -> code_backup[3];
        return 0;
    }
    return 0;
}

//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
    struct exec_context *ctx = get_current_ctx();
    struct ftrace_info *temp = ctx -> ft_md_base -> next;
    u64 faddr = regs -> entry_rip;
    while(temp != NULL){
        if(temp -> faddr == faddr){
            break;
        }
        temp = temp -> next;
    }
    u32 arg = temp -> num_args;
    int fd = temp -> fd;
    u64 *buff = (u64 *)os_alloc((arg + 1) * 8);
    buff[0] = faddr;
    if(arg >= 1){
        buff[1] = regs -> rdi;
    }
    if(arg >= 2){
        buff[2] = regs -> rsi;
    }
    if(arg >= 3){
        buff[3] = regs -> rdx;
    }
    if(arg >= 4){
        buff[4] = regs -> rcx;
    }
    if(arg >= 5){
        buff[5] = regs -> r8;
    }
    if(arg >= 6){
        buff[6] = regs -> r9;
    }
    write_inside_buffer(ctx -> files[fd],(char*)buff, 8 * (arg + 1));
    *((u64*)(regs -> entry_rsp - 8)) = regs -> rbp;
    regs -> entry_rsp = regs -> entry_rsp - 8;
    regs -> rbp = regs -> entry_rsp;
    u64 *to_write = (u64 *)os_alloc(8);
    if(temp -> capture_backtrace == 1){
        to_write[0] = faddr;
        write_inside_buffer(ctx -> files[fd], (char*)to_write, 8);
        u64 *ptr = (u64*)(regs->rbp);
        while(*(ptr + 1) != END_ADDR){
            to_write[0] = *(ptr + 1);
            write_inside_buffer(ctx -> files[fd], (char*)to_write, 8);
            ptr = (u64*)(*ptr);
        }
    }
    char delim[8] = "__END__";
    write_inside_buffer(ctx -> files[fd], delim, 8);
    os_free(to_write, 8);
    regs -> entry_rip = faddr + 4;
    return 0;
}

int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
    int byte_read = 0, read;
    int offset = 0;
    char delim[8] = "__END__";
    for(int i = 0; i < count; i++){
        read = read_inside_buffer(filep, buff + offset, 8);
        while(strcmp(buff + offset, delim) != 0){
            if(read > 0){
                offset = offset + read;
                byte_read = byte_read + read;
            }
            read = read_inside_buffer(filep, buff + offset, 8);
        }
    }
    return byte_read;
}


