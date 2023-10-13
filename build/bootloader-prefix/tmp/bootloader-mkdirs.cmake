# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/frameworks/esp-idf-v5.0/components/bootloader/subproject"
  "E:/documents/coding/active/esp32cam-idf/build/bootloader"
  "E:/documents/coding/active/esp32cam-idf/build/bootloader-prefix"
  "E:/documents/coding/active/esp32cam-idf/build/bootloader-prefix/tmp"
  "E:/documents/coding/active/esp32cam-idf/build/bootloader-prefix/src/bootloader-stamp"
  "E:/documents/coding/active/esp32cam-idf/build/bootloader-prefix/src"
  "E:/documents/coding/active/esp32cam-idf/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/documents/coding/active/esp32cam-idf/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/documents/coding/active/esp32cam-idf/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
