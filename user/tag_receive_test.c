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

// char *semaName = "sharedSemaphore";
static int thread_received1 = 0;
static int thread_received2 = 0;
static int thread_received3 = 0;
static int thread_received4 = 0;
static int thread_received5 = 0;
static int thread_received6 = 0;

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

int readCharDev(char *returned) {
    FILE *f;
    int retry = 0;
    char *path = "/dev/tsdev_205";
    /* Il seguente ciclo serve per aspettare che i permessi del char device siano effettivamente cambiati */
    do {
        if (retry) sleep(1);
        retry = access(path, R_OK);
    } while (retry == -1);

    f = fopen(path, "r");
    if (f == NULL) {
        printf("Impossibile aprire il file a causa dei permessi...\n");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    char *string = malloc(fsize + 1);
    int ch;
    fgets(string, (int) fsize, f);
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
    if (tag < 0 && errno != ENOSYS)  {
        perror("device_write_test5: tag_get");
        printf("device_write_test5: Elimino il tag e RIPROVO\n");
        tag_ctl(205, REMOVE_TAG);
        if(times < 2){
            times++;
            goto retry;
        } else {
            printf("device_write_test5: Devi montare il modulo\n");
            FAILURE;
        }
    } else if(tag < 0 && errno == ENOSYS){
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
    if (readCharDev((char *) chdev_content) == -1) {
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

    if (readCharDev((char *) chdev_content) == -1) {
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

    thread_received6 = 0;

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
    } else if (pid == 0){
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
        if(returns == SIGINT){
            SUCCESS;
        }
        printf("Figlio ha restituito %d", returns);
        FAILURE;
    }

}

FILE * perfFile;
int fd;

/**
 * Media 1.2 * 10^-5 secondi con read
 */
void performanceReadCharDev(){

    char ch[1];
    while (read(fd, ch, 1) != 0); // 0 coincide con EOF

}

int chrdev_read_performance_test7(){
    struct timeval t0, t1;
    unsigned int i;
    int times = 10;
    int key = 207;
    double results[times];
    double sum = 0.0;
    double average;
    long tag;

    if((tag = tag_get(key, CREATE_TAG, EVERYONE)) == -1){
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

    for(i = 0; i < times; i++){
        rewind(perfFile); // Riparto da 0 con il cursore
        gettimeofday(&t0, NULL);

        performanceReadCharDev(); // media 1.7 s dopo 100000 run

        gettimeofday(&t1, NULL);
        results[i] = (double) t1.tv_sec - t0.tv_sec + 1E-6 * ((double) t1.tv_usec - t0.tv_usec);
        sum += results[i];
    }

    average = sum/(double)times;

    printf("Tempo impiegato in media dopo %d chiamate %.2g secondi (totale = %.2g s)\n", times, average, sum);

    fclose(perfFile);

    if(tag_ctl((int) tag, REMOVE_TAG) == -1){
        perror("chrdev_read_performance_test7: errore nella tag_ctl");
        FAILURE;
    }


    SUCCESS;
}