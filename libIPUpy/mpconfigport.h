#include <stdint.h>

// options to control how MicroPython is built

// Use the minimal starting configuration (disables all optional features).
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

// You can disable the built-in MicroPython compiler by setting the following
// config option to 0.  If you do this then you won't get a REPL prompt, but you
// will still be able to execute pre-compiled scripts, compiled with mpy-cross.
#define MICROPY_ENABLE_COMPILER     (1)

// #define MICROPY_QSTR_EXTRA_POOL           mp_qstr_frozen_const_pool
#define MICROPY_ENABLE_GC                 (1)
#define MICROPY_HELPER_REPL               (1)
#define MICROPY_MODULE_FROZEN_MPY         (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT    (0)

// Desperate mitigations for IPU
#define MICROPY_USE_INTERNAL_ERRNO        (1)
#define MICROPY_ENABLE_PYSTACK            (1)
#define MICROPY_NLR_SETJMP                (1)
#define MICROPY_READER_VFS                (1) // pyexec needs mp_lexer_new_from_file...


// Adding more interesting interpreter feaures
#define MICROPY_PY_ARRAY                   (1)
#define MICROPY_PY_BUILTINS_BYTEARRAY      (1)
#define MICROPY_PY_BUILTINS_SLICE          (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN      (1)
#define MICROPY_PY_BUILTINS_FILTER         (1)
#define MICROPY_PY_BUILTINS_REVERSED       (1)
#define MICROPY_PY_BUILTINS_SET            (1)
#define MICROPY_PY_BUILTINS_ENUMERATE      (1)
#define MICROPY_PY_BUILTINS_STR_SPLITLINES (1)
#define MICROPY_PY_BUILTINS_MIN_MAX        (1)
#define MICROPY_PY_BUILTINS_INPUT          (0)
#define MICROPY_FLOAT_IMPL                 (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_PY_UOS                     (1)
#define MICROPY_VFS                        (1)
#define MICROPY_VFS_LFS2                   (1)
#define MICROPY_PY_COLLECTIONS             (1)
#define MICROPY_PY_COLLECTIONS_DEQUE       (1)
#define MICROPY_PY_UHEAPQ                  (0)
#define MICROPY_REPL_EVENT_DRIVEN          (1)

#define MICROPY_ALLOC_PATH_MAX            (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT    (16)

// type definitions for the specific machine

typedef intptr_t mp_int_t; // must be pointer size
typedef uintptr_t mp_uint_t; // must be pointer size
typedef long mp_off_t;



#define mp_import_stat mp_vfs_import_stat
#define mp_builtin_open_obj mp_vfs_open_obj
#define mp_type_fileio mp_type_vfs_posix_fileio
#define mp_type_textio mp_type_vfs_posix_textio

// extra built in names to add to the global namespace
#define MICROPY_PORT_BUILTINS \
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mp_builtin_open_obj) },


#define MICROPY_HW_BOARD_NAME "M2000"
#define MICROPY_HW_MCU_NAME "Colossus IPU"

#define MP_STATE_PORT MP_STATE_VM

#define MICROPY_PORT_ROOT_POINTERS \
    const char *readline_hist[8];
