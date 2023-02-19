#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <poplar/IPUModel.hpp>
#include <poplar/Program.hpp>
#include <poputil/VertexTemplates.hpp>

#include "multirepl_params.hpp"


using namespace poplar;


Device getIPU(int num_ipus = 1);


int main(int argc, char** argv) {

  Device device = getIPU(NUMIPUS);
  Graph graph(device.getTarget());

  char last_char = '\0';
  unsigned maxFileSize = NUMREPLS * PERTILEFILEMAX;
  char* fileBuf_h = (char*) malloc(maxFileSize);
  unsigned inBufSize = 2;
  unsigned perTileFileSize = 0;
  if (argc == 2) {
    std::ifstream infile(argv[1], std::ios::in);
    unsigned fileSize = infile.read(fileBuf_h, maxFileSize).gcount();
    infile.close();
    perTileFileSize = (fileSize + NUMREPLS - 1) / NUMREPLS;
  }

  Tensor printBuf = graph.addVariable(CHAR, {NUMREPLS, PRINTBUFSIZE}, "printBuf");
  Tensor tileidTensor = graph.addVariable(UNSIGNED_INT, {NUMREPLS}, "tileidTensor");
  Tensor inBuf = graph.addVariable(CHAR, {inBufSize}, "inBuf");
  Tensor fileBuf = graph.addVariable(CHAR, {NUMREPLS, perTileFileSize}, "fileBuf");
  Tensor coroutineFlag = graph.addVariable(BOOL, {NUMREPLS}, "coroutineFlag");
  Tensor doneFlag = graph.addVariable(BOOL, {NUMREPLS}, "doneFlag");
#ifdef COMMS
  Tensor commsInBuf = graph.addVariable(CHAR, {NUMREPLS, COMMSBUFSIZE}, "commsInBuf");
  Tensor commsOutBuf = graph.addVariable(CHAR, {NUMREPLS, COMMSBUFSIZE}, "commsOutBuf");
#endif

  graph.setTileMapping(inBuf, 0);
  DataStream printBufStream = graph.addDeviceToHostFIFO("printBuf-stream", poplar::CHAR, PRINTBUFSIZE * NUMREPLS);
  DataStream inBufStream = graph.addHostToDeviceFIFO("inBuf-stream", poplar::CHAR, inBufSize);
  DataStream fileBufStream = graph.addHostToDeviceFIFO("fileBuf-stream", poplar::CHAR, perTileFileSize * NUMREPLS);


  graph.addCodelets("multirepl_codelets.gp");
  ComputeSet tileid_computeset = graph.addComputeSet("TileidCS");
  ComputeSet init_computeset = graph.addComputeSet("InitCS");
  ComputeSet firstruntime_computeset = graph.addComputeSet("FirstRTCS");
  ComputeSet runtime_computeset = graph.addComputeSet("RTCS");
  ComputeSet anycoroutine_computeset = graph.addComputeSet("AlldoneCS");
  for (unsigned i = 0; i < NUMREPLS; ++i) {
    VertexRef tileid_vtx = graph.addVertex(tileid_computeset, "TileIDGrabber", {{"tileid", tileidTensor[i]}});
    VertexRef init_vtx = graph.addVertex(init_computeset, "InitVertex", {
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
  
  program::Sequence program({
    program::Copy(fileBufStream, fileBuf),
    program::Execute(tileid_computeset),
    program::Execute(init_computeset),
    program::Copy(printBuf, printBufStream),
    program::RepeatWhileFalse(program::Sequence(), doneFlag[0], program::Sequence({
      program::Copy(inBufStream, inBuf),
      program::Execute(firstruntime_computeset),
#ifdef COMMS
      program::RepeatWhileTrue(program::Execute(anycoroutine_computeset), coroutineFlag[0], program::Sequence({
        program::Copy(commsOutBuf, commsInBuf),
        program::Execute(runtime_computeset)
      })),
#endif
      program::Copy(printBuf, printBufStream)
    }))
  });


  char cacheFile[] = "pod1_dict";
  Executable exe;
  if (1) {
    printf("Running compilation...\n");
    exe = compileGraph(graph, {program}, {}); /*{{"debug.outputAllSymbols", "true"},
                                          {"target.saveArchive", "archive.a"},
                                          //{"debug.logStackSizeAnalysis", "true"},
                                          {"debug.loweredProgsFile", "progs"}});*/
    std::ofstream outFile(cacheFile);
    exe.serialize(outFile);
  } else {
    auto inFile = std::ifstream(cacheFile);
    exe = Executable::deserialize(inFile);
  }

  printf("Creating engine\n");
  Engine engine(std::move(exe));


  engine.connectStream("fileBuf-stream", fileBuf_h);
  engine.connectStreamToCallback("printBuf-stream", [&last_char](void* p){
    char* printbuf = (char*) p;
    if ((last_char != '\r' && last_char != '\t')
        || (printbuf[2] == '.' && printbuf[3] == '.' && printbuf[4] == '.')) {
      printf("%.*s", PRINTBUFSIZE, printbuf);
      return;
    }
    
    std::map<std::string, int> outputs;
    for (unsigned i = 0; i < NUMREPLS; ++i) {
        std::string x = printbuf + i * PRINTBUFSIZE;
        ++outputs[x];
    }

    for (auto const& x : outputs) {
      std::string_view content = x.first;
      if (last_char == '\t') {
        std::cout << "\n\e[01;34m[" << x.second << "x] " << "\e[0m" << content;
      } else if (content.length() > 6) {
        std::cout << "\n\e[01;34m---------- [" << x.second << "x] ----------" << "\e[0m\n";
        std::cout << content.substr(2, content.length() - 7);
      }
    }
    std::cout << "\n>>> ";
  });


  engine.connectStreamToCallback("inBuf-stream", [&last_char](void* p){
    last_char = getchar();
    *((char*)p) = last_char;
  });
  engine.load(device);

  // Put terminal in raw mode so we can send single key presses
  std::cout << "Launching " << NUMREPLS << " REPL instances\n";
  system("stty raw opost -echo");
  engine.run(0);
  system("stty cooked opost echo");

  printf("Shutting Down - \e[01;31mGoodbye\e[0m\n\n");

  free(fileBuf_h);

  return EXIT_SUCCESS;
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
