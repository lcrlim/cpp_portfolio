file(READ "CMakeLists.txt" content)
string(REPLACE "cmake_minimum_required(VERSION 3.4)" "cmake_minimum_required(VERSION 3.5)" content "${content}")
file(WRITE "CMakeLists.txt" "${content}")