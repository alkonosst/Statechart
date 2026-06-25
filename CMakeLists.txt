cmake_minimum_required(VERSION 3.14)
project(Statechart VERSION 0.2.0 LANGUAGES CXX)

# Create an INTERFACE library.
add_library(Statechart INTERFACE)

# Alias with author prefix. Consumers link "alkonosst::Statechart".
add_library(alkonosst::Statechart ALIAS Statechart)

# Specify the include directories for the library.
target_include_directories(Statechart INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Set a C++ standard requirement for the library. This will also propagate to consumers of the library.
target_compile_features(Statechart INTERFACE cxx_std_11)