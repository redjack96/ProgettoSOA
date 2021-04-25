//
// Created by giacomo on 08/01/21.
//

#include "user_tests.h"

#define SUDO if(getuid() == 0 || geteuid() == 0) // I test preceduti da questa macro sono eseguiti solo se l'user e' root

void testsuite_tag_get() {
    istantiation_test1(0);
    istantiation_ipc_private_test2();
    multiple_istantiation_ipc_private_test3();
    no_more_tagservices4();
    failing_double_instantiation_test5();
    opening_test6();
    SUDO opening_only_owner_test7();
    failing_opening_IPC_PRIVATE_test8();
    failing_opening_nonexistent_service_test9();
    device_file_existence_test10();
}

void testsuite_tag_send() {
    send_test1();
    send_and_receive_long_message_test2(); // Handler CTRL+C
    concurrent_send_multilevel_test3();
    concurrent_send_same_level_test4();
    zero_size_send_test5();
}

void testsuite_tag_receive() {
    different_levels_not_receive_test1();
    multiple_receive_same_level_test2(3);
    multiple_receive_different_level_test3(3);
    multiple_receive_different_tag_same_level_test4(3);
    device_write_test5(4, 0);
    signal_test6();
}

void testsuite_tag_ctl() {
    failed_removal_nonexistent_test1();
    SUDO not_owner_test2();
    awake_test3();
    failed_remove_waiting_thread_test4();
    failed_reopening_after_remove_test5();
    remove_send_create_concurrent_test6(4); // provato con 30 thread e funziona
    repeated_failed_remove7();
    awake_multiple_tag_test8();
}

void all_tests() {
    testsuite_tag_get();
    testsuite_tag_send();
    testsuite_tag_receive();
    testsuite_tag_ctl();
}

/**
 * Problemi incontrati:
 * - i semafori con nome funzionano con i processi, quelli senza nome SOLO con i thread
 * - i semafori senza nome: quando si usa sem_post possono deschedulare i thread. Se poi un thread voleva andare a
 * dormire, chi aspetta che va a dormire per svegliarlo pensa che ci sia andato ed esegue il lavoro, poi quando il thread iniziale torna a
 * dormire non verra' mai svegliato. La soluzione sono __sync_fetch_and_add(&var,1) nei thread che vogliono dormire + while(var != TARGET_VALUE) nei thread che attendono per svegliare.
 * - Ho messo varie variabili statiche thread_receiving. Se qualcosa non funziona, ricorda di impostarle a 0 all'inizio di ogni test.
 * @return
 */
int main() {

    printf("user=%d, effective user = %d\n", getuid(), geteuid());
    all_tests();

#define CHECK_DEVICE 1
#if (CHECK_DEVICE == 1)
    int minuti = 1;
    printf("Test corretti fin'ora (%d/%d). Eseguo il test sul char device bloccante per %d minuti.\n", success, tests, minuti);
    /*
     * Il test si blocca per i minuti specificati. Dopodiche' per verificarne il funzionamento eseguire i seguenti comandi
     * cd /dev/
     * ls tsdev_205
     * cat tsdev_205
     * less -f tsdev_205
     * gli ultimi due comandi eseguirli sia prima che dopo un segnale POSIX, per non cancellare tsdev_205 e vedere come si azzerano i valori
     */
    device_write_test5(4, minuti);
#endif

    printf("---------------------------\nTest riusciti: \t%d/%d\n---------------------------\n", success,tests);
    return 0;
}
