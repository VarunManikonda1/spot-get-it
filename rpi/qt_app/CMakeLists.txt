cmake_minimum_required(VERSION 3.19)
project(DisasterControlQt LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets)

qt_standard_project_setup()

qt_add_executable(DisasterControlQt
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/robotshmconnection.cpp
    src/robotshmconnection.h
    src/robotworkerthread.cpp
    src/robotworkerthread.h
    src/shmmonitor.cpp
    src/shmmonitor.h
    src/shmtypes.h
    src/mapdata.cpp
    src/mapdata.h
    src/dashboardwidgets.cpp
    src/dashboardwidgets.h
)

qt_add_resources(DisasterControlQt "robot_assets"
    PREFIX "/"
    FILES
        assets/logo.png
        assets/spot_robot.png
)

target_include_directories(DisasterControlQt PRIVATE ../bridge_app)

target_link_libraries(DisasterControlQt
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        pthread
        rt
)

install(TARGETS DisasterControlQt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
