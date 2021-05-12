//
// Created by giacomo on 21/03/21.
//
// Il test i senza IPC_PRIVATE usa 200+i
//

#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <syscall.h>
#include <sys/time.h>
#include "tag_receive_test.h"

#define OK (void *) 1
#define NOT_OK (void *) 0
#define AUDIT if(0)

static int thread_received1 = 0;
static int thread_received2 = 0;
static int thread_received3 = 0;
static int thread_received4 = 0;
static int thread_received5 = 0;
static int thread_received9 = 0;

void *thread_receiver_test1(void *tag) {
    long ret;
    char *buffer = malloc(sizeof(char) * MAX_MESSAGE_SIZE);

    __sync_fetch_and_add(&thread_received1, 1);
    ret = tag_receive((int) (long) tag, 4, buffer, 500);
    if (ret < 0 && errno == EINTR) {
        return (void *) OK;
    } else if (ret < 0 && errno != EINTR) {
        printf("different_levels_not_receive_test1: Errore di ricezione nel thread %d: \n", getpid());
        perror("different_levels_not_receive_test1");
    }

    return (void *) NOT_OK;
}

void *thread_function_test2(void *tag) {
    long ret;
    char *buffer = malloc(sizeof(char) * MAX_MESSAGE_SIZE);

    __sync_fetch_and_add(&thread_received2, 1);
    ret = tag_receive((int) (long) tag, 0, buffer, 500);
    if (ret < 0) {
        char messaggio[100];
        sprintf(messaggio, "multiple_receive_same_level_test2: Errore di ricezione nel thread %d", getpid());
        perror(messaggio);
        return (void *) NOT_OK;
    }


    if (strcmp("Messaggio da ricevere", buffer) != 0) {
        printf("Messaggio ricevuto ERRATO: %s\n", buffer);
        return (void *) NOT_OK;
    }

    return (void *) OK;
}

typedef struct the_level_data {
    int tag;
    int level;
    char *expected_message;
} tag_service_data;

/**
 * Deve ricevere il messaggio corretto, al livello corretto (cambia), nello stesso tag...
 * @param data
 * @return
 */
void *thread_function_test3(void *data) {
    long ret;
    char *buffer = malloc(sizeof(char) * MAX_MESSAGE_SIZE);
    tag_service_data *my_data = data;

    __sync_fetch_and_add(&thread_received3, 1);
    // il livello cambia!!
    ret = tag_receive(my_data->tag, my_data->level, buffer, 500);
    if (ret < 0) {
        char messaggio[100];
        sprintf(messaggio, "multiple_receive_same_level_test2: Errore di ricezione nel thread %d\n", getpid());
        perror(messaggio);
        return (void *) NOT_OK;
    }


    if (strcmp(my_data->expected_message, buffer) != 0) {
        printf("Messaggio ricevuto ERRATO: %s\n", buffer);
        return (void *) NOT_OK;
    }

    return (void *) OK;
}

/**
 * Devo ricevere il messaggio corretto, nel tag corretto (stesso livello).
 * @param data
 * @return
 */
void *thread_function_test4(void *data) {
    long ret;
    char *buffer = malloc(sizeof(char) * MAX_MESSAGE_SIZE);

    // la riutilizzo, ma in realta' stavolta cambia il tag!!
    tag_service_data *tagData = (tag_service_data *) data;

    // Incremento atomico GCC
    __sync_fetch_and_add(&thread_received4, 1);

    ret = tag_receive(tagData->tag, tagData->level, buffer, 500);

    if (ret < 0) {
        char messaggio[100];
        sprintf(messaggio, "multiple_receive_same_level_test2: Errore di ricezione nel thread %d", getpid());
        perror(messaggio);
        return (void *) NOT_OK;
    }


    if (strcmp(tagData->expected_message, buffer) != 0) {
        printf("Messaggio ricevuto ERRATO: %s\n", buffer);
        return (void *) NOT_OK;
    }

    return (void *) OK;
}

/**
 * Invio di un messaggio sullo stesso TAG service, ma su un diverso livello. Mi aspetto che nessuno riceva nulla.
 * 1. Crea un tag service
 * 2. crea un processo
 *  - il processo figlio attende che il padre sia pronto a inviare e si mette in ricezione (NON DEVE RICEVERE NIENTE)
 * 3. Il processo padre prova a inviare un messaggio sullo stesso tag, ma su un livello diverso.
 * 4. Poiché mi aspetto che il figlio non riceva nulla, il padre sveglia il figlio con tag_ctl(AWAKE_ALL), poi va in attesa di terminazione del figlio.
 * 5. Il padre chiude il tag service e verifica che il figlio non abbia ricevuto nulla.
 *
 * N.B: utilizzo i semafori POSIX con nome per poterli condividere tra processi diversi.
 * @return
 */
int different_levels_not_receive_test1() {
    long tag, res;
    int level = 4;
    pthread_t tid;
    void *thread_return;

    thread_received1 = 0;

    tag = tag_get(IPC_PRIVATE, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("different_levels_not_receive_test1: Errore in tag_get");
        FAILURE;
    }

    char *message = "Messaggio segreto da non ricevere!";
    size_t size = strlen(message);


    pthread_create(&tid, NULL, thread_receiver_test1, (void *) tag);

    while (thread_received1 != 1);

    res = tag_send((int) tag, level + 1, message, size);

    if (res < 0) {
        perror("different_levels_not_receive_test1: Errore in tag_send (padre)");
        res = tag_ctl((int) tag, REMOVE_TAG);
        if (res < 0) {
            perror("different_levels_not_receive_test1: Errore in tag_ctl");
            FAILURE;
        }
        FAILURE;
    }

    res = tag_ctl((int) tag, AWAKE_ALL);
    if (res < 0) {
        perror("different_levels_not_receive_test1: Errore in tag_ctl[AWAKE_ALL]. IL thread e' ancora in attesa di un messaggio");
        FAILURE;
    }

    // attendo che il figlio in ricezione abbia finito
    pthread_join(tid, &thread_return);

    res = tag_ctl((int) tag, REMOVE_TAG);
    if (res < 0) {
        perror("different_levels_not_receive_test1: Errore in tag_ctl[REMOVE_TAG]");
        FAILURE;
    }
    // controlla se il figlio non ha ricevuto niente.
    if (thread_return == NOT_OK) {
        printf("different_levels_not_receive_test1: Il figlio ha ricevuto un messaggio pur essendo in un altro livello!!!");
        FAILURE;
    }
    SUCCESS;
}

/**
 * Invio di un messaggio ad almeno due thread diversi sulla stessa coppia tag, livello.
 * Mi aspetto che tutti i thread ricevano lo stesso messaggio.
 * @return
 */
int multiple_receive_same_level_test2(int thread_number) {
    long ret, tag;
    pthread_t tid[thread_number];
    void *returns[thread_number];
    char *buffer = "Messaggio da ricevere";
    int i, k, l;
    thread_received2 = 0;

    if (thread_number < 2) {
        printf("Invio il messaggio a 2 thread invece di %d", thread_number);
        thread_number = 2;
    }


    tag = tag_get(202, CREATE_TAG, ONLY_OWNER); // provo only owner
    if (tag < 0) {
        perror("multiple_receive_same_level_test2");
        FAILURE;
    }

    for (i = 0; i < thread_number; i++) {
        pthread_create(&tid[i], NULL, thread_function_test2, (void *) tag);
    }

    // aspetto che tutti i thread vadano in attesa di ricevere messaggi

    while (thread_received2 != thread_number);

    ret = tag_send((int) tag, 0, buffer, strlen(buffer));
    if (ret < 0) {
        perror("multiple_receive_same_level_test2: tag_send");
        goto fail;
    }
    fail:
    for (k = 0; k < thread_number; k++) {
        pthread_join(tid[k], (void **) &returns[k]);
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("multiple_receive_same_level_test2: (label FAIL) tag_ctl - REMOVE: ");
        FAILURE;
    }

    for (l = 0; l < thread_number; l++) {
        if (returns[l] == NOT_OK) {
            printf("I thread non hanno ricevuto lo stesso messaggio... (errore al thread %d)\n", l);
            FAILURE;
        }
    }
    SUCCESS;
}

/**
 * Invio di almeno 2 messaggi (uno dopo l'altro) ad altrettanti thread su stesso tag, ma livello diverso.
 * I messaggi devono essere ricevuti nell'ordine dai thread corretti
 * 1. creo un tag service
 * 2. creo [thread_number] threads receiver
 * 3. attendo che i threads vadano in ricezione su stesso tag ma diverso livello
 * 4. invio [thread_number] messaggi (diversi) ai corrispettivi livelli
 * 5. aspetto che i thread finiscano il lavoro
 * 6. elimino il tag service
 * 7. Test: verifico che i thread abbiano ricevuto i messaggi attesi.
 * @return
 */
int multiple_receive_different_level_test3(int thread_number) {
    long ret, tag;
    int i, k, l;
    pthread_t tid[thread_number];
    void *returns[thread_number];

    thread_received3 = 0;

    if (thread_number < 2) {
        printf("Invio il messaggio a 2 thread invece di %d", thread_number);
        thread_number = 2;
    }

    tag = tag_get(203, CREATE_TAG, ONLY_OWNER); // provo only owner
    if (tag < 0) {
        perror("multiple_receive_different_level_test3");
        FAILURE;
    }

    for (i = 0; i < thread_number; i++) {
        char *message = malloc(sizeof(char) * 40);
        sprintf(message, "Messaggio per il livello %d", i % MAX_LEVELS);
        tag_service_data *input = (tag_service_data *) malloc(sizeof(tag_service_data));
        input->tag = (int) tag;
        input->level = i % MAX_LEVELS;
        input->expected_message = malloc(sizeof(char) * (1 + strlen(message)));
        strncpy(input->expected_message, message, strlen(message));
        pthread_create(&tid[i], NULL, thread_function_test3, (void *) input);
    }

    // aspetto che tutti i thread vadano in attesa di ricevere messaggi

    while (thread_received3 != thread_number);


    for (k = 0; k < thread_number; k++) {
        char *message = malloc(sizeof(char) * 40);
        sprintf(message, "Messaggio per il livello %d", k % MAX_LEVELS);
        ret = tag_send((int) tag, k % MAX_LEVELS, message, strlen(message));
        if (ret < 0) {
            perror("multiple_receive_different_level_test3: una delle tag_send");
            goto fail;
        }
    }

    fail:
    for (k = 0; k < thread_number; k++) {
        pthread_join(tid[k], (void **) &returns[k]);
    }

    ret = tag_ctl((int) tag, REMOVE_TAG);
    if (ret < 0) {
        perror("multiple_receive_different_level_test3: (label FAIL) tag_ctl - REMOVE: ");
        FAILURE;
    }

    for (l = 0; l < thread_number; l++) {
        if (returns[l] == NOT_OK) {
            printf("I thread non hanno ricevuto lo stesso messaggio... (errore al thread %d)\n", l);
            FAILURE;
        }
    }
    SUCCESS;
}

/**
 * Invio di almeno 2 messaggi (uno dopo l'altro) ad altrettanti thread su tag diversi, ma STESSO livello.
 * I messaggi devono essere ricevuti nell'ordine dai thread corretti
 * 1. Creo [thread_number] tag service
 * 2. Creo [thread_number] thread che vanno in ricezione ognuno su un tag diverso, ma allo stesso livello
 * 3. Attendo che tutti i thread siano andati in ricezione...
 * 4. Eseguo [thread_number] volte tag_send, su tutti i tag.
 * 5. Attendo che i thread in ricezione abbiano ricevuto tutti il messaggio
 * 6. Rimuovo tutti i tag creati
 * @return
 */
int multiple_receive_different_tag_same_level_test4(int thread_number) {
    long ret;
    long tag[thread_number];
    pthread_t tid[thread_number];
    void *returns[thread_number];
    int i, k, l;
    for (i = 0; i < thread_number; ++i) {
        returns[i] = NOT_OK;
    }

    thread_received4 = 0;

    if (thread_number < 2) {
        printf("Invio il messaggio a 2 thread invece di %d", thread_number);
        thread_number = 2;
    }
    int created = 0;

    // 1. Creo [thread_number] tag service
    for (i = 0; i < thread_number; i++) {
        tag[i] = tag_get(IPC_PRIVATE, CREATE_TAG, ONLY_OWNER); // provo only owner
        if (tag[i] < 0) {
            perror("multiple_receive_different_tag_same_level_test4");
            goto remove_tags;
        }
        created++;
    }

    // 2. Creo [thread_number] thread che vanno in ricezione ognuno su un tag diverso, ma allo stesso livello
    for (i = 0; i < thread_number; i++) {
        char *message = malloc(sizeof(char) * 40);
        sprintf(message, "Messaggio per il tag %ld", tag[i]);
        tag_service_data *input = (tag_service_data *) malloc(sizeof(tag_service_data));
        input->tag = (int) tag[i];
        input->level = 0;
        input->expected_message = malloc(sizeof(char) * (1 + strlen(message)));
        strncpy(input->expected_message, message, strlen(message));
        pthread_create(&tid[i], NULL, thread_function_test4, (void *) input);
    }

    // 3. Attendo che tutti i thread siano andati in ricezione...

    while (thread_received4 != thread_number);

    // 4. Eseguo [thread_number] volte tag_send, su tutti i tag.
    for (k = 0; k < thread_number; k++) {
        char *message = malloc(sizeof(char) * 40);
        sprintf(message, "Messaggio per il tag %ld", tag[k]);
        ret = tag_send((int) tag[k], 0, message, strlen(message));
        if (ret < 0) {
            perror("multiple_receive_different_level_test3: una delle tag_send");
            goto fail;
        }
    }

    fail:
    // 5. Attendo che i thread in ricezione abbiano ricevuto tutti il messaggio
    for (k = 0; k < thread_number; k++) {
        pthread_join(tid[k], (void **) &returns[k]);
    }

    // 6. Rimuovo tutti i tag creati
    for (i = 0; i < thread_number; i++) {
        ret = tag_ctl((int) tag[i], REMOVE_TAG);
        if (ret < 0) {
            perror("multiple_receive_different_level_test3: (label FAIL) tag_ctl - REMOVE: ");
            FAILURE;
        }
    }

    // controllo il risultato di tutti i thread.
    for (l = 0; l < thread_number; l++) {
        if (returns[l] == NOT_OK) {
            printf("I thread non hanno ricevuto lo stesso messaggio... (errore al thread %d)\n", l);
            FAILURE;
        }
    }
    SUCCESS;
    remove_tags:

    for (i = 0; i < created; i++) {
        // tag_ctl((int) tag[i], AWAKE_ALL);
        ret = tag_ctl((int) tag[i], REMOVE_TAG);
        if (ret < 0) {
            perror("multiple_receive_different_tag_same_level_test4: GRAVE");
            FAILURE;
        }
    }
    FAILURE;
}
// ELIMINARE
// #define printf if(0) printf

void *thread_receiver5(void *tag) {
    long ret;
    char *buffer = malloc(sizeof(char) * MAX_MESSAGE_SIZE);

    // printf("Qui thread %d\n", (int) (long) tag);

    // Incremento atomico GCC
    __sync_fetch_and_add(&thread_received5, 1);

    ret = tag_receive((int) (long) tag, 5, buffer, 500);

    if (ret < 0 && errno == EINTR) {
        printf("Thread %ld svegliato da AWAKE_ALL\n", (long) syscall(__NR_gettid));
        return OK;
    } else if (ret < 0) {
        char messaggio[80];
        sprintf(messaggio, "device_write_test5: Errore di ricezione nel thread %ld", (long) syscall(__NR_gettid));
        perror(messaggio);
        return NOT_OK;
    }

    if (strcmp("ciao thread", buffer) != 0) {
        printf("Messaggio ricevuto ERRATO: %s\n", buffer);
        return (void *) NOT_OK;
    }

    return (void *) OK;
}

/**
 * Legge la stringa del char device e la salva nel parametro in input
 */
int readCharDev(char *returned, const char *path) {
    FILE *f;
    int retry = 0;
    char *path = "/dev/tsdev_205";
    /* Il seguente ciclo serve per aspettare che i permessi del char device siano effettivamente cambiati */

    while (access(path, R_OK) == -1);

    f = fopen(path, "r");
    if (f == NULL) {
        printf("Impossibile aprire il file a causa dei permessi...\n");
        return -1;
    }

    int ch;
    int i = 0;
    while ((ch = fgetc(f)) != EOF) {
        *(returned + i) = (char) ch;
        i++;
    }
    *(returned + i) = 0;
    return 0;
}

/**
 * Il test e' bloccante per il numero di minuti specificati.
 * @param thread_number: numero di thread da creare e mandare in ricezione
 * @param minuti durante i quali il test e' bloccante.
 *
 * 1. Crea un tag service
 * 2. Crea [thread_number] thread in ricezione sullo stesso livello e tag
 * 3. Aspetta che tutti i thread siano andati in ricezione
 * 4. Legge il char device (DEVE FUNZIONARE)
 */
int device_write_test5(int thread_number, int minuti) {
    long ret, tag;
    int i, l;
    char chdev_content[4096];
    pthread_t tid[thread_number];
    void *returns[thread_number];
    for (i = 0; i < thread_number; ++i) {
        returns[i] = NOT_OK;
    }

    thread_received5 = 0;

    if (thread_number < 2) {
        printf("Invio il messaggio a 2 thread invece di %d\n", thread_number);
        thread_number = 2;
    }
    int times = 0;
    retry:
    tag = tag_get(205, CREATE_TAG, EVERYONE);
    if (tag < 0 && errno != ENOSYS) {
        perror("device_write_test5: tag_get");
        printf("device_write_test5: Elimino il tag e RIPROVO\n");
        tag_ctl(205, REMOVE_TAG);
        if (times < 2) {
            times++;
            goto retry;
        } else {
            printf("device_write_test5: Devi montare il modulo\n");
            FAILURE;
        }
    } else if (tag < 0 && errno == ENOSYS) {
        perror("device_write_test5");
        FAILURE;
    }

    // Crea n thread in ricezione
    for (i = 0; i < thread_number; i++) {
        pthread_create(&tid[i], NULL, thread_receiver5, (void *) tag);
    }


    // Aspetto che tutti i thread siano andati in ricezione
    while (thread_received5 != thread_number);

    sleep(60 * minuti); // Controllare durante questo tempo se il char device funziona correttamente
    if (readCharDev((char *) chdev_content, "/dev/tsdev_205") == -1) {
        printf("Errore nella prima readCharDev\n");
        tag_ctl((int) tag, AWAKE_ALL);
        tag_ctl((int) tag, REMOVE_TAG);
        FAILURE;
    }

    ret = tag_send((int) tag, 5, "ciao thread", 12);
    if (ret == -1) {
        perror("device_write_test5: errore nella send");
        goto elimina;
    }
    for (i = 0; i < thread_number; i++) {
        pthread_join(tid[i], &returns[i]);
    }

    if (readCharDev((char *) chdev_content, "/dev/tsdev_205") == -1) {
        printf("Errore nella seconda readCharDev\n");
        goto elimina;
    }

    elimina:
    // printf("Sto per rimuovere il tag...\n");
    ret = tag_ctl((int) tag, REMOVE_TAG);
    // printf("ho rimosso il tag\n");
    if (ret == -1) {
        perror("device_write_test5: errore nella REMOVE_TAG");
        FAILURE;
    }

    for (l = 0; l < thread_number; l++) {
        if (returns[l] == NOT_OK) {
            printf("I thread non hanno ricevuto lo stesso messaggio... (errore al thread %d)\n", l);
            FAILURE;
        }
    }
    SUCCESS;
}

/**
 * 1. Crea un tag
 * 2. Crea un processo figlio che va in ricezione su quel tag.
 * 3. Il processo padre invia un segnale SIGINT al processo figlio (il padre continua l'esecuzione)
 * 4. Test: il processo figlio deve svegliarsi
 * @return
 */
int signal_test6() {
    long ret;
    long tag;
    pid_t pid;
    int returns;

    sem_t *sem;
    sem = sem_open("semaforo6", O_CREAT, 0666, 0);

    tag = tag_get(206, CREATE_TAG, EVERYONE);
    if (tag < 0) {
        perror("signal_test6: tag_get");
        FAILURE;
    }

    if ((pid = fork()) == -1) {
        perror("signal_test6: fork");
        FAILURE;
    } else if (pid == 0) {
        char buffer[16];

        sem_post(sem);

        ret = tag_receive((int) (long) tag, 0, buffer, 0);
        if (ret < 0 && errno == EINTR) {
            return EXIT_SUCCESS;
        }
        printf("Dovevo ricevere un segnale SIGINT ma non l'ho ricevuto\n");
        return EXIT_FAILURE;
    } else {
        sem_wait(sem);
        kill(pid, SIGINT);

        wait(&returns);

        sem_unlink("semaforo6");
        sem_close(sem);

        if (tag_ctl((int) tag, REMOVE_TAG) < 0) {
            perror("signal_test6: tag_ctl ");
            FAILURE;
        }

        // Se il processo figlio termina con exit code 2, allora ha funzionato!!!
        if (returns == SIGINT) {
            SUCCESS;
        }
        printf("Figlio ha restituito %d", returns);
        FAILURE;
    }

}

FILE *perfFile;
int fd;

/**
 * Ricostruendo la stringa a ogni lettura: Media 1.2 e-05 secondi con read
 * Con RCU : 7 e-07, 7.3 e-07, 6.8 e-07
 */
void performanceReadCharDev() {

    char ch[1];
    while (read(fd, ch, 1) != 0); // 0 coincide con EOF

}

/**
 * Misura le performance delle letture del char device
 * @param times: quante letture fare (es. 100000)
 */
int chrdev_read_performance_test7(int times) {
    struct timeval t0, t1;
    unsigned int i;
    int key = 207;
    double results[times];
    double sum = 0.0;
    double average;
    long tag;

    if ((tag = tag_get(key, CREATE_TAG, EVERYONE)) == -1) {
        perror("chrdev_read_performance_test7: errore nella tag_get");
        FAILURE;
    }

    int retry = 0;
    char *path = "/dev/tsdev_207";
    /* Il seguente ciclo serve per aspettare che i permessi del char device siano effettivamente cambiati */
    do {
        if (retry) sleep(1);
        retry = access(path, R_OK);
    } while (retry == -1);

    perfFile = fopen(path, "r");
    fd = fileno(perfFile);

    if (perfFile == NULL) {
        printf("Impossibile aprire il file a causa dei permessi...\n");
        return -1;
    }

    for (i = 0; i < times; i++) {
        rewind(perfFile); // Riparto da 0 con il cursore
        gettimeofday(&t0, NULL);

        performanceReadCharDev(); // media 1.7 s dopo 100000 run

        gettimeofday(&t1, NULL);
        results[i] = (double) t1.tv_sec - t0.tv_sec + 1E-6 * ((double) t1.tv_usec - t0.tv_usec);
        sum += results[i];
    }

    average = sum / (double) times;

    printf("Tempo medio per leggere il char device %.2g secondi (%d letture in %.2g s)\n", average, times, sum);

    fclose(perfFile);

    if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
        perror("chrdev_read_performance_test7: errore nella tag_ctl");
        FAILURE;
    }


    SUCCESS;
}

#define BUFSIZE 2048
int remaining_senders = 0;

void copyFile(char *source, char *destination, int i) {
    /// size = numero di caratteri letti
    int size;
    char buffer[BUFSIZE];
    char string[60];
    int sd, dd; // source descriptor, destination descriptor.
    int written;


    /* Apriamo il file sorgente come RDOLY. Creati canale C e Sessione S*/
    sd = open(source, O_RDONLY, 0666);
    if (sd == -1) {
        perror("Errore nell'apertura del device driver");
        exit(1); //si chiudono tutti i canali di I/O e le sessioni.
    }

    /* Apriamo il file destinazione (esiste gia') e scrive dalla fine. Creato canale C' e sessione S'*/
    dd = open(destination, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (dd == -1) {
        perror("Errore nell'apertura del file di destinazione");
        exit(1);
    }

    /* Copia */
    do {
        /* lettura dalla sorgente di max BUFSIZE byte */
        size = read(sd, buffer, BUFSIZE);
        if (size == -1) { //se la size e' -1 si ferma
            printf("Errore di lettura dal file sorgente\n");
            exit(1);

        }
        //CONTROLLARE CHE QUELLO CHE HAI SCRITTO SIA UGUALE A QUELLO CHE HAI LETTO.
        /* scrivi da BUFSIZE sul file di destinazione per la dimensione del buffer (size) */
        written = write(dd, buffer, size);
        if (written == -1) {
            printf("Errore nel file di destinazione\n");
            exit(1);
        }
        //CONTROLLARE CHE QUELLO CHE HAI SCRITTO SIA UGUALE A QUELLO CHE HAI LETTO.
    } while (size > 0); //read ritorna 0 solo se arriviamo in fondo allo stream. (EOF)
    sprintf(string, "-------------[Fine thread %d]-------------\n", i);
    write(dd, string, strlen(string));
    close(sd); //chiudo i due file, le loro sessioni e i loro canali
    close(dd);
    //SE CI SONO ERRORI, chiudi-riapri-tronchi.
}

/**
 * Legge periodicamente i contenuti del char device e li scrive su un file.
 * @param tag
 * @return
 */
void *read_chrdev_thread(void *i) {
    char *chrdev_content;
    char path[20];
    AUDIT printf("Thread lettore %ld\n", (long) i);
    chrdev_content = malloc(sizeof(char) * 4096);
    if (!chrdev_content) {
        printf("Errore nella malloc nel thread ");
        return NOT_OK;
    }

    sprintf(path, "tsdev_log%ld.txt", (long) i);
    copyFile("/dev/tsdev_208", path, (int) (long) i);
    free(chrdev_content);
    return OK;
}

void *receiver_chrdev_thread(void *ptr) {
    long ret;
    int i = (int) (long) ptr;
    int updates_left = 10;
    char buffer[10];


    do {
        AUDIT printf("Thread receiver %d - vado in ricezione\n", i);
        ret = tag_receive(208, 16, buffer, 10);
        if (ret == -1) {
            char string[60];
            sprintf(string, "chrdev_rw_test8: Thread receiver %d - update left %d", i, updates_left);
            perror(string);
        }
        updates_left--;
    } while (updates_left > 0);

    return OK;
}

void *sender_chrdev_thread(void *ptr) {
    long ret;
    int i = (int) (long) ptr;
    int send_left = 10;
    char buffer[10] = "niente";

    do {
        AUDIT printf("Thread sender %d - vado in send\n", i);
        ret = tag_send(208, 16, buffer, 10);
        if (ret == -1 && errno == EINTR) {
            char string[60];
            sprintf(string, "chrdev_rw_test8: Thread sender %d - send_lefts %d", i, send_left);
            perror(string);
        }
        send_left--;
    } while (send_left > 0);

    __sync_fetch_and_add(&remaining_senders, -1);
    return OK;
}

/**
 * Esegue letture e scritture in modo concorrente sul char device.
 */
int chrdev_rw_test8(const int threadTrios) {
    long i;
    int key = 208;
    long tag;
    pthread_t tid[threadTrios * 3];
    void *thread_return[threadTrios * 3];

    if ((tag = tag_get(key, CREATE_TAG, EVERYONE)) == -1) {
        perror("chrdev_rw_test8: errore nella tag_get");
        FAILURE;
    }

    // Elimino tutti i tsdev_log.txt
    system("rm -f tsdev_log*");

    // Se non sono SUDO, aspetto che il file abbia i permessi in lettura...
    if (!(getuid() == 0 || geteuid() == 0))
        while (access("/dev/tsdev_208", R_OK) == -1);

    for (i = 0; i < threadTrios * 3; i++) {
        switch (i % 3) {
            case 0:
                pthread_create(&tid[i], NULL, read_chrdev_thread, (void *) i);
                break;
            case 1:
                pthread_create(&tid[i], NULL, receiver_chrdev_thread, (void *) i);
                break;
            case 2:
                __sync_fetch_and_add(&remaining_senders, 1);
                pthread_create(&tid[i], NULL, sender_chrdev_thread, (void *) i);
                break;
            default:
                printf("chrdev_rw_test8: errore grave, sto in default!!");
        }
    }

    // quando tutti i senders hanno finito, sveglio tutti i thread
    while (remaining_senders > 0);

    if (tag_ctl((int) tag, AWAKE_ALL) == -1) {
        perror("chrdev_rw_test8: errore nella awake_all");
    }


    for (i = 0; i < threadTrios; i++) {
        pthread_join(tid[i], &thread_return[i]);
    }

    if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
        perror("chrdev_rw_test8: errore nella REMOVE_TAG");
        FAILURE;
    }

    int isSuccess = (int) (long) OK;

    for (i = 0; i < threadTrios; i++) {
        isSuccess = isSuccess && (thread_return[i] == OK);
    }

    if (isSuccess) {
        SUCCESS;
    } else {
        FAILURE;
    }
}

void *waiting_thread(void *level) {
    int tag = 210;
    char buff[10];

    __sync_fetch_and_add(&thread_received9, 1);

    if (tag_receive(tag, (int) (long) level, buff, 10) == -1 && errno != EINTR) {
        perror("chrdev_10_or_more_waiting_test9 [THREAD FIGLIO]: errore nella tag_receive");
        return NOT_OK;
    }

    return OK;
}

/**
 * Fa attendere n thread in un singolo livello e legge il contenuto del buffer, verificando che sia corretto.
 * @return
 */
int chrdev_10_or_more_waiting_test9(int threads, int level) {
    long tag;
    pthread_t tid[threads];
    void *thread_return[threads];
    int key;
    key = 210;
    int ok = 1;
    int i;
    char *correct_string = malloc(sizeof(char) * 4096);
    char *read_string = malloc(sizeof(char) * 4096);

    char *header = "KEY\tEUID\tLEVEL\t#THREADS\n";
    char line[100];
    strncpy(correct_string, header, strlen(header));
    for (i = 0; i < MAX_LEVELS; i++) {
        snprintf(line, 100, "%ld\t%d\t%d\t%d\n", tag, geteuid(), i, (i == level ? threads : 0));
        strncat(correct_string, line, strlen(line));
    }

    thread_received9 = 0;

    if ((tag = tag_get(key, CREATE_TAG, EVERYONE)) == -1) {
        perror("chrdev_10_or_more_waiting_test9: errore nella tag_get");
        FAILURE;
    }

    for (i = 0; i < threads; i++) {
        pthread_create(&tid[i], NULL, waiting_thread, (void *) tag);
    }

    while (thread_received9 < threads);

    readCharDev(read_string, "/dev/tsdev_210");

    if (tag_ctl((int) tag, AWAKE_ALL) == -1) {
        perror("chrdev_10_or_more_waiting_test9: errore nella REMOVE_TAG");
        FAILURE;
    }

    for (i = 0; i < threads; i++) {
        pthread_join(tid[i], &thread_return[i]);
        ok = ok && thread_return[i] == OK;
    }

    if (tag_ctl((int) tag, REMOVE_TAG) == -1) {
        perror("chrdev_10_or_more_waiting_test9: errore nella REMOVE_TAG");
        FAILURE;
    }

    if (ok && strncmp(correct_string, read_string, strlen(correct_string)) == 0) {
        SUCCESS;
    } else {
        FAILURE;
    }

}
