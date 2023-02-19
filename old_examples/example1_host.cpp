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
  int numTiles = 1;//device.getTarget().getNumTiles();


  // Create variable in IPU memory to store program outputs
  unsigned N = 15;
  unsigned K = 3;
  int input_h[N*numTiles] = {1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0};
  int output_h[N/K * numTiles] = {0};

  graph.addCodelets("example1_codelets.gp");
  ComputeSet computeset = graph.addComputeSet("cs");
  Tensor input = graph.addVariable(INT, {N * numTiles}, "input");
  Tensor output = graph.addVariable(INT, {N/K * numTiles}, "output");
  graph.createHostWrite("input-write", input);
  graph.createHostRead("output-read", output);

  for (int tile = 0; tile < numTiles; ++tile) {
    Tensor inslice = input.slice({tile * N, (tile + 1) * N});
    Tensor outslice = output.slice({tile * (N/K), (tile + 1) * (N/K)});
    graph.setTileMapping(inslice, tile);
    graph.setTileMapping(outslice, tile);

    // Add computation vertext to IPU
    VertexRef vtx = graph.addVertex(computeset, "LogicalConv3", {
      {"input", inslice},
      {"output", outslice},
      });
    graph.setTileMapping(vtx, tile);
  }

  
  // Create and run program 
  poplar::program::Execute program(computeset);
  Engine engine(graph, program); //, {{"debug.instrument", "true"}});

  engine.load(device);
  engine.writeTensor("input-write", input_h, &input_h[N * numTiles]);
  engine.run(0);
  engine.readTensor("output-read", output_h, &output_h[N/K * numTiles]);


  // Print output
  std::cout << "{";
  for (unsigned i = 0; i < N; ++i) {
    std::cout << input_h[i] << ", ";
  }
  std::cout << "}  -->  {";
  for (unsigned i = 0; i < N/K; ++i) {
    std::cout << output_h[i] << ", ";
  }
  std::cout << "}" << std::endl;


  // engine.printProfileSummary(std::cout, {{"showExecutionSteps", "true"}});


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
