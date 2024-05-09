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

#define PAGE_SIZE 4096

void* create_shared_memory(const std::string path, size_t size);
void* open_shared_memory(const std::string path);

struct CopyVector {
  size_t offset;
  size_t size;
  CopyVector(int offset, size_t size) : offset(offset), size(size){};
};

/**
 * @brief Struct used to help copy attribs (std::pair<std::string, void*>) into
 * shared memory
 */
struct CopyAttrib {
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

class SharedMemory {
 public:
  void* mem;
  char* cur;
  size_t size;
  std::map<DataPath*, std::pair<size_t, size_t>> dp_offsets;
  std::map<size_t, Component*> comp_offsets;

  std::string GetPath() { return path; }

  SharedMemory(std::string path, size_t size);
  SharedMemory(std::string path);
  ~SharedMemory() { munmap(mem, size); }

 private:
  std::string path;
};

SharedMemory* export_component(std::string path, Component* component);
SharedMemory* export_component(
    std::string path, Component* component,
    CopyAttrib (*pack)(std::pair<std::string, void*>));

Component* import_component(std::string path);
Component* import_component(std::string path,
                            std::pair<std::string, void*> (*unpack)(
                                size_t size, std::pair<std::string, void*>));

#endif