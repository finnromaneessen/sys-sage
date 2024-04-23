#ifndef SHARED_MEM
#define SHARED_MEM

#define _GNU_SOURCE 1
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <map>
#include <string>

#include "Topology.hpp"

#define MEM_CREATE 1
#define MEM_OPEN 0

#define PAGE_SIZE 4096

void* create_shared_memory(const std::string path, size_t size);
void* open_shared_memory(const std::string path);

struct CopyVector {
  size_t offset;
  size_t size;
  CopyVector(int offset, size_t size) : offset(offset), size(size){};
};

class MemoryManager {  // TODO Rename to SharedMemory?
 public:
  void* mem;
  char* cur;
  size_t size;
  MemoryManager(std::string path, size_t size);
  MemoryManager(std::string path);
  ~MemoryManager() { munmap(mem, size); }

  // TODO calc_tree_size() (preexistent?)

 private:
  std::string path;                 // TODO?
  map<int, Component*> components;  // TODO?
};

MemoryManager* export_component(std::string path, Component* component);
Component* import_component(std::string path);

#endif