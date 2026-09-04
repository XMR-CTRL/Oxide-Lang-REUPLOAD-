cmake_minimum_required(VERSION 3.15)
project(oxide CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The oxide compiler itself is pure standard C++17. LLVM is only used out of
# process (clang consumes the ir the compiler emits), so nothing is linked
# against LLVM here. A clang or clang-cl must be on PATH at runtime.

set(OXIDE_SRC
  src/main.cpp
  src/Lexer.cpp
  src/Parser.cpp
  src/Sema.cpp
  src/Types.cpp
  src/IRGen.cpp
  src/Driver.cpp
  src/Ghost.cpp
  src/ProofSplitter.cpp
  src/ProofDispatch.cpp
)

add_executable(oxide ${OXIDE_SRC})

if(MSVC)
  target_compile_options(oxide PRIVATE /W3 /utf-8)
  target_link_options(oxide PRIVATE /STACK:8388608)
else()
  target_compile_options(oxide PRIVATE -Wall -Wno-unused -Wno-unused-but-set-variable)
endif()
