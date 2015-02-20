# raspimjpg
cmake_minimum_required(VERSION 2.0)

SET(VC_DIR /opt/vc CACHE STRING "Path to VideoCore base directory")
SET(COMPILE_DEFINITIONS -Werror)

include_directories(${VC_DIR}/include)
include_directories(${VC_DIR}/include/interface/vcos/pthreads)
include_directories(${VC_DIR}/include/interface/vmcs_host/linux)
link_directories(${VC_DIR}/lib)

add_executable(raspimjpg RaspiMJPEG.c)

set (MMAL_LIBS mmal_core mmal_util mmal_vc_client)

target_link_libraries(raspimjpg ${MMAL_LIBS} vcos bcm_host)

install(TARGETS raspimjpg RUNTIME DESTINATION bin)
