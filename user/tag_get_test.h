//
// Created by giacomo on 21/03/21.
//

#ifndef TAG_SERVICE_TAG_GET_TEST_H
#define TAG_SERVICE_TAG_GET_TEST_H

#include "test_definitions.h"

int istantiation_test1(int wait);
int istantiation_ipc_private_test2();
int multiple_istantiation_ipc_private_test3();
int no_more_tagservices4();
int failing_double_instantiation_test5();
int opening_test6();
int opening_only_owner_test7();
int failing_opening_IPC_PRIVATE_test8();
int failing_opening_nonexistent_service_test9();
int device_file_existence_test10();



#endif //TAG_SERVICE_TAG_GET_TEST_H
