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

size_t export_recursive(MemoryManager *manager, Component *component) {
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

  // ----- Export children -----
  auto size_children = component->GetChildren()->size();
  auto offset_children = (size_t)manager->cur - (size_t)manager->mem;

  std::cout << "Export: size_children = " << size_children
            << " | offset_children = " << offset_children << std::endl;

  size_t *children_data = (size_t *)manager->cur;
  manager->cur += size_children * sizeof(Component *);

  // Calculate offsets for children
  for (size_t i = 0; i < size_children; i++) {
    size_t o = export_recursive(manager, c->GetChildren()->at(i));

    std::cout << "Export: ID " << component->GetId()
              << " - Calculated Offset: " << o << std::endl;
    *(children_data++) = o;
  }

  // Create CopyVector for children
  CopyVector *cp = (CopyVector *)(c->GetChildren());
  *cp = CopyVector(offset_children, size_children);

  return self_offset;
}

/**
 * TODO:
 * - Determine whether space is sufficient?
 * - Error Handling?
 */
MemoryManager *export_component(std::string path, Component *component) {
  MemoryManager *manager = new MemoryManager(path, calc_memory_size(component));

  export_recursive(manager, component);
  return manager;
}

Component *import_recursive(MemoryManager *manager) {
  std::cout << "----- Import -----" << std::endl;
  std::cout << "Import: offset = "
            << ((size_t)manager->cur - (size_t)manager->mem) << std::endl;

  Component *com = (Component *)manager->cur;
  Component *component = nullptr;

  switch (com->GetComponentType()) {
    case SYS_SAGE_COMPONENT_THREAD:
      component = new Thread(*(Thread *)com);
      break;

    case SYS_SAGE_COMPONENT_CORE:
      component = new Core(*(Core *)com);
      break;

    case SYS_SAGE_COMPONENT_CACHE:
      component = new Cache(*(Cache *)com);
      break;

    case SYS_SAGE_COMPONENT_SUBDIVISION:
      component = new Subdivision(*(Subdivision *)com);
      break;

    case SYS_SAGE_COMPONENT_NUMA:
      component = new Numa(*(Numa *)com);
      break;

    case SYS_SAGE_COMPONENT_CHIP:
      component = new Chip(*(Chip *)com);
      break;

    case SYS_SAGE_COMPONENT_MEMORY:
      component = new Memory(*(Memory *)com);
      break;

    case SYS_SAGE_COMPONENT_STORAGE:
      component = new Storage(*(Storage *)com);
      break;

    case SYS_SAGE_COMPONENT_NODE:
      component = new Node(*(Node *)com);
      break;

    case SYS_SAGE_COMPONENT_TOPOLOGY:
      component = new Topology(*(Topology *)com);
      break;

    default:
      component = new Component(*com);
      break;
  }

  std::cout << "Import: component->id = " << component->GetId() << std::endl;

  CopyVector *cp_children = (CopyVector *)(com->GetChildren());
  std::cout << "Import: cp->offset = " << cp_children->offset << std::endl;
  std::cout << "Import: children size = " << cp_children->size << std::endl;

  auto children_data = (size_t *)((char *)manager->mem + cp_children->offset);
  auto children_vec =
      vector<size_t>(children_data, children_data + cp_children->size);

  for (size_t offset : children_vec) {
    manager->cur = (char *)manager->mem + offset;
    component->InsertChild(import_recursive(manager));
  }

  return component;
}

Component *import_component(std::string path) {
  MemoryManager *manager = new MemoryManager(path);

  Component *c = import_recursive(manager);
  std::cout << "Import: name = " << c->GetChildren()->at(0)->GetId()
            << std::endl;

  delete manager;
  return c;
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