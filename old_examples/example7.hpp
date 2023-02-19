

enum SysCallValue_t: int {
	syscall_none = 0,
	syscall_get_char = 1,
	syscall_flush_stdout = 2,
	syscall_vfs_read = 3,
	syscall_vfs_write = 4,
	syscall_run = 5,
	syscall_host_run = 6,
	syscall_shutdown = 7,
};
