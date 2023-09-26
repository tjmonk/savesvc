cmake_minimum_required(VERSION 3.10)

project(savesvc
	VERSION 0.1
    DESCRIPTION "Variable Save Service"
)

include(GNUInstallDirs)

add_executable( ${PROJECT_NAME}
    src/savesvc.c
)

target_link_libraries( ${PROJECT_NAME}
	varserver
)

target_include_directories( ${PROJECT_NAME} PRIVATE
	.
	inc
	${CMAKE_BINARY_DIR} )

target_compile_options( ${PROJECT_NAME}
	PUBLIC
	-Wall
	-Wextra
	-Wpedantic
	-Werror
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} )

