//
// Created by giacomo on 21/03/21.
//

#ifndef TAG_SERVICE_TAG_RECEIVE_TEST_H
#define TAG_SERVICE_TAG_RECEIVE_TEST_H

#include "test_definitions.h"

int different_levels_not_receive_test1();
int multiple_receive_same_level_test2(int thread_number);
int multiple_receive_different_level_test3(int thread_number);
int multiple_receive_different_tag_same_level_test4(int thread_number);
int device_write_test5(int thread_number, int minuti);
int signal_test6();

#endif //TAG_SERVICE_TAG_RECEIVE_TEST_H
