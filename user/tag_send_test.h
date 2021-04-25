//
// Created by giacomo on 21/03/21.
//

#ifndef TAG_SERVICE_TAG_SEND_TEST_H
#define TAG_SERVICE_TAG_SEND_TEST_H

#include "test_definitions.h"

int send_test1();
int send_and_receive_long_message_test2();
int concurrent_send_multilevel_test3();
int concurrent_send_same_level_test4(); // importante la linearizzazione
int zero_size_send_test5();
#endif //TAG_SERVICE_TAG_SEND_TEST_H
