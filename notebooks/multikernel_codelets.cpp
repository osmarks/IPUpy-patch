#include <poplar/Vertex.hpp>
#include <algorithm>

#include "multikernel_params.hpp"



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
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_register_syscall_callback");
extern "C" void IPUpy_register_syscall_callback(void (*f)());


static int syscallFlag = -1;
static unsigned cyclesBuf[2] = {0};
void syscallCallback() {
    switch (syscallFlag)
    {
    case 1: { // Clock  
        unsigned int U, L;
        asm volatile("get %[U], 0x61  \n"  // get $COUNT_U
                     "get %[L], 0x60  \n"  // get $COUNT_L
        : [U] "+r"(U), [L] "+r"(L) : : );
        // Reduce to 30-bit precision in each reg, and 
        // coarsen resolution by factor 4 to increase range
        // L advances 12 cycles after U is measured, so
        // may have wrapped around
        if (L < 12) U += 1;
        cyclesBuf[0] = (L >> 2) & 0x3FFFFFFF;
        cyclesBuf[1] = U & 0x3FFFFFFF;
        break;
    }
    default:
        break;
    }
    syscallFlag = -1;
}


void** addressOfInBufPtr = nullptr; 
void** addressOfOutBufPtr = nullptr;
void** addressOfDisplayBufPtr = nullptr;

template <bool useFile>
struct InitVertex: public poplar::Vertex {
    poplar::Output<poplar::Vector<char>> printBuf;
    poplar::InOut<unsigned> displayBuf;
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
        IPUpy_init(poplar_stack_bottom);
        IPUpy_add_int("__tileid", *tileid);
        if (useFile) {
            int fileLen = std::min(fileBuf.size(), strlen(&fileBuf[0]));
            IPUpy_add_memory_as_string("__tiledata", &fileBuf[0], fileLen);
        }
        IPUpy_add_int("__numtiles", NUMREPLS);

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

        IPUpy_add_memory_as_array("__syscallFlag", &syscallFlag, 1, 'i');
        IPUpy_add_memory_as_array("__cyclesBuf", cyclesBuf, 2, 'i');
        IPUpy_register_syscall_callback(syscallCallback);

        IPUpy_add_memory_as_relocatable_array("__displayBuf", &addressOfDisplayBufPtr, 4);
        IPUpy_do_str(R"(
def setPixel(r=0, g=0, b=0):
    __displayBuf[0] = 1
    __displayBuf[1] = max(0, min(255, int(r)))
    __displayBuf[2] = max(0, min(255, int(g)))
    __displayBuf[3] = max(0, min(255, int(b)))

def getTime():
    __syscallFlag[0] = 1
    ipusyscall
    return (__cyclesBuf[0], __cyclesBuf[1])

def deltaTime(t1, t2):
    t1_L, t1_U = t1
    t2_L, t2_U = t2
    if t1_U == t2_U:
        return t2_L - t1_L
    if t2_U - t1_U > 1:
        return 0x3FFFFFFF
    return t2_L + (0x3FFFFFFF - t1_L) + 1

global __rngstate
__rngstate = __tileid + 1
def rng():
    global __rngstate
    for i in range(10):
        __rngstate ^= (__rngstate & 0x1FFFF) << 13;
        __rngstate ^= __rngstate >> 17;
        __rngstate ^= (__rngstate & 0x1FFFFFF) << 5;
    return __rngstate
)", 0);

        *doneFlag = false;
        *coroutineFlag = false;
    }
};

template struct InitVertex<true>;
template struct InitVertex<false>;


bool checkpoint_is_live = false;

template <bool firstRun>
struct RuntimeVertex: public poplar::MultiVertex {
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<unsigned> displayBuf;
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

        // Update mapped memory locations in case the tensors have moved between vertices
        *addressOfDisplayBufPtr = (void*) &*displayBuf;
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
        *displayBuf = 0;

        if (inBuf[0] == '\0') {
            *doneFlag = true;
        } else {
            IPUpy_do_str(&inBuf[0], 0);
            *doneFlag = false; // Should also respond to exit calls...?
        }

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
