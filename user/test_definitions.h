//
// Created by giacomo on 21/03/21.
//

#ifndef TAG_SERVICE_TEST_DEFINITIONS_H
#define TAG_SERVICE_TEST_DEFINITIONS_H

#include <sys/wait.h>
#include <stdlib.h>
#include <sys/ipc.h> // IPC_PRIVATE
#include <stdio.h>
#include <unistd.h>
#include "../commands.h"

// Tutte le variabili e macro che seguono sono definite una sola volta!!!
int failures;
int success;
int tests;

#define SUCCESS success++; tests++; return 0 // Importante: NON USARE return SUCCESS!!
#define FAILURE failures++; tests++; return 0 // Importante: NON USARE return FAILURE!!


long tag_get(int key, int command, int permission);
long tag_send(int tag, int level, const char *buffer, size_t size);
long tag_receive(int tag, int level, char *buffer, size_t size);
long tag_ctl(int tag, int command);

// other
int expected_tag(int key);

#endif //TAG_SERVICE_TEST_DEFINITIONS_H
