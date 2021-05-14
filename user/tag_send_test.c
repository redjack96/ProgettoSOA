//
// Created by giacomo on 21/03/21.
//
// Il test i senza IPC_PRIVATE usa la chiave 11*i
//

#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <pthread.h>
#include <syscall.h>
#include "tag_send_test.h"

#define OK 1
#define NOT_OK 0
char *semaphoreName = "sharedProcessSemaphore";

static int thread_received3 = 0;
static int thread_received4 = 0;
static int thread_received5 = 0;

/**
 * 1. Istanzia un tag service con permessi EVERYONE
 * 2. Invia un messaggio, senza che nessuno sia in attesa
 * 3. Chiude il tag service
 * 4. Controlla che non ci siano errori
 * @return
 */
int send_test1() {
    long tag, res;
    int level = 5;
    tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("send_test1: Errore in tag_get");
        FAILURE;
    }

    char *message = "Messaggio dal processo A";
    res = tag_send((int) tag, level, message, strlen(message));
    if (res < 0) {
        perror("send_test1: Errore in tag_send");
        FAILURE;
    }

    res = tag_ctl((int) tag, REMOVE_TAG);
    if (res < 0) {
        perror("send_test1: Errore in tag_ctl");
        FAILURE;
    }

    SUCCESS;
}


/**
 * 1. Istanzia un tag service
 * 2. crea un Processo figlio
 *      - il processo figlio invia un messaggio che supera MAX_MESSAGE_BYTES, non appena è sicuro che il processo padre va in ricezione
 * 3. Il processo padre va in ricezione, sullo stesso tag e livello del figlio
 * 4. Il processo padre controlla se il messaggio ricevuto ha la dimensione corretta.
 *
 * N.B: se il messaggio supera MAX_MESSAGE_SIZE byte, vengono inviati e ricevuti solo MAX_MESSAGE_SIZE byte (il messaggio viene troncato)
 * @return
 */
int send_and_receive_long_message_test2() {
    long tag, res, frk;
    int level = 4;

    // creo un semaforo con nome per poterlo condividere tra processi!!!
    sem_t *semaphore2 = sem_open(semaphoreName, O_CREAT, 0600, 0);

    tag = tag_get(22, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("send_and_receive_long_message_test2: Errore in tag_get");
        FAILURE;
    }

    char *message = "Messaggio molto lungo dal processo A. Questo messaggio è così lungo che per contare il suo "
                    "contenuto ci metterai troppo tempo. Ci vorra' cosi' tanto tempo che quando sei arrivato alla fine "
                    "del messaggio ti accorgi che in realta' non e' che solo l'inizio. Il messaggio recita cosi': "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto tanto "
                    "tanto tanto tanto tanto tanto tanto tanto tanto tanto fa, hai iniziato a leggere questo messaggio... che ora era?";
    size_t size = strlen(message);

    frk = fork();
    if (frk == -1) {
        perror("errore nella fork");
        FAILURE;
    } else if (frk == 0) {
        // RIAPRO il semaforo con nome per poterlo usare nel processo figlio!!!
        semaphore2 = sem_open(semaphoreName, O_CREAT, 0600, 0);
        sem_wait(semaphore2);
        sem_close(semaphore2);
        // printf("Sono qui(figlio)\n");
        res = tag_send((int) tag, level, message, size);
        if (res < 0 && errno != EMSGSIZE) {
            printf("%ld\n", res);
            perror("send_and_receive_long_message_test2: Errore in tag_send");
            exit(-1);
        }
        exit(0);
    } else {
        int status;
        char receive_buffer[size];
        sem_post(semaphore2);
        // printf("Sono qui(padre)\n");
        res = tag_receive((int) tag, level, receive_buffer, size);
        if (res < 0) {
            res = tag_ctl((int) tag, REMOVE_TAG);
            if (res < 0) {
                perror("send_and_receive_long_message_test2: Errore in tag_ctl");
                FAILURE;
            }
            printf("Fallimento tag_receive");
            FAILURE;
        }
        wait(&status);
        sem_close(semaphore2);
        sem_unlink(semaphoreName);

        res = tag_ctl((int) tag, REMOVE_TAG);
        if (res < 0) {
            perror("send_and_receive_long_message_test2: Errore in tag_ctl");
            FAILURE;
        }

        if (strncmp(message, receive_buffer, (size >= MAX_MESSAGE_SIZE - 1 ? MAX_MESSAGE_SIZE - 1 : size)) == 0) {
            SUCCESS;
        }

        FAILURE;
    }
}

/**
 * Thread che invia un messaggio in un particolare livello
 * Utilizzato nel test 3
 * @param level
 * @return
 */
void *sender_thread3(void *level) {
    int the_level = (int) (long) level;
    char messaggio[30];
    sprintf(messaggio, "Messaggio per il livello %d", the_level);

    long ret = tag_send(33, the_level, messaggio, 30);
    if (ret < 0) {
        printf("Errore nel thread sender %ld", (long) syscall(__NR_gettid));
        return (void *) NOT_OK;
    }
    return (void *) OK;
}

/**
 * Thread che si mette in attesa in un particolare livello
 * Utilizzato nel test 3
 * @param level
 * @return
 */
void *receiver_thread3(void *level) {
    int the_level = (int) (long) level;
    char messaggio[30];

    __sync_fetch_and_add(&thread_received3, 1);
    long ret = tag_receive(33, the_level, messaggio, 30);
    if (ret < 0) {
        printf("Errore nel thread receiver %ld", (long) syscall(__NR_gettid));
        return (void *) NOT_OK;
    }

    char messaggio_expected[30];
    sprintf(messaggio_expected, "Messaggio per il livello %d", the_level);
    if (strncmp(messaggio_expected, messaggio, strlen(messaggio)) != 0) {
        printf("Errore:thread %ld ha ricevuto \"%s\", ma doveva ricevere \"%s\"", (long) syscall(__NR_gettid), messaggio,
               messaggio_expected);
        return (void *) NOT_OK;
    }
    return (void *) OK;
}

/**
 * Se almeno due thread tentano di inviare un messaggio ciascuno sullo stesso tag, ma differente livello,
 * i due messaggi devono essere consegnati in CONTEMPORANEA ai thread in attesa sui rispettivi livelli.
 * 1. Creo un tag.
 * 2. Creo 6 threads: 2 sender e 4 receivers su due livelli diversi.
 * 3. Due thread sender vanno in attesa che tutti i thread receiver siano in ricezione
 * 4. I thread sender inviano il messaggio diverso sui due livelli e terminano l'esecuzione.
 * 5. TEST: I thread receiver devono ricevere i messaggi corretti.
 * 6. Al termine di tutti i thread, elimino il tag
 * @return
 */
int concurrent_send_multilevel_test3() {
    /* My syscalls stuff */
    long tag, res;
    int THREAD_NUMBER = 6;
    pthread_t tid[THREAD_NUMBER];
    void *thread_return[THREAD_NUMBER];

    thread_received3 = 0;

    tag = tag_get(33, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("concurrent_send_multilevel_test3: Errore in tag_get");
        FAILURE;
    }

    const int RECEIVER_THREADS = 4;

    // creo prima i thread receiver, che andranno in attesa.
    pthread_create(&tid[2], NULL, receiver_thread3, (void *) 0L);
    pthread_create(&tid[3], NULL, receiver_thread3, (void *) 0L);
    pthread_create(&tid[4], NULL, receiver_thread3, (void *) 1L);
    pthread_create(&tid[5], NULL, receiver_thread3, (void *) 1L);

    // aspetto i thread receiver e poi creo i senders

    while (thread_received3 != RECEIVER_THREADS);

    pthread_create(&tid[0], NULL, sender_thread3, (void *) 0L);
    pthread_create(&tid[1], NULL, sender_thread3, (void *) 1L);

    int i;
    for (i = 0; i < THREAD_NUMBER; i++) {
        pthread_join(tid[i], &thread_return[i]);
    }
    res = tag_ctl((int) tag, REMOVE_TAG);
    if (res < 0) {
        perror("concurrent_send_multilevel_test3: Errore nella REMOVE_TAG");
        FAILURE;
    }

    SUCCESS;
}

/**
 * Thread che invia un messaggio in un particolare (tag,livello)
 * Utilizzato nel test 4
 * @param delta
 * @return
 */
void *sender_thread4(void *delta) {
    int the_difference = (int) (long) delta; // 1 o 2
    char messaggio[30];
    sprintf(messaggio, "Messaggio dal sender %d", the_difference);


    long ret = tag_send(44, 0, messaggio, 30);
    if (ret < 0) {
        printf("Errore nel thread sender %d\n", the_difference);
        perror("concurrent_send_same_level_test4: Errore");
        return (void *) NOT_OK;
    }
    return (void *) OK;
}

/**
 * Thread che si mette in attesa in un particolare (tag,livello)
 * Utilizzato nel test 4
 * @return
 */
void *receiver_thread4(void *ignored) {
    char messaggio[30];

    __sync_fetch_and_add(&thread_received4, 1);
    long ret = tag_receive(44, 0, messaggio, 30); // livello 0
    if (ret < 0) {
        printf("Errore nel thread receiver %ld", (long) syscall(__NR_gettid));
        return (void *) NOT_OK;
    }

    char *messaggio_expected1 = "Messaggio dal sender 1";
    char *messaggio_expected2 = "Messaggio dal sender 2";

    if (strncmp(messaggio_expected1, messaggio, strlen(messaggio)) == 0 ||
        strncmp(messaggio_expected2, messaggio, strlen(messaggio)) == 0) {
        return (void *) OK;
    }
    printf("concurrent_send_same_level_test4: il thread receiver ha ricevuto \"%s\", ma doveva ricevere \"%s\" o \"2\"\n",
           messaggio,
           messaggio_expected1);
    return (void *) NOT_OK;
}

/**
 * Se almeno due thread tentano di inviare un messaggio sullo stesso tag, e STESSO livello,
 * solo un thread alla volta può inviarlo. Solo il primo messaggio inviato va consegnato ai thread in attesa.
 * Attenzione alla linearizzazione.
 * 1. Creo un tag
 * 2. Creo 3 thread: 2 sender e 1 receiver, ciascuno affine a una diversa CPU.
 * 3. I thread sender aspettano che il receiver sia in ricezione nella coppia (tag, livello)
 * 4. In contemporanea, i thread sender vogliono inviare due messaggi diversi sullo stesso (tag, livello)
 * 5. Test: il messaggio ricevuto deve essere quello inviato dal primo dei due senders
 * 6. Al termine di tutti i thread, elimino il tag
 * @return
 */
int concurrent_send_same_level_test4() {
    /* My syscalls stuff */
    long tag, res;
    int THREAD_NUMBER = 3;
    int i;
    pthread_t tid[THREAD_NUMBER];
    void *thread_return[THREAD_NUMBER];

    thread_received4 = 0;
    // creo un semaforo con nome per poterlo condividere tra processi!!!

    tag = tag_get(44, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("concurrent_send_same_level_test4: Errore in tag_get");
        FAILURE;
    }

    // creo prima i thread receiver, che andranno in attesa.
    pthread_create(&tid[0], NULL, receiver_thread4, NULL);
    // sched_setaffinity(getpid(), );

    // aspetto il thread receiver e poi creo i senders
    while (thread_received4 != 1);

    pthread_create(&tid[1], NULL, sender_thread4, (void *) 1L);
    pthread_create(&tid[2], NULL, sender_thread4, (void *) 2L);

    for (i = 0; i < THREAD_NUMBER; i++) {
        pthread_join(tid[i], &thread_return[i]);
    }

    res = tag_ctl((int) tag, REMOVE_TAG);
    if (res < 0) {
        perror("concurrent_send_same_level_test4: Errore nella REMOVE_TAG");
        FAILURE;
    }

    SUCCESS;
}

/**
 * Thread che si aspetta di ricevere un messaggio lungo 0.
 * @return OK | NOT_OK
 */
void *zero_message_receiver5(void * tag) {
    char messaggio[30];

    __sync_fetch_and_add(&thread_received5, 1);
    long ret = tag_receive((int) (long) tag, 5, messaggio, 30); // livello 0
    if (ret < 0) {
        perror("Errore nel thread zero_message_receiver");
        return (void *) NOT_OK;
    }

    char *zero_message_expected = "";

    if (strncmp(zero_message_expected, messaggio, strlen(messaggio)) == 0) {
        return (void *) OK;
    }
    printf("concurrent_send_same_level_test4: il thread receiver ha ricevuto \"%s\", ma doveva ricevere un messaggio nullo\n",
           messaggio);
    return (void *) NOT_OK;
}

/**
 * Invia un messaggio di dimensione 0.
 * Il test ha successo se non ci sono errori.
 * @return
 */
int zero_size_send_test5() {
    long tag, res;
    pthread_t tid;
    void *thread_return;
    int level = 5;

    thread_received5 = 0;

    tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("zero_size_send_test5: Errore in tag_get");
        FAILURE;
    }

    pthread_create(&tid, NULL, zero_message_receiver5, (void *) tag);

    while (thread_received5 != 1);

    char *zero_message = "";
    res = tag_send((int) tag, level, zero_message, strlen(zero_message));
    if (res < 0) {
        perror("zero_size_send_test5: Errore in tag_send");
        FAILURE;
    }

    pthread_join(tid, &thread_return);


    res = tag_ctl((int) tag, REMOVE_TAG);
    if (res < 0) {
        perror("zero_size_send_test5: Errore in tag_ctl");
        FAILURE;
    }

    if (thread_return == (void *) OK) {
        SUCCESS;
    }
    printf("I thread hanno fallito\n");
    FAILURE;
}
