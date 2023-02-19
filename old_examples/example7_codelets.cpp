#include <poplar/Vertex.hpp>

#include "example7.hpp"

/*  Provided by preprocessor
#include "poplar/StackSizeDefs.hpp" 
#define RECURSIVE_FUNCTION_SIZE (5 * 1024)
extern "C" void IPUpy_deinit(void);
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_init");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_deinit");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_array");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_do_str");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_set_stdout");
extern "C" void IPUpy_init(char *stdout_memory, char *poplar_stack_bottom);
extern "C" void IPUpy_add_memory_as_array(const char* name, void* data, size_t num_elts, char dtype);
extern "C" void IPUpy_do_str(const char *src, int is_single_line);
extern "C" void IPUpy_set_stdout(char* _stdout);
*/
DEF_STACK_USAGE(0, "IPUpy_set_stdin");
extern "C" void IPUpy_set_stdin(char* _stdin);
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "mp_hal_stdout_tx_str");
extern "C" void mp_hal_stdout_tx_str(const char *str);
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "pyexec_event_repl_init");
extern "C" void pyexec_event_repl_init();
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "pyexec_event_repl_process_char");
extern "C" int pyexec_event_repl_process_char(int c);

extern "C" char * IPUpy_stdout_head;

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


class TerminalInit: public poplar::Vertex {
    public:
    // Global tensors
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> inBuf;
    poplar::InOut<poplar::Vector<char>> hostCallBuf;
    poplar::InOut<poplar::Vector<char>> filenameBuf;
    poplar::InOut<int> syscallArg;
    poplar::InOut<int> programSelector;
    poplar::InOut<int> vfsPos;
    poplar::InOut<poplar::Vector<char>> vfsDataBlock;

    bool compute() {
        char* poplar_stack_bottom;
        asm volatile(
            "mov %[poplar_stack_bottom], $m11" 
            : [poplar_stack_bottom] "+r" (poplar_stack_bottom) ::
        );
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_init(poplar_stack_bottom);
        IPUpy_add_memory_as_array("syscallArg", &*syscallArg, 1, 'i');
        IPUpy_add_memory_as_array("hostCallBuf", &hostCallBuf[0], hostCallBuf.size(), 'b');
        IPUpy_add_memory_as_array("filenameBuf", &filenameBuf[0], filenameBuf.size(), 'b');
        IPUpy_add_memory_as_array("programSelector", &*programSelector, 1, 'i');
        IPUpy_add_memory_as_array("vfsPos", &*vfsPos, 1, 'i');
        IPUpy_add_memory_as_array("vfsDataBlock", &vfsDataBlock[0], vfsDataBlock.size(), 'b');
        IPUpy_add_memory_as_array("inBuf", &inBuf[0], inBuf.size(), 'b');
        IPUpy_do_str(R"(

import uos as os

SYSCALL_NONE = 0
SYSCALL_GET_CHAR = 1
SYSCALL_FLUSH_STDOUT = 2
SYSCALL_VFS_READ = 3
SYSCALL_VFS_WRITE = 4
SYSCALL_RUN = 5
SYSCALL_HOST_RUN = 6
SYSCALL_SHUTDOWN = 7

def getchar():
    syscallArg[0] = SYSCALL_GET_CHAR
    longyield
    syscallArg[0] = SYSCALL_NONE
    return chr(inBuf[0])

def launch_program(number):
    programSelector[0] = number
    syscallArg[0] = SYSCALL_RUN
    longyield
    syscallArg[0] = SYSCALL_NONE

def vfs_exchange(block_num, action):
    vfsPos[0] = block_num
    syscallArg[0] = action
    longyield
    syscallArg[0] = SYSCALL_NONE

def host_call(command):
    for i, c in enumerate(command):
        hostCallBuf[i] = ord(c)
    hostCallBuf[len(command)] = 0
    syscallArg[0] = SYSCALL_HOST_RUN
    longyield
    syscallArg[0] = SYSCALL_NONE

def shutdown():
    syscallArg[0] = SYSCALL_SHUTDOWN
    longyield
    syscallArg[0] = SYSCALL_NONE

#from uarray import array
#__diskimg = array('b', range(1024*20))
#
#class TensorBlockDevice:
#    def __init__(self, block_size):
#        self.block_size = block_size
#
#    def readblocks(self, block_num, buf, offset=0):
#        addr = block_num * self.block_size + offset
#        for i in range(len(buf)):
#            buf[i] = __diskimg[addr + i]
#
#    def writeblocks(self, block_num, buf, offset=None):
#        if offset is None:
#            # do erase, then write
#            for i in range(len(buf) // self.block_size):
#                self.ioctl(6, block_num + i)
#            offset = 0
#        addr = block_num * self.block_size + offset
#        for i in range(len(buf)):
#            __diskimg[addr + i] = buf[i]
#
#    def ioctl(self, op, arg):
#        if op == 4: # block count
#            return len(__diskimg) // self.block_size
#        if op == 5: # block size
#            return self.block_size
#        if op == 6: # block erase
#            return 0


class ExchangeBlockDevice:
    def __init__(self, num_blocks):
        self.num_blocks = num_blocks
        self.block_size = len(vfsDataBlock)

    def readblocks(self, block_num, buf, offset=0):        
        readhead = block_num * self.block_size + offset
        offset = readhead % self.block_size
        block_num = readhead // self.block_size
        writehead = 0

        if offset > 0:
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            n = min(self.block_size - offset, len(buf))
            buf[:n] = vfsDataBlock[offset: offset + n]
            writehead = n
            block_num += 1
        
        while writehead + self.block_size <= len(buf):
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            buf[writehead: writehead + self.block_size] = vfsDataBlock[:self.block_size]
            writehead += self.block_size
            block_num += 1

        remainder = len(buf) - writehead
        if remainder > 0:
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            buf[writehead:] = vfsDataBlock[:remainder]
        

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
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            readhead = self.block_size - offset
            vfsDataBlock[offset:] = buf[:readhead]
            vfs_exchange(block_num, SYSCALL_VFS_WRITE)
            block_num += 1
        
        while readhead + self.block_size <= len(buf):
            vfsDataBlock[:self.block_size] = buf[readhead: readhead + self.block_size]
            vfs_exchange(block_num, SYSCALL_VFS_WRITE)
            readhead += self.block_size
            block_num += 1

        remainder = len(buf) - readhead
        if remainder > 0:
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            vfsDataBlock[:remainder] = buf[readhead:]
            vfs_exchange(block_num, SYSCALL_VFS_WRITE)

    def ioctl(self, op, arg):
        if op == 4: # block count
            return self.num_blocks
        if op == 5: # block size
            return self.block_size
        if op == 6: # block erase
            return 0

class IPUpySh:

    TERM_WIDTH = 60
    HISTORY_LEN = 20

    def __init__(self, ):
        self.cmd_table = {
            'ls' : self.do_ls,
            'cd' : self.do_cd,
            'pwd' : self.do_pwd,
            'lshw': self.do_lshw,
            'cat': self.do_cat,
            'mkdir': self.do_mkdir,
            'echo': self.do_echo,
        }
        self.cwd = os.getcwd()
        self.PS1 = '\033[01;32mIPUpysh\033[00m|\033[01;32mjosefd\033[00m:\033[01;34m{cwd}\033[00m$ '
        self.prompt = self.PS1.format(cwd=self.cwd)
        self.history = []
        self.history_idx = 0

    def run(self):
        current_line = []
        prev_ch = None
        while True:
            print('\r' + ' ' * self.TERM_WIDTH + '\r' + self.prompt + ''.join(current_line), end='')

            ch = getchar()
            if ch == '\x1b':
                ch = ch + getchar() + getchar()
                if ch in ('\x1b[A', '\x1b[B') and self.history:
                    self.history_idx += 1 if ch == '\x1b[A' else -1
                    self.history_idx = max(0, min(len(self.history), self.history_idx))
                    if self.history_idx != 0:
                        current_line = [c for c in self.history[-self.history_idx]]
            elif ch == '\r':
                print("\r\n", end='')
                ln = ''.join(current_line)
                self.history.append(ln)
                self.proc_line(ln)
                if len(self.history) > self.HISTORY_LEN:
                    self.history.pop(0)
                current_line = []
                self.history_idx = 0
            elif ch == '\x03':
                if prev_ch == '\x04':
                    shutdown()
                print("^C\r\n", end='')
                current_line = []
                self.history_idx = 0          
            elif ch == '\x7f':
                #print('\r' + ' ' * self.TERM_WIDTH, end='')
                if current_line:
                    current_line.pop()
            else:
                current_line.append(ch)
            
            prev_ch = ch

    def proc_line(self, ln):
        if not ln:
            return
        cmd, *args = ln.split()
        if cmd in self.cmd_table:
            self.cmd_table[cmd](args)
        elif cmd.startswith('./example'):
            self.launch_example(cmd, args)
        else:
            if ln[0] == '!':
                ln = ln[1:]
            host_call(ln)

    def launch_example(self, progname, args):
        try:
            number = int(progname[9:])
            if number == 7:
                print("That's a bad idea")
                return
            if number in (1, 2, 3, 4, 5, 8):
                if args:
                    for i, c in enumerate(args[0]):
                        filenameBuf[i] = ord(c)
                    filenameBuf[len(args[0])] = 0
                launch_program(number)
                return
        except:
            pass
        print("Invalid program name '{}'".format(cmd))

    def do_cd(self, args):
        if len(args) > 1:
            print('-IPUpysh: cd: too many arguments')
            return
        arg = args[0] if len(args) == 1 else ''
        new_cwd = arg if arg[0] in ('/', '~') else self.cwd + '/' + arg

        parts = new_cwd.split('/')
        acc = []
        for part in parts:
            if part == '' or part == '.':
                pass
            elif part == '..' and acc:
                acc.pop()
            else:
                acc.append(part)
        new_cwd = '/' + '/'.join(acc)

        try:
            os.chdir(new_cwd)
        except:
            print("-IPUpysh: cd: {}: No such file or directory".format(new_cwd))
            return
        self.cwd = os.getcwd()
        self.prompt = self.PS1.format(cwd=self.cwd)

    def do_pwd(self, args):
        print(self.cwd)   

    def do_ls(self, args):
        if '-a' in args:
            opt_a = True
            args.remove('-a')
        else:
            opt_a = False
        if not args:
            args = ('.',)
        for arg in args:
            try:
                items = os.listdir(arg)
            except:
                print("ls: cannot access '{}': No such file or directory".format(arg))
                continue
            if not opt_a:
                items = list(filter(lambda x: not x[0] == '.', items))
            if not items:
                continue
            items.sort()
            width = max(len(x) for x in items) + 1
            columns = 80 // width
            rows = (len(items) + columns - 1) // columns
            for r in range(len(items) % rows):
                print(' '.join('{:{}}'.format(x, width) for x in items[r::rows]))
            for r in range(len(items) % rows, rows):
                print(' '.join('{:{}}'.format(x.strip(), width) for x in items[r::rows]))

    def do_lshw(self, args):
        print('IPU!')

    def do_cat(self, args):
        for fname in args:
            try:
                with open(fname, 'r') as f:
                    print(f.read())
            except:
                print('{}: No such file or directory'.format(fname))

    def do_mkdir(self, args):
        for arg in args:
            arg = arg if arg[0] == '/' else self.cwd + '/' + arg
            os.mkdir(arg)

    def do_rmdir(self, args):
        for arg in args:
            arg = arg if arg[0] == '/' else self.cwd + '/' + arg
            os.rmdir(arg)

    def do_echo(self, args):
        if len(args) < 2 or args[-2] not in ('>', '>>'):
            print(' '.join(args))
            return
        fname = args[-1]
        with open(fname, 'w' if args[-2] == '>' else 'a') as f:
            f.write(' '.join(args[:-2]))
        


)", 0);

        *syscallArg = syscall_none;
        *vfsPos = -1; // No filesystem yet
        return true;
    }
};


class TerminalBody: public poplar::MultiVertex {
    public:
    // Global tensors
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> inBuf;
    poplar::InOut<poplar::Vector<char>> hostCallBuf;
    poplar::InOut<poplar::Vector<char>> filenameBuf;
    poplar::InOut<int> syscallArg;
    poplar::InOut<int> programSelector;
    poplar::InOut<int> vfsPos;
    poplar::InOut<poplar::Vector<char>> vfsDataBlock;

    bool compute(unsigned tid) {
        if (tid != 5) {
            return true;
        }
        IPUpy_stdout_head = &printBuf[0];
        *IPUpy_stdout_head = '\0';
        *syscallArg = syscall_none;
    
        // Longyield mechanism
        static bool checkpoint_is_live = false;
        if (checkpoint_is_live) {
            checkpoint_is_live = false;
            longjmp(IPUpy_checkpoint_env, 1);
        }
        checkpoint_is_live = setjmp(IPUpy_exit_env);
        if (checkpoint_is_live) {
            return true;
        }

        // Body code
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_do_str(R"(


if vfsPos[0] == -1:    
    __disk_device = ExchangeBlockDevice(40)
    #__disk_device = TensorBlockDevice(256)
    os.VfsLfs2.mkfs(__disk_device)
    os.mount(__disk_device, '/')
    os.chdir('/')
    #with open('src.cpp', 'w') as f:
    #    f.write('int main(void) {\n  return 0;\n}\n')
        
    shell = IPUpySh()
    
shell.run()

)", 0);

        return true;
    }
};


class VfsReadWrite: public poplar::Vertex {
    public:
    poplar::Input<int> syscallArg;
    poplar::InOut<poplar::Vector<char>> vfsDisk;
    poplar::Input<int> vfsPos;
    poplar::InOut<char> vfsDataBlock;
    
    bool compute() {
        if (*syscallArg == syscall_vfs_read) {
            *vfsDataBlock = vfsDisk[*vfsPos];
        } else if (*syscallArg == syscall_vfs_write) {
            vfsDisk[*vfsPos] = *vfsDataBlock;
        }
        return true;
    }
};


class Example1Body: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<int>> input;
    poplar::InOut<poplar::Vector<char>> printBuf;

    bool compute() { 

        #pragma IPUpy_start [X=input, stdout=printBuf]

        print('\n[{}] -> ['.format(' '.join(str(i) for i in X)), end='')
        output = []
        for i in range(0, len(X) // 3):
            output.append(str(int(any(X[3 * i : 3 * (i + 1)]))))
        print(' '.join(output) + ']\n')


        #pragma IPUpy_end

        return true;
    }
};



class Example2Body: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<float>> X;

    bool compute() {
        
        #pragma IPUpy_start [stdout=printBuf, X=X<float>]
        
        print('\nBeginning ComplexOp...')

        class Worker:
            def __init__(self, X):
                self.X = X
            
            def run(self, *args):
                return self.run_impl(X, *args)

        def run_impl(X, transform, condition):
            return list(
                    filter(
                        condition,
                        [transform(x) for x in X]
                    )
                )

        w = Worker(X)
        w.run_impl = run_impl
        result = w.run(
            lambda x: -x,
            lambda x: x < -5 or x > 3
        )

        print('Result: {}\n'.format(result))

        #pragma IPUpy_end
        return true;
    }
};


class Example3Body: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> inbuf;
    poplar::Output<poplar::Vector<char>> outbuf;

    bool compute() {
        
#pragma IPUpy_start [stdout=outbuf, inbuf=inbuf<char>]

for i, c in enumerate(inbuf):
    if c == 0:
        src = ''.join(chr(x) for x in inbuf[:i])
        break


POPPPY_HEADER = """
// ------------------------ This code was inserted by IPUpyc -------------------------- //
#include <poplar/Vertex.hpp>
#include "poplar/StackSizeDefs.hpp" 
#define RECURSIVE_FUNCTION_SIZE (5 * 1024)
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_init");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_deinit");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_add_memory_as_array");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_do_str");
DEF_STACK_USAGE(RECURSIVE_FUNCTION_SIZE, "IPUpy_set_stdout");
extern "C" void IPUpy_init(char *poplar_stack_bottom);
extern "C" void IPUpy_deinit(void);
extern "C" void IPUpy_add_memory_as_array(const char* name, void* data, size_t num_elts, char dtype);
extern "C" void IPUpy_do_str(const char *src, int is_single_line);
extern "C" void IPUpy_set_stdout(char* _stdout, int len);
// ------------------------ This code was inserted by IPUpyc -------------------------- //
"""
IPUpy_START_LINES = """
{indent}// ------------------------ This code was generated by IPUpyc -------------------------- //
{indent}char* poplar_stack_bottom;
{indent}asm volatile("mov %[poplar_stack_bottom], $m11" : [poplar_stack_bottom] "+r" (poplar_stack_bottom) ::);
{indent}IPUpy_set_stdout({set_stdout_args});
{indent}IPUpy_init(poplar_stack_bottom);
"""
IPUpy_VARIABLE_LINE = """{indent}IPUpy_add_memory_as_array("{pyname}", &{cppname}[0], {cppname}.size(), {dtype});"""
IPUpy_PAYLOAD_START = """
{indent}IPUpy_do_str(\""""
IPUpy_PAYLOAD_END = """", 0);
{indent}IPUpy_deinit();
{indent}// ------------------------- This code was generated by IPUpyc ------------------------ //
"""


def parse_varname(varname):
    pyname, cppname = varname.split("=") if "=" in varname else (varname, varname)
    pyname, cppname = pyname.strip(), cppname.strip()
    if cppname.endswith("<float>"):
        cppname, dtype = cppname[:-7].strip(), ord("f")
    elif cppname.endswith("<int>"):
        cppname, dtype = cppname[:-5].strip(), ord("i")
    elif cppname.endswith("<char>"):
        cppname, dtype = cppname[:-6].strip(), ord("b")
    else:
        dtype = ord("i")
    return pyname, cppname, dtype

def preprocess_block(lines):
    header, python_lines = lines[0], lines[1:]
    indent = header[:header.find('#pragma')]
    var_lines = []
    set_stdout_args = "NULL, 0"
    var_list = ' '.join(header.split()[2:])
    assert(var_list[0] == '[' and var_list[-1] == ']')
    for item in var_list[1:-1].split(','):
        pyname, cppname, dtype = parse_varname(item)
        if pyname == 'stdout':
            set_stdout_args = '&{cppname}[0], {cppname}.size()'.format(cppname=cppname)
            continue
        var_lines.append(IPUpy_VARIABLE_LINE.format(
            indent=indent, pyname=pyname, cppname=cppname, dtype=dtype))

    print(IPUpy_START_LINES.format(indent=indent, set_stdout_args=set_stdout_args))
    for ln in var_lines:
        print(ln)
    print(IPUpy_PAYLOAD_START.format(indent=indent), end='')
    for line in filter(lambda ln: ln.strip(), python_lines):
        assert(line.startswith(indent))
        line = line.replace('\\', '\\\\').replace('"', '\\\"')
        print(line[len(indent):] + '\\n', end="")
    print(IPUpy_PAYLOAD_END.format(indent=indent))


current_block = []
print(POPPPY_HEADER)

for line in src.splitlines():
    line_start = ' '.join(line.split()[:2])

    # Not already in a IPUpy block
    if not current_block:
        # Enter a new block
        if line_start == '#pragma IPUpy_start':
            current_block.append(line)
        elif line_start == '#pragma IPUpy_end':
            raise RuntimeError("Found python block end outside a python block")
        else:
            print(line)
        continue

    # We're in a block, is this the end?
    if line_start == '#pragma IPUpy_end':
        preprocess_block(current_block)
        current_block = []
        continue

    # Stay in the block
    current_block.append(line)

if current_block:
    raise RuntimeError("End of file found inside python block")


#pragma IPUpy_end
        return true;
    }
};



class Example4Init: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> diskImg;
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> inBuf;
    poplar::InOut<bool> doneFlag;

    bool compute() {
        char* poplar_stack_bottom;
        asm volatile(
            "mov %[poplar_stack_bottom], $m11" 
            : [poplar_stack_bottom] "+r" (poplar_stack_bottom) ::
        );
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_init(poplar_stack_bottom);
        IPUpy_add_memory_as_array("__diskimg", &diskImg[0], diskImg.size(), 'b');
        IPUpy_do_str(R"(
import uos as os

class TensorBlockDevice:
    def __init__(self, block_size):
        self.block_size = block_size

    def readblocks(self, block_num, buf, offset=0):
        addr = block_num * self.block_size + offset
        for i in range(len(buf)):
            buf[i] = __diskimg[addr + i]

    def writeblocks(self, block_num, buf, offset=None):
        if offset is None:
            # do erase, then write
            for i in range(len(buf) // self.block_size):
                self.ioctl(6, block_num + i)
            offset = 0
        addr = block_num * self.block_size + offset
        for i in range(len(buf)):
            __diskimg[addr + i] = buf[i]

    def ioctl(self, op, arg):
        if op == 4: # block count
            return len(__diskimg) // self.block_size
        if op == 5: # block size
            return self.block_size
        if op == 6: # block erase
            return 0

__disk_device = TensorBlockDevice(block_size=512)
#os.VfsLfs2.mkfs(__disk_device)
os.mount(__disk_device, '/')

)", 0);

        *doneFlag = false;
        pyexec_event_repl_init();
    
        return true;
    }
};



class Example4Body: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> diskImg;
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> inBuf;
    poplar::InOut<bool> doneFlag;

    bool compute() {
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        char c = inBuf[0];
        *doneFlag = (c == '\0') || pyexec_event_repl_process_char(c);
        if (*doneFlag) {
            mp_hal_stdout_tx_str("REPL Finished - Goodbye \e[01;31m✿\e[0m\n\n");
            IPUpy_do_str("os.umount('/')\n", 0);
        }
        return true;
    }
};



class Example5Init: public poplar::MultiVertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> msgBuf;
    poplar::InOut<bool> doneFlag;

    bool compute(unsigned tid) {
        if (tid != 5) return true;
        
        char* poplar_stack_bottom;
        asm volatile(
            "mov %[poplar_stack_bottom], $m11" 
            : [poplar_stack_bottom] "+r" (poplar_stack_bottom) ::
        );
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_init(poplar_stack_bottom);
        IPUpy_add_memory_as_array("msgBuf", &msgBuf[0], msgBuf.size(), 'b');

        *doneFlag = false;
        return true;
    }
};



class Example5Body: public poplar::MultiVertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> msgBuf;
    poplar::InOut<bool> doneFlag;

    bool compute(unsigned tid) {
        if (tid != 5) return true;

        static bool checkpoint_is_live = false;
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


def main():
    message = ""
    while message != "STOP":
        longyield
        message = "".join(chr(byte) for byte in msgBuf)
        print("Received:", message)

if __name__ == "__main__":
    main()


)", 0);

        *doneFlag = true;
        return true;
    }
};


class Example6Body: public poplar::MultiVertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> msgBuf;
    poplar::InOut<bool> doneFlag;

    bool compute(unsigned tid) {
        if (tid != 5) return true;

        static bool checkpoint_is_live = false;
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


def main():
    message = ""
    while message != "STOP":
        longyield
        message = "".join(chr(byte) for byte in msgBuf)
        print("Received:", message)

if __name__ == "__main__":
    main()


)", 0);

        *doneFlag = true;
        return true;
    }
};



class Example8Init: public poplar::Vertex {
    public:
    // Global tensors
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> filenameBuf;
    poplar::InOut<int> syscallArg;
    poplar::InOut<int> vfsPos;
    poplar::InOut<poplar::Vector<char>> vfsDataBlock;

    bool compute() {
        char* poplar_stack_bottom;
        asm volatile(
            "mov %[poplar_stack_bottom], $m11" 
            : [poplar_stack_bottom] "+r" (poplar_stack_bottom) ::
        );
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        IPUpy_init(poplar_stack_bottom);
        IPUpy_add_memory_as_array("syscallArg", &*syscallArg, 1, 'i');
        IPUpy_add_memory_as_array("filenameBuf", &filenameBuf[0], filenameBuf.size(), 'b');
        IPUpy_add_memory_as_array("vfsPos", &*vfsPos, 1, 'i');
        IPUpy_add_memory_as_array("vfsDataBlock", &vfsDataBlock[0], vfsDataBlock.size(), 'b');
        
        IPUpy_do_str(R"(

import uos as os

SYSCALL_NONE = 0
SYSCALL_GET_CHAR = 1
SYSCALL_FLUSH_STDOUT = 2
SYSCALL_VFS_READ = 3
SYSCALL_VFS_WRITE = 4
SYSCALL_RUN = 5
SYSCALL_HOST_RUN = 6
SYSCALL_SHUTDOWN = 7

def vfs_exchange(block_num, action):
    vfsPos[0] = block_num
    syscallArg[0] = action
    longyield
    syscallArg[0] = SYSCALL_NONE


#from uarray import array
#__diskimg = array('b', range(1024*20))
#
#class TensorBlockDevice:
#    def __init__(self, block_size):
#        self.block_size = block_size
#
#    def readblocks(self, block_num, buf, offset=0):
#        addr = block_num * self.block_size + offset
#        for i in range(len(buf)):
#            buf[i] = __diskimg[addr + i]
#
#    def writeblocks(self, block_num, buf, offset=None):
#        if offset is None:
#            # do erase, then write
#            for i in range(len(buf) // self.block_size):
#                self.ioctl(6, block_num + i)
#            offset = 0
#        addr = block_num * self.block_size + offset
#        for i in range(len(buf)):
#            __diskimg[addr + i] = buf[i]
#
#    def ioctl(self, op, arg):
#        if op == 4: # block count
#            return len(__diskimg) // self.block_size
#        if op == 5: # block size
#            return self.block_size
#        if op == 6: # block erase
#            return 0


class ExchangeBlockDevice:
    def __init__(self, num_blocks):
        self.num_blocks = num_blocks
        self.block_size = len(vfsDataBlock)

    def readblocks(self, block_num, buf, offset=0):        
        readhead = block_num * self.block_size + offset
        offset = readhead % self.block_size
        block_num = readhead // self.block_size
        writehead = 0

        if offset > 0:
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            n = min(self.block_size - offset, len(buf))
            buf[:n] = vfsDataBlock[offset: offset + n]
            writehead = n
            block_num += 1
        
        while writehead + self.block_size <= len(buf):
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            buf[writehead: writehead + self.block_size] = vfsDataBlock[:self.block_size]
            writehead += self.block_size
            block_num += 1

        remainder = len(buf) - writehead
        if remainder > 0:
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            buf[writehead:] = vfsDataBlock[:remainder]
        

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
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            readhead = self.block_size - offset
            vfsDataBlock[offset:] = buf[:readhead]
            vfs_exchange(block_num, SYSCALL_VFS_WRITE)
            block_num += 1
        
        while readhead + self.block_size <= len(buf):
            vfsDataBlock[:self.block_size] = buf[readhead: readhead + self.block_size]
            vfs_exchange(block_num, SYSCALL_VFS_WRITE)
            readhead += self.block_size
            block_num += 1

        remainder = len(buf) - readhead
        if remainder > 0:
            vfs_exchange(block_num, SYSCALL_VFS_READ)
            vfsDataBlock[:remainder] = buf[readhead:]
            vfs_exchange(block_num, SYSCALL_VFS_WRITE)

    def ioctl(self, op, arg):
        if op == 4: # block count
            return self.num_blocks
        if op == 5: # block size
            return self.block_size
        if op == 6: # block erase
            return 0

)", 0);

        *syscallArg = syscall_none;
        *vfsPos = -1; 
        return true;
    }
};


class Example8Body: public poplar::MultiVertex {
    public:
    // Global tensors
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<char>> filenameBuf;
    poplar::InOut<int> syscallArg;
    poplar::InOut<int> vfsPos;
    poplar::InOut<poplar::Vector<char>> vfsDataBlock;

    bool compute(unsigned tid) {
        if (tid != 5) {
            return true;
        }
        IPUpy_stdout_head = &printBuf[0];
        *IPUpy_stdout_head = '\0';
        *syscallArg = syscall_none;
    
        // Longyield mechanism
        static bool checkpoint_is_live = false;
        if (checkpoint_is_live) {
            checkpoint_is_live = false;
            longjmp(IPUpy_checkpoint_env, 1);
        }
        checkpoint_is_live = setjmp(IPUpy_exit_env);
        if (checkpoint_is_live) {
            return true;
        }

        // Body code
        IPUpy_set_stdout(&printBuf[0], printBuf.size());
        static char pythonSrcStr[2000];
        IPUpy_add_memory_as_array("pythonSrcStr", &pythonSrcStr[0], 2000, 'b');
        IPUpy_do_str(R"(

__disk_device = ExchangeBlockDevice(40)
#__disk_device = TensorBlockDevice(256)
#os.VfsLfs2.mkfs(__disk_device)
os.mount(__disk_device, '/')


for i, c in enumerate(filenameBuf):
    if c == 0:
        filename = ''.join(chr(x) for x in filenameBuf[:i])
        break
else:
    filename = "filenameBuf overflow?"

with open(filename, 'r') as f:
    src = f.read()

for i, c in enumerate(src):
    pythonSrcStr[i] = ord(c)
pythonSrcStr[len(src)] = 0

os.umount('/')

)", 0);

        IPUpy_do_str(pythonSrcStr, 0);
        IPUpy_deinit();
        *syscallArg = syscall_shutdown;
        return true;
    }
};
