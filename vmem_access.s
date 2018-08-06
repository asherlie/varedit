	.file	"vmem_access.c"
	.text
	.globl	free_mem_map
	.type	free_mem_map, @function
free_mem_map:
.LFB5:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%rdi, -24(%rbp)
	movl	%esi, %eax
	movb	%al, -28(%rbp)
	movq	-24(%rbp), %rax
	movq	80(%rax), %rax
	testq	%rax, %rax
	je	.L7
	cmpb	$0, -28(%rbp)
	je	.L4
	movq	-24(%rbp), %rax
	movq	56(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	jmp	.L1
.L4:
	movq	$0, -8(%rbp)
	jmp	.L5
.L6:
	movq	-24(%rbp), %rax
	movq	64(%rax), %rax
	movq	-8(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	addq	$1, -8(%rbp)
.L5:
	movq	-24(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -8(%rbp)
	jb	.L6
	movq	-24(%rbp), %rax
	movq	64(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	jmp	.L1
.L7:
	nop
.L1:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE5:
	.size	free_mem_map, .-free_mem_map
	.globl	read_bytes_from_pid_mem
	.type	read_bytes_from_pid_mem, @function
read_bytes_from_pid_mem:
.LFB6:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$112, %rsp
	movl	%edi, -84(%rbp)
	movl	%esi, -88(%rbp)
	movq	%rdx, -96(%rbp)
	movq	%rcx, -104(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	cmpq	$0, -104(%rbp)
	jne	.L9
	movl	-88(%rbp), %eax
	movl	%eax, -68(%rbp)
	jmp	.L10
.L9:
	movq	-104(%rbp), %rdx
	movq	-96(%rbp), %rax
	subq	%rax, %rdx
	movq	%rdx, %rax
	movl	%eax, -68(%rbp)
.L10:
	movl	-68(%rbp), %eax
	cltq
	salq	$4, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -48(%rbp)
	movl	-68(%rbp), %eax
	addl	$1, %eax
	cltq
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -40(%rbp)
	movl	-68(%rbp), %eax
	movslq	%eax, %rdx
	movq	-40(%rbp), %rax
	addq	%rdx, %rax
	movb	$0, (%rax)
	movl	$0, -64(%rbp)
	movq	-48(%rbp), %rax
	movq	-40(%rbp), %rdx
	movq	%rdx, (%rax)
	movl	-68(%rbp), %eax
	movslq	%eax, %rdx
	movq	-48(%rbp), %rax
	movq	%rdx, 8(%rax)
	movl	$0, -60(%rbp)
	jmp	.L11
.L12:
	movl	-64(%rbp), %eax
	movslq	%eax, %rcx
	movl	-60(%rbp), %eax
	cltq
	salq	$4, %rax
	movq	%rax, %rdx
	movq	-48(%rbp), %rax
	addq	%rdx, %rax
	movq	-40(%rbp), %rdx
	addq	%rcx, %rdx
	movq	%rdx, (%rax)
	movl	-60(%rbp), %eax
	cltq
	salq	$4, %rax
	movq	%rax, %rdx
	movq	-48(%rbp), %rax
	addq	%rdx, %rax
	movq	$1024, 8(%rax)
	addl	$1024, -64(%rbp)
	addl	$1, -60(%rbp)
.L11:
	movl	-60(%rbp), %eax
	cmpl	-68(%rbp), %eax
	jl	.L12
	movq	-96(%rbp), %rax
	movq	%rax, -32(%rbp)
	movl	-68(%rbp), %eax
	cltq
	movq	%rax, -24(%rbp)
	movl	$0, -56(%rbp)
	cmpl	$1023, -68(%rbp)
	jg	.L13
	movl	-68(%rbp), %eax
	movslq	%eax, %rdx
	leaq	-32(%rbp), %rcx
	movq	-48(%rbp), %rsi
	movl	-84(%rbp), %eax
	movl	$0, %r9d
	movl	$1, %r8d
	movl	%eax, %edi
	call	process_vm_readv@PLT
	jmp	.L14
.L13:
	movl	$0, -52(%rbp)
	jmp	.L15
.L16:
	movl	-56(%rbp), %eax
	cltq
	salq	$4, %rax
	movq	%rax, %rdx
	movq	-48(%rbp), %rax
	leaq	(%rdx,%rax), %rsi
	leaq	-32(%rbp), %rdx
	movl	-84(%rbp), %eax
	movl	$0, %r9d
	movl	$1, %r8d
	movq	%rdx, %rcx
	movl	$1024, %edx
	movl	%eax, %edi
	call	process_vm_readv@PLT
	movq	-32(%rbp), %rax
	addq	$1024, %rax
	movq	%rax, -32(%rbp)
	movq	$1024, -24(%rbp)
	addl	$1024, -56(%rbp)
	addl	$1, -52(%rbp)
.L15:
	movl	-68(%rbp), %eax
	leal	1023(%rax), %edx
	testl	%eax, %eax
	cmovs	%edx, %eax
	sarl	$10, %eax
	addl	$1, %eax
	cmpl	%eax, -52(%rbp)
	jl	.L16
.L14:
	movq	-48(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-40(%rbp), %rax
	movq	-8(%rbp), %rdi
	xorq	%fs:40, %rdi
	je	.L18
	call	__stack_chk_fail@PLT
.L18:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE6:
	.size	read_bytes_from_pid_mem, .-read_bytes_from_pid_mem
	.globl	read_single_val_from_pid_mem
	.type	read_single_val_from_pid_mem, @function
read_single_val_from_pid_mem:
.LFB7:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$80, %rsp
	movl	%edi, -68(%rbp)
	movl	%esi, -72(%rbp)
	movq	%rdx, -80(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	movl	$0, -52(%rbp)
	leaq	-52(%rbp), %rax
	movq	%rax, -48(%rbp)
	movl	-72(%rbp), %eax
	cltq
	movq	%rax, -40(%rbp)
	movq	-80(%rbp), %rax
	movq	%rax, -32(%rbp)
	movl	-72(%rbp), %eax
	cltq
	movq	%rax, -24(%rbp)
	leaq	-32(%rbp), %rdx
	leaq	-48(%rbp), %rsi
	movl	-68(%rbp), %eax
	movl	$0, %r9d
	movl	$1, %r8d
	movq	%rdx, %rcx
	movl	$1, %edx
	movl	%eax, %edi
	call	process_vm_readv@PLT
	movl	-52(%rbp), %eax
	movq	-8(%rbp), %rcx
	xorq	%fs:40, %rcx
	je	.L21
	call	__stack_chk_fail@PLT
.L21:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE7:
	.size	read_single_val_from_pid_mem, .-read_single_val_from_pid_mem
	.globl	read_str_from_mem_block
	.type	read_str_from_mem_block, @function
read_str_from_mem_block:
.LFB8:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$16, %rsp
	movl	%edi, -4(%rbp)
	movq	%rsi, -16(%rbp)
	movl	%edx, -8(%rbp)
	movl	-8(%rbp), %eax
	movslq	%eax, %rdx
	movq	-16(%rbp), %rax
	leaq	(%rdx,%rax), %rcx
	movq	-16(%rbp), %rdx
	movl	-4(%rbp), %eax
	movl	$1, %esi
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE8:
	.size	read_str_from_mem_block, .-read_str_from_mem_block
	.globl	read_str_from_mem_block_slow
	.type	read_str_from_mem_block_slow, @function
read_str_from_mem_block_slow:
.LFB9:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$80, %rsp
	movl	%edi, -52(%rbp)
	movq	%rsi, -64(%rbp)
	movq	%rdx, -72(%rbp)
	movl	$1, -32(%rbp)
	movl	-32(%rbp), %eax
	cltq
	addq	$1, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -24(%rbp)
	movl	$0, -28(%rbp)
	movq	-64(%rbp), %rax
	movq	%rax, -16(%rbp)
	jmp	.L25
.L30:
	movq	-16(%rbp), %rdx
	movl	-52(%rbp), %eax
	movl	$1, %esi
	movl	%eax, %edi
	call	read_single_val_from_pid_mem
	movb	%al, -33(%rbp)
	cmpb	$0, -33(%rbp)
	jle	.L26
	cmpb	$127, -33(%rbp)
	jne	.L27
.L26:
	movq	-24(%rbp), %rax
	jmp	.L28
.L27:
	movl	-28(%rbp), %eax
	cmpl	-32(%rbp), %eax
	jne	.L29
	addl	$10, -32(%rbp)
	movl	-32(%rbp), %eax
	cltq
	addq	$1, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -8(%rbp)
	movl	-32(%rbp), %eax
	movslq	%eax, %rdx
	movq	-8(%rbp), %rax
	movl	$0, %esi
	movq	%rax, %rdi
	call	memset@PLT
	movq	-24(%rbp), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	strcpy@PLT
	movq	-24(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-8(%rbp), %rax
	movq	%rax, -24(%rbp)
.L29:
	movl	-28(%rbp), %eax
	leal	1(%rax), %edx
	movl	%edx, -28(%rbp)
	movslq	%eax, %rdx
	movq	-24(%rbp), %rax
	addq	%rax, %rdx
	movzbl	-33(%rbp), %eax
	movb	%al, (%rdx)
	addq	$1, -16(%rbp)
.L25:
	movq	-16(%rbp), %rax
	cmpq	-72(%rbp), %rax
	jne	.L30
	movq	-24(%rbp), %rax
.L28:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE9:
	.size	read_str_from_mem_block_slow, .-read_str_from_mem_block_slow
	.globl	pid_memcpy
	.type	pid_memcpy, @function
pid_memcpy:
.LFB10:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$48, %rsp
	movl	%edi, -20(%rbp)
	movl	%esi, -24(%rbp)
	movq	%rdx, -32(%rbp)
	movq	%rcx, -40(%rbp)
	movl	%r8d, -44(%rbp)
	movb	$1, -9(%rbp)
	call	getpid@PLT
	cmpl	%eax, -24(%rbp)
	jne	.L32
	movl	-44(%rbp), %eax
	cltq
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -8(%rbp)
	movl	-44(%rbp), %eax
	movslq	%eax, %rdx
	movq	-40(%rbp), %rcx
	movq	-8(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	jmp	.L33
.L32:
	movq	-40(%rbp), %rdx
	movl	-44(%rbp), %esi
	movl	-24(%rbp), %eax
	movl	$0, %ecx
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	movq	%rax, -8(%rbp)
.L33:
	call	getpid@PLT
	cmpl	%eax, -20(%rbp)
	jne	.L34
	movl	-44(%rbp), %eax
	movslq	%eax, %rdx
	movq	-8(%rbp), %rcx
	movq	-32(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	jmp	.L35
.L34:
	movq	-8(%rbp), %rcx
	movq	-32(%rbp), %rdx
	movl	-44(%rbp), %esi
	movl	-20(%rbp), %eax
	movl	%eax, %edi
	call	write_bytes_to_pid_mem
	movb	%al, -9(%rbp)
.L35:
	movq	-8(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
	movzbl	-9(%rbp), %eax
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE10:
	.size	pid_memcpy, .-pid_memcpy
	.globl	write_bytes_to_pid_mem
	.type	write_bytes_to_pid_mem, @function
write_bytes_to_pid_mem:
.LFB11:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%rbx
	subq	$88, %rsp
	.cfi_offset 3, -24
	movl	%edi, -68(%rbp)
	movl	%esi, -72(%rbp)
	movq	%rdx, -80(%rbp)
	movq	%rcx, -88(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	movq	-88(%rbp), %rax
	movq	%rax, -64(%rbp)
	movl	-72(%rbp), %eax
	cltq
	movq	%rax, -56(%rbp)
	movq	-80(%rbp), %rax
	movq	%rax, -48(%rbp)
	movl	-72(%rbp), %eax
	cltq
	movq	%rax, -40(%rbp)
	movl	-72(%rbp), %eax
	movslq	%eax, %rbx
	leaq	-48(%rbp), %rdx
	leaq	-64(%rbp), %rsi
	movl	-68(%rbp), %eax
	movl	$0, %r9d
	movl	$1, %r8d
	movq	%rdx, %rcx
	movl	$1, %edx
	movl	%eax, %edi
	call	process_vm_writev@PLT
	cmpq	%rax, %rbx
	sete	%al
	movq	-24(%rbp), %rcx
	xorq	%fs:40, %rcx
	je	.L39
	call	__stack_chk_fail@PLT
.L39:
	addq	$88, %rsp
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE11:
	.size	write_bytes_to_pid_mem, .-write_bytes_to_pid_mem
	.globl	write_int_to_pid_mem
	.type	write_int_to_pid_mem, @function
write_int_to_pid_mem:
.LFB12:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movq	%rsi, -32(%rbp)
	movl	%edx, -24(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	movl	-24(%rbp), %eax
	movl	%eax, -12(%rbp)
	leaq	-12(%rbp), %rcx
	movq	-32(%rbp), %rdx
	movl	-20(%rbp), %eax
	movl	$4, %esi
	movl	%eax, %edi
	call	write_bytes_to_pid_mem
	movq	-8(%rbp), %rsi
	xorq	%fs:40, %rsi
	je	.L42
	call	__stack_chk_fail@PLT
.L42:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE12:
	.size	write_int_to_pid_mem, .-write_int_to_pid_mem
	.globl	write_str_to_pid_mem
	.type	write_str_to_pid_mem, @function
write_str_to_pid_mem:
.LFB13:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	%edi, -4(%rbp)
	movq	%rsi, -16(%rbp)
	movq	%rdx, -24(%rbp)
	movq	-24(%rbp), %rax
	movq	%rax, %rdi
	call	strlen@PLT
	movl	%eax, %esi
	movq	-24(%rbp), %rcx
	movq	-16(%rbp), %rdx
	movl	-4(%rbp), %eax
	movl	%eax, %edi
	call	write_bytes_to_pid_mem
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE13:
	.size	write_str_to_pid_mem, .-write_str_to_pid_mem
	.globl	populate_mem_map
	.type	populate_mem_map, @function
populate_mem_map:
.LFB14:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%rbx
	subq	$248, %rsp
	.cfi_offset 3, -24
	movq	%rdi, -232(%rbp)
	movl	%esi, -236(%rbp)
	movl	%edx, -240(%rbp)
	movl	%ecx, %eax
	movl	%r8d, %edx
	movl	%r9d, -252(%rbp)
	movb	%al, -244(%rbp)
	movl	%edx, %eax
	movb	%al, -248(%rbp)
	movq	-232(%rbp), %rax
	movl	-252(%rbp), %edx
	movl	%edx, 88(%rax)
	movq	-232(%rbp), %rax
	movl	-240(%rbp), %edx
	movl	%edx, 76(%rax)
	movq	-232(%rbp), %rax
	movzbl	-244(%rbp), %edx
	movb	%dl, 92(%rax)
	movq	$0, -160(%rbp)
	cmpl	$0, -240(%rbp)
	je	.L46
	cmpl	$2, -240(%rbp)
	jne	.L47
.L46:
	movq	-232(%rbp), %rax
	movq	24(%rax), %rax
	movq	%rax, -152(%rbp)
	movq	-232(%rbp), %rax
	movq	32(%rax), %rax
	movq	%rax, %rdx
	movq	-152(%rbp), %rax
	subq	%rax, %rdx
	movq	%rdx, %rax
	movq	%rax, -160(%rbp)
.L47:
	cmpl	$1, -240(%rbp)
	je	.L48
	cmpl	$2, -240(%rbp)
	jne	.L49
.L48:
	movq	-232(%rbp), %rax
	movq	8(%rax), %rax
	movq	%rax, -144(%rbp)
	movq	-232(%rbp), %rax
	movq	16(%rax), %rax
	movq	%rax, %rdx
	movq	-144(%rbp), %rax
	subq	%rax, %rdx
	movq	%rdx, %rax
	addq	%rax, -160(%rbp)
.L49:
	cmpb	$0, -244(%rbp)
	je	.L50
	movl	$0, -220(%rbp)
	jmp	.L51
.L52:
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-220(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	%rax, %rcx
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-220(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	subq	%rax, %rcx
	movq	%rcx, %rax
	addq	%rax, -160(%rbp)
	addl	$1, -220(%rbp)
.L51:
	movq	-232(%rbp), %rax
	movl	40(%rax), %eax
	cmpl	%eax, -220(%rbp)
	jl	.L52
.L50:
	movq	-232(%rbp), %rax
	movq	$0, 80(%rax)
	cmpb	$0, -248(%rbp)
	je	.L53
	movl	-252(%rbp), %eax
	movslq	%eax, %rbx
	movq	-160(%rbp), %rax
	cqto
	idivq	%rbx
	movq	%rax, -160(%rbp)
	movq	-160(%rbp), %rax
	salq	$4, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, %rdx
	movq	-232(%rbp), %rax
	movq	%rdx, 56(%rax)
	jmp	.L54
.L53:
	movq	-160(%rbp), %rax
	salq	$4, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, %rdx
	movq	-232(%rbp), %rax
	movq	%rdx, 64(%rax)
.L54:
	movq	$0, -136(%rbp)
	movq	$0, -128(%rbp)
	cmpb	$0, -248(%rbp)
	je	.L55
	movq	-160(%rbp), %rdx
	movq	-232(%rbp), %rax
	movq	%rdx, 80(%rax)
	cmpl	$0, -240(%rbp)
	je	.L56
	cmpl	$2, -240(%rbp)
	jne	.L57
.L56:
	movq	-232(%rbp), %rax
	movq	32(%rax), %rcx
	movq	-152(%rbp), %rdx
	movl	-252(%rbp), %esi
	movl	-236(%rbp), %eax
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	movq	%rax, -40(%rbp)
	jmp	.L58
.L59:
	movq	-232(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rax, %rdx
	movq	-152(%rbp), %rax
	movq	%rax, (%rdx)
	movq	-232(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movl	$0, 8(%rax)
	movl	-252(%rbp), %eax
	movslq	%eax, %rdx
	movq	-128(%rbp), %rcx
	movq	-40(%rbp), %rax
	leaq	(%rcx,%rax), %rsi
	movq	-232(%rbp), %rax
	movq	56(%rax), %rdi
	movq	-136(%rbp), %rax
	leaq	1(%rax), %rcx
	movq	%rcx, -136(%rbp)
	salq	$4, %rax
	addq	%rdi, %rax
	addq	$8, %rax
	movq	%rax, %rdi
	call	memcpy@PLT
	movl	-252(%rbp), %eax
	cltq
	addq	%rax, -128(%rbp)
	movl	-252(%rbp), %eax
	cltq
	addq	%rax, -152(%rbp)
.L58:
	movq	-232(%rbp), %rax
	movq	32(%rax), %rax
	cmpq	%rax, -152(%rbp)
	jne	.L59
	movq	-40(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
.L57:
	cmpl	$1, -240(%rbp)
	je	.L60
	cmpl	$2, -240(%rbp)
	jne	.L61
.L60:
	movq	$0, -128(%rbp)
	movq	-232(%rbp), %rax
	movq	16(%rax), %rcx
	movq	-144(%rbp), %rdx
	movl	-252(%rbp), %esi
	movl	-236(%rbp), %eax
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	movq	%rax, -32(%rbp)
	jmp	.L62
.L63:
	movq	-232(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rax, %rdx
	movq	-144(%rbp), %rax
	movq	%rax, (%rdx)
	movq	-232(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movl	$0, 8(%rax)
	movl	-252(%rbp), %eax
	movslq	%eax, %rdx
	movq	-128(%rbp), %rcx
	movq	-32(%rbp), %rax
	leaq	(%rcx,%rax), %rsi
	movq	-232(%rbp), %rax
	movq	56(%rax), %rdi
	movq	-136(%rbp), %rax
	leaq	1(%rax), %rcx
	movq	%rcx, -136(%rbp)
	salq	$4, %rax
	addq	%rdi, %rax
	addq	$8, %rax
	movq	%rax, %rdi
	call	memcpy@PLT
	movl	-252(%rbp), %eax
	cltq
	addq	%rax, -128(%rbp)
	movl	-252(%rbp), %eax
	cltq
	addq	%rax, -144(%rbp)
.L62:
	movq	-232(%rbp), %rax
	movq	16(%rax), %rax
	cmpq	%rax, -144(%rbp)
	jne	.L63
	movq	-32(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
.L61:
	cmpb	$0, -244(%rbp)
	je	.L88
	movl	$0, -216(%rbp)
	jmp	.L65
.L68:
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-216(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rcx
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-216(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rdx
	movl	-252(%rbp), %esi
	movl	-236(%rbp), %eax
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	movq	%rax, -24(%rbp)
	movq	$0, -128(%rbp)
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-216(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	%rax, -120(%rbp)
	jmp	.L66
.L67:
	movq	-232(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rax, %rdx
	movq	-120(%rbp), %rax
	movq	%rax, (%rdx)
	movq	-232(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movl	$0, 8(%rax)
	movl	-252(%rbp), %eax
	movslq	%eax, %rdx
	movq	-128(%rbp), %rcx
	movq	-24(%rbp), %rax
	leaq	(%rcx,%rax), %rsi
	movq	-232(%rbp), %rax
	movq	56(%rax), %rdi
	movq	-136(%rbp), %rax
	leaq	1(%rax), %rcx
	movq	%rcx, -136(%rbp)
	salq	$4, %rax
	addq	%rdi, %rax
	addq	$8, %rax
	movq	%rax, %rdi
	call	memcpy@PLT
	movl	-252(%rbp), %eax
	cltq
	addq	%rax, -128(%rbp)
	movl	-252(%rbp), %eax
	cltq
	addq	%rax, -120(%rbp)
.L66:
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-216(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	cmpq	%rax, -120(%rbp)
	jne	.L67
	movq	-24(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
	addl	$1, -216(%rbp)
.L65:
	movq	-232(%rbp), %rax
	movl	40(%rax), %eax
	cmpl	%eax, -216(%rbp)
	jl	.L68
	jmp	.L88
.L55:
	movl	$1, -188(%rbp)
	movb	$0, -221(%rbp)
	cmpl	$0, -240(%rbp)
	je	.L70
	cmpl	$2, -240(%rbp)
	jne	.L71
.L70:
	movl	-188(%rbp), %eax
	movl	%eax, -212(%rbp)
	movl	$0, -208(%rbp)
	movq	-232(%rbp), %rax
	movq	32(%rax), %rcx
	movq	-152(%rbp), %rdx
	movl	-236(%rbp), %eax
	movl	$1, %esi
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	movq	%rax, -72(%rbp)
	movq	-152(%rbp), %rax
	movq	%rax, -104(%rbp)
	movq	-232(%rbp), %rax
	movq	32(%rax), %rax
	movq	%rax, %rdx
	movq	-152(%rbp), %rax
	subq	%rax, %rdx
	movq	%rdx, %rax
	movl	%eax, -184(%rbp)
	movl	$0, -204(%rbp)
	jmp	.L72
.L77:
	movl	-204(%rbp), %eax
	movslq	%eax, %rdx
	movq	-72(%rbp), %rax
	addq	%rdx, %rax
	movzbl	(%rax), %eax
	testb	%al, %al
	je	.L73
	movl	-204(%rbp), %eax
	movslq	%eax, %rdx
	movq	-72(%rbp), %rax
	addq	%rdx, %rax
	movzbl	(%rax), %eax
	cmpb	$126, %al
	ja	.L73
	movzbl	-221(%rbp), %eax
	xorl	$1, %eax
	testb	%al, %al
	je	.L74
	movq	-104(%rbp), %rax
	movq	%rax, -96(%rbp)
	movl	-188(%rbp), %eax
	movl	%eax, -212(%rbp)
	movl	-212(%rbp), %eax
	cltq
	addq	$1, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -112(%rbp)
	movl	$0, -208(%rbp)
	movl	-212(%rbp), %eax
	movslq	%eax, %rdx
	movq	-112(%rbp), %rax
	movl	$0, %esi
	movq	%rax, %rdi
	call	memset@PLT
.L74:
	movb	$1, -221(%rbp)
	movl	-212(%rbp), %eax
	subl	$1, %eax
	cmpl	%eax, -208(%rbp)
	jl	.L75
	addl	$20, -212(%rbp)
	movl	-212(%rbp), %eax
	cltq
	addq	$1, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -64(%rbp)
	movq	-112(%rbp), %rdx
	movq	-64(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	strcpy@PLT
	movq	-112(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-64(%rbp), %rax
	movq	%rax, -112(%rbp)
.L75:
	movl	-204(%rbp), %eax
	movslq	%eax, %rdx
	movq	-72(%rbp), %rax
	addq	%rdx, %rax
	movzbl	(%rax), %ecx
	movl	-208(%rbp), %eax
	leal	1(%rax), %edx
	movl	%edx, -208(%rbp)
	movslq	%eax, %rdx
	movq	-112(%rbp), %rax
	addq	%rdx, %rax
	movl	%ecx, %edx
	movb	%dl, (%rax)
	jmp	.L76
.L73:
	cmpb	$0, -221(%rbp)
	je	.L76
	movb	$0, -221(%rbp)
	movq	-232(%rbp), %rax
	movq	64(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rax, %rdx
	movq	-96(%rbp), %rax
	movq	%rax, (%rdx)
	movq	-232(%rbp), %rax
	movq	64(%rax), %rcx
	movq	-136(%rbp), %rax
	leaq	1(%rax), %rdx
	movq	%rdx, -136(%rbp)
	salq	$4, %rax
	leaq	(%rcx,%rax), %rdx
	movq	-112(%rbp), %rax
	movq	%rax, 8(%rdx)
	movq	-232(%rbp), %rax
	movq	80(%rax), %rax
	leaq	1(%rax), %rdx
	movq	-232(%rbp), %rax
	movq	%rdx, 80(%rax)
.L76:
	addq	$1, -104(%rbp)
	addl	$1, -204(%rbp)
.L72:
	movl	-204(%rbp), %eax
	cmpl	-184(%rbp), %eax
	jl	.L77
	movq	-72(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
.L71:
	cmpl	$1, -240(%rbp)
	je	.L78
	cmpl	$2, -240(%rbp)
	jne	.L79
.L78:
	movb	$0, -221(%rbp)
	movl	-188(%rbp), %eax
	movl	%eax, -180(%rbp)
	movq	-232(%rbp), %rax
	movq	16(%rax), %rcx
	movq	-144(%rbp), %rdx
	movl	-236(%rbp), %eax
	movl	$1, %esi
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	movq	%rax, -56(%rbp)
	movq	-144(%rbp), %rax
	movq	%rax, -88(%rbp)
	movq	-232(%rbp), %rax
	movq	16(%rax), %rax
	movq	%rax, %rdx
	movq	-144(%rbp), %rax
	subq	%rax, %rdx
	movq	%rdx, %rax
	movl	%eax, -176(%rbp)
	movl	$0, -172(%rbp)
	movl	$0, -200(%rbp)
	jmp	.L80
.L82:
	movl	-200(%rbp), %eax
	movslq	%eax, %rdx
	movq	-56(%rbp), %rax
	addq	%rdx, %rax
	movzbl	(%rax), %eax
	testb	%al, %al
	jle	.L81
	movl	-200(%rbp), %eax
	movslq	%eax, %rdx
	movq	-56(%rbp), %rax
	addq	%rdx, %rax
	movzbl	(%rax), %eax
	cmpb	$127, %al
	je	.L81
	movl	-200(%rbp), %eax
	movslq	%eax, %rdx
	movq	-56(%rbp), %rax
	addq	%rdx, %rax
	movq	%rax, %rdi
	call	strlen@PLT
	movl	%eax, -172(%rbp)
	movl	-172(%rbp), %eax
	cltq
	leaq	1(%rax), %rdx
	movq	-232(%rbp), %rax
	movq	64(%rax), %rax
	movq	-136(%rbp), %rcx
	salq	$4, %rcx
	leaq	(%rax,%rcx), %rbx
	movq	%rdx, %rdi
	call	malloc@PLT
	movq	%rax, 8(%rbx)
	movl	-172(%rbp), %eax
	movslq	%eax, %rdx
	movl	-200(%rbp), %eax
	movslq	%eax, %rcx
	movq	-56(%rbp), %rax
	addq	%rax, %rcx
	movq	-232(%rbp), %rax
	movq	64(%rax), %rax
	movq	-136(%rbp), %rsi
	salq	$4, %rsi
	addq	%rsi, %rax
	movq	8(%rax), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	movq	-232(%rbp), %rax
	movq	64(%rax), %rcx
	movq	-136(%rbp), %rax
	leaq	1(%rax), %rdx
	movq	%rdx, -136(%rbp)
	salq	$4, %rax
	leaq	(%rcx,%rax), %rdx
	movq	-88(%rbp), %rax
	movq	%rax, (%rdx)
	movq	-232(%rbp), %rax
	movq	80(%rax), %rax
	leaq	1(%rax), %rdx
	movq	-232(%rbp), %rax
	movq	%rdx, 80(%rax)
.L81:
	addq	$1, -88(%rbp)
	addl	$1, -200(%rbp)
.L80:
	movl	-200(%rbp), %eax
	cmpl	-176(%rbp), %eax
	jl	.L82
	movq	-56(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
.L79:
	cmpb	$0, -244(%rbp)
	je	.L88
	movb	$0, -221(%rbp)
	movl	$0, -168(%rbp)
	movl	$0, -196(%rbp)
	jmp	.L83
.L87:
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-196(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rcx
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-196(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rdx
	movl	-236(%rbp), %eax
	movl	$1, %esi
	movl	%eax, %edi
	call	read_bytes_from_pid_mem
	movq	%rax, -48(%rbp)
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-196(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	%rax, -80(%rbp)
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-196(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	%rax, %rcx
	movq	-232(%rbp), %rax
	movq	48(%rax), %rax
	movl	-196(%rbp), %edx
	movslq	%edx, %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	subq	%rax, %rcx
	movq	%rcx, %rax
	movl	%eax, -164(%rbp)
	movl	$0, -192(%rbp)
	jmp	.L84
.L86:
	movl	-192(%rbp), %eax
	movslq	%eax, %rdx
	movq	-48(%rbp), %rax
	addq	%rdx, %rax
	movzbl	(%rax), %eax
	testb	%al, %al
	jle	.L85
	movl	-192(%rbp), %eax
	movslq	%eax, %rdx
	movq	-48(%rbp), %rax
	addq	%rdx, %rax
	movzbl	(%rax), %eax
	cmpb	$127, %al
	je	.L85
	movl	-192(%rbp), %eax
	movslq	%eax, %rdx
	movq	-48(%rbp), %rax
	addq	%rdx, %rax
	movq	%rax, %rdi
	call	strlen@PLT
	movl	%eax, -168(%rbp)
	movl	-168(%rbp), %eax
	cltq
	leaq	1(%rax), %rdx
	movq	-232(%rbp), %rax
	movq	64(%rax), %rax
	movq	-136(%rbp), %rcx
	salq	$4, %rcx
	leaq	(%rax,%rcx), %rbx
	movq	%rdx, %rdi
	call	malloc@PLT
	movq	%rax, 8(%rbx)
	movl	-168(%rbp), %eax
	movslq	%eax, %rdx
	movl	-192(%rbp), %eax
	movslq	%eax, %rcx
	movq	-48(%rbp), %rax
	addq	%rax, %rcx
	movq	-232(%rbp), %rax
	movq	64(%rax), %rax
	movq	-136(%rbp), %rsi
	salq	$4, %rsi
	addq	%rsi, %rax
	movq	8(%rax), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	movq	-232(%rbp), %rax
	movq	64(%rax), %rcx
	movq	-136(%rbp), %rax
	leaq	1(%rax), %rdx
	movq	%rdx, -136(%rbp)
	salq	$4, %rax
	leaq	(%rcx,%rax), %rdx
	movq	-80(%rbp), %rax
	movq	%rax, (%rdx)
	movq	-232(%rbp), %rax
	movq	80(%rax), %rax
	leaq	1(%rax), %rdx
	movq	-232(%rbp), %rax
	movq	%rdx, 80(%rax)
	movl	-168(%rbp), %eax
	addl	%eax, -192(%rbp)
	movl	-168(%rbp), %eax
	cltq
	addq	%rax, -80(%rbp)
.L85:
	addl	$1, -192(%rbp)
.L84:
	movl	-192(%rbp), %eax
	cmpl	-164(%rbp), %eax
	jl	.L86
	movq	-48(%rbp), %rax
	movq	%rax, %rdi
	call	free@PLT
	addl	$1, -196(%rbp)
.L83:
	movq	-232(%rbp), %rax
	movl	40(%rax), %eax
	cmpl	%eax, -196(%rbp)
	jl	.L87
.L88:
	nop
	addq	$248, %rsp
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE14:
	.size	populate_mem_map, .-populate_mem_map
	.globl	update_mem_map
	.type	update_mem_map, @function
update_mem_map:
.LFB15:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%rbx
	subq	$168, %rsp
	.cfi_offset 3, -24
	movq	%rdi, -168(%rbp)
	movl	%esi, %eax
	movb	%al, -172(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	movzbl	-172(%rbp), %eax
	xorl	$1, %eax
	testb	%al, %al
	jne	.L90
	movq	-168(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	$102399, %rax
	ja	.L91
.L90:
	cmpb	$0, -172(%rbp)
	je	.L92
	movq	$0, -152(%rbp)
	jmp	.L93
.L94:
	movq	-168(%rbp), %rax
	movq	56(%rax), %rax
	movq	-152(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rdx
	movq	-168(%rbp), %rax
	movl	88(%rax), %ecx
	movq	-168(%rbp), %rax
	movl	72(%rax), %eax
	movq	-168(%rbp), %rsi
	movq	56(%rsi), %rsi
	movq	-152(%rbp), %rdi
	salq	$4, %rdi
	leaq	(%rsi,%rdi), %rbx
	movl	%ecx, %esi
	movl	%eax, %edi
	call	read_single_val_from_pid_mem
	movl	%eax, 8(%rbx)
	addq	$1, -152(%rbp)
.L93:
	movq	-168(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -152(%rbp)
	jb	.L94
	jmp	.L98
.L92:
	movq	$0, -144(%rbp)
	jmp	.L96
.L97:
	movq	-168(%rbp), %rax
	movq	64(%rax), %rax
	movq	-144(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	%rax, %rdi
	call	strlen@PLT
	movl	%eax, -156(%rbp)
	movq	-168(%rbp), %rax
	movq	64(%rax), %rax
	movq	-144(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-168(%rbp), %rax
	movq	64(%rax), %rax
	movq	-144(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rcx
	movq	-168(%rbp), %rax
	movl	72(%rax), %eax
	movq	-168(%rbp), %rdx
	movq	64(%rdx), %rdx
	movq	-144(%rbp), %rsi
	salq	$4, %rsi
	leaq	(%rdx,%rsi), %rbx
	movl	-156(%rbp), %edx
	movq	%rcx, %rsi
	movl	%eax, %edi
	call	read_str_from_mem_block
	movq	%rax, 8(%rbx)
	addq	$1, -144(%rbp)
.L96:
	movq	-168(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -144(%rbp)
	jb	.L97
	jmp	.L98
.L91:
	movq	-168(%rbp), %rcx
	movq	(%rcx), %rax
	movq	8(%rcx), %rdx
	movq	%rax, -128(%rbp)
	movq	%rdx, -120(%rbp)
	movq	16(%rcx), %rax
	movq	24(%rcx), %rdx
	movq	%rax, -112(%rbp)
	movq	%rdx, -104(%rbp)
	movq	32(%rcx), %rax
	movq	40(%rcx), %rdx
	movq	%rax, -96(%rbp)
	movq	%rdx, -88(%rbp)
	movq	48(%rcx), %rax
	movq	%rax, -80(%rbp)
	movq	-168(%rbp), %rax
	movl	88(%rax), %r8d
	movzbl	-172(%rbp), %edi
	movq	-168(%rbp), %rax
	movzbl	92(%rax), %eax
	movzbl	%al, %ecx
	movq	-168(%rbp), %rax
	movl	76(%rax), %edx
	movq	-168(%rbp), %rax
	movl	72(%rax), %esi
	leaq	-128(%rbp), %rax
	movl	%r8d, %r9d
	movl	%edi, %r8d
	movq	%rax, %rdi
	call	populate_mem_map
	movl	$0, -160(%rbp)
	movq	$0, -136(%rbp)
	jmp	.L99
.L102:
	movq	-168(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rdx
	movq	-72(%rbp), %rax
	movq	-136(%rbp), %rcx
	salq	$4, %rcx
	addq	%rcx, %rax
	movq	(%rax), %rax
	cmpq	%rax, %rdx
	jne	.L100
	movq	-72(%rbp), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	leaq	(%rax,%rdx), %rcx
	movq	-168(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rax, %rdx
	movl	8(%rcx), %eax
	movl	%eax, 8(%rdx)
	jmp	.L101
.L100:
	addl	$1, -160(%rbp)
	movq	-168(%rbp), %rax
	movq	56(%rax), %rax
	movq	-136(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rdx
	movq	-168(%rbp), %rax
	movl	88(%rax), %ecx
	movq	-168(%rbp), %rax
	movl	72(%rax), %eax
	movq	-168(%rbp), %rsi
	movq	56(%rsi), %rsi
	movq	-136(%rbp), %rdi
	salq	$4, %rdi
	leaq	(%rsi,%rdi), %rbx
	movl	%ecx, %esi
	movl	%eax, %edi
	call	read_single_val_from_pid_mem
	movl	%eax, 8(%rbx)
	addq	$1, -136(%rbp)
.L101:
	addq	$1, -136(%rbp)
.L99:
	movq	-168(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -136(%rbp)
	jb	.L102
	movzbl	-172(%rbp), %edx
	leaq	-128(%rbp), %rax
	movl	%edx, %esi
	movq	%rax, %rdi
	call	free_mem_map
.L98:
	nop
	movq	-24(%rbp), %rax
	xorq	%fs:40, %rax
	je	.L103
	call	__stack_chk_fail@PLT
.L103:
	addq	$168, %rsp
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE15:
	.size	update_mem_map, .-update_mem_map
	.globl	narrow_mem_map_int
	.type	narrow_mem_map_int, @function
narrow_mem_map_int:
.LFB16:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$48, %rsp
	movq	%rdi, -40(%rbp)
	movl	%esi, -44(%rbp)
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	movq	%rax, -16(%rbp)
	movq	$0, -24(%rbp)
	jmp	.L105
.L107:
	movq	-40(%rbp), %rax
	movq	56(%rax), %rax
	movq	-24(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movl	8(%rax), %eax
	cmpl	%eax, -44(%rbp)
	je	.L106
	movq	-40(%rbp), %rax
	movq	56(%rax), %rdx
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	leaq	-1(%rax), %rcx
	movq	-40(%rbp), %rax
	movq	%rcx, 80(%rax)
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	salq	$4, %rax
	leaq	(%rdx,%rax), %rsi
	movq	-40(%rbp), %rax
	movq	56(%rax), %rcx
	movq	-24(%rbp), %rax
	leaq	-1(%rax), %rdx
	movq	%rdx, -24(%rbp)
	salq	$4, %rax
	addq	%rax, %rcx
	movq	(%rsi), %rax
	movq	8(%rsi), %rdx
	movq	%rax, (%rcx)
	movq	%rdx, 8(%rcx)
.L106:
	addq	$1, -24(%rbp)
.L105:
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -24(%rbp)
	jb	.L107
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	testq	%rax, %rax
	jne	.L108
	movq	-40(%rbp), %rax
	movl	$1, %esi
	movq	%rax, %rdi
	call	free_mem_map
	jmp	.L104
.L108:
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -16(%rbp)
	jbe	.L104
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	salq	$4, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -8(%rbp)
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	salq	$4, %rax
	movq	%rax, %rdx
	movq	-40(%rbp), %rax
	movq	56(%rax), %rcx
	movq	-8(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	movq	-40(%rbp), %rax
	movq	56(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-40(%rbp), %rax
	movq	-8(%rbp), %rdx
	movq	%rdx, 56(%rax)
.L104:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE16:
	.size	narrow_mem_map_int, .-narrow_mem_map_int
	.globl	narrow_mem_map_str
	.type	narrow_mem_map_str, @function
narrow_mem_map_str:
.LFB17:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$64, %rsp
	movq	%rdi, -40(%rbp)
	movq	%rsi, -48(%rbp)
	movl	%edx, %eax
	movb	%al, -52(%rbp)
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	movq	%rax, -16(%rbp)
	movq	$0, -24(%rbp)
	jmp	.L111
.L119:
	cmpb	$0, -52(%rbp)
	je	.L112
	movq	-40(%rbp), %rax
	movq	64(%rax), %rax
	movq	-24(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	testq	%rax, %rax
	je	.L113
	movq	-40(%rbp), %rax
	movq	64(%rax), %rax
	movq	-24(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	-48(%rbp), %rdx
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	strcmp@PLT
	testl	%eax, %eax
	je	.L115
.L113:
	movq	-40(%rbp), %rax
	movq	64(%rax), %rax
	movq	-24(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-40(%rbp), %rax
	movq	64(%rax), %rdx
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	leaq	-1(%rax), %rcx
	movq	-40(%rbp), %rax
	movq	%rcx, 80(%rax)
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	salq	$4, %rax
	leaq	(%rdx,%rax), %rsi
	movq	-40(%rbp), %rax
	movq	64(%rax), %rcx
	movq	-24(%rbp), %rax
	leaq	-1(%rax), %rdx
	movq	%rdx, -24(%rbp)
	salq	$4, %rax
	addq	%rax, %rcx
	movq	(%rsi), %rax
	movq	8(%rsi), %rdx
	movq	%rax, (%rcx)
	movq	%rdx, 8(%rcx)
	jmp	.L115
.L112:
	movq	-40(%rbp), %rax
	movq	64(%rax), %rax
	movq	-24(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	testq	%rax, %rax
	je	.L116
	movq	-40(%rbp), %rax
	movq	64(%rax), %rax
	movq	-24(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rdx
	movq	-48(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	is_substr@PLT
	xorl	$1, %eax
	testb	%al, %al
	je	.L115
.L116:
	movq	-40(%rbp), %rax
	movq	64(%rax), %rax
	movq	-24(%rbp), %rdx
	salq	$4, %rdx
	addq	%rdx, %rax
	movq	8(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	leaq	-1(%rax), %rdx
	movq	-40(%rbp), %rax
	movq	%rdx, 80(%rax)
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	testq	%rax, %rax
	je	.L122
	movq	-40(%rbp), %rax
	movq	64(%rax), %rdx
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	salq	$4, %rax
	leaq	(%rdx,%rax), %rsi
	movq	-40(%rbp), %rax
	movq	64(%rax), %rcx
	movq	-24(%rbp), %rax
	leaq	-1(%rax), %rdx
	movq	%rdx, -24(%rbp)
	salq	$4, %rax
	addq	%rax, %rcx
	movq	(%rsi), %rax
	movq	8(%rsi), %rdx
	movq	%rax, (%rcx)
	movq	%rdx, 8(%rcx)
.L115:
	addq	$1, -24(%rbp)
.L111:
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -24(%rbp)
	jb	.L119
	jmp	.L118
.L122:
	nop
.L118:
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	testq	%rax, %rax
	jne	.L120
	movq	-40(%rbp), %rax
	movl	$0, %esi
	movq	%rax, %rdi
	call	free_mem_map
	jmp	.L110
.L120:
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	cmpq	%rax, -16(%rbp)
	jbe	.L110
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	salq	$4, %rax
	movq	%rax, %rdi
	call	malloc@PLT
	movq	%rax, -8(%rbp)
	movq	-40(%rbp), %rax
	movq	80(%rax), %rax
	salq	$4, %rax
	movq	%rax, %rdx
	movq	-40(%rbp), %rax
	movq	64(%rax), %rcx
	movq	-8(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	movq	-40(%rbp), %rax
	movq	64(%rax), %rax
	movq	%rax, %rdi
	call	free@PLT
	movq	-40(%rbp), %rax
	movq	-8(%rbp), %rdx
	movq	%rdx, 64(%rax)
.L110:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE17:
	.size	narrow_mem_map_str, .-narrow_mem_map_str
	.ident	"GCC: (GNU) 7.3.1 20180312"
	.section	.note.GNU-stack,"",@progbits
