//
// Created by giacomo on 23/04/21.
//

#include <string.h>
#include <errno.h>
#include "user_program.h"

int getNumber() {
    char input[256];
    int value;
    fgets(input, 256, stdin);
    value = (int) strtol(input, NULL, 10);
    return value;
}

int main() {
    char buffer[MAX_MESSAGE_SIZE];
    char receive_buffer[MAX_MESSAGE_SIZE];
    char path[256];
    char line[128];
    int number, key, permission, livello;
    long tag, result;
    FILE *file;
    restart:
    printf("Scegli il numero della system call o funzionalita' da provare...\n");
    printf("1: CREA un nuovo tag service\n");
    printf("2: APRI un tag service gia' esistente\n");
    printf("3: INVIA un messaggio tramite tag service\n");
    printf("4: RICEVI un messaggio tramite tag service\n");
    printf("5: SVEGLIA i thread in ricezione su un tag service\n");
    printf("6: ELIMINA un tag service\n");
    printf("7: LEGGI char device di un tag service\n");
    printf("8: ESCI. Puoi anche usare Ctrl+C.\n");
    number = getNumber();
    switch (number) {
        case 1: // CREATE_TAG
            printf("Scegli una chiave. 0 per IPC_PRIVATE: ");
            number = getNumber();
            key = (number == 0) ? IPC_PRIVATE : number;
            printf("Scrivi 0 se vuoi essere il proprietario esclusivo del tag: ");
            permission = (getNumber() == 0) ? ONLY_OWNER : EVERYONE;
            tag = tag_get(key, CREATE_TAG, permission);
            if (tag < 0) {
                printf("---------------------------------------------\n");
                perror("Errore nella creazione del tag:");
                printf("---------------------------------------------\n");
                goto exit;
            }
            printf("---------------------------------------------\n");
            printf("Tag service creato correttamente con tag descriptor %ld\n", tag);
            break;
        case 2: // OPEN_TAG
            printf("Scegli una chiave. 0 per IPC_PRIVATE (dara' errore): ");
            number = getNumber();
            key = (number == 0) ? IPC_PRIVATE : number;
            printf("Scrivi 0 se vuoi essere il proprietario esclusivo del tag (E' indifferente): ");
            permission = (getNumber() == 0) ? ONLY_OWNER : EVERYONE;
            tag = tag_get(key, OPEN_TAG, permission);
            if (tag < 0) {
                printf("---------------------------------------------\n");
                perror("Errore nella apertura del tag:");
                printf("---------------------------------------------\n");
                goto exit;
            }
            printf("---------------------------------------------\n");
            printf("Il tag relativo alla chiave %d e' %ld\n", key, tag);
            break;
        case 3: // send
            printf("Scegli un tag: ");
            tag = getNumber();
            printf("Scegli un livello a cui inviare: ");
            livello = getNumber();
            printf("Messaggio: ");
            fgets(buffer, MAX_MESSAGE_SIZE, stdin);
            buffer[strlen(buffer) - 1] = '\0'; // per sovrascrivere \n
            result = tag_send((int) tag, livello, buffer, strlen(buffer));
            memset(buffer, 0, strlen(buffer));
            if (result < 0) {
                printf("---------------------------------------------\n");
                if(errno != EINTR){
                    printf("Thread svegliato da AWAKE_ALL\n\n");
                    goto restart;
                }
                perror("Errore nell'invio del messaggio:");
                printf("---------------------------------------------\n");
                goto exit;
            }
            printf("---------------------------------------------\n");
            printf("Messaggio correttamente inviato...\n");
            break;
        case 4: // receive
            printf("Scegli un tag: ");
            tag = getNumber();
            printf("Scegli un livello da cui mettersi in ricezione: ");
            livello = getNumber();
            printf("Entro in ricezione, in attesa di un messaggio o di un comando awake_all\n");
            result = tag_receive((int) tag, livello, receive_buffer, MAX_MESSAGE_SIZE);
            if (result < 0) {
                printf("---------------------------------------------\n");
                perror("Errore nell'invio del messaggio:");
                printf("---------------------------------------------\n");
                goto exit;
            }
            printf("---------------------------------------------\n");
            printf("Messaggio ricevuto dal tag, livello (%ld,%d):\n%s\n", tag, livello, receive_buffer);
            memset(receive_buffer, 0, MAX_MESSAGE_SIZE);
            break;
        case 5: // awake_all
            printf("Scegli un tag da svegliare: ");
            tag = getNumber();
            result = tag_ctl((int) tag, AWAKE_ALL);
            if (result < 0) {
                printf("---------------------------------------------\n");
                perror("Impossibile svegliare i thread in attesa:");
                goto exit;
            }
            printf("---------------------------------------------\n");
            printf("Eseguita AWAKE_ALL sul tag %ld\n", tag);
            break;
        case 6: // remove_tag
            printf("Scegli un tag da rimuovere: ");
            tag = getNumber();
            result = tag_ctl((int) tag, REMOVE_TAG);
            if (result < 0) {
                printf("---------------------------------------------\n");
                perror("Impossibile rimuovere il tag:");
                goto exit;
            }
            printf("---------------------------------------------\n");
            printf("Tag %ld rimosso con successo\n", tag);
            break;
        case 7: // leggi char device
            printf("Inserire il tag del char device: ");
            tag = getNumber();
            sprintf(path, "/dev/tsdev_%ld", tag);
            file = fopen(path, "r");

            if (file == NULL) {
                printf("---------------------------------------------\n");
                printf("File %s non esistente.\n", path);
                break;
            }
            printf("---------------------------------------------\n");
            printf("Path del char device file: %s\n", path);
            memset(line, 0, strlen(line));
            while (fgets(line, sizeof(line), file)) { // leggo l'intero file
                printf("%s", line);
            }

            fclose(file);
            break;
        case 8: // esci
            printf("---------------------------------------------\n");
            printf("Uscita.\n");
            goto exit;
        default:
            printf("---------------------------------------------\n");
            printf("Inserire un numero tra 1 e 8\n");
            break;

    }
    printf("---------------------------------------------\n");
    goto restart;
    exit:
    return 0;
}
