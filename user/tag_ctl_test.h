//
// Created by giacomo on 21/03/21.
//

#ifndef TAG_SERVICE_TAG_CTL_TEST_H
#define TAG_SERVICE_TAG_CTL_TEST_H

#include "test_definitions.h"

int failed_removal_nonexistent_test1();
int not_owner_test2();
int awake_test3();
int failed_remove_waiting_thread_test4();
int failed_reopening_after_remove_test5();
int remove_send_create_concurrent_test6(int num_thread);
int repeated_failed_remove7();
int awake_multiple_tag_test8();
int change_permission_during_send_test9(int test);

#endif //TAG_SERVICE_TAG_CTL_TEST_H
