#include "shared_mem.hpp"

MemoryManager::MemoryManager(std::string path, size_t size)
    : size(size), path(path) {
  mem = create_shared_memory(path, size);
  cur = (char *)mem;
}

MemoryManager::MemoryManager(std::string path) {
  mem = open_shared_memory(path);
  cur = (char *)mem;

  size = *((size_t *)mem - 1);
  std::cout << "MemoryManager() size = " << size << std::endl;
}

size_t calc_memory_size(Component *tree) {
  unsigned a, b;
  size_t min_size = tree->GetTopologySize(&a, &b);
  size_t tmp = (min_size + PAGE_SIZE - 1) & -PAGE_SIZE;

  std::cout << "cal_memory_size(): " << tmp << " | " << (tmp % PAGE_SIZE)
            << std::endl;

  return tmp;
}

CopyAttrib pack_default(std::pair<std::string, void *> attrib) {
  std::string key = attrib.first;
  void *value = attrib.second;

  if (!key.compare("CATcos") || !key.compare("CATL3mask")) {
    return {key, sizeof(uint64_t), value};
  }

  if (!key.compare("mig_size")) {
    return {key, sizeof(long long), value};
  }

  if (!key.compare("Number_of_streaming_multiprocessors") ||
      !key.compare("Number_of_cores_in_GPU") ||
      !key.compare("Number_of_cores_per_SM") || !key.compare("Bus_Width_bit")) {
    return {key, sizeof(int), value};
  }

  if (!key.compare("Clock_Frequency")) {
    return {key, sizeof(double), value};
  }

  if (!key.compare("latency") || !key.compare("latency_max") ||
      !key.compare("latency_min")) {
    return {key, sizeof(float), value};
  }

  if (!key.compare("CUDA_compute_capability") || !key.compare("mig_uuid")) {
    std::string *val = (std::string *)value;
    return {key, val->size() + 1, (void *)(val->c_str())};
  }

  if (!key.compare("freq_history")) {
    std::vector<std::tuple<long long, double>> *val =
        (std::vector<std::tuple<long long, double>> *)value;

    return {key, val->size() * sizeof(std::tuple<long long, double>),
            val->data()};
  }

  if (!key.compare("GPU_Clock_Rate")) {  // TODO Long strings
    return {key, sizeof(std::tuple<double, std::string>), value};
  }

  return {key, 0, nullptr};
}

std::pair<std::string, void *> unpack_default(
    size_t size, std::pair<std::string, void *> attrib) {
  std::string key = attrib.first;
  void *value = attrib.second;

  if (!key.compare("CUDA_compute_capability") || !key.compare("mig_uuid")) {
    std::string *str = new std::string((char *)value);
    free(value);

    return {key, str};
  }

  if (!key.compare("freq_history")) {
    char *end = (char *)value + size;
    std::cout << "unpack_default() size: " << size << " | "
              << sizeof(std::tuple<long long, double>) << std::endl;
    std::vector<std::tuple<long long, double>> *vec =
        new std::vector<std::tuple<long long, double>>(
            (std::tuple<long long, double> *)value,
            (std::tuple<long long, double> *)end);
    // TODO free value?
    free(value);
    return {key, vec};
  }

  return {key, value};
}

size_t export_recursive(MemoryManager *manager, Component *component,
                        CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  std::cout << "----- Export -----" << std::endl;

  // Get own size
  // TODO Better Solution??
  auto size_comp = 0;
  switch (component->GetComponentType()) {
    case SYS_SAGE_COMPONENT_THREAD:
      size_comp = sizeof(Thread);
      break;

    case SYS_SAGE_COMPONENT_CORE:
      size_comp = sizeof(Core);
      break;

    case SYS_SAGE_COMPONENT_CACHE:
      size_comp = sizeof(Cache);
      break;

    case SYS_SAGE_COMPONENT_SUBDIVISION:
      size_comp = sizeof(Subdivision);
      break;

    case SYS_SAGE_COMPONENT_NUMA:
      size_comp = sizeof(Numa);
      break;

    case SYS_SAGE_COMPONENT_CHIP:
      size_comp = sizeof(Chip);
      break;

    case SYS_SAGE_COMPONENT_MEMORY:
      size_comp = sizeof(Memory);
      break;

    case SYS_SAGE_COMPONENT_STORAGE:
      size_comp = sizeof(Storage);
      break;

    case SYS_SAGE_COMPONENT_NODE:
      size_comp = sizeof(Node);
      break;

    case SYS_SAGE_COMPONENT_TOPOLOGY:
      size_comp = sizeof(Topology);
      break;

    default:
      size_comp = sizeof(Component);
      break;
  }

  std::cout << "Export: Component-Size: " << size_comp
            << " | ID: " << component->GetId() << std::endl;

  // Get own offset
  auto self_offset = (size_t)manager->cur - (size_t)manager->mem;

  // Copy Class and initialize Component
  memcpy(manager->cur, (void *)component, size_comp);
  Component *c = (Component *)manager->cur;
  manager->cur += size_comp;

  // ----- Export attribs -----
  size_t *num_attribs = (size_t *)manager->cur;
  *num_attribs = 0;
  manager->cur += sizeof(size_t);  // TODO calc small things into memory size??

  CopyAttrib attrib;

  for (auto const &a : component->attrib) {
    if (attrib = pack_default(a); attrib.size != 0) {
      (*num_attribs)++;
      manager->cur += attrib.copy(manager->cur);
    } else if (attrib = pack(a); attrib.size != 0) {
      (*num_attribs)++;
      manager->cur += attrib.copy(manager->cur);
    }

    /*TODO Remove
    if (manager->attrib_sizes.find(a.first) != manager->attrib_sizes.end()) {
      (*num_attribs)++;
      auto size = manager->attrib_sizes[a.first];

      CopyAttrib copy_attrib{a.first, size, a.second};
      manager->cur += copy_attrib.copy(manager->cur);
    } else if ((custom_attrib = pack(a)); custom_attrib.size != 0) {
      std::cout << "Export: custom attrib " << a.first
                << " exported. Size: " << custom_attrib.size << std::endl;
      (*num_attribs)++;
      manager->cur += custom_attrib.copy(manager->cur);
    }*/
  }
  std::cout << "Export: num_attribs = " << *num_attribs << std::endl;

  // ----- Export children -----
  auto size_children = component->GetChildren()->size();
  auto offset_children = (size_t)manager->cur - (size_t)manager->mem;

  std::cout << "Export: size_children = " << size_children
            << " | offset_children = " << offset_children << std::endl;

  size_t *children_data = (size_t *)manager->cur;
  manager->cur += size_children * sizeof(Component *);

  // Calculate offsets for children
  for (size_t i = 0; i < size_children; i++) {
    size_t o = export_recursive(manager, c->GetChildren()->at(i), pack);

    std::cout << "Export: ID " << component->GetId()
              << " - Calculated Offset: " << o << std::endl;
    *(children_data++) = o;
  }

  // Create CopyVector for children
  CopyVector *cp = (CopyVector *)(c->GetChildren());
  *cp = CopyVector(offset_children, size_children);

  return self_offset;
}

MemoryManager *export_component(
    std::string path, Component *component,
    CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  MemoryManager *manager = new MemoryManager(path, calc_memory_size(component));

  export_recursive(manager, component, pack);
  return manager;
}

MemoryManager *export_component(std::string path, Component *component) {
  return export_component(
      path, component, [](std::pair<std::string, void *> attrib) -> CopyAttrib {
        return {attrib.first, 0, nullptr};
      });
}

Component *import_recursive(MemoryManager *manager,
                            std::pair<std::string, void *> (*unpack)(
                                size_t size, std::pair<std::string, void *>)) {
  std::cout << "----- Import -----" << std::endl;
  std::cout << "Import: offset = "
            << ((size_t)manager->cur - (size_t)manager->mem) << std::endl;

  Component *com = (Component *)manager->cur;
  Component *component = nullptr;

  size_t size_comp = 0;
  switch (com->GetComponentType()) {
    case SYS_SAGE_COMPONENT_THREAD:
      component = new Thread(*(Thread *)com);
      size_comp = sizeof(Thread);
      break;

    case SYS_SAGE_COMPONENT_CORE:
      component = new Core(*(Core *)com);
      size_comp = sizeof(Core);
      break;

    case SYS_SAGE_COMPONENT_CACHE:
      component = new Cache(*(Cache *)com);
      size_comp = sizeof(Cache);
      break;

    case SYS_SAGE_COMPONENT_SUBDIVISION:
      component = new Subdivision(*(Subdivision *)com);
      size_comp = sizeof(Subdivision);
      break;

    case SYS_SAGE_COMPONENT_NUMA:
      component = new Numa(*(Numa *)com);
      size_comp = sizeof(Numa);
      break;

    case SYS_SAGE_COMPONENT_CHIP:
      component = new Chip(*(Chip *)com);
      size_comp = sizeof(Chip);
      break;

    case SYS_SAGE_COMPONENT_MEMORY:
      component = new Memory(*(Memory *)com);
      size_comp = sizeof(Memory);
      break;

    case SYS_SAGE_COMPONENT_STORAGE:
      component = new Storage(*(Storage *)com);
      size_comp = sizeof(Storage);
      break;

    case SYS_SAGE_COMPONENT_NODE:
      component = new Node(*(Node *)com);
      size_comp = sizeof(Node);
      break;

    case SYS_SAGE_COMPONENT_TOPOLOGY:
      component = new Topology(*(Topology *)com);
      size_comp = sizeof(Topology);
      break;

    default:
      component = new Component(*com);
      size_comp = sizeof(Component);
      break;
  }

  manager->cur += size_comp;
  std::cout << "Import: component->id = " << component->GetId() << std::endl;

  // ----- Import attribs -----
  size_t num_attribs = *(size_t *)manager->cur;
  manager->cur += sizeof(size_t);
  std::cout << "Import: num_attribs = " << num_attribs << std::endl;

  for (size_t i = 0; i < num_attribs; i++) {
    const auto [total_size, key, value] = recreate_attrib(manager->cur);
    size_t size_attrib = total_size - (key.size() + 1 + sizeof(size_t));
    std::cout << "Import: attrib " << key << " imported" << std::endl;
    component->attrib.insert(
        unpack(size_attrib, unpack_default(size_attrib, {key, value})));

    manager->cur += total_size;
  }

  // ----- Import Children -----
  CopyVector *cp_children = (CopyVector *)(com->GetChildren());
  std::cout << "Import: cp->offset = " << cp_children->offset << std::endl;
  std::cout << "Import: children size = " << cp_children->size << std::endl;

  auto children_data = (size_t *)((char *)manager->mem + cp_children->offset);
  auto children_vec =
      vector<size_t>(children_data, children_data + cp_children->size);

  for (size_t offset : children_vec) {
    manager->cur = (char *)manager->mem + offset;
    component->InsertChild(import_recursive(manager, unpack));
  }

  return component;
}

Component *import_component(std::string path) {
  return import_component(
      path,
      [](size_t size, std::pair<std::string, void *> attrib)
          -> std::pair<std::string, void *> { return attrib; });
}

Component *import_component(std::string path,
                            std::pair<std::string, void *> (*unpack)(
                                size_t size, std::pair<std::string, void *>)) {
  MemoryManager *manager = new MemoryManager(path);

  Component *c = import_recursive(manager, unpack);
  std::cout << "Import: name = " << c->GetChildren()->at(0)->GetId()
            << std::endl;

  delete manager;
  return c;
}

std::tuple<size_t, std::string, void *> recreate_attrib(void *src) {
  char *cur = (char *)src;

  // Create key
  std::string key(cur);
  cur += key.size() + 1;

  // Read size of data
  size_t size = *(size_t *)cur;
  cur += sizeof(size_t);

  // Create Value
  void *val = malloc(size);
  if (!val) {
    std::cout << "recreate_attrib(): ERROR (malloc failed)\n";
    return {0, "", nullptr};  // TODO Error Handling?
  }
  memcpy(val, cur, size);

  std::cout << "recreate_attrib(): " << *((uint64_t *)val) << std::endl;

  size_t total_size = key.size() + 1 + sizeof(size_t) + size;
  return {total_size, key, val};
}

void *create_shared_memory(const std::string path, size_t size) {
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
  if (fd == -1) {
    perror("Error opening file for writing");
    exit(EXIT_FAILURE);
  }

  int res = lseek(fd, size - 1, SEEK_SET);
  if (res == -1) {
    close(fd);
    perror("Error calling lseek() to 'stretch' the file");
    exit(EXIT_FAILURE);
  }

  res = write(fd, "", 1);
  if (res != 1) {
    close(fd);
    perror("Error writing last byte of the file");
    exit(EXIT_FAILURE);
  }

  size_t *map =
      (size_t *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (map == MAP_FAILED) {
    close(fd);
    perror("Error mmapping the file");
    exit(EXIT_FAILURE);
  }

  std::cout << "Created shared memory at " << path << std::endl;

  // Adjust for "header"
  *map++ = size;

  return (void *)map;
}

void *open_shared_memory(const std::string path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    perror("Error opening file for reading");
    exit(EXIT_FAILURE);
  }

  // TODO Cleaner??
  size_t *tmp = (size_t *)mmap(0, sizeof(size_t), PROT_READ, MAP_SHARED, fd, 0);
  size_t size = *tmp;
  munmap(tmp, size);
  char *map = (char *)mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);

  std::cout << "OPEN_SHARED: " << size << std::endl;

  // char *map = (char *)mremap(size, sizeof(size_t), *size, MREMAP_MAYMOVE);

  if (map == MAP_FAILED) {
    close(fd);
    perror("Error mmapping the file");
    exit(EXIT_FAILURE);
  }

  // Adjust map for size "header"
  map += sizeof(size_t);
  std::cout << "HERE\n";

  std::cout << "Opened shared memory at " << path << std::endl;

  return map;
}