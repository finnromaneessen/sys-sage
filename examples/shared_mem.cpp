#include <unistd.h>

#include <iostream>

#include "sys-sage.hpp"

CopyAttrib pack(std::pair<std::string, void*> a) {
  if (!a.first.compare("custom_attrib")) {
    return {a.first, sizeof(size_t), a.second};
  }
  return {a.first, 0, nullptr};
}

SharedMemory* export_process() {
  // ----- Components -----
  Topology* topo = new Topology();

  Node* n1 = new Node(topo, 1);
  Node* n2 = new Node(topo, 2);

  Memory* mem = new Memory(n1, "memory", 100);
  Core* core = new Core(n1);

  Cache* cache = new Cache(core);

  // ----- Attribs -----
  int gpu_cores = 120;
  n1->attrib["Number_of_cores_in_GPU"] = &gpu_cores;

  size_t custom = 999;
  mem->attrib["custom_attrib"] = &custom;

  //----- DataPaths -----
  DataPath* dp1 = new DataPath(n1, n2, SYS_SAGE_DATAPATH_ORIENTED);
  DataPath* dp2 = new DataPath(mem, core, SYS_SAGE_DATAPATH_BIDIRECTIONAL);

  // ----- Export -----
  SharedMemory* shmem = export_topology("/tmp/mpi/1", topo, pack);

  return shmem;
}

void import_process() {
  Component* comp = import_topology("/tmp/mpi/1");

  comp->PrintSubtree();
  
  int custom_attrib = *(int *)(comp->GetChildren()->at(0)->attrib["Number_of_cores_in_GPU"]);
  std::cout << "IMPORT: n1->Number_of_cores_in_GPU = " << custom_attrib << std::endl;
}

int main(int argc, char* argv[]) {
  SharedMemory* shmem = export_process();

  pid_t c_pid = fork();

  if (c_pid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  } else if (c_pid == 0) {
    import_process();
    return 0;
  }

  delete shmem;
  return 0;
}