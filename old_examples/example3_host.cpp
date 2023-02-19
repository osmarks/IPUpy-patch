#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>

#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <poplar/IPUModel.hpp>
#include <poplar/Program.hpp>

using namespace poplar;


Device getIPU(bool use_hardware = true, int num_ipus = 1);


int main(int argc, char** argv) {

  if (argc != 2) {
    std::cout << "USAGE: " << argv[0] << " <infile>\n";
    return EXIT_FAILURE;
  }

  // Read file
  const int bufSize = 20000;
  char buffer_h[bufSize] = "";
  std::ifstream file(argv[1], std::ios::ate);
  std::streamsize fileSize = file.tellg();
  file.seekg(0, std::ios::beg);
  if (!file.read(buffer_h, fileSize)) {
    std::cout << "Failed to read file " << argv[1] << std::endl;
    return EXIT_FAILURE;
  }
  buffer_h[fileSize] = '\0';

  Device device = getIPU(true);
  Graph graph(device.getTarget());

  // Create variable in IPU memory 
  Tensor inbuf = graph.addVariable(CHAR, {bufSize}, "inbuf");
  Tensor outbuf = graph.addVariable(CHAR, {bufSize}, "outbuf");
  graph.setTileMapping(inbuf, 0);
  graph.setTileMapping(outbuf, 0);
  graph.createHostWrite("inbuf-write", inbuf);
  graph.createHostRead("outbuf-read", outbuf);

  // Add computation vertext to IPU
  graph.addCodelets("example3_codelets.gp");
  ComputeSet computeset = graph.addComputeSet("cs");
  VertexRef vtx = graph.addVertex(computeset, "Preprocessor", {
    {"inbuf", inbuf},
    {"outbuf", outbuf},
  });
  graph.setTileMapping(vtx, 0);
  
  // Create and run program 
  poplar::program::Execute program(computeset);
  Engine engine(graph, program);
  engine.load(device);
  engine.writeTensor("inbuf-write", buffer_h, &buffer_h[bufSize]);
  engine.run(0);
  engine.readTensor("outbuf-read", buffer_h, &buffer_h[bufSize]);

  // Print output
  printf("%s", buffer_h);

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
    // std::cout << "Attached to IPU " << it->getId() << std::endl;
    return std::move(*it);
    
  } else {
    IPUModel ipuModel;
    return ipuModel.createDevice(); 
  }
}
