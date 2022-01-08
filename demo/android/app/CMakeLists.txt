cmake_minimum_required(VERSION 3.4.1)

set(lib_DIR ${CMAKE_SOURCE_DIR}/libs)

include_directories(${CMAKE_SOURCE_DIR}/includes)

add_library( MNN SHARED IMPORTED )
set_target_properties(
        MNN
        PROPERTIES IMPORTED_LOCATION
        ${lib_DIR}/${ANDROID_ABI}/libMNN.so
)

add_library( MNN_Vulkan SHARED IMPORTED )
set_target_properties(
        MNN_Vulkan
        PROPERTIES IMPORTED_LOCATION
        ${lib_DIR}/${ANDROID_ABI}/libMNN_Vulkan.so
)

file(GLOB_RECURSE CPP_SRCS src/main/jni/*.cpp )
add_library( mnncore SHARED ${CPP_SRCS} )

find_library( log-lib log )
find_library( jnigraphics-lib jnigraphics )

add_definitions(-DMNN_USE_LOGCAT)
target_link_libraries( mnncore MNN MNN_Vulkan ${log-lib} ${jnigraphics-lib})
