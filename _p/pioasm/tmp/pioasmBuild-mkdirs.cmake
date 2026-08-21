# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/claude/pico-sdk/tools/pioasm"
  "/home/claude/fix/pio-ftdi/_p/pioasm"
  "/home/claude/fix/pio-ftdi/_p/pioasm-install"
  "/home/claude/fix/pio-ftdi/_p/pioasm/tmp"
  "/home/claude/fix/pio-ftdi/_p/pioasm/src/pioasmBuild-stamp"
  "/home/claude/fix/pio-ftdi/_p/pioasm/src"
  "/home/claude/fix/pio-ftdi/_p/pioasm/src/pioasmBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/claude/fix/pio-ftdi/_p/pioasm/src/pioasmBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/claude/fix/pio-ftdi/_p/pioasm/src/pioasmBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
