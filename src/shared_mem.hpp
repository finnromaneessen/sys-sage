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
  CopyVector(int offset, size_t size)
      : offset(offset), size(size){};  // TODO Probably not needed
};

struct CopyAttrib {  // TODO RENAME?
  std::string key;
  size_t size;
  void* data;

  /**
   * @brief Gets the total size of the CopyAttrib
   *
   * @return size_t Total size
   */
  size_t getTotalSize() { return key.size() + 1 + sizeof(size_t) + size; }

  /**
   * @brief Copies the CopyAttrib to the provided pointer.
   *
   * @param dst Destination pointer
   * @return size_t Total bytes written.
   */
  size_t copy(void* dst) {
    char* cur = (char*)dst;

    // Copy Key
    strcpy((char*)cur, key.c_str());
    cur += key.size() + 1;

    // Copy size
    memcpy(cur, &size, sizeof(size_t));
    cur += sizeof(size_t);

    // Copy Data
    memcpy(cur, data, size);

    return getTotalSize();
  }
};
/**
 * @brief Recreates the attrib pair from shared memory
 *
 * @param src Pointer to the attrib
 * @return std::tuple<size_t, std::string, void*> Tuple of total bytes written
 * and Key-Value pair of the attrib
 */
std::tuple<size_t, std::string, void*> recreate_attrib(void* src);

class MemoryManager {  // TODO Rename to SharedMemory?
 public:
  void* mem;
  char* cur;
  size_t size;
  MemoryManager(std::string path, size_t size);
  MemoryManager(std::string path);
  ~MemoryManager() { munmap(mem, size); }

  // TODO calc_tree_size() (preexistent?)

  std::map<std::string, size_t> attrib_sizes = {
      // TODO REMOVE
      {"CATcos", sizeof(uint64_t)},
      {"CATL3mask", sizeof(uint64_t)},
      {"mig_size", sizeof(long long)},
      {"Number_of_streaming_multiprocessors", sizeof(int)},
      {"Number_of_cores_in_GPU", sizeof(int)},
      {"Number_of_cores_per_SM", sizeof(int)},
      {"Bus_Width_bit", sizeof(int)},
      {"Clock_Frequency", sizeof(double)},
      {"latency", sizeof(float)},
      {"latency_min", sizeof(float)},
      {"latency_max", sizeof(float)},
      //{"CUDA_compute_capability", sizeof(std::string)},
      //{"mig_uuid", sizeof(std::string)},
      //{"freq_history", sizeof(std::vector<std::tuple<long long,double>>)},
      //{"GPU_Clock_Rate", sizeof(std::tuple<double, std::string>)},
  };

 private:
  std::string path;                 // TODO?
  map<int, Component*> components;  // TODO?
};

MemoryManager* export_component(std::string path, Component* component);
MemoryManager* export_component(
    std::string path, Component* component,
    CopyAttrib (*pack)(std::pair<std::string, void*>));
Component* import_component(std::string path);
Component* import_component(std::string path,
                            std::pair<std::string, void*> (*unpack)(
                                size_t size, std::pair<std::string, void*>));

#endif