//
// Created by giacomo on 21/03/21.
//
// Il test 'i' senza IPC_PRIVATE usa la chiave 100+i
//

#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>
#include "tag_ctl_test.h"

#define OK (void *) 1
#define NOT_OK (void *) 0

/* ************ Thread functions ************ */

static int thread_receiving3;
static int thread_receiving8;

/**
 * Va in ricezione su un certo livello e aspetta di essere svegliato
 * @param lev
 * @return
 */
void *thread_funct_awake_test3(void *lev) {
    long level = (long) lev;
    long ret;
    int tag = expected_tag(103);
    char buffer[10];

    __sync_fetch_and_add(&thread_receiving3, 1);
    ret = tag_receive(tag, (int) level, buffer, MAX_MESSAGE_SIZE);

    if (ret < 0 && errno == EINTR) {
        return OK; // OK
    }
    return NOT_OK; // No
}


sem_t sem6;
typedef struct the_thread_data {
    int mode;
    int tag;
} thread_data;

void *thread_func6(void *input) {
    thread_data *args = (thread_data *) input;
    long ret;

    sem_wait(&sem6);
    // printf("eseguo %s\n", (args->mode == 0?"REMOVE_TAG":"SEND"));
    if (args->mode == 0) {
        ret = tag_ctl(args->tag, REMOVE_TAG);
        /*if (ret == -1) {
            char *string = malloc(sizeof(char) * 30);
            sprintf(string, "errore nel thread remove %ld", (long) syscall(__NR_gettid));
            perror(string);
        }*/
    } else if (args->mode == 1) {
        ret = tag_send(args->tag, 0, "messaggio", 10);
        /*if (ret == -1) {
            char *string = malloc(sizeof(char) * 30);
            sprintf(string, "errore nel thread send %ld", (long) syscall(__NR_gettid));
            perror(string);
        }*/
    } else {
        ret = tag_get(106, CREATE_TAG, EVERYONE);
    }
    free((thread_data *) input);
    return (void *) ret;
}

/* ************ Test functions *********** */

/**
 * Tenta di rimuovere un tag_service inesistente.
 * @return
 */
int failed_removal_nonexistent_test1() {
    long res;
    int key = 101;
    res = tag_ctl(key, REMOVE_TAG);
    if (res == 1) { //  se tag_ctl ha successo, il test fallisce
        perror("failed_removal_nonexistent_test1: Il res 999 non esiste e non sarebbe dovuto essere eliminato");
        FAILURE;
    } else if (res < 0 && errno != EBADF) {
        perror("failed_removal_nonexistent_test1: tag_ctl");
        FAILURE;
    }
    SUCCESS;
}

/**
 * [SUDO] Un utente diverso dal proprietario prova a utilizzare un tag protetto da permessi ONLY_OWNER, ma fallisce.
 * 1. Crea un Tag service con permission ONLY_OWNER
 * 2. Un processo figlio esegue seteuid e tenta di eseguire tutte e 6 le operazioni sullo stesso tag service.
 * 3. TEST: Il processo figlio non deve poter effettuare nessuna operazione
 * 4. Il processo padre attende la terminazione del figlio e chiude il tag service
 * @return
 */
int not_owner_test2() {
    // FUNZIONA SOLO SE L'UTENTE usa SUDO
    long tag, ret, frk;
    int status;
    int key = 102;

    tag = tag_get(key, CREATE_TAG, ONLY_OWNER);
    if (tag < 0) {
        perror("not_owner_test2: (tag_get)");
        FAILURE;
    }

    frk = fork();
    if (frk == -1) {
        perror("errore nella fork");
        FAILURE;
    } else if (frk == 0) {
        seteuid(1001); // <-------- funziona solo se ROOT
        // Provo ad eseguire tutti comandi possibili
        long ret_create, ret_open, ret_send, ret_recv, ret_awake, ret_remove;
        char messaggio[10];
        // 1. CREATE
        ret_create = tag_get(key, CREATE_TAG, ONLY_OWNER);
        ret_open = tag_get(key, OPEN_TAG, ONLY_OWNER);
        ret_send = tag_send((int) tag, 0, "Hacker", 7);
        ret_recv = tag_receive((int) tag, 0, messaggio, 10);
        ret_awake = tag_ctl((int) tag, AWAKE_ALL);
        ret_remove = tag_ctl((int) tag, REMOVE_TAG);

        if (ret_create >= 0 || ret_open >= 0 || ret_send >= 0 || ret_recv >= 0 || ret_awake >= 0 || ret_remove >= 0) {
            exit(EXIT_FAILURE); // se una delle operazioni ha successo, il test fallisce
        }
        exit(EXIT_SUCCESS);
    } else {
        wait(&status);
        ret = tag_ctl((int) tag, REMOVE_TAG);
        if (ret < 0) {
            perror("not_owner_test2: (tag_ctl)");
            FAILURE;
        }
    }
    // WEXITSTATUS(status) identico a (status >> 8)
    if (WEXITSTATUS(status) == EXIT_FAILURE) {
        printf("not_owner_test2: Il tag_service è stato aperto anche dal processo appartenente a un altro utente, oppure il processo apparteneva allo stesso utente\n");
        FAILURE;
    }
    SUCCESS;
}


/**
 * Verifica che un tag service con thread in attesa su k=3 livelli diversi, quando si esegue AWAKE_ALL su quel tag,
 * vengano svegliati tutti i thread. Le tag_receive devono restituire un errore.
 * 1. Creo un tag_service
 * 2. creo 3 thread in ricezione sul tag service ma in 3 livelli diversi
 * 3. Attendo che tutti i thread siano in ricezione
 * 4. Sveglio i threads
 * 5. Aspetto che i thread terminano tutti ed elimino il tag service
 * 6. Test: controllo che tutti i thread siano stati svegliati da AWAKE_ALL
 * @return
 */
int awake_test3() {
    pthread_t tid, tid2, tid3;
    long tag, ret, ret2;
    void *thread_return;
    void *thread_return2;
    void *thread_return3;

    thread_receiving3 = 0;

    tag = tag_get(103, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("awake_test3 (tag_get)");
        FAILURE;
    }

    long level1 = 1;
    long level2 = 2;
    long level3 = 3;

    pthread_create(&tid, NULL, thread_funct_awake_test3, (void *) level1);
    pthread_create(&tid2, NULL, thread_funct_awake_test3, (void *) level2);
    pthread_create(&tid3, NULL, thread_funct_awake_test3, (void *) level3);


    while (thread_receiving3 != 3);

    ret = tag_ctl((int) tag, AWAKE_ALL);
    if (ret < 0) {
        perror("awake_test3 (tag_ctl)");
        FAILURE;
    }

    pthread_join(tid, &thread_return);
    pthread_join(tid2, &thread_return2);
    pthread_join(tid3, &thread_return3);

    ret2 = tag_ctl((int) tag, REMOVE_TAG);
    if (ret2 < 0) {
        perror("awake_test3 (tag_ctl2)");
        FAILURE;
    }

    if (thread_return == NOT_OK || thread_return2 == NOT_OK || thread_return3 == NOT_OK) {
        // printf("Thread_return %ld \n", (long) thread_return);
        // printf("Thread_return2 %ld\n", (long) thread_return2);
        // printf("Thread_return3 %ld\n", (long) thread_return3);
        FAILURE;
    }
    SUCCESS;
}

/**
 * L'eliminazione di un tag service con processi in attesa di ricezione deve fallire!
 * 1. Crea un tag service
 * 2. Un altro processo si mette in attesa su quel tag
 * 3. Il primo processo vuole rimuovere il tag
 * 4. TEST: La rimozione deve fallire.
 * 5. Il primo processo sveglia il secondo e poi entrambi terminano.
 *
 * NOTA BENE: la prima remove puo' precedere la tag_receive del processo in ricezione. In tal caso ignoro il test.
 * @return
 */
int failed_remove_waiting_thread_test4() {
    long tag, ret, ret_test;
    char *semaName = "semaforo4";


    tag = tag_get(104, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("failed_remove_waiting_thread_test4 (tag_get)");
        FAILURE;
    }

    pid_t padre = fork();
    if (padre == -1) {
        perror("errore nella fork");
        FAILURE;
    } else if (!padre) {
        sem_t *semaphore4 = sem_open(semaName, O_CREAT, 0666, 0);
        if (semaphore4 == SEM_FAILED) {
            perror("failed_remove_waiting_thread_test4: errore nell'apertura del semaforo, assicurati che i parametri siano corretti es. semaName, O_CREAT, 0666, 0");
            exit(EXIT_FAILURE);
        }
        char buffer[20];

        sem_post(semaphore4);
        // problema se viene deschedulato qui, prima della tag_receive e dopo sem_post...
        ret = tag_receive((int) tag, 1, buffer, 20);

        sem_close(semaphore4);
        if (ret < 0 && errno == EINTR) {
            exit(EXIT_SUCCESS);
        } else if (ret < 0 && errno == EBADF) {
            /*
             * Ignoro il test, perche' il tag e' stato eliminato troppo presto.
             * La linearizzazione ha portato a eliminare il tag prima che il processo figlio avesse chiamato tag_receive.
             */
            // printf("failed_remove_waiting_thread_test4 (PROCESSO %d):  Ignoro il test, perche' la prima remove ha avuto successo.\n", getpid());
            exit(EXIT_SUCCESS);
        }
        printf("failed_remove_waiting_thread_test4: Il processo figlio doveva rimanere in attesa, ma ha ricevuto un messaggio o c'è stato un altro errore");
        exit(EXIT_FAILURE);
    } else {
        sem_t *semaphore4 = sem_open(semaName, O_CREAT, 0666, 0);
        sem_wait(semaphore4); // aspetto che il figlio va in ricezione
        if (semaphore4 == SEM_FAILED) {
            perror("failed_remove_waiting_thread_test4: errore nell'apertura del semaforo, assicurati che i parametri siano corretti es. semaName, O_CREAT, 0666, 0");
            FAILURE;
        }

        ret_test = tag_ctl((int) tag, REMOVE_TAG);
        // se minore di 0 tutto bene!
        if (ret_test > 0) { // questo viene eseguito nel caso in cui la tag_ctl riesca a eseguire prima di tag_receive
            int status;
            wait(&status);
            if (WEXITSTATUS(status) == EXIT_SUCCESS) {
                SUCCESS;
            }
        }

        ret = tag_ctl((int) tag, AWAKE_ALL);
        if (ret < 0) {
            perror("failed_remove_waiting_thread_test4: tag_ctl2: AWAKE_ALL");
        }
        // aspetto la terminazione del figlio, cosi' sono sicuro che non sta in ricezione.
        int status;
        wait(&status);

        sem_close(semaphore4);
        sem_unlink(semaName); // mi evita eventuali valori rimasti a 0
        // Rimuovo il tag
        ret = tag_ctl((int) tag, REMOVE_TAG);
        if (ret < 0) {
            perror("failed_remove_waiting_thread_test4 tag_ctl3: REMOVE_TAG");
            FAILURE;
        }

        // verifico il risultato del thread e che l'eliminazione abbia fallito
        if (ret_test == -1 && WEXITSTATUS(status) == EXIT_SUCCESS) {
            SUCCESS;
        }
        FAILURE;
    }
}


/**
 * Prova ad aprire un tag service eliminato
 * 1. Crea un tag service
 * 2. Rimuove il tag service
 * 3. TEST: Prova ad aprire il tag service (deve fallire)
 * @return
 */
int failed_reopening_after_remove_test5() {
    long tag, ret;
    int key = 105;
    tag = tag_get(key, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("failed_reopening_after_remove_test5: (tag_get)");
        FAILURE;
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("failed_reopening_after_remove_test5: (tag_ctl)");
        FAILURE;
    }

    tag = tag_get(key, OPEN_TAG, EVERYONE);
    if (tag < 0) {
        SUCCESS;
    }
    FAILURE;
}

/**
 * Controllo la concorrenza di send, remove e create. Se non si blocca, il test ha successo...
 * @param num_thread numero di thread
 * @return
 */
int remove_send_create_concurrent_test6(int num_thread) {
    long tag = tag_get(106, CREATE_TAG, EVERYONE);
    int i;
    if (tag < 0) {
        perror("remove_send_create_concurrent_test6");
        FAILURE;
    }
    if (num_thread < 3) num_thread = 3;

    pthread_t tid[num_thread];
    void *thread_return[num_thread];

    sem_init(&sem6, 0, 0);

    for (i = 0; i < num_thread; i++) {
        // Memoria liberata nel thread
        thread_data *input = (thread_data *) malloc(sizeof(thread_data));

        input->mode = i % 3;
        input->tag = (int) tag;

        // send or remove
        pthread_create(&tid[i], NULL, thread_func6, input);
    }

    for (i = 0; i < num_thread; i++) {
        sem_post(&sem6);
    }

    for (i = 0; i < num_thread; i++) {
        pthread_join(tid[i], &thread_return[i]);
    }

    tag_ctl((int) tag, REMOVE_TAG);
    // non controllo niente...

    SUCCESS;
}

/**
 * Prova ad eliminare tutti i tag service, anche se non esistono.
 * Ha successo se il numero di fallimenti e' pari al numero di tag service inesistenti
 * @return
 */
int repeated_failed_remove7() {
    long fails = MAX_TAG_SERVICES;
    int i;
    long works = 0;

    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        long ret = tag_ctl(i, REMOVE_TAG);
        if (ret < 0 && errno == EBADF) {
            fails += ret;
        } else if (ret < 0 && errno == ENOSYS) { // Not implemented
            perror("repeated_failed_remove7");
            FAILURE;
        } else if (ret < 0) {
            char string[50];
            sprintf(string, "repeated_failed_remove7: %d", i);
            perror(string);
        } else {
            works++;
        }
    }

    if (fails - works == 0) {
        SUCCESS;
    }
    printf("repeated_failed_remove7: La remove ha funzionato anche se non avrebbe dovuto\n fails: %ld, works: %ld",
           fails, works);
    FAILURE;
}

void *thread_sleeper8(void *tag) {
    char buffer[10];
    long ret;
    __sync_fetch_and_add(&thread_receiving8, 1);
    ret = tag_receive((int) (long) tag, 0, buffer, 10);
    if (ret < 0 && errno == EINTR) {
        return OK;
    } else if (ret < 0) {
        perror("awake_multiple_tag_test8: Thread sleeper - ");
        return NOT_OK;
    } else {
        printf("awake_multiple_tag_test8 - Thread sleeper ha ricevuto il messaggio, ma non era per lui...\n");
        return NOT_OK;
    }
}

void *thread_receiver8(void *tag) {
    char buffer[10];
    long ret;
    __sync_fetch_and_add(&thread_receiving8, 1);
    ret = tag_receive((int) (long) tag, 0, buffer, 10);
    if (strcmp("ciao", buffer) == 0) { // messaggio ricevuto correttamente
        return OK;
    } else if (ret < 0 && errno == EINTR) {
        perror("awake_multiple_tag_test8: Thread receiver - il thread e' stato svegliato, ma la awake_all era diretta a un altro tag...");
        return NOT_OK;
    } else {
        printf("awake_multiple_tag_test8 - Thread receiver: errore grave\n");
        return NOT_OK;
    }
}

/**
 * Un thread va in ricezione sulla coppia tag-livello (x,y) e un altro su (u,v).
 * Viene eseguito AWAKE_ALL su x e send su y.
 * Test: Il primo thread si deve svegliare, e il secondo deve ricevere il messaggio.
 */
int awake_multiple_tag_test8() {
    long tag1, tag2;
    long ret1, ret2, retAwake, retSend;
    pthread_t tid1, tid2;
    void *return1;
    void *return2;
    int num_thread = 2;
    thread_receiving8 = 0;

    tag1 = tag_get(108, CREATE_TAG, EVERYONE);
    tag2 = tag_get(208, CREATE_TAG, EVERYONE);
    if (tag1 < 0) {
        perror("awake_multiple_tag_test8 tag_get1");
        FAILURE;
    }
    if (tag2 < 0) {
        perror("awake_multiple_tag_test8 tag_get2");
        goto remove_first;
    }


    pthread_create(&tid1, NULL, thread_sleeper8, (void *) tag1);
    pthread_create(&tid2, NULL, thread_receiver8, (void *) tag2);

    while (thread_receiving8 != num_thread);

    retAwake = tag_ctl(108, AWAKE_ALL); // il 208 non si deve svegliare...
    retSend = tag_send(208, 0, "ciao", 5);
    if (retSend < 0) {
        perror("awake_multiple_tag_test8: errore nella send. Un thread rimane in attesa...");
        goto remove_all;
    }
    if (retAwake < 0) {
        perror("awake_multiple_tag_test8: GRAVE errore nella awake. Ci sono thread ancora in attesa...");
        goto remove_all;
    }

    pthread_join(tid1, &return1);
    pthread_join(tid2, &return2);

    remove_all: // rimuovo prima il secondo
    ret2 = tag_ctl(208, REMOVE_TAG);
    if (ret2 < 0) {
        perror("awake_multiple_tag_test8: errore GRAVE nella remove del secondo tag (208)");
    }
    remove_first:
    ret1 = tag_ctl(108, REMOVE_TAG);
    if (ret1 < 0) {
        perror("awake_multiple_tag_test8 errore GRAVE nella remove del primo tag (108): (forse) non sono riuscito a creare il secondo tag");
        FAILURE;
    }

    if (return1 == OK && return2 == OK) {
        SUCCESS;
    }
    if (return1 == NOT_OK) {
        printf("il thread sleeper ha ricevuto il messaggio, ma non era per lui\n");
    }
    if (return2 == NOT_OK) {
        printf("il thread receiver e' stato svegliato, ma l'awake era rivolta a un tag diverso\n");
    }
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
int change_permission_during_send_test9(int test) {
    long tag, frk, ret;
    int status;
    int key = 109;
    sem_t *sem11;
    sem_t *sem112;

    if (test != 1 && test != 2 && test != 3) test = 1;

    if ((tag = tag_get(key, CREATE_TAG, EVERYONE)) < 0) {
        perror("change_permission_during_send_test9: tag_get");
        FAILURE;
    }

    sem11 = sem_open("sem_11", O_CREAT, 0666, 0);
    if (test == 2) sem112 = sem_open("sem_112", O_CREAT, 0666, 0);

    frk = fork();
    if (frk == -1) {
        perror("change_permission_during_send_test9: errore nella fork");
        FAILURE;
    } else if (frk == 0) {

        seteuid(1001); // <-------- funziona solo se ROOT

        if (test == 3) {
            sem_wait(sem11); // Aspetto solo nel caso 3, ovvero quando la send viene prima di ctl e get
        }

        if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
            perror("change_permission_during_send_test9 (processo figlio): errore nella REMOVE_TAG");
            exit(EXIT_FAILURE);
        }

        if (test == 2) {
            sem_post(sem11); // permetti al processo padre di provare a inviare, ma poi aspetta che finisce l'invio
            sem_wait(sem112); // aspetta la send
        } // nel caso 1 e 3 vado avanti senza aspettare


        if (tag_get(key, CREATE_TAG, ONLY_OWNER) < 0) {
            perror("change_permission_during_send_test9 (processo figlio): errore nella CREATE_TAG");
            exit(EXIT_FAILURE);
        }

        if (test == 1) {
            sem_post(sem11); // Ho finito tutto ora tocca alla send
        } // negli altri casi ho finito

        exit(EXIT_SUCCESS); // qui deve funzionare tutto
    } else {

        // aspetto REMOVE e/o CREATE prima di inviare, eccetto quando devo iniziare prima io (caso 3)
        if (test != 3) sem_wait(sem11);

        ret = tag_send((int) tag, 0, "ciao", 5);
        if (ret == -1) {
            switch (errno) { // nessuno deve ricevere
                case EACCES: // siamo nel caso 1. Devo aspettarmi che il tag esista ma non mi faccia accedere perche' non ho i permessi
                    ret = tag_get(key, OPEN_TAG, EVERYONE);
                    if (ret != -1) {
                        printf("change_permission_during_send_test9: Sono riuscito ad accedere a un tag service PUR NON AVENDO I PERMESSI!!!!!");
                        goto failure; // Poiche' in qualche modo sono riuscito ad accedere rimuovo il tag per tornare allo stato precedente
                    } else if (errno != EACCES) {
                        perror("change_permission_during_send_test9 GRAVE: non posso accedere al tag service, ma non perche' non ho i permessi...");
                        goto failure;
                    }
                    break;
                case EBADF: // siamo nel caso 2. Devo aspettarmi che il tag sia stato eliminato, quindi controllo che lo sia davvero
                    ret = tag_get(key, OPEN_TAG, EVERYONE);
                    if (ret != -1) {
                        printf("change_permission_during_send_test9: Sono riuscito ad accedere a un tag service  eliminato!!!!!");
                        goto failure;
                    }
                    break;
                default:
                    perror("change_permission_during_send_test9 (processo padre): errore nella tag send");
                    // NON SERVE CAMBIARE UTENTE: altrimenti sarei nel caso 1
                    goto failure;
            }
        }

        if (test == 2) {
            sem_post(sem112);
        } else if (test == 3) {
            sem_post(sem11);
        } // nel caso 1 ho già finito coi semafori

        wait(&status); // aspetto il processo figlio

        // non mi interessa quanti gettoni ha il semaphore... lo chiudo e basta
        sem_unlink("sem_11"); // vedi /dev/shm per vedere se rimane li'
        sem_close(sem11);

        if (test == 2) {
            sem_unlink("sem_112"); // vedi /dev/shm per vedere se rimane li'
            sem_close(sem112);
        }

        // Per la riga successiva, questo test va eseguito per ultimo...
        seteuid(1001); // qua serve cambiare utente... perche' il processo figlio ha creato un tag con utente 1001
        if (tag_ctl((int) tag, REMOVE_TAG) == -1) { // elimino per ristabilire lo stato precedente.
            perror("change_permission_during_send_test9: errore grave impossibile eliminare al termine del test");
            FAILURE;
        }
        if (WEXITSTATUS(status) == EXIT_FAILURE) {
            printf("change_permission_during_send_test9: il processo figlio ha fallito nella remove o nella create");
            FAILURE;
        }
    }
    SUCCESS;
    failure:

    sem_unlink("sem_11");
    sem_close(sem11);

    if (test == 2) {
        sem_unlink("sem_112"); // vedi /dev/shm per vedere se rimane li'
        sem_close(sem112);
    }

    wait(&status);
    seteuid(1001);
    if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
        perror("change_permission_during_send_test9 GRAVE: impossibile eliminare il tag service dopo che sia tag_send che tag_get falliscono");
        FAILURE;
    }
    FAILURE;
}