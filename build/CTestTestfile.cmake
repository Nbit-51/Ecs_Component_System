# CMake generated Testfile for 
# Source directory: /home/navaneeth/CPSR/MiniProjects/ECS_System
# Build directory: /home/navaneeth/CPSR/MiniProjects/ECS_System/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[ecs_unit_tests]=] "/home/navaneeth/CPSR/MiniProjects/ECS_System/build/test_ecs")
set_tests_properties([=[ecs_unit_tests]=] PROPERTIES  PASS_REGULAR_EXPRESSION "\\[doctest\\] test cases: .* \\| 0 failed" TIMEOUT "30" _BACKTRACE_TRIPLES "/home/navaneeth/CPSR/MiniProjects/ECS_System/CMakeLists.txt;61;add_test;/home/navaneeth/CPSR/MiniProjects/ECS_System/CMakeLists.txt;0;")
