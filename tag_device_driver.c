//
// Created by giacomo on 16/04/21.
//
#include "tag_device_driver.h"

/* IL prefisso del nome di un nodo di I/O (char device file). Dopo questo prefisso va scritto
 * il MINOR del char device file che coincide con il tag descriptor del tag_service */
#define TAG_SERVICE_DEVICE_NAME "tsdev_"
#define TAG_SERVICE_DRIVER_NAME "ts_driver"  // nome del driver
#define TAG_SERVICE_CLASS_NAME "ts_class"    // nome della classe in /sys/class

#define MODNAME "TAG_DEVICE_DRIVER"

#define LOG(msg) AUDIT _LOG(msg, MODNAME, KERN_DEFAULT)
#define LOG1(msg, num) AUDIT _LOG1(msg, MODNAME, KERN_DEFAULT, num)
#define ERR(msg) AUDIT _LOG(msg, MODNAME, KERN_ERR)
#define ERR1(msg, num) AUDIT _LOG1(msg, MODNAME, KERN_ERR, num)

#define BUFSIZE 4096

// variabili globali esclusive di questo file
struct class *ts_class = NULL;  // class condivisa da tutti i char device files (usata solo in questo file)
unsigned int ts_major = 0;      // MAJOR number condiviso da tutti i char device files
static ts_management *tsm;      // Copia del puntatore in tag_service.c: static altrimenti e' definito due volte...

typedef struct my_dev_manager {
    struct cdev cdev[MAX_TAG_SERVICES];
    struct mutex device_lock[MAX_TAG_SERVICES]; // Un thread che vuole modificare la stringa deve aspettare tutti gli standing readers...
    char *content[MAX_TAG_SERVICES]; // la stringa salvata per una epoca ... non puo' essere modificata finche' tutti non hanno letto
} dev_manager;

dev_manager *dm;
const char *header = "KEY\tEUID\tLEVEL\t#THREADS\n";
const int header_size = 24; // Escluso il \0

int countCharsOfNumber(long n) {
    int count;

    if (n == 0) return 1;

    count = 0;
    if (n < 0) {
        count++;
        n = -n;
    }

    while (n != 0) {
        n = n / 10; // Es. 25 -> 2 -> 0: 2 cifre. 36453 -> 3645 -> 364 -> 36 -> 3 -> 0: 5 cifre.
        count++;
    }
    return count;
}

/*
 * Conta le cifre dei numeri da 0 a n incluso. Esempio:
 * 5 -> 6
 * 10 -> 12
 * 100 -> 193
 */
long fastSommaDelleCifreDaZeroFinoIncluso(long n) {
    int numeroCifre;
    long ultimoNumeroConUnaCifraInMeno;
    long costante;
    long correzione;
    long sum;
    int i;
    numeroCifre = countCharsOfNumber(n);
    ultimoNumeroConUnaCifraInMeno = (long) int_pow(10, numeroCifre - 1) - 1;
    costante = numeroCifre * (long) int_pow(10, numeroCifre - 1);
    correzione = 0;
    sum = 10;
    for (i = 1; i < numeroCifre; i++) {
        correzione += sum;
        sum *= 10;
    }
    costante -= correzione;

    return numeroCifre * (n - ultimoNumeroConUnaCifraInMeno) + costante;
}

/**
 * O(N) traduce una stringa in numero.
 * @param str
 * @return
 */
int my_atoi(const char *str) {
    int sign, base, i;
    sign = 1;
    base = 0;
    i = 0;

    // if whitespaces then ignore.
    while (str[i] == ' ' || str[i] == '\t') {
        i++;
    }

    // sign of number
    if (str[i] == '-' || str[i] == '+') {
        sign = 1 - 2 * (str[i++] == '-');
    }

    // checking for valid input
    while (str[i] >= '0' && str[i] <= '9') {
        // handling overflow test case
        if (base > INT_MAX / 10
            || (base == INT_MAX / 10
                && str[i] - '0' > 7)) {
            if (sign == 1)
                return INT_MAX;
            else
                return INT_MIN;
        }
        base = 10 * base + (str[i++] - '0');
    }
    return base * sign;
}

/**
 * Permette di impostare i permessi di tutti i char device con la classe ts_class, in modo tale da
 * poter essere LETTI (e solo letti) da chiunque, anche senza SUDO */
int my_dev_uevent(struct device *dev, struct kobj_uevent_env *env) {
    add_uevent_var(env, "DEVMODE=%#o", 0444);
    // printk("%s: thread %d - ora tsdev_%d puo' essere letto da chiunque\n", MODNAME, current->pid, MINOR(dev->devt));
    return 0;
}

/**
 * Copia la stringa nel parametro ts_status. Chiamata in una sezione critica RCU.
 */
void get_tag_status(int tag_minor, char *ts_status) {
    char *pointer;

    // dm->content[ts->tag] e' allocato durante la creazione del char device
    // ed e' deallocato durante l'eliminazione del char device
    // prima di cambiare il suo valore con un altro buffer, il buffer precedente viene liberato dallo scrittore (tag_receive/tag_get)
    pointer = rcu_dereference(dm->content[tag_minor]);
    memcpy(ts_status, pointer, BUFSIZE); // non bloccante
    asm volatile ("mfence");

}

/**
 * Funzione di apertura per il singolo nodo di I/O relativo a un tag_service
 * - salva i metadati del tag_service corrispondente nella sessione filp
 */
int ts_open(struct inode *inode, struct file *filp) {
    unsigned int major = imajor(inode);
    unsigned int minor = iminor(inode);

    /* Se il major non corrisponde al major del driver oppure il minor non e' nel
     * range definito in alloc_chrdev_region, l'apertura del file device fallisce. */
    if (major != ts_major || minor < 0 || minor >= MAX_TAG_SERVICES) {
        printk(KERN_WARNING "%s "
               "Impossibile trovare un device con MAJOR=%d e MINOR=%d\n",
               MODNAME, major, minor);
        return -ENODEV; /* No such device */
    }

    /* Salva nella struct file (sessione del file) un puntatore a una struct tag_service
     * per essere accedibile dalle altre funzioni del driver */
    filp->private_data = tsm->all_tag_services[minor]; // permette di salvare il puntatore al nostra struttura per renderlo accessibile alle altre funzioni del driver

    if (inode->i_cdev != &dm->cdev[minor]) {
        printk(KERN_WARNING "%s open: Errore interno\n", MODNAME);
        return -ENODEV; /* No such device */
    }

    return 0;
}

/**
 * Funzione di chiusura del device file. NOP.
 */
int ts_release(struct inode *inode, struct file *filp) {
    return 0;
}

/**
 * Permette di leggere il contenuto del device file: la stringa viene restituita direttamente dalla struct
 * tag_service associata al char device file. Puo' leggere un solo thread alla volta dallo stesso nodo di I/O.
 *
 * @param filp puntatore alla sessione di I/O
 * @param buf buffer utente (es. fornito da cat/less)
 * @param count numero di byte da leggere
 * @param f_pos posizione da cui iniziare a leggere
 * @return {
 *  - se > 0: numero di byte effettivamente letti.
 *  - se < 0: errore
 *  - se = 0: EOF
 *  }
 */
ssize_t ts_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    char *ts_status;
    ssize_t size, retval;

    tag_service *ts = (tag_service *) filp->private_data;

    // Liberato qui sotto, prima di ritornare al chiamante
    ts_status = kmalloc(BUFSIZE * sizeof(char), GFP_KERNEL);

    // Copio la stringa da restituire all'utente in ts_status
    rcu_read_lock(); // internamente chiama preempt_disable

    get_tag_status(ts->tag, ts_status);

    rcu_read_unlock(); // internamente chiama preempt_enable

    size = strlen(ts_status);

    // Se si usa 'less -f tsdev_#' e non legge tutto in una volta, e' possibile che non riesce ad andare piu' avanti se impediamo ulteriori letture...
    if (mutex_lock_killable(&dm->device_lock[ts->tag])) {
        kfree(ts_status);
        return -EINTR;
    }

    // Se arriviamo a EOF con la lettura, abbiamo finito di leggere - quindi esco.
    if (*f_pos >= size) {
        mutex_unlock(&dm->device_lock[ts->tag]);
        kfree(ts_status);
        return 0; // EOF
    }

    /* Se il valore del cursore sommato al numero di byte da leggere supera la dimensione del messaggio,
     * riduco il numero di byte da leggere */
    if (*f_pos + count > size)
        count = size - *f_pos;

    /* Se i byte da leggere superano ancora la dimensione della stringa da restituire, leggo esattamente size byte */
    if (count > size)
        count = size;

    /* Creo il buffer con tutti i dati e lo invio all'utente per potergli far leggere lo stato del tag service */
    if (copy_to_user(buf, ts_status, count) != 0) {
        mutex_unlock(&dm->device_lock[ts->tag]);
        kfree(ts_status);
        printk("%s: Impossibile leggere tutti i dati del char device tsdev_%d", MODNAME, ts->tag);
        return -EFAULT;
    }

    /* Incremento il cursore del numero di byte effettivamente letti */
    *f_pos += count;
    retval = count;

    mutex_unlock(&dm->device_lock[ts->tag]);
    kfree(ts_status);
    return retval;
}

/**
 * Permette di scrivere caratteri nel device file.
 * Inutilizzata, la lettura avviene direttamente dalle mie struct kernel.
 *
 * @param filp sessione a questo device file
 * @param buf [inutilizzato] buffer utente da scrivere nel device file
 * @param count numero di byte da scrivere
 * @param f_pos posizione del cursore
 * @return numero di byte effettivamente scritti.
 * Se 0, cat/less potrebbero continuare a richiamare questa funzione all'infinito
 */
ssize_t ts_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    printk("%s: funzione write del driver non implementata per il char device con MAJOR, MINOR %d, %d\n", MODNAME,
           MAJOR(filp->f_inode->i_rdev), MINOR(filp->f_inode->i_rdev));
    return count; // Facciamo finta che tutti i byte sono stati scritti!
}

/**
 * Driver del char device e di tutti i nodi di I/O che hanno lo stesso MAJOR number
 */
struct file_operations ts_fops = {
        .owner =    THIS_MODULE,
        .read =     ts_read,
        .write =    ts_write,
        .open =     ts_open,
        .release =  ts_release,
};

/**
 * Inizializza e registra il nodo di I/O (char device file) nella cartella /dev/ con lo specifico indice,
 * pari al minor number. La funzione richiama tre API:
 * - cdev_init() inizializza la struct cdev con il driver
 * - cdev_add() per registrare il chardevice file
 * - device_create() per istanziare il nodo di I/O
 *
 * @param tag_minor: il tag descriptor del tag service. Viene utilizzato come minor number del char device.
 *
 * Importante: prima di eseguire questa funzione, esegui init_device_driver(), affinche' la struct class sia creata
 */
int ts_create_char_device_file(int tag_minor) {
    int err;
    dev_t devt_node = MKDEV(ts_major, tag_minor);
    struct device *device = NULL;
    int i;
    // size_t size;
    char line[100];
    tag_service *ts;
    //char *temp_buffer;

    BUG_ON(ts_class == NULL);

    // Inizializzo la stringa per il buffer
    ts = tsm->all_tag_services[tag_minor];

    // TODO: mi serve un buffer temporaneo??
    //temp_buffer = kmalloc(BUFSIZE * sizeof(char), GFP_KERNEL);

    // Inizializza la struct cdev coi metadati del nodo
    mutex_lock(&dm->device_lock[tag_minor]);
    cdev_init(&dm->cdev[tag_minor], &ts_fops);
    dm->cdev[tag_minor].owner = THIS_MODULE;

    // Aggiunge i metadati del nodo di I/O al sistema (non istanzia il nodo di I/O)
    err = cdev_add(&dm->cdev[tag_minor], devt_node, 1);
    if (err) {
        mutex_unlock(&dm->device_lock[tag_minor]);
        printk(KERN_WARNING "%s Errore %d mentre si prova ad aggiungere il cdev per %s%d", MODNAME, err,
               TAG_SERVICE_DEVICE_NAME "%d",
               tag_minor);
        return err;
    }

    /*
     * Crea un chardevice file (nodo di I/O) con path /dev/tsdev_#
     * - Imposto parent a NULL cosi' viene istanziato direttamente nella cartella /dev/
     * - drvdata NULL: non aggiungo dati addizionali.
     * - assegno il minor number (il # del path) passato come parametro a questo device (cioe' il tag descriptor).
     */
    device = device_create(ts_class, NULL, devt_node, NULL, TAG_SERVICE_DEVICE_NAME "%d", tag_minor);

    if (IS_ERR(device)) {
        mutex_unlock(&dm->device_lock[tag_minor]);
        err = (int) PTR_ERR(device);
        printk(KERN_WARNING "%s Errore %d durante la creazione del chardevice %s%d", MODNAME, err,
               TAG_SERVICE_DEVICE_NAME, tag_minor);
        cdev_del(&dm->cdev[tag_minor]); // dealloca la struttura cdev con i metadati del nodo di I/O
        return err;
    }

    /* Se tutto va a buon fine, inizializzo SOLO UNA VOLTA la struttura dati con i contenuti del char device.
     * Liberato in ts_destroy_char_device_file e in update_chrdev*/
    dm->content[tag_minor] = kmalloc(BUFSIZE * sizeof(char), GFP_ATOMIC); // Sto in sezione critica, non voglio dormire
    asm volatile ("mfence");

    // Questa stringa non viene piu' ricostruita quando eseguo la tag_receive, ma solo quando chiamo tag_get la prima volta
    strncpy(dm->content[tag_minor], header, strlen(header));
    for (i = 0; i < MAX_LEVELS; i++) {

        // Numero di cifre
        int num1 = countCharsOfNumber(MAX_TAG_SERVICES); // es. per 512, num1 = 3
        int num2 = countCharsOfNumber(ts->owner_euid);
        int num3 = countCharsOfNumber(MAX_LEVELS);
        int num4 = countCharsOfNumber((long) ts->level[i].thread_waiting);

        snprintf(line, num1 + num2 + num3 + num4 + 5, "%d\t%d\t%d\t%lu\n", ts->key, ts->owner_euid, i,
                 ts->level[i].thread_waiting);
        strncat(dm->content[tag_minor], line, strlen(line));
    }

    mutex_unlock(&dm->device_lock[tag_minor]);
    return 0;
}

/**
 * Distrugge il nodo di I/O del chardevice e i suoi metadati.
 * - device_destroy(): distrugge il nodo di I/O relativo alla class e alla coppia MAJOR, MINOR
 * - cdev_del(): dealloca la struttura cdev, a patto che non ci siano sessioni aperte sul device file */
void ts_destroy_char_device_file(int tag_minor) {

    BUG_ON(ts_class == NULL);

    mutex_lock(&dm->device_lock[tag_minor]);
    device_destroy(ts_class, MKDEV(ts_major, tag_minor));
    cdev_del(&dm->cdev[tag_minor]); // pulisce i metadati del device file (nodo di I/O)

    // Non serve aspettare che finiscano i lettori perche' elimino (tag_ctl) il char device solo se non ci sono lettori (tag_receive) in attesa e quindi se il modulo kernel non è bloccato!!!
    // non posso eliminare un tag service e quindi il char device se ci sono lettori in attesa!!!
    kfree(dm->content[tag_minor]); // Libero la memoria del buffer corretto
    mutex_unlock(&dm->device_lock[tag_minor]);

}

/**
 * Inizializza il char device driver:
 * - alloca Major e MAX_TAG_SERVICES Minor numbers
 * - crea la classe per il device driver in /sys/class/
 * @return 0 se ha successo
 */
int init_device_driver(ts_management *the_tsm) {
    int err, i;
    dev_t dev = 0;

    tsm = the_tsm;

    // Deallocato in destroy_driver_and_all_devices()
    dm = kmalloc(sizeof(dev_manager), GFP_KERNEL);

    /* Alloca una regione di MAX_TAG_SERVICES Minor Numbers (a partire da 0)
     * Inoltre fa scegliere il MAJOR al kernel e salva la coppia MAJOR, MINOR=0 in dev
     * Non imposta le file operations...*/
    err = alloc_chrdev_region(&dev, 0, MAX_TAG_SERVICES, TAG_SERVICE_DRIVER_NAME);

    if (err < 0) {
        ERR("Errore in alloc_chrdev_region()");
        return err;
    }

    // salvo il major number nella variabile globale e la passo anche a tag_device.c
    ts_major = MAJOR(dev);
    tsm->major = ts_major;

    /* Crea la class per device driver */
    ts_class = class_create(THIS_MODULE, TAG_SERVICE_CLASS_NAME);

    if (IS_ERR(ts_class)) {
        err = (int) PTR_ERR(ts_class);
        unregister_chrdev_region(dev, MAX_TAG_SERVICES);
        ERR("Errore in class_create()");
        return err;
    }
    //impostazione dei permessi per il device
    ts_class->dev_uevent = my_dev_uevent;
    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        mutex_init(&dm->device_lock[i]);
    }

    LOG1("Installato char device driver con MAJOR", ts_major);

    return 0; /* success */
}

/**
 * Elimina il device driver dal kernel.
 * - Se ci sono dei char device file nel sistema, li distrugge richiamando ts_destroy_char_device_file.
 * - class_destroy: distrugge la struttura class associata al driver
 * - distrugge la regione di MAJOR e [MAX_TAG_SERVICES] Minor numbers
 */
void destroy_driver_and_all_devices(void) {
    int i;
    // printk(KERN_DEBUG "Inizio disinstallazione dei char device e del device driver\n");
    // Rimuovo gli eventuali char device nodes che sono ancora presenti...
    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        // se il tag service non e' nullo dobbiamo eliminare anche il char device associato
        if (tsm->all_tag_services[i] != NULL) {
            LOG1("Elimino il char device", i);
            ts_destroy_char_device_file(i);
        }
    }

    for (i = 0; i < MAX_TAG_SERVICES; i++) {
        mutex_destroy(&dm->device_lock[i]);
    }

    /* Se la ts_class non e' NULL, la elimino*/
    if (ts_class) {
        class_destroy(ts_class);
    }

    /* dealloca la regione di MAJOR e MINOR number per questo device driver */
    unregister_chrdev_region(MKDEV(ts_major, 0), MAX_TAG_SERVICES);

    kfree(dm);

    printk("%s: Hai disinstallato con successo il device driver con MAJOR %d\n", MODNAME, ts_major);
}

/**
 * [SCRITTORE] permette di cambiare la stringa del char device
 * - Viene chiamata da tag_get quando crea il tag_service
 * - Viene chiamata da tag_receive quando un thread entra o esce dall'attesa.
 *
 * FIX: Ora l'unica cosa che cambia e' il numero di thread in ricezione nel livello specificato e non tutte le righe
 *
 * @param tag_minor il tag service/char device a cui cambiare la stringa
 */
int update_chrdev(int tag_minor, int level) {

    int i, ret;
    tag_service *ts;
    char waiting[10];
    char *temp_buffer;
    size_t content_size;
    int delimiters_found;
    unsigned long waiting_n;
    int before_token; // Posizione del terzo \t del livello 'level'
    char ch;
    char *after_string; // Da \n del livello 'level' alla fine
    char *before_string;
    char *final_string;

    if(tag_minor > MAX_TAG_SERVICES || tag_minor < -1 || level > MAX_LEVELS || level < 0)
        return -1;


    ts = tsm->all_tag_services[tag_minor];

    if(!ts)
        return -1;

    // Deallocato al termine
    temp_buffer = kmalloc(BUFSIZE * sizeof(char), GFP_KERNEL);

    waiting_n = atomic_read((atomic_t *) &ts->level[level].thread_waiting); // Non vogliamo leggere quando un altro thread sta scrivendo
    ret = 0;

    // Sincronizzo solo chi scrive nella struttura dati (tag_receive (fuori dalla RCU) e tag_get)
    mutex_lock(&dm->device_lock[ts->tag]);
    content_size = strlen(dm->content[ts->tag]);
    memcpy(temp_buffer, dm->content[ts->tag], content_size);


    // Suddividi la stringa in tre parti e poi uniscile:
    // La parte prima dei thread in attesa (fino al terzo \t incluso)
    // Il numero di nuovi thread in attesa
    // La parte dal \n in poi
    i = 0; // spiazzamento del buffer
    delimiters_found = 0; // numero di delimitatori trovati
    ch = 'a'; // dummy char

    while (delimiters_found < (level + 2) && ch != '\0') { // +2 perche' escludo l'header
        i++; // l'ultima volta, andiamo a capo
        if ((ch = temp_buffer[i]) == '\n') {
            delimiters_found++;
        }
    }

    // Deallocato al termine
    after_string = kmalloc(sizeof(char) * BUFSIZE / 2, GFP_ATOMIC);
    strncpy(after_string, temp_buffer + i, content_size - i + 1);

    // Finche' non arrivo al primo tab (da destra), oppure i == 0
    while (ch != '\t' && i > 0) {
        i--;
        ch = temp_buffer[i];
    }
    before_token = i + 1; // vado al carattere successivo a \t (il numero di thread in ricezione)

    // Deallocato al termine
    before_string = kmalloc(sizeof(char) * before_token + 1, GFP_ATOMIC);
    // strncpy deve copiare esattamente [before_token] caratteri e non di piu'. Dopo e' necessario aggiungere il terminatore.
    strncpy(before_string, temp_buffer, before_token);
    before_string[before_token] = 0;

    // Deallocato da ts_destroy_char_device_file()
    final_string = kmalloc(sizeof(char) * BUFSIZE, GFP_ATOMIC);
    strncat(final_string, before_string, BUFSIZE / 2);
    sprintf(waiting, "%lu", waiting_n); // spero ci sia \0
    strncat(final_string, waiting, BUFSIZE / 2);
    strncat(final_string, after_string, BUFSIZE / 2);


    kfree(dm->content[ts->tag]);

    // assegno al content il mio buffer temporaneo con memory barriers
    rcu_assign_pointer(dm->content[ts->tag], final_string);
    mutex_unlock(&dm->device_lock[ts->tag]);
    /*printk("%s: before_token = %d, before_string:\n%s\nnumero thread in attesa: %s, after_string:\n%s",
           MODNAME, before_token, before_string, waiting, after_string);*/
    // After string viene
    kfree(before_string);
    kfree(after_string);
    kfree(temp_buffer); // elimina anche after_string

    return ret;
}
