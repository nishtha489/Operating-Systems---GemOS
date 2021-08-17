#include<types.h>
#include<context.h>
#include<file.h>
#include<lib.h>
#include<serial.h>
#include<entry.h>
#include<memory.h>
#include<fs.h>
#include<kbd.h>


/************************************************************************************/
/***************************Do Not Modify below Functions****************************/
/************************************************************************************/

void free_file_object(struct file *filep)
{
	if(filep)
	{
		os_page_free(OS_DS_REG ,filep);
		stats->file_objects--;
	}
}

struct file *alloc_file()
{
	struct file *file = (struct file *) os_page_alloc(OS_DS_REG); 
	file->fops = (struct fileops *) (file + sizeof(struct file)); 
	bzero((char *)file->fops, sizeof(struct fileops));
	file->ref_count = 1;
	file->offp = 0;
	stats->file_objects++;
	return file; 
}

void *alloc_memory_buffer()
{
	return os_page_alloc(OS_DS_REG); 
}

void free_memory_buffer(void *ptr)
{
	os_page_free(OS_DS_REG, ptr);
}

/* STDIN,STDOUT and STDERR Handlers */

/* read call corresponding to stdin */

static int do_read_kbd(struct file* filep, char * buff, u32 count)
{
	kbd_read(buff);
	return 1;
}

/* write call corresponding to stdout */

static int do_write_console(struct file* filep, char * buff, u32 count)
{
	struct exec_context *current = get_current_ctx();
	return do_write(current, (u64)buff, (u64)count);
}

long std_close(struct file *filep)
{
	filep->ref_count--;
	if(!filep->ref_count)
		free_file_object(filep);
	return 0;
}
struct file *create_standard_IO(int type)
{
	struct file *filep = alloc_file();
	filep->type = type;
	if(type == STDIN)
		filep->mode = O_READ;
	else
		filep->mode = O_WRITE;
	if(type == STDIN){
		filep->fops->read = do_read_kbd;
	}else{
		filep->fops->write = do_write_console;
	}
	filep->fops->close = std_close;
	return filep;
}

int open_standard_IO(struct exec_context *ctx, int type)
{
	int fd = type;
	struct file *filep = ctx->files[type];
	if(!filep){
		filep = create_standard_IO(type);
	}else{
		filep->ref_count++;
		fd = 3;
		while(ctx->files[fd])
			fd++; 
	}
	ctx->files[fd] = filep;
	return fd;
}
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/
/**********************************************************************************/

/* File exit handler */
void do_file_exit(struct exec_context *ctx)
{
	/*TODO the process is exiting. Adjust the refcount
	of files*/
	int max = MAX_OPEN_FILES;
	for(int i = 0; i < max; i++){
	    ctx -> files[i] -> ref_count --;
	    if(!ctx -> files[i] -> ref_count)
		free_file_object(ctx -> files[i]);
	    ctx -> files[i] = NULL;
	}
}

/*Regular file handlers to be written as part of the assignmemnt*/


static int do_read_regular(struct file *filep, char * buff, u32 count)
{
	/** 
	*  TODO Implementation of File Read, 
	*  You should be reading the content from File using file system read function call and fill the buf
	*  Validate the permission, file existence, Max length etc
	*  Incase of Error return valid Error code 
	**/

	if(filep == NULL)
		return -EINVAL;

	if(filep != NULL){
      	    if((filep -> inode -> mode & 0x1) && (filep -> mode & 0x1)){
		filep -> offp += flat_read(filep -> inode, buff, count, &(filep -> offp));
                return flat_read(filep -> inode, buff, count, &(filep -> offp));
      	    }else{
                return -EACCES;
            }
        }
}

/*write call corresponding to regular file */

static int do_write_regular(struct file *filep, char * buff, u32 count)
{
	/** 
	*   TODO Implementation of File write, 
	*   You should be writing the content from buff to File by using File system write function
	*   Validate the permission, file existence, Max length etc
	*   Incase of Error return valid Error code 
	* */
	if(filep  != NULL){
            if((filep -> inode -> mode & 0x2) && (filep -> mode & 0x2)){
	        if(flat_write(filep -> inode, buff, count, &(filep -> offp)) < 0){
                    return -EINVAL;
                }else{
                    filep -> offp += flat_write(filep -> inode, buff, count, &(filep -> offp));
                    return flat_write(filep -> inode, buff, count, &(filep -> offp));
                }
            }else{
                return -EACCES;
            }
        }
        int ret_fd = -EINVAL; 
        return ret_fd;
}

long do_file_close(struct file *filep)
{
	/** TODO Implementation of file close  
	*   Adjust the ref_count, free file object if needed
	*   Incase of Error return valid Error code 
	*/
    if(filep){ 
        filep -> ref_count--;
        if(!filep -> ref_count)
	    free_file_object(filep);
	return 0;
    }

    int ret_fd = -EINVAL; 
    return ret_fd;
}

static long do_lseek_regular(struct file *filep, long offset, int whence)
{
	/** 
	*   TODO Implementation of lseek 
	*   Set, Adjust the ofset based on the whence
	*   Incase of Error return valid Error code 
	* */
	if(!filep)
            return -EINVAL;

        long updated_off;
    	if(whence == SEEK_CUR){
	    if(filep -> inode -> file_size < filep -> offp + offset)
		return -EINVAL;
            updated_off = filep -> offp + offset;
    	}else if(whence == SEEK_SET){
	    if(filep -> inode -> file_size < offset)
		return -EINVAL;
            updated_off = offset;
    	}else if(whence == SEEK_END){
	    if(filep -> inode -> file_size < filep -> inode -> file_size + offset)
		return -EINVAL;
            updated_off = filep -> inode -> file_size + offset;
    	}else{
            int ret_fd = -EINVAL; 
            return ret_fd;
    	}
    
        filep -> offp = updated_off;
        return updated_off;
}

extern int do_regular_file_open(struct exec_context *ctx, char* filename, u64 flags, u64 mode)
{

	/**  
	*  TODO Implementation of file open, 
	*  You should be creating file(use the alloc_file function to creat file), 
	*  To create or Get inode use File system function calls, 
	*  Handle mode and flags 
	*  Validate file existence, Max File count is 16, Max Size is 4KB, etc
	*  Incase of Error return valid Error code 
	* */
	struct inode* reg_inode = lookup_inode(filename);	
	struct file *flp = alloc_file();
    	
	if(flp == NULL){
      	    return -ENOMEM;
    	}
        
        if(((flags & O_WRITE) && (!(mode & O_WRITE))) || ((!(mode & O_READ)) && (flags & O_READ))){
      	    return -EACCES;
      	}
	
	if(reg_inode -> file_size > FILE_SIZE)
            return -EOTHERS;

      	if(!reg_inode){
	    if(flags & O_CREAT){
                struct inode* new_inode = create_inode(filename, mode);
		if(!new_inode)	
			return -EOTHERS;
       	        flp -> inode = new_inode;
	    }
	    else
		return -EINVAL;
	}
        flp -> inode = reg_inode;


        flp -> mode = flags;
        flp -> fops -> write = do_write_regular;
        flp -> fops -> close = do_file_close;
        flp -> fops -> read = do_read_regular;
        flp -> pipe = NULL;
        flp -> fops -> lseek = do_lseek_regular;
        flp -> offp = 0;
        flp -> type = REGULAR;
        
	int i = 3;
        while(ctx -> files[i]){
            if(i == 31){
                return -EOTHERS;
            }
            i++;
        }
        ctx->files[i] = flp;
        return i;
}

/**
 * Implementation dup 2 system call;
 */
int fd_dup2(struct exec_context *current, int oldfd, int newfd)
{
	/** 
	*  TODO Implementation of the dup2 
	*  Incase of Error return valid Error code 
	**/
	if(newfd < 0 || newfd > MAX_OPEN_FILES || oldfd < 0 || oldfd > MAX_OPEN_FILES)
		return -EINVAL; 

	if(current -> files[oldfd] == NULL){
            return -EINVAL;
        }
	
	if(!current -> files[newfd]){
      	    free_file_object(current -> files[newfd]);
            current -> files[newfd] = NULL;
        }

    	if(newfd == oldfd){
      	    current -> files[newfd] -> ref_count ++;
      	    return newfd;
    	}
	
       	current -> files[newfd] = current -> files[oldfd];
    	current -> files[newfd] -> ref_count ++;
    	return newfd;
}

int do_sendfile(struct exec_context *ctx, int outfd, int infd, long *offset, int count) {
	/** 
	*  TODO Implementation of the sendfile 
	*  Incase of Error return valid Error code 
	**/
	if(infd < 0 || infd > MAX_OPEN_FILES || outfd < 0 || outfd > MAX_OPEN_FILES)
		return -EINVAL;
	if(!ctx -> files[outfd] || !ctx -> files[infd])
		return -EINVAL;
	if( !((ctx -> files[infd]) -> inode -> mode & O_READ) || !((ctx -> files[outfd]) -> inode -> mode & O_WRITE))
		return -EACCES;
	
	void *mem_buffer = alloc_memory_buffer();
	if(!mem_buffer)
		return -ENOMEM;
	
	struct file *file_in = ctx -> files[infd];
	struct file *file_out = ctx -> files[outfd];

	if(offset){
	    u32 offp = file_in -> offp;
	    file_in -> offp = *offset;
	    int in_size = file_in -> fops -> read(file_in, mem_buffer, count);
	    int out_size = file_out -> fops -> write(file_out, mem_buffer, in_size);
	    if(in_size < 0 || out_size < 0)
	        return -EINVAL;
	    file_in -> offp = offp;
	    free_memory_buffer(mem_buffer);
	    return  out_size;
	
       }
	
	if(!offset){
	    int in_size = file_in -> fops -> read(file_in, mem_buffer, count);
	    int out_size = file_out -> fops -> write(file_out, mem_buffer, in_size);
	    if(file_in -> fops -> read(file_in, mem_buffer, count) < 0 || file_out -> fops -> write(file_out, mem_buffer, in_size) < 0)
	        return -EINVAL;
	    free_memory_buffer(mem_buffer);
	    return  out_size;
	}
	return -EINVAL;
}

