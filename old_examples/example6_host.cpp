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
  // int numTiles = device.getTarget().getNumTiles();

  unsigned blockSize = 256; //numTiles - 1;
  unsigned numBlocks = 40;
  unsigned printBufSize = 2000;
  char printBuf_h[printBufSize];


  // Tensor diskImg = graph.addVariable(CHAR, {diskSize}, "diskImg");
  Tensor printBuf = graph.addVariable(CHAR, {printBufSize}, "printBuf");
  Tensor dataBlock = graph.addVariable(CHAR, {blockSize}, "dataBlock");
  Tensor disk = graph.addVariable(CHAR, {blockSize * numBlocks}, "disk");
  Tensor doneFlag = graph.addVariable(BOOL, {}, "doneFlag");
  Tensor rwFlag = graph.addVariable(INT, {}, "rwFlag");
  Tensor diskPos = graph.addVariable(INT, {}, "diskPos");
  graph.setTileMapping(printBuf, 0);
  graph.setTileMapping(dataBlock, 0);
  graph.setTileMapping(doneFlag, 0);
  graph.setTileMapping(rwFlag, 0);
  graph.setTileMapping(diskPos, 0);
  DataStream printBufStream = graph.addDeviceToHostFIFO("printBuf-stream", poplar::CHAR, printBufSize);

  // Add computation vertext to IPU
  graph.addCodelets("example6_codelets.gp");
  ComputeSet init_computeset = graph.addComputeSet("InitCS");
  ComputeSet body_computeset = graph.addComputeSet("BodyCS");
  ComputeSet readwrite_computeset = graph.addComputeSet("ReadWriteCS");
  VertexRef int_vtx = graph.addVertex(init_computeset, "InitVertex", {
    {"printBuf", printBuf}, {"dataBlock", dataBlock}, {"doneFlag", doneFlag}, {"rwFlag", rwFlag}, {"diskPos", diskPos}});
  VertexRef body_vtx = graph.addVertex(body_computeset, "BodyVertex", {
    {"printBuf", printBuf}, {"dataBlock", dataBlock}, {"doneFlag", doneFlag}, {"rwFlag", rwFlag}, {"diskPos", diskPos}});
  graph.setTileMapping(int_vtx, 0);
  graph.setTileMapping(body_vtx, 0);
  
  for (unsigned i = 0; i < blockSize; ++i) {
    auto diskSlice = disk.slice(i * numBlocks, (i + 1) * numBlocks);
    VertexRef readwrite_vtx = graph.addVertex(readwrite_computeset, "ReadWriteVertex", {
      {"disk", diskSlice}, 
      {"value", dataBlock[i]},
      {"diskPos", diskPos},
      {"rwFlag", rwFlag}
    });
    graph.setTileMapping(diskSlice, i+1);
    graph.setTileMapping(readwrite_vtx, i+1);
  }
  
  // Create and run program 
  program::Sequence program({
    program::Execute(init_computeset),
    program::Copy(printBuf, printBufStream),
    program::RepeatWhileFalse(program::Sequence(), doneFlag, program::Sequence({
      program::Execute(readwrite_computeset),
      program::Execute(body_computeset),
    })),
    program::Copy(printBuf, printBufStream)
  });

  Engine engine(graph, program);
  engine.connectStream("printBuf-stream", printBuf_h);
  engine.connectStreamToCallback("printBuf-stream", [printBufSize](void* p){printf("%.*s", printBufSize, (char*)p);});
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
