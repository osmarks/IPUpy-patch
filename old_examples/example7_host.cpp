#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <poplar/IPUModel.hpp>
#include <poplar/Program.hpp>

#include "example7.hpp"

using namespace poplar;

Device getIPU(bool use_hardware = true, int num_ipus = 1);

// ----------------------- Globals ----------------------- //
const unsigned printBufSize = 2048;
const unsigned inBufSize = 1;
const unsigned hostCallBufSize = 256;
const unsigned filenameBufSize = 256;
const int vfsBlockSize = 1024;
const int vfsNumBlocks = 40;
Tensor printBuf;
Tensor inBuf;
Tensor constFalse;
Tensor constTrue;
Tensor shutdownFlag;
Tensor syscallArg;
Tensor programSelector;
Tensor hostCallBuf;
Tensor filenameBuf;
Tensor vfsPos;
Tensor vfsDataBlock;
Tensor vfsDisk;
DataStream printBufStream;
DataStream inBufStream;
DataStream hostCallBufStream;
DataStream filenameBufStream;
ComputeSet vfsCS;
char printBuf_h[printBufSize];
char inBuf_h[inBufSize];
char hostCallBuf_h[hostCallBufSize];
char filenameBuf_h[filenameBufSize];

void createGlobalVars(Graph& graph) {
  const int tile = 0;

  bool falseValue = false;
  bool trueValue = true;
  constFalse = graph.addConstant(BOOL, {}, &falseValue, "constFalse");
  constTrue = graph.addConstant(BOOL, {}, &trueValue, "constTrue");
  printBuf = graph.addVariable(CHAR, {printBufSize}, "printBuf");
  inBuf = graph.addVariable(CHAR, {inBufSize}, "inBuf");
  hostCallBuf = graph.addVariable(CHAR, {hostCallBufSize}, "hostCallBuf");
  filenameBuf = graph.addVariable(CHAR, {filenameBufSize}, "filenameBuf");
  shutdownFlag = graph.addVariable(BOOL, {}, "shutdownFlag");
  syscallArg = graph.addVariable(INT, {}, "syscallArg");
  programSelector = graph.addVariable(INT, {}, "programSelector");
  vfsPos = graph.addVariable(INT, {}, "vfsPos");
  vfsDataBlock = graph.addVariable(CHAR, {vfsBlockSize}, "vfsDataBlock");
  vfsDisk = graph.addVariable(CHAR, {vfsBlockSize * vfsNumBlocks}, "vfsDisk");

  graph.setTileMapping(printBuf, tile);
  graph.setTileMapping(inBuf, tile);
  graph.setTileMapping(hostCallBuf, tile);
  graph.setTileMapping(filenameBuf, tile);
  graph.setTileMapping(constFalse, tile);
  graph.setTileMapping(constTrue, tile);
  graph.setTileMapping(shutdownFlag, tile);
  graph.setTileMapping(syscallArg, tile);
  graph.setTileMapping(programSelector, tile);
  graph.setTileMapping(vfsPos, tile);
  graph.setTileMapping(vfsDataBlock, tile);

  printBufStream = graph.addDeviceToHostFIFO("printBuf-stream", poplar::CHAR, printBufSize);
  inBufStream = graph.addHostToDeviceFIFO("inBuf-stream", poplar::CHAR, inBufSize);
  hostCallBufStream = graph.addDeviceToHostFIFO("hostCallBuf-stream", poplar::CHAR, hostCallBufSize);
  filenameBufStream = graph.addDeviceToHostFIFO("filenameBuf-stream", poplar::CHAR, filenameBufSize);

  // Compute sets for VFS
  int diskTileStart = 10;
  vfsCS = graph.addComputeSet("VfsCS");
  for (int i = 0; i < vfsBlockSize; ++i) {
    auto diskSlice = vfsDisk.slice(i * vfsNumBlocks, (i + 1) * vfsNumBlocks);
    VertexRef readwriteVtx = graph.addVertex(vfsCS, "VfsReadWrite", {
      {"vfsDisk", diskSlice}, {"vfsDataBlock", vfsDataBlock[i]}, {"vfsPos", vfsPos}, {"syscallArg", syscallArg}
    });
    graph.setTileMapping(diskSlice, diskTileStart + i);
    graph.setTileMapping(readwriteVtx, diskTileStart + i);
  }
}

void initGlobalVars(Engine& engine) {
  engine.connectStream("filenameBuf-stream", filenameBuf_h);

  engine.connectStreamToCallback("printBuf-stream", [](void* p){
    printf("%.*s", printBufSize, (char*)p);
  });
  engine.connectStreamToCallback("inBuf-stream", [](void* p){
    *((char*)p) = getchar();
  });
  engine.connectStreamToCallback("hostCallBuf-stream", [](void* p) {
    char tmp[hostCallBufSize];
    strncpy(tmp, (char*)p, hostCallBufSize);
    system(tmp);
  });
}


// ----------------------- Example1 ----------------------- //

const unsigned ex1_N = 15;
int ex1Input_h[ex1_N] = {1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0};

program::Sequence createExample1Prog(Graph& graph) {
  int tile = 1;
  
  Tensor input = graph.addVariable(INT, {ex1_N}, "ex1Input");
  DataStream inputStream = graph.addHostToDeviceFIFO("ex1Input-stream", poplar::INT, ex1_N);
  graph.setTileMapping(input, tile);
  
  ComputeSet computeset = graph.addComputeSet("Ex1CS");
  VertexRef vtx = graph.addVertex(computeset, "Example1Body", {
    {"input", input}, {"printBuf", printBuf},
  });
  graph.setTileMapping(vtx, tile);

  program::Sequence program({
    program::Copy(inputStream, input),
    program::Execute(computeset),
    program::Copy(printBuf, printBufStream),
  });
  return program;
}

void initExample1Vars(Engine& engine) {
  engine.connectStream("ex1Input-stream", ex1Input_h);
}


// ----------------------- Example2 ----------------------- //

const unsigned ex2_N = 10;
float ex2Input_h[ex2_N] = {-4., 0., 0.1, 9., 10., -2., -7., 1.5, -1., 0.};

program::Sequence createExample2Prog(Graph& graph) {
  int tile = 2;
  
  Tensor input = graph.addVariable(FLOAT, {ex2_N}, "ex2Input");
  DataStream inputStream = graph.addHostToDeviceFIFO("ex2Input-stream", poplar::FLOAT, ex2_N);
  graph.setTileMapping(input, tile);
  
  ComputeSet computeset = graph.addComputeSet("Ex2CS");
  VertexRef vtx = graph.addVertex(computeset, "Example2Body", {
    {"X", input}, {"printBuf", printBuf},
  });
  graph.setTileMapping(vtx, tile);

  program::Sequence program({
    program::Copy(inputStream, input),
    program::Execute(computeset),
    program::Copy(printBuf, printBufStream),
  });
  return program;
}

void initExample2Vars(Engine& engine) {
  engine.connectStream("ex2Input-stream", ex2Input_h);
}

// ----------------------- Example3 ----------------------- //


const unsigned ex3_N = 20000;
char ex3Buf_h[ex3_N];
program::Sequence createExample3Prog(Graph& graph) {
  int tile = 3;
  
  Tensor input = graph.addVariable(CHAR, {ex3_N}, "ex3Input");
  Tensor output = graph.addVariable(CHAR, {ex3_N}, "ex3Output");
  DataStream inputStream = graph.addHostToDeviceFIFO("ex3Input-stream", CHAR, ex3_N);
  DataStream outputStream = graph.addDeviceToHostFIFO("ex3Output-stream", CHAR, ex3_N);
  graph.setTileMapping(input, tile);
  graph.setTileMapping(output, tile);

  ComputeSet computeset = graph.addComputeSet("Ex3CS");
  VertexRef vtx = graph.addVertex(computeset, "Example3Body", {
    {"inbuf", input}, {"outbuf", output},
  });
  graph.setTileMapping(vtx, tile);
  
  program::Sequence program({
    program::Copy(filenameBuf, filenameBufStream),
    program::Sync(poplar::SyncType::GLOBAL),
    program::Copy(inputStream, input),
    program::Execute(computeset),
    program::Copy(output, outputStream),
  });
  return program;
}

void readfile(const char* fname, char* buf, int maxSize) {
    std::ifstream file(fname, std::ios::ate);
    std::streamsize fileSize = file.tellg();
    if (fileSize > maxSize) {
      std::cout << "File is too big for buffer: " << fname << std::endl;
      buf[0] = '\0';
      return;
    }
    file.seekg(0, std::ios::beg);
    if (!file.read(buf, fileSize)) {
      std::cout << "Failed to read file " << fname << std::endl;
      buf[0] = '\0';
      return;
    }
    buf[fileSize] = '\0';
}

void initExample3Vars(Engine& engine) {
  engine.connectStream("ex3Input-stream", ex3Buf_h);
  engine.connectStream("ex3Output-stream", ex3Buf_h);
  engine.connectStreamToCallback("ex3Input-stream", [](void* p){
    readfile(filenameBuf_h, (char*)p, ex3_N);
  });
  engine.connectStreamToCallback("ex3Output-stream", [](void* p){
    printf("%.*s", ex3_N, (char*)p);
  });
}

// ----------------------- Example4 ----------------------- //


const unsigned ex4DiskSize = 256 * 64;
char ex4Disk_h[ex4DiskSize];
program::Sequence createExample4Prog(Graph& graph) {
  int tile = 4;

  Tensor disk = graph.addVariable(CHAR, {ex4DiskSize}, "ex4Disk");
  Tensor doneFlag = graph.addVariable(BOOL, {}, "ex4DoneFlag");
  DataStream diskInStream = graph.addHostToDeviceFIFO("ex4DiskImg-in-stream", poplar::CHAR, ex4DiskSize);
  DataStream diskOutStream = graph.addDeviceToHostFIFO("ex4DiskImg-out-stream", poplar::CHAR, ex4DiskSize);
  graph.setTileMapping(disk, tile);
  graph.setTileMapping(doneFlag, tile);

  ComputeSet initCS = graph.addComputeSet("Ex4InitCS");
  ComputeSet bodyCS = graph.addComputeSet("Ex4BodyCS");
  VertexRef intVtx = graph.addVertex(initCS, "Example4Init", {
    {"printBuf", printBuf}, {"inBuf", inBuf}, {"doneFlag", doneFlag}, {"diskImg", disk}});
  VertexRef bodyVtx = graph.addVertex(bodyCS, "Example4Body", {
    {"printBuf", printBuf}, {"inBuf", inBuf}, {"doneFlag", doneFlag}, {"diskImg", disk}});
  graph.setTileMapping(intVtx, tile);
  graph.setTileMapping(bodyVtx, tile);

  program::Sequence program({
    program::Copy(diskInStream, disk),
    program::Execute(initCS),
    program::Copy(printBuf, printBufStream),
    program::Copy(constFalse, doneFlag),
    program::RepeatWhileFalse(program::Sequence(), doneFlag, program::Sequence({
      program::Copy(inBufStream, inBuf),
      program::Execute(bodyCS),
      program::Copy(printBuf, printBufStream)
    })),
    program::Copy(disk, diskOutStream),
  });

  return program;
}


void initExample4Vars(Engine& engine) {
  engine.connectStream("ex4DiskImg-in-stream", ex4Disk_h);
  engine.connectStream("ex4DiskImg-out-stream", ex4Disk_h);
  engine.connectStreamToCallback("ex4DiskImg-in-stream", [](void* p) {
    readfile("disk.img", (char*)p, ex4DiskSize);
  });
  engine.connectStreamToCallback("ex4DiskImg-out-stream", [](void* p) {
    std::ofstream outfile("disk.img", std::ios::out);
    if (!outfile.write((char*)p, ex4DiskSize)) {
      std::cout << "Failed to write file disk.img\n";
      return;
    }
    outfile.close();
  });
}


// ----------------------- Example5 ----------------------- //


const unsigned ex5MsgSize = 4;
const unsigned ex5NumMsgs = 6;
char ex5MsgBuf_h[ex5MsgSize * ex5NumMsgs] = {"DATA""SENT""FROM""HOST""STOP""\0\0\0"};
program::Sequence createExample5Prog(Graph& graph) {
  int tile = 5;

  Tensor msgBuf = graph.addVariable(CHAR, {ex5MsgSize}, "ex5MsgBuf");
  Tensor doneFlag = graph.addVariable(BOOL, {}, "ex5DoneFlag");
  DataStream msgBufStream = graph.addHostToDeviceFIFO("ex5MsgBuf-stream", poplar::CHAR, ex5MsgSize);
  ComputeSet initCS = graph.addComputeSet("ex5InitCS");
  ComputeSet bodyCS = graph.addComputeSet("ex5BodyCS");
  VertexRef intVtx = graph.addVertex(initCS, "Example5Init", {
    {"printBuf", printBuf}, {"msgBuf", msgBuf}, {"doneFlag", doneFlag}});
  VertexRef bodyVtx = graph.addVertex(bodyCS, "Example5Body", {
    {"printBuf", printBuf}, {"msgBuf", msgBuf}, {"doneFlag", doneFlag}});
  graph.setTileMapping(msgBuf, tile);
  graph.setTileMapping(doneFlag, tile);
  graph.setTileMapping(intVtx, tile);
  graph.setTileMapping(bodyVtx, tile);

  program::Sequence program({
    program::Execute(initCS),
    program::Copy(printBuf, printBufStream),
    program::Copy(constFalse, doneFlag),
    program::RepeatWhileFalse(program::Sequence(), doneFlag, program::Sequence({
      program::Execute(bodyCS),
      program::Copy(msgBufStream, msgBuf),
    })),
    program::Copy(printBuf, printBufStream)
  });

  return program;
}

void initExample5Vars(Engine& engine) {
  engine.connectStream("ex5MsgBuf-stream", ex5MsgBuf_h, &ex5MsgBuf_h[ex5MsgSize * ex5NumMsgs]);
}

// ----------------------- Example8 ----------------------- //


program::Sequence createExample8Prog(Graph& graph) {
  int tile = 8;

  Tensor doneFlag = graph.addVariable(BOOL, {}, "ex8DoneFlag");
  ComputeSet initCS = graph.addComputeSet("ex8InitCS");
  ComputeSet bodyCS = graph.addComputeSet("ex8BodyCS");
  VertexRef intVtx = graph.addVertex(initCS, "Example8Init", {
    {"printBuf", printBuf}, {"syscallArg", syscallArg}, {"filenameBuf", filenameBuf}, 
    {"vfsPos", vfsPos}, {"vfsDataBlock", vfsDataBlock}
  });
  VertexRef bodyVtx = graph.addVertex(bodyCS, "Example8Body", {
    {"printBuf", printBuf}, {"syscallArg", syscallArg}, {"filenameBuf", filenameBuf}, 
    {"vfsPos", vfsPos}, {"vfsDataBlock", vfsDataBlock}
  });
  graph.setTileMapping(doneFlag, tile);
  graph.setTileMapping(intVtx, tile);
  graph.setTileMapping(bodyVtx, tile);

  program::Sequence program({
    program::Execute(initCS),
    program::Copy(printBuf, printBufStream),
    program::Copy(constFalse, doneFlag),
    program::RepeatWhileFalse(program::Sequence(), doneFlag, program::Sequence({
      program::Execute(bodyCS),
      program::Switch(syscallArg, {
        {syscall_vfs_read, program::Execute(vfsCS)},
        {syscall_vfs_write, program::Execute(vfsCS)},
        {syscall_shutdown, program::Copy(constTrue, doneFlag)},
      })
    })),
    program::Copy(printBuf, printBufStream)
  });

  return program;
}

void initExample8Vars(Engine& engine) {
  (void) engine;
}

// ------------------------- Main ------------------------- //


int main() {

  Device device = getIPU(true);
  Graph graph(device.getTarget());


  // Add computation vertext to IPU
  graph.addCodelets("example7_codelets.gp");
  createGlobalVars(graph);
  
  // Create compute sets for terminal
  int tile = 0;
  ComputeSet termInitCS = graph.addComputeSet("TerminalInitCS");
  ComputeSet termBodyCS = graph.addComputeSet("TerminalBodyCS");
  VertexRef initVtx = graph.addVertex(termInitCS, "TerminalInit", {
    {"printBuf", printBuf}, {"inBuf", inBuf}, {"syscallArg", syscallArg}, 
    {"hostCallBuf", hostCallBuf}, {"filenameBuf", filenameBuf}, 
    {"programSelector", programSelector}, {"vfsPos", vfsPos}, {"vfsDataBlock", vfsDataBlock}, 
  });
  VertexRef bodyVtx = graph.addVertex(termBodyCS, "TerminalBody", {
    {"printBuf", printBuf}, {"inBuf", inBuf}, {"syscallArg", syscallArg}, 
    {"hostCallBuf", hostCallBuf}, {"filenameBuf", filenameBuf}, 
    {"programSelector", programSelector}, {"vfsPos", vfsPos}, {"vfsDataBlock", vfsDataBlock}, 
  });
  graph.setTileMapping(initVtx, tile);
  graph.setTileMapping(bodyVtx, tile);

  
  // Create main program loop
  program::Sequence mainProgram;
  {
    using namespace poplar::program;

    mainProgram = Sequence({
      Execute(termInitCS),
      Copy(printBuf, printBufStream),
      Copy(constFalse, shutdownFlag),
      RepeatWhileFalse(Sequence(), shutdownFlag, Sequence({
        Execute(termBodyCS),
        Copy(printBuf, printBufStream),
        Switch(syscallArg, {
          {syscall_none, Sequence()},
          {syscall_get_char, Copy(inBufStream, inBuf)},
          {syscall_flush_stdout, ErrorProgram("syscall_flush_stdout not implemented", constFalse)},
          {syscall_vfs_read, Execute(vfsCS)},
          {syscall_vfs_write, Execute(vfsCS)},
          {syscall_shutdown, Copy(constTrue, shutdownFlag)},
          {syscall_host_run, Copy(hostCallBuf, hostCallBufStream)},
          {syscall_run, Switch(programSelector, {
                    {0, Sequence()},
                    {1, createExample1Prog(graph)},
                    {2, createExample2Prog(graph)},
                    {3, createExample3Prog(graph)},
                    {4, createExample4Prog(graph)},
                    {5, createExample5Prog(graph)},
                    {8, createExample8Prog(graph)},
              })},
        }),
      })),
    });
  }
  
  // Create session
  Engine engine(graph, mainProgram);
  initGlobalVars(engine);
  initExample1Vars(engine);
  initExample2Vars(engine);
  initExample3Vars(engine);
  initExample4Vars(engine);
  initExample5Vars(engine);
  initExample8Vars(engine);

  engine.load(device);

  // // Put terminal in raw mode so we can send single key presses
  
  printf("\e[01;31m✿\e[0m  Entering IPUpysh\e[01;31m ✿\e[0m\nPress ^D^C to escape...\n");
  system("stty raw opost -echo");
  engine.run(0);
  system("stty cooked opost echo");
  printf("^D^C\n\e[01;31m✿\e[0m  Leaving IPUpysh\e[01;31m ✿\e[0m\n");

  

  return EXIT_SUCCESS;
}


// Helper utility //
Device getIPU(bool use_hardware, int num_ipus) {

  if (use_hardware) {
auto manager = DeviceManager::createDeviceManager();
    auto devices = manager.getDevices(TargetType::IPU, num_ipus);
    auto it = std::find_if(devices.begin(), devices.end(), [](Device &device) {
	return device.attach();
      });
    if (it == devices.end()) {
      std::cerr << "Error attaching to device\n";
      exit(EXIT_FAILURE);
    }
    std::cout << "Attached to IPU " << it->getId() << std::endl;
    return std::move(*it);
    
  } else {
    IPUModel ipuModel;
    return ipuModel.createDevice(); 
  }
}


