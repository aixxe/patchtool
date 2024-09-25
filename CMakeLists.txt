cmake_minimum_required(VERSION 3.28)

project(patchtool LANGUAGES CXX VERSION 0.1.0.0)

include(cmake/get_cpm.cmake)

CPMAddPackage(
    NAME              args
    GIT_TAG           6.4.6
    GITHUB_REPOSITORY Taywee/args
    OPTIONS           "ARGS_BUILD_EXAMPLE OFF" "ARGS_BUILD_UNITTESTS OFF"
)

CPMAddPackage(
    NAME              rapidjson
    GIT_TAG           3f73edae00aba5b0112a80b4d41e6f1ff7d92a3d
    GITHUB_REPOSITORY Tencent/rapidjson
    OPTIONS           "RAPIDJSON_BUILD_DOC OFF" "RAPIDJSON_BUILD_EXAMPLES OFF"
                      "RAPIDJSON_BUILD_TESTS OFF" "RAPIDJSON_BUILD_CXX17 ON"
)

CPMAddPackage(
    NAME              yaml-cpp
    GIT_TAG           0.8.0
    GITHUB_REPOSITORY jbeder/yaml-cpp
    OPTIONS           "YAML_CPP_BUILD_CONTRIB OFF" "YAML_CPP_BUILD_TOOLS OFF"
)

add_executable(patchtool
    src/patchtool.cc
)

target_link_libraries(patchtool PRIVATE args yaml-cpp)
target_compile_features(patchtool PRIVATE cxx_std_23)
target_precompile_headers(patchtool PRIVATE src/patchtool.h)
target_include_directories(patchtool PRIVATE ${rapidjson_SOURCE_DIR}/include)

set_property(TARGET patchtool PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)