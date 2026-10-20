cmake_minimum_required(VERSION 3.16)
project(mobile_viewer CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MOBILE_VIEWER_SOURCES 
    "src/main.cpp"
)

# Branch target compilation rules dynamically based on configuration tags
if(ANDROID)
    add_library(mobile_viewer SHARED ${MOBILE_VIEWER_SOURCES})
    target_link_libraries(mobile_viewer PRIVATE 
        nano_prc SDL3-shared GLESv3 log android
    )
elseif(IOS)
    add_executable(mobile_viewer MACOSX_BUNDLE ${MOBILE_VIEWER_SOURCES})
    set_target_properties(mobile_viewer PROPERTIES
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
    )
    target_link_libraries(mobile_viewer PRIVATE nano_prc SDL3-shared)
else()
    # This captures your local Windows testing profile seamlessly!
    add_executable(mobile_viewer ${MOBILE_VIEWER_SOURCES})
    
    set(MOBILE_SYSTEM_LIBS)
    if(WIN32)
        list(APPEND MOBILE_SYSTEM_LIBS "opengl32")
    endif()
    
    target_link_libraries(mobile_viewer PRIVATE 
        nano_prc SDL3-shared ${MOBILE_SYSTEM_LIBS}
    )
endif()

target_include_directories(mobile_viewer PRIVATE "src")
