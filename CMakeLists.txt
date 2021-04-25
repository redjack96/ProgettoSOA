cmake_minimum_required(VERSION 3.17)
set(PROJECT_NAME TAG-SERVICE)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 99)

#abilita syntax-highlighting
add_library(${PROJECT_NAME} tag_service.c tag_service.h commands.h lib/usctm.c lib/usctm.h lib/vtpm.c lib/vtpm.h tag_device_driver.c tag_device_driver.h)
add_executable(test
        user/user_tests.c user/user_tests.h
        commands.h
        user/tag_get_test.c user/tag_get_test.h
        user/tag_ctl_test.c user/tag_ctl_test.h
        user/tag_send_test.c user/tag_send_test.h
        user/tag_receive_test.c user/tag_receive_test.h
        user/test_definitions.c user/test_definitions.h)
add_executable(user user/user_program.c user/user_program.h user/test_definitions.c user/test_definitions.h)

#risolve i problemi di printk e MODULE_X
add_definitions(-D__KERNEL__ -DMODULE)

#Elimina la cartella cmake-build-debug nel caso rimanga la versione precedente dopo un aggiornamento.
# message(${CMAKE_HOST_SYSTEM_VERSION})

#trova le librerie
include_directories(PUBLIC /lib/modules/${CMAKE_HOST_SYSTEM_VERSION}/build/include/)
include_directories(PUBLIC /lib/modules/${CMAKE_HOST_SYSTEM_VERSION}/build/arch/x86/include/)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(test Threads::Threads)