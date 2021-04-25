//
// Created by giacomo on 21/03/21.
//


#include "test_definitions.h"

#define __NR_tag_get 134        // __NR_uselib
#define __NR_tag_send 174       // __NR_create_module
#define __NR_tag_receive 177    // __NR_putpmsg
#define __NR_tag_ctl 178    // __NR_afs_syscall

/**
 * Istanzia o apre un nuovo tag_service
 * @param key IPC_PRIVATE per creare senza problemi. Un intero che viene tradotto in tag per aprire.
 * La chiave deve essere nota ad altri thread per aprire lo stesso tag_service
 * @param command CREATE_TAG o OPEN_TAG (fallisce con IPC_PRIVATE)
 * @param permission EVERYONE o ONLY_OWNER
 * @return descrittore del tag_service >= 0. Altrimenti un errore.
 */
long tag_get(int key, int command, int permission) {
    return (long) syscall(__NR_tag_get, key, command, permission);
}

/**
 * Invia un messaggio a tutti i thread in attesa su un certo tag service, ad un certo livello
 * @param tag
 * @param level
 * @param buffer
 * @param size
 * @return
 */
long tag_send(int tag, int level, const char *buffer, size_t size) {
    return (long) syscall(__NR_tag_send, tag, level, buffer, size);
}

/**
 * Bloccante. Riceve un messaggio da un certo tag service e livello.
 * @param tag
 * @param level
 * @param buffer
 * @param size
 * @return
 */
long tag_receive(int tag, int level, char *buffer, size_t size) {
    return (long) syscall(__NR_tag_receive, tag, level, buffer, size);
}

/**
 * Permette di rimuovere un tag_service o svegliare tutti i thread in attesa di ricevere un messaggio da esso
 * @param tag
 * @param command REMOVE_TAG o AWAKE_ALL
 * @return
 */
long tag_ctl(int tag, int command) {
    return (long) syscall(__NR_tag_ctl, tag, command);
}

/**
 * Esegue l'algoritmo di hashing (di tag_get) per trasformare la chiave in tag
 * @return tag atteso.
 */
int expected_tag(int key) {
    return key % MAX_TAG_SERVICES;
}