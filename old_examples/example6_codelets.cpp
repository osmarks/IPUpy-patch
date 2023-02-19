#include <poplar/Vertex.hpp>

/*  Provided by preprocessor
#include "poplar/StackSizeDefs.hpp" 
#define RECURSIVE_FUNCTION_SIZE (5 * 1024)
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_init");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_set_stdout");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_deinit");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_array");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_do_str");
extern "C" void IPUpy_init(char *stdout_memory, char *poplar_stack_bottom);
extern "C" void IPUpy_deinit(void);
extern "C" void IPUpy_add_memory_as_array(const char* name, void* data, size_t num_elts, char dtype);
extern "C" void IPUpy_do_str(const char *src, int is_single_line);
extern "C" void IPUpy_set_stdout(char* _stdout);
*/

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


class InitVertex: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<bool> doneFlag;
    poplar::InOut<int> rwFlag;
    poplar::InOut<poplar::Vector<char>> dataBlock;
    poplar::InOut<int> diskPos;

    bool compute() {        
        char* poplar_stack_bottom;
        asm volatile(
            "mov %[poplar_stack_bottom], $m11" 
            : [poplar_stack_bottom] "+r" (poplar_stack_bottom) ::
        );
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_init(poplar_stack_bottom);
        IPUpy_add_memory_as_array("dataBlock", &dataBlock[0], dataBlock.size(), 'b');
        IPUpy_add_memory_as_array("rwFlag", &*rwFlag, 1, 'i');
        IPUpy_add_memory_as_array("diskPos", &*diskPos, 1, 'i');

        *doneFlag = doneFlag;
        *rwFlag = 0;
        return true;
    }
};

bool checkpoint_is_live = false;

class BodyVertex: public poplar::MultiVertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<bool> doneFlag;
    poplar::InOut<int> rwFlag;
    poplar::InOut<poplar::Vector<char>> dataBlock;
    poplar::InOut<int> diskPos;

    bool compute(unsigned tid) {
        if (tid != 5) return true;

        if (checkpoint_is_live) {
            checkpoint_is_live = false;
            longjmp(IPUpy_checkpoint_env, 1);
        }
        checkpoint_is_live = setjmp(IPUpy_exit_env);
        if (checkpoint_is_live) {
            return true;
        }
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_do_str(R"(
            
READ = 1
WRITE = -1

def __exchangeblock(block_num, action):
    diskPos[0] = block_num
    rwFlag[0] = action
    longyield


class ExchangeBlockDevice:
    def __init__(self, num_blocks):
        self.num_blocks = num_blocks
        self.block_size = len(dataBlock)

    def readblocks(self, block_num, buf, offset=0):        
        readhead = block_num * self.block_size + offset
        offset = readhead % self.block_size
        block_num = readhead // self.block_size
        writehead = 0

        if offset > 0:
            __exchangeblock(block_num, READ)
            n = min(self.block_size - offset, len(buf))
            buf[:n] = dataBlock[offset: offset + n]
            writehead = n
            block_num += 1
        
        while writehead + self.block_size <= len(buf):
            __exchangeblock(block_num, READ)
            buf[writehead: writehead + self.block_size] = dataBlock[:self.block_size]
            writehead += self.block_size
            block_num += 1

        remainder = len(buf) - writehead
        if remainder > 0:
            __exchangeblock(block_num, READ)
            buf[writehead:] = dataBlock[:remainder]
        

    def writeblocks(self, block_num, buf, offset=None):
        if offset is None:
            # do erase, then write
            for i in range(len(buf) // self.block_size):
                self.ioctl(6, block_num + i)
            offset = 0

        addr = block_num * self.block_size + offset
        offset = addr % self.block_size
        block_num = addr // self.block_size
        readhead = 0

        if offset > 0:
            __exchangeblock(block_num, READ)
            readhead = self.block_size - offset
            dataBlock[offset:] = buf[:readhead]
            __exchangeblock(block_num, WRITE)
            block_num += 1
        
        while readhead + self.block_size <= len(buf):
            dataBlock[:self.block_size] = buf[readhead: readhead + self.block_size]
            __exchangeblock(block_num, WRITE)
            readhead += self.block_size
            block_num += 1

        remainder = len(buf) - readhead
        if remainder > 0:
            __exchangeblock(block_num, READ)
            dataBlock[:remainder] = buf[readhead:]
            __exchangeblock(block_num, WRITE)

    def ioctl(self, op, arg):
        if op == 4: # block count
            return self.num_blocks
        if op == 5: # block size
            return self.block_size
        if op == 6: # block erase
            return 0


import uos as os

__disk_device = ExchangeBlockDevice(40)
os.VfsLfs2.mkfs(__disk_device)
os.mount(__disk_device, '/')

with open('f.txt', 'w') as f:
    x = f.write('Some data')

os.rename('f.txt', 'longfilename.txt')

with open('longfilename.txt', 'r') as f:
    print(f.read())


)", 0);

        *doneFlag = true;
        return true;
    }
};


class ReadWriteVertex: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> disk;
    poplar::InOut<char> value;
    poplar::Input<int> diskPos;
    poplar::Input<int> rwFlag;
    
    bool compute() {

        if (*rwFlag == 1) {
            // Read
            *value = disk[*diskPos];
        } else if (*rwFlag == -1) {
            // Write
            disk[*diskPos] = *value;
        }
        return true;
    }
};
