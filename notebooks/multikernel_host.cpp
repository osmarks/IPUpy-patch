#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <list>
#include <string>
#include <string_view>
#include <stdio.h>

#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <poplar/IPUModel.hpp>
#include <poplar/Program.hpp>
#include <poputil/VertexTemplates.hpp>

#include "multikernel_params.hpp"


using namespace poplar;


Device getIPU(int num_ipus = 1);

struct KernelResult {
  int count;
  const char * data;
};

static std::unique_ptr<Engine> engine;
static char inBuf_h[INPUTBUFSIZE] = {0};
std::map<std::string, int> resultCounter;
std::list<KernelResult> resultQueue;
 

extern "C" int init() {
  Device device = getIPU(NUMIPUS);
  Graph graph(device.getTarget());

  unsigned maxFileSize = NUMREPLS * PERTILEFILEMAX;
  char* fileBuf_h = (char*) malloc(maxFileSize);
  unsigned perTileFileSize = 0;
  bool useFile = 1; // argc == 2;
  if (useFile) {
    const char* filename = "/usr/share/dict/words";  // argv[1];
    std::ifstream infile(filename, std::ios::in);
    if (!infile.is_open()) {
      std::cout << "Failed to open file " << filename << "\n";
      return EXIT_FAILURE;
    }
    unsigned fileSize = infile.read(fileBuf_h, maxFileSize).gcount();
    infile.close();
    perTileFileSize = (fileSize + NUMREPLS - 1) / NUMREPLS;
    if (fileSize == maxFileSize) {
      std::cout << "WARNING: Trimming input file to fit predefined buffer size\n";
    }
  }

  Tensor printBuf = graph.addVariable(CHAR, {NUMREPLS, PRINTBUFSIZE}, "printBuf");
  Tensor tileidTensor = graph.addVariable(UNSIGNED_INT, {NUMREPLS}, "tileidTensor");
  Tensor inBuf = graph.addVariable(CHAR, {INPUTBUFSIZE}, "inBuf");
  Tensor fileBuf = graph.addVariable(CHAR, {NUMREPLS, perTileFileSize}, "fileBuf");
  Tensor coroutineFlag = graph.addVariable(BOOL, {NUMREPLS}, "coroutineFlag");
  Tensor doneFlag = graph.addVariable(BOOL, {NUMREPLS}, "doneFlag");
#ifdef COMMS
  Tensor commsInBuf = graph.addVariable(CHAR, {NUMREPLS, COMMSBUFSIZE}, "commsInBuf");
  Tensor commsOutBuf = graph.addVariable(CHAR, {NUMREPLS, COMMSBUFSIZE}, "commsOutBuf");
#endif

  graph.setTileMapping(inBuf, 0);
  DataStream printBufStream = graph.addDeviceToHostFIFO("printBuf-stream", poplar::CHAR, PRINTBUFSIZE * NUMREPLS);
  DataStream inBufStream = graph.addHostToDeviceFIFO("inBuf-stream", poplar::CHAR, INPUTBUFSIZE);
  DataStream fileBufStream = graph.addHostToDeviceFIFO("fileBuf-stream", poplar::CHAR, perTileFileSize * NUMREPLS);


  graph.addCodelets("multikernel_codelets.gp");
  ComputeSet tileid_computeset = graph.addComputeSet("TileidCS");
  ComputeSet init_computeset = graph.addComputeSet("InitCS");
  ComputeSet firstruntime_computeset = graph.addComputeSet("FirstRTCS");
  ComputeSet runtime_computeset = graph.addComputeSet("RTCS");
  ComputeSet anycoroutine_computeset = graph.addComputeSet("AlldoneCS");
  for (unsigned i = 0; i < NUMREPLS; ++i) {
    VertexRef tileid_vtx = graph.addVertex(tileid_computeset, "TileIDGrabber", {{"tileid", tileidTensor[i]}});
    VertexRef init_vtx = graph.addVertex(init_computeset, poputil::templateVertex("InitVertex", useFile ? "true" : "false"), {
      {"printBuf", printBuf[i]}, 
      {"fileBuf", fileBuf[i]},
      {"inBuf", inBuf}, 
      {"doneFlag", doneFlag[i]}, 
      {"coroutineFlag", coroutineFlag[i]}, 
      {"tileid", tileidTensor[i]}
    });
    VertexRef firstruntime_vtx = graph.addVertex(firstruntime_computeset, poputil::templateVertex("RuntimeVertex", "true"), {
      {"printBuf", printBuf[i]}, 
      {"fileBuf", fileBuf[i]}, 
      {"inBuf", inBuf},
      {"doneFlag", doneFlag[i]},
      {"coroutineFlag", coroutineFlag[i]}
    });
    VertexRef runtime_vtx = graph.addVertex(runtime_computeset, poputil::templateVertex("RuntimeVertex", "false"), {
      {"printBuf", printBuf[i]}, 
      {"fileBuf", fileBuf[i]}, 
      {"inBuf", inBuf},
      {"doneFlag", doneFlag[i]},
      {"coroutineFlag", coroutineFlag[i]}
    });
#ifdef COMMS
    graph.connect(firstruntime_vtx["commsInBuf"], commsInBuf.flatten());
    graph.connect(firstruntime_vtx["commsOutBuf"], commsOutBuf[i]);
    graph.connect(runtime_vtx["commsInBuf"], commsInBuf.flatten());
    graph.connect(runtime_vtx["commsOutBuf"], commsOutBuf[i]);
    graph.setTileMapping(commsInBuf[i], i);
    graph.setTileMapping(commsOutBuf[i], i);
#endif
    graph.setTileMapping(tileid_vtx, i);
    graph.setTileMapping(init_vtx, i);
    graph.setTileMapping(firstruntime_vtx, i);
    graph.setTileMapping(runtime_vtx, i);
    graph.setTileMapping(tileidTensor[i], i);
    graph.setTileMapping(printBuf[i], i);
    graph.setTileMapping(fileBuf[i], i);
    graph.setTileMapping(doneFlag[i], i);
    graph.setTileMapping(coroutineFlag[i], i);
  }
  VertexRef anycoroutine_vtx = graph.addVertex(anycoroutine_computeset, "Any", {{"flags", coroutineFlag}});
  graph.setTileMapping(anycoroutine_vtx, 0);
  
  program::Sequence init_program({
    program::Copy(fileBufStream, fileBuf),
    program::Execute(tileid_computeset),
    program::Execute(init_computeset),
    program::Copy(printBuf, printBufStream)
  });

  program::Sequence runtime_program({
    program::Copy(inBufStream, inBuf),
    program::Execute(firstruntime_computeset),
#ifdef COMMS
    program::RepeatWhileTrue(program::Execute(anycoroutine_computeset), coroutineFlag[0], program::Sequence({
      program::Copy(commsOutBuf, commsInBuf),
      program::Execute(runtime_computeset)
    })),
#endif
    program::Copy(printBuf, printBufStream)
  });


  char cacheFile[] = "pod1_dict";
  Executable exe;
  if (0) {
    
    std::cout << "Running compilation...\n";
    fflush(stdout);
    exe = compileGraph(graph, {init_program, runtime_program}, {}); /*
        {{"debug.outputAllSymbols", "true"},
        {"target.saveArchive", "archive.a"},
        //{"debug.logStackSizeAnalysis", "true"},
        {"debug.loweredProgsFile", "progs"}});*/
    std::ofstream outFile(cacheFile);
    exe.serialize(outFile);
  } else {
    printf("Loading cached exe %s\n", cacheFile);
    auto inFile = std::ifstream(cacheFile);
    exe = Executable::deserialize(inFile);
  }

  std::cout << "Creating engine...\n";
  fflush(stdout);
  engine = std::make_unique<Engine>(Engine(std::move(exe)));


  engine->connectStream("fileBuf-stream", fileBuf_h);
  engine->connectStream("inBuf-stream", inBuf_h);
  engine->connectStreamToCallback("printBuf-stream", [](void* p){
    char* printbuf = (char*) p;
    if (printbuf[0] == '\0') return;
    
    resultCounter.clear();
    resultQueue.clear();
    for (unsigned i = 0; i < NUMREPLS; ++i) {
        std::string x = printbuf + i * PRINTBUFSIZE;
        ++resultCounter[x];
    }
    for (auto const& x : resultCounter) {
      resultQueue.push_front({x.second, x.first.data()});
    }
  });

  std::cout << "Loading executable...\n";
  fflush(stdout);
  engine->load(device);
  std::cout << "Running init program\n";
  fflush(stdout);
  engine->run(0);
  std::cout << "Init complete\n";
  fflush(stdout);

  free(fileBuf_h);

  return EXIT_SUCCESS;
}


extern "C" int execute(const char* code) {
  size_t input_len = strnlen(code, INPUTBUFSIZE);
  if (input_len == 0) return 0;
  if (input_len == INPUTBUFSIZE) return -1;
  memcpy(inBuf_h, code, input_len);
  inBuf_h[input_len] = '\0';
  engine->run(1);
  return 1;
}


extern "C" KernelResult getResult() {
  if (resultQueue.size() == 0) {
    return {0, "DEADBEEF"};
  }

  KernelResult res = resultQueue.back();
  resultQueue.pop_back();
  static char superSafePersistentMemory[PRINTBUFSIZE];
  memcpy(superSafePersistentMemory, res.data, strnlen(res.data, PRINTBUFSIZE - 1) + 1);
  return {res.count, superSafePersistentMemory};
}


Device getIPU(int num_ipus) {
  auto manager = DeviceManager::createDeviceManager();
  auto devices = manager.getDevices(TargetType::IPU, num_ipus);
  auto it = std::find_if(devices.begin(), devices.end(), [](Device &device) {return device.attach();});
  if (it == devices.end()) {
    std::cerr << "Error attaching to device\n";
    exit(EXIT_FAILURE);
  }
  std::cout << "Attached to IPU " << it->getId() << std::endl;
  return std::move(*it);
}
