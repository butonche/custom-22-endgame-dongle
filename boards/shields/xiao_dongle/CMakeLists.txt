zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include)

target_sources(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/input_processor_6dof.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sixdof_serial.c
)
