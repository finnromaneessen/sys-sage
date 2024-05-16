#include "shared_mem.hpp"

SharedMemory::SharedMemory(std::string path, size_t size)
    : size(size), path(path) {
  mem = create_shared_memory(path, size);
  cur = (char *)mem;
}

SharedMemory::SharedMemory(std::string path) {
  mem = open_shared_memory(path);
  cur = (char *)mem;

  if (mem) {
    size = *((size_t *)mem - 1);
  }
}

size_t attrib_memory_size(std::map<std::string, void *> *attribs,
                          CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  size_t size = 0;
  for (const auto &a : *attribs) {
    auto [key, size_attrib, ptr] = pack(a);
    size += size_attrib + key.size() + sizeof(size_t);
  }
  return size;
}

size_t size_attribs_recursive(
    Component *comp, CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  size_t size = attrib_memory_size(&(comp->attrib), pack);

  for (const auto &dp : *(comp->GetDataPaths(SYS_SAGE_DATAPATH_OUTGOING))) {
    size += attrib_memory_size(&(dp->attrib), pack);
    size += 2 * sizeof(size_t);  // Account for DataPath copy overhead
  }

  for (const auto &dp : *(comp->GetDataPaths(SYS_SAGE_DATAPATH_INCOMING))) {
    size += attrib_memory_size(&(dp->attrib), pack);
    size += 2 * sizeof(size_t);  // Account for DataPath copy overhead
  }

  for (const auto &c : *(comp->GetChildren())) {
    size += size_attribs_recursive(c, pack);
  }

  return size;
}

size_t calc_memory_size(Component *tree,
                        CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  unsigned a, b;
  size_t min_size = tree->GetTopologySize(&a, &b);
  min_size += size_attribs_recursive(tree, pack);

  size_t tmp = (min_size + PAGE_SIZE - 1) & -PAGE_SIZE;

  return tmp;
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
    return {0, "", nullptr};
  }

  memcpy(val, cur, size);

  size_t total_size = key.size() + 1 + sizeof(size_t) + size;
  return {total_size, key, val};
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

  if (!key.compare("GPU_Clock_Rate")) {
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

    std::vector<std::tuple<long long, double>> *vec =
        new std::vector<std::tuple<long long, double>>(
            (std::tuple<long long, double> *)value,
            (std::tuple<long long, double> *)end);

    free(value);
    return {key, vec};
  }

  return {key, value};
}

void export_attribs(SharedMemory *manager,
                    CopyAttrib (*pack)(std::pair<std::string, void *>),
                    std::map<std::string, void *> *attribs) {
  size_t *num_attribs = (size_t *)manager->cur;
  *num_attribs = 0;
  manager->cur += sizeof(size_t);

  CopyAttrib attrib;

  for (auto const &a : *attribs) {
    if (attrib = pack_default(a); attrib.size != 0) {
      (*num_attribs)++;
      manager->cur += attrib.copy(manager->cur);
    } else if (attrib = pack(a); attrib.size != 0) {
      (*num_attribs)++;
      manager->cur += attrib.copy(manager->cur);
    }
  }
}

size_t export_recursive(SharedMemory *manager, Component *component,
                        CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  // Get own size
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

  // Get own offset
  auto self_offset = (size_t)manager->cur - (size_t)manager->mem;

  // Copy Class and initialize Component
  memcpy(manager->cur, (void *)component, size_comp);
  Component *c = (Component *)manager->cur;
  manager->cur += size_comp;

  // ----- DataPaths -----
  for (DataPath *dp : *(component->GetDataPaths(SYS_SAGE_DATAPATH_OUTGOING))) {
    if (auto search = manager->dp_offsets.find(dp);
        search != manager->dp_offsets.end()) {
      if (search->second.first == ~0 && search->second.second != self_offset) {
        search->second.first = self_offset;
      }
    } else {
      manager->dp_offsets[dp] = {self_offset, ~0};
    }
  }

  for (DataPath *dp : *(component->GetDataPaths(SYS_SAGE_DATAPATH_INCOMING))) {
    if (auto search = manager->dp_offsets.find(dp);
        search != manager->dp_offsets.end()) {
      if (search->second.second == ~0 && search->second.first != self_offset) {
        search->second.second = self_offset;
      }
    } else {
      manager->dp_offsets[dp] = {~0, self_offset};
    }
  }

  // ----- Export attribs -----
  export_attribs(manager, pack, &(component->attrib));

  // ----- Export children -----
  auto size_children = component->GetChildren()->size();
  auto offset_children = (size_t)manager->cur - (size_t)manager->mem;

  size_t *children_data = (size_t *)manager->cur;
  manager->cur += size_children * sizeof(Component *);

  // Calculate offsets for children
  for (size_t i = 0; i < size_children; i++) {
    *(children_data++) =
        export_recursive(manager, c->GetChildren()->at(i), pack);
  }

  // Create CopyVector for children
  CopyVector *cp = (CopyVector *)(c->GetChildren());
  *cp = CopyVector(offset_children, size_children);

  return self_offset;
}

void export_datapaths(SharedMemory *manager,
                      CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  size_t *num_dp = (size_t *)(manager->cur);
  *num_dp = 0;
  manager->cur += sizeof(size_t);

  for (auto const &[dp, offsets] : manager->dp_offsets) {
    // Only export DataPaths when both Components are exported
    if (offsets.first == ~0 || offsets.second == ~0) {
      continue;
    }
    (*num_dp)++;

    // ----- Write offsets -----
    size_t *tmp = (size_t *)(manager->cur);
    *(tmp++) = offsets.first;
    *tmp = offsets.second;
    manager->cur += 2 * sizeof(size_t);

    // ----- Export DataPath -----
    memcpy(manager->cur, (void *)dp, sizeof(DataPath));
    manager->cur += sizeof(DataPath);

    // ----- Export attribs -----
    export_attribs(manager, pack, &(dp->attrib));
  }
}

SharedMemory *export_topology(
    std::string path, Component *component,
    CopyAttrib (*pack)(std::pair<std::string, void *>)) {
  SharedMemory *manager =
      new SharedMemory(path, calc_memory_size(component, pack));

  //----- Error Handling -----
  if (!(manager->mem)) {
    return nullptr;
  }

  //----- Export -----
  export_recursive(manager, component, pack);
  export_datapaths(manager, pack);
  return manager;
}

SharedMemory *export_topology(std::string path, Component *component) {
  return export_topology(
      path, component, [](std::pair<std::string, void *> attrib) -> CopyAttrib {
        return {attrib.first, 0, nullptr};
      });
}

void import_attribs(SharedMemory *manager,
                    std::map<std::string, void *> *attribs,
                    std::pair<std::string, void *> (*unpack)(
                        size_t size, std::pair<std::string, void *>)) {
  size_t num_attribs = *(size_t *)manager->cur;
  manager->cur += sizeof(size_t);

  for (size_t i = 0; i < num_attribs; i++) {
    const auto [total_size, key, value] = recreate_attrib(manager->cur);
    size_t size_attrib = total_size - (key.size() + 1 + sizeof(size_t));

    attribs->insert(
        unpack(size_attrib, unpack_default(size_attrib, {key, value})));

    manager->cur += total_size;
  }
}

void import_datapaths(SharedMemory *manager,
                      std::pair<std::string, void *> (*unpack)(
                          size_t size, std::pair<std::string, void *>)) {
  size_t num_dp = *(size_t *)manager->cur;
  manager->cur += sizeof(size_t);

  for (auto i = 0; i < num_dp; i++) {
    // ----- Translate component offsets -----
    Component *in = manager->comp_offsets[*(size_t *)manager->cur];
    manager->cur += sizeof(size_t);

    Component *out = manager->comp_offsets[*(size_t *)manager->cur];
    manager->cur += sizeof(size_t);

    // ----- Import DataPath -----
    DataPath *tmp = (DataPath *)manager->cur;
    DataPath *dp = new DataPath(in, out, tmp->GetOriented(), tmp->GetDpType(),
                                tmp->GetBw(), tmp->GetLatency());

    manager->cur += sizeof(DataPath);
    // ----- Import attribs -----
    import_attribs(manager, &(dp->attrib), unpack);
  }
}

Component *import_recursive(SharedMemory *manager,
                            std::pair<std::string, void *> (*unpack)(
                                size_t size, std::pair<std::string, void *>)) {
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
  // ----- Import DataPaths -----
  auto self_offset = (size_t)manager->cur - (size_t)manager->mem;
  manager->comp_offsets.insert({self_offset, component});

  manager->cur += size_comp;

  // ----- Import attribs -----
  import_attribs(manager, &(component->attrib), unpack);

  // ----- Import Children -----
  CopyVector *cp_children = (CopyVector *)(com->GetChildren());
  auto children_data = (size_t *)((char *)manager->mem + cp_children->offset);

  for (size_t i = 0; i < cp_children->size; i++) {
    manager->cur = (char *)manager->mem + *(children_data++);
    component->InsertChild(import_recursive(manager, unpack));
  }

  return component;
}

Component *import_topology(std::string path) {
  return import_topology(
      path,
      [](size_t size, std::pair<std::string, void *> attrib)
          -> std::pair<std::string, void *> { return attrib; });
}

Component *import_topology(std::string path,
                           std::pair<std::string, void *> (*unpack)(
                               size_t size, std::pair<std::string, void *>)) {
  SharedMemory *shmem = new SharedMemory(path);

  // ----- Error Handling -----
  if (!(shmem->mem)) {
    return nullptr;
  }

  // ----- Import -----
  Component *component = import_recursive(shmem, unpack);
  import_datapaths(shmem, unpack);

  delete shmem;
  return component;
}

void *create_shared_memory(const std::string path, size_t size) {
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
  if (fd == -1) {
    return nullptr;
  }

  int res = lseek(fd, size - 1, SEEK_SET);
  if (res == -1) {
    close(fd);
    return nullptr;
  }

  res = write(fd, "", 1);
  if (res != 1) {
    close(fd);
    return nullptr;
  }

  size_t *map =
      (size_t *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (map == MAP_FAILED) {
    close(fd);
    return nullptr;
  }

  close(fd);

  // Adjust for "header"
  *map++ = size;

  return (void *)map;
}

void *open_shared_memory(const std::string path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    return nullptr;
  }

  // Create mapping, read size and remap
  size_t *tmp = (size_t *)mmap(0, sizeof(size_t), PROT_READ, MAP_SHARED, fd, 0);
  size_t size = *tmp;
  munmap(tmp, sizeof(size_t));
  char *map = (char *)mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);

  if (map == MAP_FAILED) {
    close(fd);
    return nullptr;
  }

  close(fd);

  // Adjust map for size "header"
  map += sizeof(size_t);
  return map;
}