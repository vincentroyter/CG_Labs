add_executable(EDAN35_Project_Water)
target_compile_features(EDAN35_Project_Water PRIVATE cxx_std_17)

target_sources(
  EDAN35_Project_Water
  PRIVATE
    project_water.hpp
    project_water.cpp
    WaterHeightfield.hpp
    WaterHeightfield.cpp
    WaterMesh.hpp
    WaterMesh.cpp
    WaterRenderer.hpp
    WaterRenderer.cpp
    main.cpp
)

target_link_libraries(EDAN35_Project_Water PRIVATE assignment_setup)
install(TARGETS EDAN35_Project_Water DESTINATION bin)
copy_dlls(EDAN35_Project_Water "${CMAKE_CURRENT_BINARY_DIR}")
