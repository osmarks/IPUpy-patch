#include <poplar/Vertex.hpp>
#include <algorithm>

#include "multirepl_params.hpp"


DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "pyexec_event_repl_init");
extern "C" void pyexec_event_repl_init();
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "pyexec_event_repl_process_char");
extern "C" int pyexec_event_repl_process_char(int c);
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_relocatable_array");
extern "C" void IPUpy_add_memory_as_relocatable_array(const char* name, void*** data_ptr, size_t num_elts);

typedef struct jmp_buf_t {
    unsigned int MRF[5];
    float ARF[2];
} jmp_buf[1];
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "setjmp");
extern "C" int __attribute__((noinline)) setjmp(jmp_buf env);
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "longjmp");
extern "C" void __attribute__((noreturn)) longjmp(jmp_buf env, int val);
extern "C" jmp_buf IPUpy_exit_env;
extern "C" jmp_buf IPUpy_checkpoint_env;


struct TileIDGrabber: public poplar::SupervisorVertex {
    poplar::Output<unsigned> tileid;
    __attribute__((target("supervisor")))
    bool compute() {
        *tileid = __builtin_ipu_get_tile_id();
        return true;
    }
};


void** addressOfInBufPtr = nullptr; 
void** addressOfOutBufPtr = nullptr;

struct InitVertex: public poplar::Vertex {
    poplar::Output<poplar::Vector<char>> printBuf;
    poplar::Input<poplar::Vector<char>> fileBuf;
    poplar::Input<poplar::Vector<char>> inBuf;
    poplar::InOut<bool> doneFlag;
    poplar::InOut<bool> coroutineFlag;
    poplar::Input<unsigned> tileid;

    void compute() {
        char* poplar_stack_bottom;
        asm volatile(
            "mov %[poplar_stack_bottom], $m11" 
            : [poplar_stack_bottom] "+r" (poplar_stack_bottom) ::
        );
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_init(poplar_stack_bottom, *tileid);
        int fileLen = std::min(fileBuf.size(), strlen(&fileBuf[0]));
        IPUpy_add_memory_as_string("__filedata", &fileBuf[0], fileLen);
#ifdef COMMS
        IPUpy_add_memory_as_relocatable_array("__sendbuf", &addressOfOutBufPtr, COMMSBUFSIZE);
        IPUpy_add_memory_as_relocatable_array("__recvbuf", &addressOfInBufPtr, COMMSBUFSIZE * NUMREPLS);
        IPUpy_do_str(R"(
def alltoall(payload):
    send_limit = len(__sendbuf)
    num_repls = len(__recvbuf) // send_limit

    payload = bytes(repr(payload), 0)
    send_failed = len(payload) >= send_limit
    if send_failed:
        payload = bytes(repr(None), 0)
    __sendbuf[:len(payload)] = payload
    __sendbuf[send_limit - 1] = len(payload)

    longyield

    if send_failed:
        raise ValueError("Payload too large for alltoall")
   
    ret = []
    for i in range(num_repls):
        pos = i * send_limit
        size = __recvbuf[pos + send_limit - 1]
        ret.append(eval(__recvbuf[pos: pos + size], {}))
    return ret
)", 0);
#endif

        pyexec_event_repl_init();
    
        *doneFlag = false;
        *coroutineFlag = false;
    }
};


bool checkpoint_is_live = false;

template <bool firstRun>
struct RuntimeVertex: public poplar::MultiVertex {
    poplar::Output<poplar::Vector<char>> printBuf;
    poplar::Input<poplar::Vector<char>> fileBuf;
    poplar::Input<poplar::Vector<char>> inBuf;
    poplar::InOut<bool> doneFlag;
    poplar::InOut<bool> coroutineFlag;
#ifdef COMMS
    poplar::Input<poplar::Vector<char>> commsInBuf;
    poplar::Output<poplar::Vector<char>> commsOutBuf;
#endif

    // Time-travelling spaghetti-code; Do Not Read
    void compute(unsigned tid) {
        if (tid != 5) return;

#ifdef COMMS
        *addressOfInBufPtr = (void*) &commsInBuf[0];
        *addressOfOutBufPtr = (void*) &commsOutBuf[0];
#endif

        if (!firstRun && checkpoint_is_live) {
            checkpoint_is_live = false;
            longjmp(IPUpy_checkpoint_env, 1);
        }
        checkpoint_is_live = setjmp(IPUpy_exit_env);
        if (checkpoint_is_live) {
            if (firstRun) *coroutineFlag = true;
            return;
        }

        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        if (*doneFlag || (!firstRun && !*coroutineFlag)) return;

        char c = inBuf[0];
        *doneFlag = (c == '\0') || pyexec_event_repl_process_char(c);

        *coroutineFlag = false;
    }
};
template struct RuntimeVertex<true>;
template struct RuntimeVertex<false>;



struct Any: public poplar::SupervisorVertex {
    poplar::InOut<poplar::Vector<bool>> flags;
    __attribute__((target("supervisor")))
    void compute() {
        for(unsigned i = 0; i < flags.size(); ++i) {
            if (flags[i]) {
                flags[0] = true;
                return;
            }
        }
        flags[0] = false;
    }
};
