cmake_minimum_required(VERSION 3.12)

set(VSP_PROJECT_NAME VSP_App)

add_executable(${VSP_PROJECT_NAME}
    src/AppVSP.cpp
    src/VSPModel.cpp
    src/VSPRCSPSolver.cpp
#    src/VSPData.cpp
)

target_compile_definitions(${VSP_PROJECT_NAME} PRIVATE BCP_RCSP_IS_FOUND)

# Rutas hardcodeadas (Boost y LEMON no propagan al subproyecto en esta config de CMake)
set(BAPCOD_BOOST_INCLUDE "${CMAKE_SOURCE_DIR}/Tools/boost_1_76_0/build/include")
set(BAPCOD_LEMON_INCLUDE "${CMAKE_SOURCE_DIR}/Tools/lemon-1.3.1/build/include")

target_include_directories(${VSP_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Bapcod/include
    ${CMAKE_SOURCE_DIR}/Bapcod/include_dev
    ${BCP_RCSP_INCLUDE_DIR}
    ${PROJECT_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${BAPCOD_BOOST_INCLUDE}
    ${BAPCOD_LEMON_INCLUDE}
)

target_link_libraries(${VSP_PROJECT_NAME}
    bapcod
    ${BCP_RCSP_LIBRARY}
    ${ADDITIONAL_LIBRARIES}
)

if(ADDITIONAL_LIBRARY_DIRS)
    target_link_directories(${VSP_PROJECT_NAME} PRIVATE ${ADDITIONAL_LIBRARY_DIRS})
endif()

if(BCP_RCSP_LIBRARY_DIRS)
    target_link_directories(${VSP_PROJECT_NAME} PRIVATE ${BCP_RCSP_LIBRARY_DIRS})
endif()
