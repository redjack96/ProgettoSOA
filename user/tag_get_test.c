//
// Created by giacomo on 21/03/21.
//
// Il test 'i' senza IPC_PRIVATE usano chiave i*10
//

#include <errno.h>
#include <semaphore.h>
#include <fcntl.h>
#include "tag_get_test.h"

/**
 * 1. TEST: Crea un tag_service a partire da una chiave diversa da IPC_PRIVATE e poi lo elimina.
 * @return
 */
int istantiation_test1(int wait) {
    long tag, ret;

    tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("istantiation_test1 (tag_get)");
        FAILURE;
    }

    if (wait) {
        sleep(3 * 60);
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("istantiation_test1 (tag_ctl)");
        FAILURE;
    }

    SUCCESS;
}

/**
 * 1. TEST: Crea un tag service IPC_PRIVATE e lo cancella
 */
int istantiation_ipc_private_test2() {
    long tag, ret;

    tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("istantiation_ipc_private_test2 (tag_get)");
        FAILURE;
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("istantiation_ipc_private_test2 (tag_ctl)");
        FAILURE;
    }
    SUCCESS;
}

/**
 * 1. Crea un tag service IPC_PRIVATE
 * 2. Crea di nuovo un tag_service IPC_PRIVATE
 * 3. Elimina i due tag_services.
 * 4. TEST: Controlla che i tag siano diversi
 */
int multiple_istantiation_ipc_private_test3() {
    long tag, tag2, ret;

    tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("multiple_istantiation_ipc_private_test3 (tag_get)");
        FAILURE;
    }

    tag2 = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag2 < 0) {
        perror("multiple_istantiation_ipc_private_test3 (tag_get2)");
        tag_ctl((int) tag, REMOVE_TAG); // rimuovo il primo tag_service nel caso fallisca la creazione del secondo
        FAILURE;
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("multiple_istantiation_ipc_private_test3 (tag_ctl)");
        FAILURE;
    }
    ret = tag_ctl((int) tag2, REMOVE_TAG);
    if (ret < 0) {
        perror("multiple_istantiation_ipc_private_test3 (tag_ctl)");
        FAILURE;
    }

    if (tag != tag2 && tag >= 0 && tag2 >= 0) {
        SUCCESS;
    } else {
        printf("================tag1=%ld, tag2=%ld\n", tag, tag2);
        FAILURE;
    }
}

/**
 * 1) Istanzia MAX_TAG_SERVICES tag_services
 * 2) Prova a istanziare un altro tag_service
 * 3) Elimina tutti i tag services istanziati
 * 4) TEST: Verifica che il tag service del punto 2) non venga istanziato
 * @return
 */
int no_more_tagservices4() {
    long tag, error = -1, ret;
    int i, created = 0;
    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE); //
        if (tag < 0 && errno != ENOSYS) goto remove;
        else if (tag < 0 && errno == ENOSYS) {
            perror("no_more_tagservices4");
            FAILURE;
        }
        created++;
    }

    error = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);

    remove:
    for (i = 0; i < created; i++) {
        ret = tag_ctl(i, REMOVE_TAG);
        if (ret < 0) {
            printf("no_more_tagservices4: ho provato a eliminare un tag service %d che non esiste...\n", i);
        }
    }

    // Controllo che il tag_service (MAX_TAG_SERVICES+1)-esimo non sia stato istanziato
    if (error >= 0) {
        printf("no_more_tagservices4: La tag_get ha avuto successo, ma lo spazio era finito.\n");
        FAILURE;
    }
    SUCCESS;
}

/**
 * 1. Istanzia un tag_service con una certa chiave.
 * 2. Prova a istanziarlo di nuovo con la stessa chiave
 * 3. TEST: Mi attendo che la seconda volta fallisca...
 * 4. Elimina il tag_service iniziale.
 * @return
 */
int failing_double_instantiation_test5() {
    long tag, tag2, ret;
    int key = 50;
    int expected = expected_tag(key);

    tag = tag_get(key, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("failing_double_instantiation_test5 (tag_get)");
        FAILURE;
    }

    tag2 = tag_get(key, CREATE_TAG, EVERYONE);
    if (tag2 == expected) {
        printf("failing_double_instantiation_test5: è stato creato due volte un tag service nello stesso punto");
        perror("failing_double_instantiation_test5 (tag_get2)");
        FAILURE; // Se viene creato il tag2, fallisce!!!
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("failing_double_instantiation_test5 (tag_ctl)");
        FAILURE;
    }

    SUCCESS;
}

/**
 * 1. Crea un tag_service con una certa chiave.
 * 2. TEST: Apre lo stesso tag_service con un altro processo grazie al tag ricevuto
 * 3. Il processo iniziale chiude il tag_service
 * @return
 */
int opening_test6() {
    long tag, tag2, ret, frk;
    int key = 60;
    int expected = expected_tag(key);

    tag = tag_get(key, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("opening_test6 (tag_get)");
        FAILURE;
    }

    frk = fork();
    if (frk == -1) {
        perror("opening_test6: errore nella fork");
        FAILURE;
    } else if (frk == 0) { // processo figlio: provo ad aprire il tag_service
        tag2 = tag_get(key, OPEN_TAG, EVERYONE);
        if (tag2 != expected) {
            printf("opening_test6: non è stato aperto un tag service attivo, creato dal processo padre");
            perror("opening_test6 (tag_get2)");
            FAILURE;
        }
        exit(0); // esce solo il processo figlio
    } else {
        wait(NULL);
        ret = tag_ctl((int) tag, REMOVE_TAG);
        if (ret < 0) {
            perror("opening_test6 (tag_ctl)");
            FAILURE;
        }
    }

    SUCCESS;
}

/**
 * [SUDO]
 * 1. Crea un Tag service con permission ONLY_OWNER
 * 2. Un processo figlio esegue seteuid e tenta di aprire lo stesso tag service.
 * 3. TEST: Il processo figlio deve fallire
 * 4. Il processo padre attende la terminazione del figlio e chiude il tag service
 */
int opening_only_owner_test7() {  // FUNZIONA SOLO SE L'UTENTE usa SUDO
    long tag, tag2, ret, frk;
    int status;
    int key = 70;

    tag = tag_get(key, CREATE_TAG, ONLY_OWNER);
    if (tag < 0) {
        perror("opening_only_owner_test7: (tag_get)");
        FAILURE;
    }

    frk = fork();
    if (frk == -1) {
        perror("opening_only_owner_test7: errore nella fork");
        FAILURE;
    } else if (frk == 0) {
        seteuid(1001); // <-------- funziona solo se ROOT
        tag2 = tag_get(key, OPEN_TAG, EVERYONE); // EVERYONE è ignorato perché sto aprendo

        if (tag2 < 0) {
            exit(EXIT_SUCCESS); // ok, non lo apre
        }
        exit(EXIT_FAILURE); // NON DOVEVA APRIRLO
    } else {
        wait(&status);
        ret = tag_ctl((int) tag, REMOVE_TAG);
        if (ret < 0) {
            perror("opening_only_owner_test7: tag_ctl");
            FAILURE;
        }
    }
    // WEXITSTATUS(status) identico a (status >> 8)
    if (WEXITSTATUS(status) == EXIT_FAILURE) {
        printf("opening_only_owner_test7: Il tag_service è stato aperto anche dal processo appartenente a un altro utente, oppure il processo apparteneva allo stesso utente");
        FAILURE;
    }
    SUCCESS;
}

/**
 * 1. Prova ad APRIRE un tag service utilizzando IPC_PRIVATE
 * 2. TEST: La system call deve restituire un errore
 * @return
 */
int failing_opening_IPC_PRIVATE_test8() {
    long tag;
    long tag2;
    long ret;
    // Questa chiamata deve funzionare
    tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("failing_opening_IPC_PRIVATE_test8");
        FAILURE;
    }

    // Questa no!
    tag2 = tag_get(IPC_PRIVATE, OPEN_TAG, 0); // 0 = EVERYONE, ma è ignorato
    if (tag2 < 0) {
        tag_ctl((int) tag, REMOVE_TAG); // Chiudo il primo tag
        SUCCESS;
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        FAILURE;
    }

    printf("failing_opening_IPC_PRIVATE_test8: il tag_service non può essere aperto con IPC_PRIVATE");
    SUCCESS;
}

/**
 * TEST: prova ad aprire un tag service non istanziato.
 * @return
 */
int failing_opening_nonexistent_service_test9() {
    long tag;
    tag = tag_get(90, OPEN_TAG, 0);
    if (tag >= 0) {
        perror("failing_opening_nonexistent_service_test9: il tag_service non dovrebbe essere aperto se ha chiave inesistente");
        FAILURE;
    } else if (tag < 0 && errno != ENOSYS) {
        SUCCESS;
    }
    perror("failing_opening_nonexistent_service_test9");
    FAILURE;
}

/**
 * Controlla se i nodi di I/O dei char device file vengono creati ed eliminati correttamente rispettivamente dopo tag_get e tag_receive.
 * 1. Crea un tag_service
 * 2. Test: controlla tramite la system call access() se la cartella del device driver esiste in /sys/class
 * 3. Test: controlla tramite la system call access() se il device file esiste
 * 4. Elimina il tag service
 * 5. Test: controlla tramite la system call access() se il device file non esiste
 * @return
 */
int device_file_existence_test10() {
    long tag, ret;
    int test1, test2, test3, test4, test5, err4;

    tag = tag_get(100, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("device_file_existence_test10: tag_get");
        FAILURE;
    }

    // esiste la cartella (class)?
    test1 = access("/sys/class/ts_class", F_OK);
    if (test1 == -1) {
        perror("device_file_existence_test10: errore nella prima access()");
        goto elimina;
    }
    // esiste il nodo di i/o?
    test2 = access("/sys/class/ts_class/tsdev_100", F_OK);
    if (test2 == -1) {
        perror("device_file_existence_test10: errore nella seconda access()");
        goto elimina;
    }

    test3 = access("/dev/tsdev_100", F_OK);
    if (test3 == -1) {
        perror("device_file_existence_test10: errore nella terza access()");
        goto elimina;
    }

    elimina:
    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("device_file_existence_test10: tag_ctl");
        FAILURE;
    }

    test4 = access("/sys/class/ts_class/tsdev_100", F_OK);
    err4 = errno;
    test5 = access("/dev/tsdev_100", F_OK);

    if (test1 == 0 && test2 == 0 && test3 == 0 && test4 == -1 && err4 == ENOENT && test5 == -1 &&
        errno == ENOENT) { // errno deve essere 2
        SUCCESS;
    }

    printf("device_file_existence_test10: test1 = %d, test2 = %d, test3 = %d, \ntest4 = %d, errno4 = %d, test5 = %d, errno5 = %d\n",
           test1, test2, test3, test4, err4, test5, errno);
    FAILURE;
}

/**
 * [SUDO]
 * 1) Creo un tag service con permessi everyone e con chiave fissata.
 * 2) Un thread/processo A vuole inviare un messaggio
 * 3) Un thread/processo B di a un utente DIVERSO invece vuole eliminare il tag service e ricrearlo con permessi ONLY_OWNER
 * @param test: può essere 1,2 o 3. Per default è 1.
 *  1 - B elimina il tag service prima che A invia, allora A deve fallire perché il tag service e' stato eliminato
 *  2 - B elimina il tag service e poi lo ricrea con only owner, A deve fallire perché non ha i permessi per accedere.
 *  3 - A invia il messaggio prima di B, allora tutte e tre le system call devono avere successo
 * @return
 */
/*
int change_permission_during_send_test11(int test) {
    long tag, frk, ret;
    int status;

    if (test != 1 && test != 2 && test != 3) test = 1;
    //sem_t *sem11;
    tag = tag_get(100, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("change_permission_during_send_test11: tag_get");
        FAILURE;
    }

    // sem11 = sem_open("sem_11", O_CREAT, 0666, 1);

    frk = fork();
    if (frk == -1) {
        perror("change_permission_during_send_test11: errore nella fork");
        FAILURE;
    } else if (frk == 0) {
        if (test == 3) sleep(1); // aspetta la send. Non fallisce niente
        seteuid(1001); // <-------- funziona solo se ROOT

        if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
            perror("change_permission_during_send_test11 (processo figlio): errore nella REMOVE_TAG");
            exit(EXIT_FAILURE);
        }

        if (test == 1) sleep(1); // aspetta che fallisca la send, dopo la remove

        if (tag_get(100, CREATE_TAG, ONLY_OWNER) < 0) {
            perror("change_permission_during_send_test11 (processo figlio): errore nella CREATE_TAG");
            exit(EXIT_FAILURE);
        }

        exit(EXIT_SUCCESS); // qui deve funzionare tutto
    } else {
        if (test != 3)
            sleep(test); // aspetta la ctl se test = 1, aspetta anche la get se test = 2. Altrimenti non aspetta.

        ret = tag_send((int) tag, 0, "ciao", 5);
        if (ret == -1) {
            switch (errno) { // nessuno deve ricevere
                case EACCES: // siamo nel caso 1. Devo aspettarmi che il tag esista ma non mi faccia accedere perche' non ho i permessi
                    ret = tag_get(100, OPEN_TAG, EVERYONE);
                    if (ret != -1) {
                        printf("change_permission_during_send_test11: Sono riuscito ad accedere a un tag service PUR NON AVENDO I PERMESSI!!!!!");
                        goto failure; // Poiche' in qualche modo sono riuscito ad accedere rimuovo il tag per tornare allo stato precedente
                    } else if (errno != EACCES) {
                        perror("change_permission_during_send_test11 GRAVE: non posso accedere al tag service, ma non perche' non ho i permessi...");
                        seteuid(1001);
                        goto failure;
                    }
                    break;
                case EBADF: // siamo nel caso 2. Devo aspettarmi che il tag sia stato eliminato, quindi controllo che lo sia davvero
                    ret = tag_get(100, OPEN_TAG, EVERYONE);
                    if (ret != -1) {
                        printf("change_permission_during_send_test11: Sono riuscito ad accedere a un tag service  eliminato!!!!!");
                        goto failure;
                    }
                    break;
                default:
                    perror("change_permission_during_send_test11 (processo padre): errore nella tag send");
                    // NON SERVE CAMBIARE UTENTE: altrimenti sarei nel caso 1
                    goto failure;
            }
        }
//        sem_post(sem11);
        wait(&status); // aspetto il processo figlio
        seteuid(1001); // qua serve cambiare utente...
        if (tag_ctl((int) tag, REMOVE_TAG) == -1) { // elimino per ristabilire lo stato precedente.
            perror("change_permission_during_send_test11: errore grave impossibile eliminare al termine del test");
            FAILURE;
        }
        if (WEXITSTATUS(status) == EXIT_FAILURE) {
            printf("change_permission_during_send_test11: il processo figlio ha fallito nella remove o nella create");
            FAILURE;
        }
    }
    SUCCESS;
failure:
    // FIXME: liberare i vari semafori prima di aspettare...
    //  sem_post(sem11);
    wait(&status);

    if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
        perror("change_permission_during_send_test11 GRAVE: impossibile eliminare il tag service dopo che sia tag_send che tag_get falliscono");
        FAILURE;
    }
    FAILURE;
}
*/


int change_permission_during_send_test11(int test) {
    long tag, frk, ret;
    int status;

    if (test != 1 && test != 2 && test != 3) test = 1;
    sem_t *sem11;
    sem_t *sem112;
    tag = tag_get(100, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("change_permission_during_send_test11: tag_get");
        FAILURE;
    }

    sem11 = sem_open("sem_11", O_CREAT, 0666, 0);
    if(test == 2) sem112 = sem_open("sem_112", O_CREAT, 0666, 0);

    frk = fork();
    if (frk == -1) {
        perror("change_permission_during_send_test11: errore nella fork");
        FAILURE;
    } else if (frk == 0) {

        seteuid(1001); // <-------- funziona solo se ROOT

        if(test == 1){
            // vai avanti
        } else if (test == 2){
            // vai avanti
//            sem_post(sem11); // aggiungi un gettone, ma non basta per far partire la send... Il gettone viene subito preso dalla send
        } else {
            sem_wait(sem11);
        }

        if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
            perror("change_permission_during_send_test11 (processo figlio): errore nella REMOVE_TAG");
            exit(EXIT_FAILURE);
        }

        if(test == 1){
            // vai avanti
        } else if (test == 2){
            sem_post(sem11); // permetti al processo padre di provare a inviare, ma poi aspetta che finisce l'invio
            sem_wait(sem112); // aspetta la send
        }


        if (tag_get(100, CREATE_TAG, ONLY_OWNER) < 0) {
            perror("change_permission_during_send_test11 (processo figlio): errore nella CREATE_TAG");
            exit(EXIT_FAILURE);
        }

        if(test == 1){
            sem_post(sem11); // Ho finito tutto ora tocca alla send
        } else if(test == 2){
            // non fare nulla, ci pensa il padre ad eliminare i semafori
        } else {
            // non fare nulla, ci pensa il padre ad eliminare i semafori
        }

        exit(EXIT_SUCCESS); // qui deve funzionare tutto
    } else {

        if(test == 1){
            sem_wait(sem11); // aspetto REMOVE e CREATE prima di inviare
        } else if (test == 2) {
            sem_wait(sem11); // mi servono due gettoni per andare avanti..
        } else {
            // vai avanti
        }

        ret = tag_send((int) tag, 0, "ciao", 5);
        if (ret == -1) {
            switch (errno) { // nessuno deve ricevere
                case EACCES: // siamo nel caso 1. Devo aspettarmi che il tag esista ma non mi faccia accedere perche' non ho i permessi
                    ret = tag_get(100, OPEN_TAG, EVERYONE);
                    if (ret != -1) {
                        printf("change_permission_during_send_test11: Sono riuscito ad accedere a un tag service PUR NON AVENDO I PERMESSI!!!!!");
                        goto failure; // Poiche' in qualche modo sono riuscito ad accedere rimuovo il tag per tornare allo stato precedente
                    } else if (errno != EACCES) {
                        perror("change_permission_during_send_test11 GRAVE: non posso accedere al tag service, ma non perche' non ho i permessi...");
                        goto failure;
                    }
                    break;
                case EBADF: // siamo nel caso 2. Devo aspettarmi che il tag sia stato eliminato, quindi controllo che lo sia davvero
                    ret = tag_get(100, OPEN_TAG, EVERYONE);
                    if (ret != -1) {
                        printf("change_permission_during_send_test11: Sono riuscito ad accedere a un tag service  eliminato!!!!!");
                        goto failure;
                    }
                    break;
                default:
                    perror("change_permission_during_send_test11 (processo padre): errore nella tag send");
                    // NON SERVE CAMBIARE UTENTE: altrimenti sarei nel caso 1
                    goto failure;
            }
        }
        if(test == 1){
            // non mi interessa quanti gettoni ha il semaphore... lo chiudo e basta
            sem_unlink("sem_11"); // vedi /dev/shm per vedere se rimane li'
            sem_close(sem11);
        } else if(test == 2){
            sem_post(sem112);
        } else {
            sem_post(sem11);
        }

//        sem_post(sem11);
        wait(&status); // aspetto il processo figlio

        if(test == 2){
            sem_unlink("sem_11"); // vedi /dev/shm per vedere se rimane li'
            sem_unlink("sem_112"); // vedi /dev/shm per vedere se rimane li'
            sem_close(sem11);
            sem_close(sem112);
        } else if(test == 3){
            sem_unlink("sem_11");
            sem_close(sem11);
        }

        seteuid(1001); // qua serve cambiare utente... perche' il processo figlio ha creato un tag con utente 1001
        if (tag_ctl((int) tag, REMOVE_TAG) == -1) { // elimino per ristabilire lo stato precedente.
            perror("change_permission_during_send_test11: errore grave impossibile eliminare al termine del test");
            FAILURE;
        }
        if (WEXITSTATUS(status) == EXIT_FAILURE) {
            printf("change_permission_during_send_test11: il processo figlio ha fallito nella remove o nella create");
            FAILURE;
        }
    }
    SUCCESS;
    failure:

    if(test == 1){
        sem_unlink("sem_11");
        sem_close(sem11);
    } else if(test == 2){
        sem_unlink("sem_11"); // vedi /dev/shm per vedere se rimane li'
        sem_unlink("sem_112"); // vedi /dev/shm per vedere se rimane li'
        sem_close(sem11);
        sem_close(sem112);
    } else {
        sem_unlink("sem_11");
        sem_close(sem11);
    }

    // FIXME: liberare i vari semafori prima di aspettare...
    //  sem_post(sem11);
    wait(&status);
    seteuid(1001);
    if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
        perror("change_permission_during_send_test11 GRAVE: impossibile eliminare il tag service dopo che sia tag_send che tag_get falliscono");
        FAILURE;
    }
    FAILURE;
}