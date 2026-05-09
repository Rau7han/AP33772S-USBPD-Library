# CMakeLists.txt — Spark Analyzer ESP-IDF Firmware
cmake_minimum_required(VERSION 3.16)

# Set target before including ESP-IDF
set(IDF_TARGET esp32)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SparkAnalyzer)
