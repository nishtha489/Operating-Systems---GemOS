#include <debug.h>
#include <context.h>
#include <entry.h>
#include <lib.h>
#include <memory.h>


/*****************************HELPERS******************************************/

/*
 * allocate the struct which contains information about debugger
 *
 */
struct debug_info *alloc_debug_info()
{
	struct debug_info *info = (struct debug_info *) os_alloc(sizeof(struct debug_info));
	if(info)
		bzero((char *)info, sizeof(struct debug_info));
	return info;
}

/*
 * frees a debug_info struct
 */
void free_debug_info(struct debug_info *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct debug_info));
}

/*
 * allocates memory to store registers structure
 */
struct registers *alloc_regs()
{
	struct registers *info = (struct registers*) os_alloc(sizeof(struct registers));
	if(info)
		bzero((char *)info, sizeof(struct registers));
	return info;
}

/*
 * frees an allocated registers struct
 */
void free_regs(struct registers *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct registers));
}

/*
 * allocate a node for breakpoint list
 * which contains information about breakpoint
 */
struct breakpoint_info *alloc_breakpoint_info()
{
	struct breakpoint_info *info = (struct breakpoint_info *)os_alloc(
		sizeof(struct breakpoint_info));
	if(info)
		bzero((char *)info, sizeof(struct breakpoint_info));
	return info;
}

/*
 * frees a node of breakpoint list
 */
void free_breakpoint_info(struct breakpoint_info *ptr)
{
	if(ptr)
		os_free((void *)ptr, sizeof(struct breakpoint_info));
}

/*
 * Fork handler.
 * The child context doesnt need the debug info
 * Set it to NULL
 * The child must go to sleep( ie move to WAIT state)
 * It will be made ready when the debugger calls wait_and_continue
 */
void debugger_on_fork(struct exec_context *child_ctx)
{
	child_ctx->dbg = NULL;
	child_ctx->state = WAITING;
}


/******************************************************************************/

/* This is the int 0x3 handler
 * Hit from the childs context
 */
long int3_handler(struct exec_context *ctx)
{
	/* parent exec_context */
	int ppid = ctx -> ppid;
	struct exec_context* parent = get_ctx_by_pid(ppid);

	struct breakpoint_info* head = parent -> dbg -> head;
	if(head == NULL){
		return -1;
	}

	/* changing states and handling registers */
	parent -> state = READY;
	ctx -> state = WAITING;

	parent -> regs.rax = ctx -> regs.entry_rip - 1;

	ctx -> regs.entry_rsp -= 8;
	*(u64 *)ctx -> regs.entry_rsp = ctx -> regs.rbp;
	
	/* handling backtrace */
	u64 rp = ctx -> regs.entry_rsp;

	parent -> dbg -> arrback[parent -> dbg -> arr_count] = ctx -> regs.entry_rip - 1;
	(parent -> dbg -> arr_count) ++;

	while(END_ADDR != *(u64 *)(rp + 8) && (parent -> dbg -> arr_count) < MAX_BACKTRACE){
		//rp = *(u64 *)(rp + 8);
		parent -> dbg -> arrback[parent -> dbg -> arr_count] = *(u64 *)(rp + 8);
		(parent -> dbg -> arr_count) ++;
		rp = *(u64 *)rp;
	}
	/* scheduling parent */
	schedule(parent);

	return -1;
}

/*
 * Exit handler.
 * Called on exit of Debugger and Debuggee
 */
void debugger_on_exit(struct exec_context *ctx)
{
	/* handling debuggee */
	if(ctx -> dbg == NULL){
		struct exec_context* parent = get_ctx_by_pid(ctx -> ppid);
		parent -> state = READY;
		parent -> regs.rax = CHILD_EXIT;
		return;
	}
	/* handling debugger */
	if(ctx -> dbg != NULL){
		while(ctx -> dbg -> head != NULL){
			struct breakpoint_info* temp = ctx -> dbg -> head;
			ctx -> dbg -> head = ctx -> dbg -> head -> next;
			free_breakpoint_info(temp);
		}
		free_debug_info(ctx -> dbg);
		return;
	}
}
/*
 * called from debuggers context
 * initializes debugger state
 */
int do_become_debugger(struct exec_context *ctx)
{
	/* allocating memory and initializing */
	ctx -> dbg = alloc_debug_info();
	if(ctx -> dbg == NULL){
		return -1;
	}

	ctx -> dbg -> head = NULL;
	ctx -> dbg -> num_breakpts = 0;
	ctx -> dbg -> arr_count = 0;
	for(int i = 0; i < MAX_BACKTRACE; i++){
		ctx -> dbg -> arrback[i] = 0;
	}
	return 0;
}

/*
 * called from debuggers context
 */
int do_set_breakpoint(struct exec_context *ctx, void *addr)
{
	*(u8 *) addr = INT3_OPCODE;
	if(ctx -> dbg == NULL){
		return -1;
	} 
	/* handling the one node case */
	if(ctx -> dbg -> head == NULL){
		struct breakpoint_info* h = alloc_breakpoint_info();
		++(ctx -> dbg -> num_breakpts);
		h -> num = ctx -> dbg -> num_breakpts;
		h -> status = 1;
		h -> next = NULL;
		h -> addr = (u64 *)addr;
		ctx -> dbg -> head = h;
		return 0;
	}

	/* checking the number of nodes condition */
	struct breakpoint_info * head = ctx -> dbg -> head;
	int n = 0;
	while(head != NULL){
		head = head -> next;
		n++;
	}
	if(n >= MAX_BREAKPOINTS){
		return -1;
	}

	/* traversing list */
	struct breakpoint_info* temp = ctx -> dbg -> head;

	while(temp -> next != NULL){
		/* if breakpoint already exists at the address */
		if((u64 *)temp -> addr == (u64 *)addr){
			temp -> status = 1;
			return 0;
		}
		temp = temp -> next;
	}
	/* setting breakpoint */
	struct breakpoint_info* h = alloc_breakpoint_info();
	++(ctx -> dbg -> num_breakpts);
	h -> num = ctx -> dbg -> num_breakpts;
	h -> status = 1;
	h -> next = NULL;
	h -> addr = (u64 *)addr;
	temp -> next = h;

	return 0;
}

/*
 * called from debuggers context
 */
int do_remove_breakpoint(struct exec_context *ctx, void *addr)
{
	/* if head is to be removed */
	struct breakpoint_info* head = ctx -> dbg -> head;
	if((u64 *)head -> addr == (u64 *)addr){
		/* only head node */
		if(head -> next == NULL){
			free_breakpoint_info(head);
			*(u8 *) addr = PUSHRBP_OPCODE;
			return 0;
		}else{ 
		/* changing head */
			struct breakpoint_info * temp = ctx -> dbg -> head;
			ctx -> dbg -> head = head -> next;
			free_breakpoint_info(temp);
			*(u8 *) addr = PUSHRBP_OPCODE;
			return 0;
		}
	}
	/* hadnling middle cases */
	while(head -> next != NULL){
		if(head -> next -> addr == (u64 *)addr){
			struct breakpoint_info* temp = head -> next;
			head -> next = head -> next -> next;
			free_breakpoint_info(temp);
			*(u8 *) addr = PUSHRBP_OPCODE;
			return 0;
		}
		head = head -> next;
	}
	/* checking last node */
	if((u64 *)head -> addr == (u64 *)addr){
			free_breakpoint_info(head);
			*(u8 *) addr = PUSHRBP_OPCODE;
			return 0;
	}

	/* if no breakpoint has the address */
	if(head != NULL)
		return -1;
	return 0;
}

/*
 * called from debuggers context
 */
int do_enable_breakpoint(struct exec_context *ctx, void *addr)
{
	/* changing status of breakpoint at given address */
	struct breakpoint_info* head = ctx -> dbg -> head;
	while(head != NULL){
		if((u64 *)head -> addr == (u64 *)addr){
			head -> status = 1;
			*(u8 *) addr = INT3_OPCODE;
			return 0;
		}
		head = head -> next;
	}
	return -1;
}

/*
 * called from debuggers context
 */
int do_disable_breakpoint(struct exec_context *ctx, void *addr)
{
	/* changing status of breakpoint at given address */
	struct breakpoint_info* head = ctx -> dbg -> head;
	while(head != NULL){
		if((u64 *)head -> addr == (u64 *)addr){
			head -> status = 0;
			*(u8 *) addr = PUSHRBP_OPCODE;
			return 0;
		}
		head = head -> next;
	}
	return -1;
}

/*
 * called from debuggers context
 */
int do_info_breakpoints(struct exec_context *ctx, struct breakpoint *ubp)
{
	int n = 0;
	/* checking number of breakpoints condition */
	struct breakpoint_info* temp = ctx -> dbg -> head;
	if(temp == NULL){
		return -1;
	}
	while(temp != NULL){
		temp = temp -> next;
		n++;
	}
	if(n > MAX_BREAKPOINTS){
		return -1;
	}
	/* assigning values to ubp */
	struct breakpoint_info* head = ctx -> dbg -> head;
	n = 0;
	while(head != NULL){
		ubp[n].num = head -> num;
		ubp[n].addr = head -> addr;
		ubp[n].status = head -> status;

		head = head -> next;
		n++;
	}
	return n;
}

/*
 * called from debuggers context
 */
int do_info_registers(struct exec_context *ctx, struct registers *regs)
{
	/* getting child context from parent */
	int cpid = ((ctx -> pid + 1) == MAX_PROCESSES) ? 1 : (ctx -> pid + 1);
	struct exec_context* child = get_ctx_by_pid(cpid);

	if(child == NULL){		
		return -1;
	}

	/* assigning values to struct registers */
	regs -> r15 = child -> regs.r15;
	regs -> r14 = child -> regs.r14;
	regs -> r13 = child -> regs.r13;
	regs -> r12 = child -> regs.r12;
	regs -> r11 = child -> regs.r11;
	regs -> r10 = child -> regs.r10;
	regs -> r9 = child -> regs.r9;
	regs -> r8 = child -> regs.r8;
	regs -> rbp = child -> regs.rbp;
	regs -> rdi = child -> regs.rdi;
	regs -> rsi = child -> regs.rsi;
	regs -> rdx = child -> regs.rdx;
	regs -> rcx = child -> regs.rcx;
	regs -> rbx = child -> regs.rbx;
	regs -> rax = child -> regs.rax;
	regs -> entry_rip = child -> regs.entry_rip - 1;
	regs -> entry_cs = child -> regs.entry_cs;
	regs -> entry_rflags = child -> regs.entry_rflags;
	regs -> entry_rsp = child -> regs.entry_rsp + 8;
	regs -> entry_ss = child -> regs.entry_ss;
	return 0;
}

/*
 * Called from debuggers context
 */
int do_backtrace(struct exec_context *ctx, u64 bt_buf)
{
	/* copuing array to bt_buf */
	int count = ctx -> dbg -> arr_count;
	if(count > MAX_BACKTRACE){
		return -1;
	}
	int add = 0;
	for(int i = 0; i < count; i++){
		*(u64 *)(bt_buf + add) = ctx -> dbg -> arrback[i];
		add += 8;
	}
	return count;
}


/*
 * When the debugger calls wait
 * it must move to WAITING state
 * and its child must move to READY state
 */

s64 do_wait_and_continue(struct exec_context *ctx)
{
	/* getting child context from parent */
	int cpid = ((ctx -> pid + 1) == MAX_PROCESSES) ?  1 : (ctx -> pid + 1);
	struct exec_context* child = get_ctx_by_pid(cpid);

	// if(child -> ppid != ctx -> pid || child == NULL){
	// 	return -1;
	// }

	/* scheduling and changing states */
	child -> state = READY;
	ctx -> state = WAITING;
	schedule(child);
	return -1;
}
