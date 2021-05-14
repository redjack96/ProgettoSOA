#include "tag_service.h"
#include "tag_device_driver.h"
#include <linux/rcupdate.h>

#define MODNAME "TAG-SERVICE"
#define printk AUDIT printk
#define LOG(msg) AUDIT _LOG(msg, MODNAME, KERN_DEFAULT)
#define LOG1(msg, num) AUDIT _LOG1(msg, MODNAME, KERN_DEFAULT, num)
#define ERR(msg) AUDIT _LOG(msg, MODNAME, KERN_ERR)
#define ERR1(msg, num) AUDIT _LOG1(msg, MODNAME, KERN_ERR, num)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giacomo Lorenzo Rossi  <giacomo.redjack@gmail.com>");
MODULE_DESCRIPTION("TAG-based message exchange");
/* Global variable definitions*/
unsigned long *hacked_ni_syscall = NULL;
unsigned long **hacked_syscall_tbl = NULL;

// static altrimenti e' definito due volte...
static ts_management tsm;

// trovi i parametri del modulo in /sys/modules/the_tag_service/parameters/...
unsigned long sys_call_table_address = 0x0;

module_param(sys_call_table_address, ulong, 0660);

unsigned long sys_ni_syscall_address = 0x0;

module_param(sys_ni_syscall_address, ulong, 0660);

int free_entries[MAX_FREE]; // definita in usctm.h
module_param_array(free_entries, int, NULL, 0660);

#define NO 0
#define YES 1
DECLARE_WAIT_QUEUE_HEAD(the_queue);


/**
 * Restituisce 1, se la entry è disponibile per creare un nuovo tag_service, altrimenti 0
 * @param tag
 * @return
 */
int is_entry_available(int tag) {
    if (tsm.all_tag_services[tag] != NULL) return FAILURE;
    return SUCCESS;
}

/**
 * @return restituisce vero se non e' possibile creare ulteriori tag services.
 */
int no_more_tag_service(void) {
    return (tsm.remaining_entries == 0);
}

/**
 * Aggiorna e restituisce la prima entry libera in tsm.first_free_entry
 * @return l'indice della prima entry libera. Se è tutto pieno, restituisce -1;
 */
int find_first_entry_available(void) {
    int i;
    if (is_entry_available(tsm.first_free_entry)) goto ret;
    if (no_more_tag_service()) goto full;

    // Parte dall'inizio e  prende la prima entry disponibile.
    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        if (is_entry_available(i)) { // se l'entry è vuota, abbiamo finito
            tsm.first_free_entry = i;
            break;
        }
    }
    ret:
    return tsm.first_free_entry;
    full:
    return -1;
}

/**
 * Funzione che ricava il tag a partire dalla chiave
 * @param key un intero qualsiasi
 * @return il tag ottenuto a partire dalla chiave.
 */
int hash_function(int key) {
    return key % MAX_TAG_SERVICES;
}

/**
 * Funzione richiamata da tag_send dopo aver svegliato i thread: rimuove il messaggio scritto, ma solo
 * quando tutti i lettori hanno finito di leggere.
 * @param rp
 */
void free_after_send(struct rcu_head *rp) {
    tag_level *level_to_free;
    int tag;

    level_to_free = container_of(rp, tag_level, rcu);
    level_to_free->message_ready = NOT_READY;
    // imposta a zero i contenuti del messaggio, per migliore sicurezza, ma non libera la memoria
    memset(level_to_free->message, 0, MAX_MESSAGE_SIZE);

    // ricavo il tag e libero il lock di accesso corretto
    tag = level_to_free->tag;

    // nessun altro può scrivere finché non si libera questo lock
    mutex_unlock(&tsm.access_lock[tag]);

    printk("%s: Daemon thread %d - status: %s. Esco dall'attesa di access_lock\n", MODNAME, current->pid,
           (level_to_free->message_ready == NOT_READY) ? "NOT_READY" : "READY");
}

/**
 * Funzione richiamata da AWAKE ALL al termine della sezione critica RCU, dopo che tutti i
 * thread svegliati sono usciti dalla sezione.*/
void reset_awake_condition(struct rcu_head *rp) {
    tag_service *ts;

    // ricavo la struttura tag_service a partire dal suo membro tag_rcu
    ts = container_of(rp, tag_service, tag_rcu);

    // Resetto la condizione di risveglio, per non far svegliare immediatamente i thread che arrivano dopo una AWAKE_ALL
    ts->awake_request = NO;

    // Il lock sta in AWAKE_ALL... qui sblocco i thread che vogliono scrivere o eliminare (perche' solo questi modificano la struttura dati).
    mutex_unlock(&tsm.access_lock[ts->tag]);
    LOG("Demone RCU: resettata condizione per awake all.");
}

/**
 * sys_tag_get: crea o istanzia un tag_service
 * @param key chiave intera con cui ricavare il descrittore del tag, oppure IPC_PRIVATE per far scegliere al kernel
 * @param command CREATE_TAG o OPEN_TAG. OPEN_TAG fallisce se si usa la chiave IPC_PRIVATE.
 * @param permission ONLY_OWNER (solo l'utente proprietario puo' usare il tag) o EVERYONE. Ignorato se si usa OPEN_TAG
 * @returns il tag descriptor se ha successo, -1 se c'e' un errore ed imposta errno.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)

__SYSCALL_DEFINEx(3, _tag_get, int, key, int, command, int, permission) {
#else
    asmlinkage long sys_tag_get(int key, int command,
                                int permission) { // asmlinkage: riceve parametri dai registri di processore, non dallo stack.
#endif
    int tag;
    int lev;
    int lev_created = 0;
    tag_service *ts;

    printk("%s: il thread %d esegue tag_get con key=%d, command=%s, permission=%s\n",
           MODNAME, current->pid, key,
           command == CREATE_TAG ? "CREATE_TAG" : command == OPEN_TAG ? "OPEN_TAG" : "UNKNOWN",
           permission == EVERYONE ? "EVERYONE" : permission == ONLY_OWNER ? "ONLY_OWNER" : "UNKNOWN");

    try_module_get(THIS_MODULE);
    if ((permission != EVERYONE && permission != ONLY_OWNER) ||
        (command != CREATE_TAG && command != OPEN_TAG)) { // se uno di questi e' nullo
        ERR("tag_get: I parametri permission o command non sono compatibili");
        module_put(THIS_MODULE);
        return -EINVAL;
    }


    if (key != IPC_PRIVATE) {
        tag = hash_function(key);
    } else {
        tag = find_first_entry_available();
        // Controllo se ha fallito (non c'e' piu' spazio nell'array)
        if (tag == -1 && command == CREATE_TAG) {
            ERR("Impossibile creare un altro tag_service: spazio insufficiente nella struttura dati");
            module_put(THIS_MODULE);
            return -ENOMEM;
        }
    }

    // Non voglio che delle tag_get concorrenti instanzino piu' volte lo stesso tag
    /* Inizio sezione critica */
    mutex_lock(&tsm.access_lock[tag]);

    /*
     * Scenari:
     * 1. se due thread con stessa chiave != IPC_PRIVATE provano ad entrare, solo uno riesce.
     * 2. se due thread con IPC_PRIVATE eseguono la hash function per creare un nuovo tag e ottengono
     *  - due tag diversi, entreranno entrambi.
     *  - lo stesso tag a causa della concorrenza, solo uno alla volta entrerà e il secondo fallisce perche' la entry e' occupata.
     *  - se uno ottiene il tag e l'altro -1 perche' finisce lo spazio, solo il primo va avanti.
     * 3. se vengono eseguite in concorrenza due CREATE_TAG una IPC_PRIVATE e l'altra no:
     *  - se nella concorrenza ottengono lo stesso valore, la prima che entra andra' avanti, l'altra fallisce
     *  - se ottengono due valori diversi e validi: entreranno in concorrenza per creare ciascuna dei tag diversi
     *  - se la IPC_PRIVATE ottiene -1, fallira' prima di entrare in sezione critica.
     *  - se la key richiesta dalla non IPC_PRIVATE e' gia' occupata, fallira'
     */


    switch (command) {
        case CREATE_TAG:

            // controllo se il tag_service non sia già stato creato...
            if (!is_entry_available(tag)) {
                mutex_unlock(&tsm.access_lock[tag]);
                ERR1("Chiave gia' utilizzata: impossibile creare un nuovo tag service con chiave", key);
                module_put(THIS_MODULE);
                return -ENOKEY; // required key not available
            }

            // alloca la memoria per il tag_service (Non bloccante perche' sto in sezione critica). Deallocato allo smontaggio
            ts = kzalloc(sizeof(tag_service), GFP_ATOMIC);
            if (!ts) {
                mutex_unlock(&tsm.access_lock[tag]);
                ERR("Errore nella kzalloc durante sys_tag_get");
                module_put(THIS_MODULE);
                return -EFAULT; // bad address
            }

            /*
             * Se si usa una key IPC_PRIVATE (inter-process-communication private), si deve creare un nuovo tag_service
             * e bisogna restituire sempre un nuovo tag service ogni volta che lo si usa.
             * Nel char device, la chiave IPC_PRIVATE e' identificata con -1
             */
            ts->key = (key == IPC_PRIVATE ? -1 : key);
            ts->permission = permission;
            ts->owner_euid = EUID;
            ts->owner_uid = UID;
            ts->awake_request = NO;
            ts->tag = tag; // Se l'elemento e' gia' occupato, la tag_get fallisce prima
            ts->level = kzalloc(sizeof(tag_level) * MAX_LEVELS, GFP_ATOMIC); // Deallocato allo smontaggio
            if (!ts->level) {
                mutex_unlock(&tsm.access_lock[tag]);
                ERR1("Impossibile allocare spazio per i livelli del tag", tag);
                goto fail_no_mem;
            }
            // incrementato atomicamente quando un thread attende il messaggio,
            // decrementato atomicamente quando quel thread FINISCE di leggere il messaggio
            ts->thread_waiting_message_count = 0;
            // IMPOSTATO A yes quando si vuole eliminare tutto il tag. Le mie system_call falliscono se e' YES
            ts->lazy_deleted = NO;
            for (lev = 0; lev < MAX_LEVELS; lev++) {
                ts->level[lev].thread_waiting = 0;
                // nessun messaggio pronto all'inizio.
                ts->level[lev].message_ready = NOT_READY;
                ts->level[lev].size = 0;
                ts->level[lev].tag = tag;
                // Alloca tutti i messaggi in anticipo. Memoria in più, migliori prestazioni. Deallocato allo smontaggio
                ts->level[lev].message = kzalloc(sizeof(char) * MAX_MESSAGE_SIZE,
                                                 GFP_ATOMIC); // non voglio che sia deschedulato, siamo in sezione critica
                if (!ts->level[lev].message) {
                    mutex_unlock(&tsm.access_lock[tag]);
                    ERR1("Impossibile allocare spazio per i messaggi del tag", tag);
                    goto fail_no_mem;
                }
                lev_created++;
            }

            // aggiungo il tag_service all'array di tutti i tag_services.
            tsm.all_tag_services[ts->tag] = ts; // Ora eventuali thread in attesa di creare lo stesso tag service vedranno la entry occupata e falliranno

            /* Fine della sezione critica */
            mutex_unlock(&tsm.access_lock[tag]);

            atomic_dec((atomic_t *) &(tsm.remaining_entries));

            // Creo il char device corrispondente. IMPORTANTE: uso il tag descriptor come minor!!
            // Inoltre, inizializza il buffer con i contenuti corretti
            ts_create_char_device_file(tag);

            printk("%s: installato un tag-service in posizione %d e "
                   "il chardevice associato con (MAJOR, MINOR) = (%d,%d)\n", MODNAME, ts->tag,
                   tsm.major, tag);

            module_put(THIS_MODULE);
            return tag;
        fail_no_mem:
            for (lev = 0; lev < lev_created; lev++) {
                kfree(ts->level[lev].message); // messaggi dei livelli
            }
            kfree(ts->level); // livelli
            kfree(ts);
            module_put(THIS_MODULE);
            return -ENOMEM;
        case OPEN_TAG:
            // se sto aprendo un tag gia' creato, vado avanti
            if (!is_entry_available(tag) && key != IPC_PRIVATE) {
                ts = tsm.all_tag_services[tag];
                // La open fallisce se il tag service è stato eliminato logicamente
                if (ts->lazy_deleted == YES) {
                    mutex_unlock(&tsm.access_lock[tag]);
                    ERR("Impossibile aprire il tag service perche' e' stato eliminato (logicamente)!");
                    module_put(THIS_MODULE);
                    return -EBADR; // invalid request descriptor
                }


                if (ts->owner_euid != EUID && ts->permission == ONLY_OWNER) {
                    mutex_unlock(&tsm.access_lock[tag]);
                    printk("%s: L'utente non dispone dei permessi per accedere al tag service %d, "
                           "permission = %s, owner_euid = %d, UID = %d, EUID = %d\n", MODNAME, tag,
                           ts->permission == ONLY_OWNER ? "ONLY_OWNER" : "EVERYONE",
                           ts->owner_euid, UID, EUID);
                    module_put(THIS_MODULE);
                    return -EACCES; // Permission Denied
                }
                mutex_unlock(&tsm.access_lock[tag]);
                LOG1("Aperto il tag_service", tag);
                module_put(THIS_MODULE);
                return tag;
            } else { // se la entry è occupata o si usa open con IPC_PRIVATE, fallisce
                mutex_unlock(&tsm.access_lock[tag]);
                ERR("Impossibile aprire un tag service inesistente o con chiave IPC_PRIVATE. Prima bisogna istanziarlo.");
                module_put(THIS_MODULE);
                return -EBADR; // invalid request descriptor
            }
        default:
            mutex_unlock(&tsm.access_lock[tag]);
            ERR("tag_get - Comando sconosciuto");
            module_put(THIS_MODULE);
            return -ENOSYS; // function not implemented
    }
}


/**
 * sys_tag_send: permette di inviare un messaggio a tutti i thread in attesa su un certo tag e livello.
 * @param tag: il valore restituito da tag_get
 * @param level: il livello tra 0 e MAX_LEVELS-1 a cui inviare il messaggio
 * @param buffer: il messaggio utente da inviare
 * @param size: la dimensione del messaggio utente. Se supera MAX_MESSAGE_SIZE, il messagio viene troncato.
 * @returns 0 se ha successo, -1 se c'e' un errore ed imposta errno.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)

__SYSCALL_DEFINEx(4, _tag_send, int, tag, int, level, char*, buffer, size_t, size) {
#else
    asmlinkage long sys_tag_send(int tag, int level, char *buffer,
                                 size_t size) { // asmlinkage: riceve parametri dai registri di processore, non dallo stack.
#endif
    size_t real_size;
    unsigned long not_copied;
    tag_service *ts;
    char *intermediate_buffer;

    try_module_get(THIS_MODULE);
    printk("%s: il thread %d esegue  tag_send con parametri tag=%d, livello=%d, size=%zu\n",
           MODNAME, current->pid, tag, level, size);

    if (tag < 0 || tag > MAX_TAG_SERVICES) {
        ERR1("tag_send: tag fuori dal range 0-", MAX_TAG_SERVICES - 1);
        return -EINVAL;
    }

    // Il livello e' nel range??
    if (level < 0 || level >= MAX_LEVELS) {
        printk(KERN_ERR "%s: tag_send: Il livello deve essere tra 0 e %d (fornito %d).", MODNAME, MAX_LEVELS, level);
        module_put(THIS_MODULE);
        return -EINVAL; // Invalid argument
    }


    // Copio il messaggio che la send deve inviare in un buffer intermedio fuori da sezione critica, per NON far degradare le prestazioni.
    intermediate_buffer = (void *) get_zeroed_page(GFP_KERNEL);

    // controlla la dimensione della stringa e la riduce se necessario
    real_size = size > MAX_MESSAGE_SIZE - 1 ? MAX_MESSAGE_SIZE - 1 : strlen(buffer) + 1;

    not_copied = copy_from_user(intermediate_buffer, buffer, real_size);

    ts = tsm.all_tag_services[tag];

    // Il Tag service esiste ?
    if (!ts) {
        free_pages((unsigned long) intermediate_buffer, 0);
        printk(KERN_ERR "%s: Il thread %d sta tentando di inviare messaggi da un tag_service inesistente.\n", MODNAME,
               current->pid);
        module_put(THIS_MODULE);
        return -EBADF; // bad file descriptor
    }
    /*
     * Inizio adesso con la sezione critica. Il buffer intermedio e' gia stato inizializzato con copy_from_user (preemptable)
     * perche' non ha bisogno di accedere al tag_service. Ora quando e' il suo turno, tag_send
     * copia con memcpy() il buffer intermedio nella struttura tag_service. Questo migliora le prestazioni.
     *
     * Fix: ora i permessi vengono controllati dopo aver preso il lock.
     * Dato un tag service con descrittore x e permessi EVERYONE, se un thread A prova a inviare un messaggio,
     * non e' possibile che un altro thread B (appartenente a un altro utente) che usa tag_ctl(x, REMOVE_TAG)
     * e poi tag_get(x + k*MAX_TAG_SERVICES, CREATE_TAG, ONLY_OWNER), per k intero (0,1,2,...),
     * nella concorrenza permetta ad A di inviare comunque pur appartenendo a un altro utente, perché:
     * - se l'ordine di accesso al mutex è A - B:
     *     A inizia la tag_send e controlla i permessi. B non puo' eliminare perche' deve attendere di acquisire il mutex
     * - se l'ordine è B - A.
     *     supponendo che non ci siano thread in attesa di ricevere, B esegue la tag_ctl(REMOVE_TAG) ed elimina il tag service
     *     ora ci sono due possibilita' nell'accedere al mutex:
     *      - Entra A con tag_send: B viene deschedulato e A non appena supera l'istruzione seguente scopre che il thread e' stato eliminato e rinuncia all'invio
     *      - Entra B con CREATE_TAG e poi A va in esecuzione: il tag è stato ricreato, ma se non ha il permesso di accedere a esso, fallisce.
     * Inoltre:
     *  - non può accadere che remove, send e create siano concorrenti perche' prendono lo stesso mutex
     *  - la tag_receive ha due possibilita rispetto alla tag send:
     *     .. se arriva prima la send: nessuno riceve il messaggio e poi la receive rimane in attesa
     *     .. se arriva prima la receive: questa va in attesa e riceve il messaggio non appena e' inviato
     */
    mutex_lock(&tsm.access_lock[tag]);

    // Il tag service e' stato eliminato logicamente ?
    if (ts->lazy_deleted == YES) {
        mutex_unlock(&tsm.access_lock[tag]);
        free_pages((unsigned long) intermediate_buffer, 0);
        ERR("Il tag service su cui vuole accedere questo thread e' stato eliminato.");
        module_put(THIS_MODULE);
        return -EBADF; // bad file descriptor
    }

    // L'Utente ha i permessi corretti ?
    if (ts->owner_euid != EUID && ts->permission == ONLY_OWNER) {
        mutex_unlock(&tsm.access_lock[tag]);
        free_pages((unsigned long) intermediate_buffer, 0);
        ERR1("L'utente non dispone dei permessi per inviare messaggi dal tag service", tag);
        module_put(THIS_MODULE);
        return -EACCES; // Permission Denied
    }


    // Non bloccante!!
    memcpy(ts->level[level].message, intermediate_buffer, real_size - not_copied);

    // aggiungo il carattere terminatore, verra' contato tra i byte del messaggio
    ts->level[level].message[real_size - not_copied] = '\0';
    // aggiungo la size del messaggio nella struttura tag_service, per restituire i byte corretti con la tag_receive
    ts->level[level].size = real_size - not_copied;
    ts->level[level].message_ready = READY;

    if (ts->thread_waiting_message_count > 0) {
        wake_up_all(&the_queue);
        call_rcu(&ts->level[level].rcu, free_after_send); // ESEGUO UNLOCK CON LA RCU !!!
    } else { // altrimenti pulisco subito il messaggio e sblocco il lock!!
        ts->level[level].message_ready = NOT_READY;
        memset(ts->level[level].message, 0, MAX_MESSAGE_SIZE);
        mutex_unlock(&tsm.access_lock[tag]);
        LOG("Esco dall'attesa senza chiamare rcu");
    }

    // libero la pagina allocata dal buddy-allocator
    free_pages((unsigned long) intermediate_buffer, 0);

    // Se il buffer utente era troppo grande, avviso l'utente di aver inviato un messaggio troncato a MAX_MESSAGE_SIZE-1
    if (size > MAX_MESSAGE_SIZE - 1) {
        ERR1("Il messaggio e' stato troncato perche' supera la massima dimensione gestita (compreso terminatore):",
             MAX_MESSAGE_SIZE);
        module_put(THIS_MODULE);
        return -EMSGSIZE;
    }
    module_put(THIS_MODULE);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)

/**
 * sys_tag_receive: permette a un thread di aspettare un messaggio in arrivo in una coppia tag, livello.
 * @param tag: il valore restituito della tag_get
 * @param level: il livello in cui mettersi in ricezione
 * @param buffer: buffer utente su cui salvare il messaggio ricevuto
 * @param size: dimensione del buffer utente
 * @returns 0 se ha successo, -1 se c'e' un errore ed imposta errno.
 */
__SYSCALL_DEFINEx(4, _tag_receive, int, tag, int, level, char*, buffer, size_t, size) {
#else
    asmlinkage long sys_tag_receive(int tag, int level, char *buffer,
                                    size_t size) { // asmlinkage: riceve parametri dai registri di processore, non dallo stack.
#endif
    tag_service *ts;
    unsigned long not_copied;
    size_t real_size;

    try_module_get(THIS_MODULE);

    if (tag < 0 || (tag > MAX_TAG_SERVICES - 1)) {
        ERR1("per tag_receive() tag fuori range 0,", MAX_TAG_SERVICES);
        module_put(THIS_MODULE);
        return -EINVAL;
    }

    if (level < 0 || level >= MAX_LEVELS) {
        printk(KERN_ERR "%s: Il livello deve essere tra 0 e %d (fornito %d).", MODNAME, MAX_LEVELS, level);
        module_put(THIS_MODULE);
        return -EINVAL; // Invalid argument
    }

    ts = tsm.all_tag_services[tag];

    // controllo che il tag service esista
    if (!ts) {
        printk(KERN_ERR "%s: Il thread %d sta tentando di ricevere messaggi da un tag_service inesistente.\n", MODNAME,
               current->pid);
        module_put(THIS_MODULE);
        return -EBADF; // bad file descriptor
    }

    // Controllo se il tag service e' stato eliminato logicamente
    if (ts->lazy_deleted == YES) {
        ERR("Il tag su cui si vuole ricevere e' stato eliminato");
        module_put(THIS_MODULE);
        return -EBADF; // bad file descriptor
    }

    // Controllo se l'utente ha i permessi per ricevere da questo tag
    if (ts->owner_euid != EUID && ts->permission == ONLY_OWNER) {
        ERR1("L'utente non dispone dei permessi per ricevere messaggi dal tag service ", tag);
        module_put(THIS_MODULE);
        return -EACCES; // Permission Denied
    }

    // Se il buffer utente e' NULL, fallisce
    if (!buffer) {
        ERR("Il buffer punta a un'area di memoria non mappata");
        module_put(THIS_MODULE);
        return -EACCES;
    }

    // Correggo la dimensione del messaggio se e' troppo grande
    real_size = (size >= MAX_MESSAGE_SIZE - 1) ? MAX_MESSAGE_SIZE - 1 : size;

    atomic_inc((atomic_t *) &(ts->thread_waiting_message_count));
    atomic_inc((atomic_t *) &(ts->level[level].thread_waiting));


    // Aggiorna il char device ricostruendo la stringa
    update_chrdev(tag, level);

    /*
     * Il thread va in wait_queue. Sara' svegliato quando una delle seguenti condizioni si verifica
     * 1. arriva un segnale posix
     * 2. viene svegliato tramite tag_ctl con comando AWAKE_ALL
     * 3. viene svegliato tramite tag_send perche' viene inviato un messaggio al thread
     *
     * La condizione non si controlla automaticamente: il thread va svegliato.
     */
    wait_event_interruptible(the_queue, ts->level[level].message_ready == READY || ts->awake_request == YES);

    // Non c'e' bisogno di controllare per lazy_deleted == YES perche' essendoci ancora almeno un thread in ricezione, le eventuali REMOVE_TAG falliscono
    // non mi blocco, voglio solo leggere. Quando tutti hanno letto, permetto alla tag_receive di azzerare il messaggio
    rcu_read_lock();

    // controlla se sono arrivati dei segnali POSIX
    if (signal_pending(current)) {
        rcu_read_unlock();
        atomic_dec((atomic_t *) &ts->thread_waiting_message_count);
        atomic_dec((atomic_t *) &ts->level[level].thread_waiting);
        update_chrdev(tag, level);
        LOG("Ricevuto un segnale di terminazione");
        module_put(THIS_MODULE);
        return -EINTR; // Interrupted system call
    }

    if (!(ts->level[level].message_ready == READY)) {
        rcu_read_unlock();
        atomic_dec((atomic_t *) &ts->thread_waiting_message_count);
        atomic_dec((atomic_t *) &ts->level[level].thread_waiting);
        update_chrdev(tag, level);
        printk("%s: thread (pid = %d) - svegliato da comando AWAKE_ALL. Esco senza messaggi{thread rimasti in attesa nel tag: %lu}\n",
               MODNAME, current->pid, ts->thread_waiting_message_count);
        module_put(THIS_MODULE);
        return -EINTR; // Interrupted system call
    }

    // eventualmente ricalcolo la dimensione
    if (ts->level[level].size < real_size) {
        real_size = ts->level[level].size;
    }

    if (ts->level[level].message != NULL) {
        not_copied = copy_to_user(buffer, ts->level[level].message, real_size); // real_size
        if (not_copied > 0) {
            LOG1("Byte NON copiati ", not_copied);
        }
    } else {
        LOG("Messaggio nullo...");
    }
    rcu_read_unlock(); // fine della sezione critica RCU

    atomic_dec((atomic_t *) &ts->thread_waiting_message_count);
    atomic_dec((atomic_t *) &ts->level[level].thread_waiting);
    update_chrdev(tag, level);

    LOG1("Esco dall'attesa. Messaggio ricevuto lungo", real_size);
    module_put(THIS_MODULE);

    return 0;
}

/**
 * sys_tag_ctl: permette di svegliare i thread in attesa o di eliminare un tag_service
 * @param il tag restituito da tag_get
 * @param REMOVE_TAG o AWAKE_ALL. Se ci sono thread in attesa, REMOVE_TAG fallisce. Se non ci sono, AWAKE_ALL non fa nulla.
 * @returns 1 se ha successo, -1 se fallisce (e imposta errno)
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)

__SYSCALL_DEFINEx(2, _tag_ctl, int, tag, int, command) {
#else
    // asmlinkage: riceve parametri dai registri di processore, non dallo stack.
    asmlinkage long sys_tag_ctl(int tag, int command) {
#endif
    unsigned long awakened;
    tag_service *ts; // se avessi usato il valore nello stack, mi darebbe il warning -Wframe-larger-than 1024 bytes

    try_module_get(THIS_MODULE);
    printk("%s: il thread %d esegue  tag_ctl con parametri tag=%d, command=%s\n",
           MODNAME, current->pid, tag,
           command == REMOVE_TAG ? "REMOVE_TAG" : (command == AWAKE_ALL ? "AWAKE_ALL" : "UNKNOWN"));

    if ((tag < 0 || tag > MAX_TAG_SERVICES) || (command != REMOVE_TAG && command != AWAKE_ALL)) {
        ERR("Parametri tag e/o command non validi per tag_ctl()");
        module_put(THIS_MODULE);
        return -EINVAL;
    }
    ts = tsm.all_tag_services[tag];
    if (!ts) {
        ERR("Si sta tentando di controllare un tag_service inesistente.");
        module_put(THIS_MODULE);
        return -EBADF; // bad file descriptor
    }

    mutex_lock(&tsm.access_lock[ts->tag]);

    // se il tag service e' stato eliminato, fallisce
    if (ts->lazy_deleted == YES) {
        mutex_unlock(&tsm.access_lock[ts->tag]);
        LOG("Il tag service in questione e' stato gia' eliminato logicamente da un'altra tag_ctl");
        module_put(THIS_MODULE);
        return SUCCESS;
    }

    // Controllo se il thread ha i permessi per controllare il tag service (siamo in sezione critica)
    if (ts->permission == ONLY_OWNER && ts->owner_euid != EUID) {
        mutex_unlock(&tsm.access_lock[ts->tag]);
        printk(KERN_ERR "%s: thread %d - L'utente non dispone dei permessi per %s tag_service %d", MODNAME,
               current->pid, command == REMOVE_TAG ?
                             "rimuovere il" : (command == AWAKE_ALL ? "svegliare i thread in attesa nel"
                                                                    : "accedere al"), tag);
        module_put(THIS_MODULE);
        return -EACCES; // permission denied
    }

    switch (command) {
        case REMOVE_TAG:
            // Se ci sono thread in attesa, la REMOVE FALLISCE
            if (ts->thread_waiting_message_count > 0) {
                mutex_unlock(&tsm.access_lock[ts->tag]);
                ERR1("Impossibile rimuovere il tag service. Thread(s) in attesa", ts->thread_waiting_message_count);
                module_put(THIS_MODULE);
                return -EBUSY; // Device or resource busy
            }
            ts->lazy_deleted = YES;

            mutex_unlock(&tsm.access_lock[ts->tag]);
            /* Rimuovo il device file */
            ts_destroy_char_device_file(ts->tag);

            tsm.all_tag_services[tag] = NULL; // importante annullare, altrimenti non funziona nulla...
            find_first_entry_available(); // cerca e imposta la prima entry disponibile (caso peggiore O(N))


            atomic_inc((atomic_t *) &(tsm.remaining_entries));

            asm volatile ("mfence");

            kfree(ts); // nessun performance issue usando kfree invece di kzfree: con la seconda, il buffer da azzerare potrebbe essere più grande della dimensione passata a kzalloc.

            printk("%s: Thread %d - Rimosso Tag service %d e chardevice associato con MAJOR %d e MINOR %d."
                   " Entry disponibili: %lu\n", MODNAME, current->pid, tag, tsm.major, tag, tsm.remaining_entries);
            module_put(THIS_MODULE);
            return SUCCESS;
        case AWAKE_ALL:
            printk("%s: Sveglio tutti i thread sul tag %d", MODNAME, tag);

            // non voglio che ulteriori thread entrino nello stesso tag service per ricevere (o inviare, forse)
            awakened = ts->thread_waiting_message_count;
            ts->awake_request = YES;

            wake_up_all(&the_queue);

            // ora i thread possono entrare per ricevere!
            if (awakened > 0) {
                call_rcu(&ts->tag_rcu, reset_awake_condition);
                LOG1("AWAKE_ALL: Chiamata RCU. Thread svegliati:", awakened);
            } else { // evito di scomodare la rcu, se non ci sono thread in sezione critica rcu
                ts->awake_request = NO;
                mutex_unlock(&tsm.access_lock[ts->tag]);
                LOG("AWAKE_ALL: Nessun thread svegliato. Reimposto immediatamente la condizione di risveglio");
            }
            module_put(THIS_MODULE);
            return SUCCESS;
        default:
            ERR("tag_ctl - Comando sconosciuto");
            module_put(THIS_MODULE);
            return -ENOSYS; // function not implemented
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
static unsigned long sys_tag_get = (unsigned long) __x64_sys_tag_get;
static unsigned long sys_tag_send = (unsigned long) __x64_sys_tag_send;
static unsigned long sys_tag_receive = (unsigned long) __x64_sys_tag_receive;
static unsigned long sys_tag_ctl = (unsigned long) __x64_sys_tag_ctl;
#endif

/**
 * Inizializzo le system call, il driver di dispositivo e i metadati necessari al modulo
 * @return 0 se ha successo. -1 se fallisce il montaggio.
 */
int start(void) {
    int i;

    LOG(" -------------------- Inizializzazione Modulo --------------------");
    syscall_table_finder(&hacked_syscall_tbl, &hacked_ni_syscall, free_entries);
    if (!hacked_syscall_tbl) {
        LOG("Impossibile trovare la sys_call_table");
        return MOUNT_FAILURE;
    }
    // Deallocato allo smontaggio
    tsm.all_tag_services = kzalloc(sizeof(tag_service) * MAX_TAG_SERVICES, GFP_ATOMIC);
    if (!tsm.all_tag_services) {
        return MOUNT_FAILURE;
    }
    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        mutex_init(&tsm.access_lock[i]);
    }

    tsm.first_free_entry = 0;
    tsm.remaining_entries = MAX_TAG_SERVICES;

    sys_ni_syscall_address = (unsigned long) hacked_ni_syscall;
    sys_call_table_address = (unsigned long) hacked_syscall_tbl;
    printk("%s: sys_call_table_address %lx\n", MODNAME, sys_call_table_address);
    printk("%s: sys_ni_syscall_address %lx\n", MODNAME, sys_ni_syscall_address);
    printk("%s: free entries[0] %d:   %px\n", MODNAME, free_entries[0], hacked_syscall_tbl[free_entries[0]]);
    printk("%s: free entries[1] %d:   %px\n", MODNAME, free_entries[1], hacked_syscall_tbl[free_entries[1]]);
    printk("%s: free entries[2] %d:   %px\n", MODNAME, free_entries[2], hacked_syscall_tbl[free_entries[2]]);
    printk("%s: free entries[3] %d:   %px\n", MODNAME, free_entries[3], hacked_syscall_tbl[free_entries[3]]);

    install_syscall((unsigned long *) sys_tag_get, free_entries[0]);
    install_syscall((unsigned long *) sys_tag_send, free_entries[1]);
    install_syscall((unsigned long *) sys_tag_receive, free_entries[2]);
    install_syscall((unsigned long *) sys_tag_ctl, free_entries[3]);

    // inizializzo il device driver per poter creare i nodi di I/O dei device file durante una tag_get
    init_device_driver(&tsm);

    LOG("Modulo montato");
    return MOUNT_SUCCESS;
}

/**
 * Distrugge tutti i device driver rimasti in memoria, tutti tag service e i loro metadati e
 * ripristina la tabella delle system call allo stato precedente al montaggio.
 */
void end(void) {
    int i, lev;
    LOG(" -------------------- Disinstallazione in corso --------------------");
    // distruggo il device driver (utilizza l'array tsm).
    destroy_driver_and_all_devices();


    // Dealloco gli eventuali tag service rimasti in memoria...
    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        if (tsm.all_tag_services[i]) {
            for (lev = 0; lev < MAX_LEVELS; lev++) {
                kfree(tsm.all_tag_services[i]->level[lev].message); // Dealloco i [MAX_LEVELS] messaggi
            }
            kfree(tsm.all_tag_services[i]->level); // Dealloco il level di ogni tag service
            kfree(tsm.all_tag_services[i]); // Dealloco il ts
            LOG1("Eliminato tag_service", i);
        }
    }

    // elimino l'array di tutti i tag_services
    kzfree(tsm.all_tag_services);

    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        mutex_destroy(&tsm.access_lock[i]);
    }


    uninstall_syscall((unsigned long *) sys_tag_get, free_entries[0]);
    uninstall_syscall((unsigned long *) sys_tag_send, free_entries[1]);
    uninstall_syscall((unsigned long *) sys_tag_receive, free_entries[2]);
    uninstall_syscall((unsigned long *) sys_tag_ctl, free_entries[3]);
    LOG("Modulo smontato");
}

module_init(start);

module_exit(end);