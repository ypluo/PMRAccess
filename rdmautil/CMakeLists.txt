find_library(IBVERB_LIBRARIES NAMES ibverbs)
if(NOT IBVERB_LIBRARIES)
  message(FATAL_ERROR "IBVERB_LIBRARIES library not found!")
endif()

add_library(rdma SHARED rdmautil.cc socketutil.cc)
target_link_libraries(rdma ibverbs)