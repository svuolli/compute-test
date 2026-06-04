add_executable(compute-test)
target_sources(
    compute-test
    PRIVATE
    main.cpp)
set_property(TARGET compute-test PROPERTY CXX_STANDARD 23)
target_link_libraries(
    compute-test
    PUBLIC
    glad
    SDL3::SDL3
    glm::glm)
