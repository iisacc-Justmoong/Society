if(NOT IIMULTIMODALAGENT_VIEWS_DIR OR NOT EXISTS "${IIMULTIMODALAGENT_VIEWS_DIR}/UserQuestionsSheet.lvui")
    message(FATAL_ERROR "The selected iiMultimodalAgent package has no UserQuestionsSheet.lvui view")
endif()
lvui_generate(NAME UserQuestionsSheet SOURCE "${IIMULTIMODALAGENT_VIEWS_DIR}/UserQuestionsSheet.lvui"
    FRAMEWORK iiGUI INCLUDES iiMultimodalAgent/agent/NativeQuestionViewModel.h
    OUTPUT_VARIABLE _society_native_question_view)
include(cmake/NativeOverlay.cmake)
target_sources(Society PRIVATE
    App/Society/Mcp/NativeQuestionPresenter.h App/Society/Mcp/NativeQuestionPresenter.cpp
    App/Society/Native/OverlayShutdown.h
    "${_society_native_question_view}")
target_include_directories(Society PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Society PRIVATE LVUI::Native iiGUI::iiGUI Qt6::Gui)
target_compile_definitions(Society PRIVATE SOCIETY_NATIVE_QUESTION_PRESENTER=1)
