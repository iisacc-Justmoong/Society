cmake_minimum_required(VERSION 3.24)
project(SocietyQuestionCoordinatorTests LANGUAGES CXX)
include(CTest)
add_executable(SocietyQuestionCoordinatorTests
    coordinator.cpp ../../App/Society/Native/NativeOverlayCoordinator.cpp)
target_compile_features(SocietyQuestionCoordinatorTests PRIVATE cxx_std_23)
target_include_directories(SocietyQuestionCoordinatorTests PRIVATE ../../App)
add_test(NAME Society.QuestionOverlayCoordinator COMMAND SocietyQuestionCoordinatorTests)
set_tests_properties(Society.QuestionOverlayCoordinator PROPERTIES TIMEOUT 30)
