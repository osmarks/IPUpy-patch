#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <poplar/IPUModel.hpp>
#include <poplar/Program.hpp>

using namespace poplar;


Device getIPU(bool use_hardware = true, int num_ipus = 1);


int main() {

  Device device = getIPU(true);
  Graph graph(device.getTarget());

  // Create variable in IPU memory to store program outputs
  unsigned printBufSize = 1000;
  unsigned msgSize = 4;
  unsigned numMsgs = 5;
  char printBuf_h[printBufSize];
  char msgBuf_h[msgSize * numMsgs] = {"DATA""SENT""FROM""HOST""STOP"};
  
  Tensor printBuf = graph.addVariable(CHAR, {printBufSize}, "printBuf");
  Tensor msgBuf = graph.addVariable(CHAR, {msgSize}, "msgBuf");
  Tensor doneFlag = graph.addVariable(BOOL, {}, "doneFlag");
  graph.setTileMapping(printBuf, 0);
  graph.setTileMapping(msgBuf, 0);
  graph.setTileMapping(doneFlag, 0);
  DataStream printBufStream = graph.addDeviceToHostFIFO("printBuf-stream", poplar::CHAR, printBufSize);
  DataStream msgBufStream = graph.addHostToDeviceFIFO("msgBuf-stream", poplar::CHAR, msgSize);

  // Add computation vertext to IPU
  graph.addCodelets("example5_codelets.gp");
  ComputeSet init_computeset = graph.addComputeSet("InitCS");
  ComputeSet body_computeset = graph.addComputeSet("BodyCS");
  VertexRef int_vtx = graph.addVertex(init_computeset, "InitVertex", {
    {"printBuf", printBuf}, {"msgBuf", msgBuf}, {"doneFlag", doneFlag}});
  VertexRef body_vtx = graph.addVertex(body_computeset, "RTVertex", {
    {"printBuf", printBuf}, {"msgBuf", msgBuf}, {"doneFlag", doneFlag}});
  graph.setTileMapping(int_vtx, 0);
  graph.setTileMapping(body_vtx, 0);
  
  // Create and run program 
  program::Sequence program({
    program::Execute(init_computeset),
    program::Copy(printBuf, printBufStream),
    program::RepeatWhileFalse(program::Sequence(), doneFlag, program::Sequence({
      program::Execute(body_computeset),
      program::Copy(msgBufStream, msgBuf),
    })),
    program::Copy(printBuf, printBufStream)
  });

  Engine engine(graph, program);
  engine.connectStream("printBuf-stream", printBuf_h);
  engine.connectStream("msgBuf-stream", msgBuf_h, &msgBuf_h[msgSize * numMsgs]);
  engine.connectStreamToCallback("printBuf-stream", [printBufSize](void* p){printf("%.*s", printBufSize, (char*)p);});
  // static char counter = 0;
  // engine.connectStreamToCallback("msgBuf-stream", [](void* p){*((char*)p) = counter++;});
  engine.load(device);

  engine.run(0);


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
