set(FRONT_SRC pmr/pmr.cc group/group.cc pmem/pmem.cc)
add_library(frontend ${FRONT_SRC})
target_link_libraries(frontend rdma)