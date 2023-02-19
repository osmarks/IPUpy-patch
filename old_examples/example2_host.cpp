#include <algorithm>
#include <chrono>
#include <cstdlib>
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
  unsigned printBufSize = 2000;
  unsigned N = 10;
  char printBuf_h[printBufSize] = {0};
  float X_h[N] = {-4., 0., 0.1, 9., 10., -2., -7., 1.5, -1., 0.};

  Tensor printBuf = graph.addVariable(CHAR, {printBufSize}, "printBuf");
  Tensor X = graph.addVariable(FLOAT, {N}, "X");
  graph.setTileMapping(printBuf, 0);
  graph.setTileMapping(X, 0);
  graph.createHostRead("printBuf-read", printBuf);
  graph.createHostWrite("X-write", X);

  // Add computation vertext to IPU
  graph.addCodelets("example2_codelets.gp");
  ComputeSet computeset = graph.addComputeSet("cs");
  VertexRef vtx = graph.addVertex(computeset, "ComplexOp", {
    {"printBuf", printBuf},
    {"X", X},
    });
  graph.setTileMapping(vtx, 0);
  
  // Create and run program 
  poplar::program::Execute program(computeset);
  Engine engine(graph, program);
  engine.load(device);
  engine.writeTensor("X-write", X_h, &X_h[N]);
  engine.run(0);
  engine.readTensor("printBuf-read", printBuf_h, &printBuf_h[printBufSize]);

  // Print output
  printf("\n%.*s\n", printBufSize, printBuf_h);

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
