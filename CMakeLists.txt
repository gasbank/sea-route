project(searoute)

add_subdirectory(libpng)
include_directories(libpng)


set(Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost 1.58 COMPONENTS system date_time filesystem regex REQUIRED )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )
if (MSVC)
    add_definitions(-D_WIN32_WINNT=0x0501 -D_CRT_NONSTDC_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -DBOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE)
endif ()

if (MSVC)
	#/wd4819: disable warning C4819: 'The file contains a character that cannot be represented in the current code page (949).'
	# /WX: treat warning as error
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4819 /WX")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4819 /WX")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

MACRO(ADD_MSVC_PRECOMPILED_HEADER PrecompiledHeader PrecompiledSource SourcesVar)
  IF(MSVC)
    GET_FILENAME_COMPONENT(PrecompiledBasename ${PrecompiledHeader} NAME_WE)
    SET(PrecompiledBinary "$(IntDir)/${PrecompiledBasename}.pch")
    SET(Sources ${${SourcesVar}})

    SET_SOURCE_FILES_PROPERTIES(${PrecompiledSource}
                                PROPERTIES COMPILE_FLAGS "/Yc\"${PrecompiledHeader}\" /Fp\"${PrecompiledBinary}\""
                                           OBJECT_OUTPUTS "${PrecompiledBinary}")
    SET_SOURCE_FILES_PROPERTIES(${Sources}
                                PROPERTIES COMPILE_FLAGS "/Yu\"${PrecompiledHeader}\" /FI\"${PrecompiledHeader}\" /Fp\"${PrecompiledBinary}\""
                                           OBJECT_DEPENDS "${PrecompiledBinary}")  
    # Add precompiled header to SourcesVar
    LIST(APPEND ${SourcesVar} ${PrecompiledSource})
  ENDIF(MSVC)
ENDMACRO(ADD_MSVC_PRECOMPILED_HEADER)

# pthread deps on Linux server build
find_package(Threads)

SET(SeaRouteSources
searoute.cpp
searoute.hpp
MaxMatch.cpp
MaxMatch.h)

ADD_MSVC_PRECOMPILED_HEADER("precompiled.hpp" "precompiled.cpp" SeaRouteSources)
ADD_EXECUTABLE(searoute ${SeaRouteSources} AStar.c AStar.h)
target_link_libraries(searoute png_static ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
