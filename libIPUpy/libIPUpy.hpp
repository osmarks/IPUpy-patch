#pragma once

#include "poplar/StackSizeDefs.hpp" 

#define RECURSIVE_FUNCTION_SIZE (5 * 1024)

DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_init");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_deinit");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_array");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_string");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_relocatable_array");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_int");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_do_str");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_set_stdout");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "pyexec_event_repl_init");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "pyexec_event_repl_process_char");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_relocatable_array");
extern "C" void IPUpy_deinit(void);
extern "C" void IPUpy_init(char *poplar_stack_bottom);
extern "C" void IPUpy_add_memory_as_array(const char* name, void* data, size_t num_elts, char dtype);
extern "C" void IPUpy_add_memory_as_string(const char* name, const char* data, size_t size);
extern "C" void IPUpy_add_memory_as_relocatable_array(const char* name, void*** data_ptr, size_t num_elts);
extern "C" void IPUpy_add_int(const char* name, unsigned tileid);
extern "C" void IPUpy_do_str(const char *src, int is_single_line);
extern "C" void IPUpy_set_stdout(char* _stdout, int len);
extern "C" void pyexec_event_repl_init();
extern "C" int pyexec_event_repl_process_char(int c);
extern "C" void IPUpy_add_memory_as_relocatable_array(const char* name, void*** data_ptr, size_t num_elts);