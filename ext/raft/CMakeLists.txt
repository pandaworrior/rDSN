include(ExternalProject)

set(PROJ_NAME raft)
set(RAFT_INSTALL_PREFIX "${PROJECT_BINARY_DIR}")

set (RAFT_CMAKE_ARGS
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE};"
    "-DCMAKE_INSTALL_PREFIX=${RAFT_INSTALL_PREFIX};"
)

ExternalProject_Add(${PROJ_NAME}
    GIT_REPOSITORY https://github.com/pandaworrior/raft.git
    CMAKE_ARGS ${RAFT_CMAKE_ARGS}
)

ExternalProject_Get_Property(${PROJ_NAME} SOURCE_DIR)
ExternalProject_Get_Property(${PROJ_NAME} BINARY_DIR)

set(RAFT_INCLUDE_DIR ${SOURCE_DIR}/include PARENT_SCOPE)
set(RAFT_LIB_DIR ${BINARY_DIR}/lib PARENT_SCOPE)
