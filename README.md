# Scambio dati basato su TAG service
Implementare un sottosistema del kernel Linux che permette di scambiare messaggi tra thread.
Il sottosistema per default ha 32 livelli, chiamati TAG, che devono essere gestiti tramite le seguenti system call:
    
`int tag_get(int key, int command, int permission)`
- Questa system call instanzia (`O_CREAT`) o apre (`O_OPEN`) il TAG service associato alla `key` in base al valore di `command`. 
Il valore IPC_PRIVATE dovrebbe essere usato su `key` per istanziare il servizio in modo che non può essere riaperto da questa stessa system call. 
Il valore di ritorno dovrebbe indicare il **descrittore del TAG service** che serve per gestire le reali operazioni del TAG service e deve valere -1 in caso di errore. 
Inoltre le `permission` devono indicare se il servizio TAG è creato per essere utilizzato da thread in esecuzione da un programma dello stesso utente o da un utente qualsiasi. 

`int tag_send(int tag, int livello, char* buffer, size_t size)`
- Questo servizio consegna al TAG service indicato dal descrittore `tag` il messaggio posto nel `buffer` lungo `size` bytes.
Tutti i threads che sono correntemente in attesa (in sleep) di ricevere tale messaggio sul corrispondente valore di `livello`, quando ricevono un messaggio devono continuare la loro esecuzione e 
ricevere il messaggio. I messaggi di dimensione 0 sono permessi. Il servizio non mantiene traccia del log dei messaggi inviati, quindi se nessun ricevitore sta aspettando, il messaggio
viene semplicemente scartato.

`int tag_receive(int tag, int livello, char* buffer, size_t size)`
- Questo servizio permette a un thread di chiamare l'operazione **bloccante** di ricezione del messaggio dal corrispondente
descrittore `tag` a un dato livello. L'operzione può fallire anche a causa dell'invio di un segnale Posix al thread mentre esso
attende il messaggio.  

`int tag_ctl(int tag, int command)`
- Questa system call permette al chiamante di controllare il TAG service con il descrittore `tag` in base al `command`, che può essere alternativamente AWAKE_ALL o REMOVE.
    - AWAKE_ALL: permette di svegliare tutti i thread in attesa di un messaggio, indipendentemente dal livello
    - REMOVE: permette di rimuovere il TAG service dal sistema. Un Tag service non deve essere rimosso se ci sono dei thread in attesa di ricevere messaggi da esso e scriverà un errore (in errno!!!)
    
Per default, il software deve gestire almeno 256 TAG services e la dimensione massima di ogni messaggio gestito dovrebbe essere di almeno 4 KB.
Inoltre deve essere implementato un device driver per permettere all'utente di controllare lo stato corrente di tutti i TAG services. 
Ogni linea del device file corrispondente deve essere strutturata così:

    TAG-key TAG-creator TAG-livello Waiting-threads
    
- TAG-key: la **chiave** del tag service usata all'interno di `tag_get()`.
- TAG-creator: il **nome dell'utente** (o UID) che ha creato il tag service.
- TAG-livello: il **livello** associato al tag service.
- Waiting-thread: **numero di thread in attesa** di un messaggio sul particolare tag service. 

# TAG-based data exchange
This specification is related to the implementation of a Linux kernel subsystem that allows exchanging messages across threads. The service has 32 levels (namely, tags) by default, and should be driven by the following system calls:
    
`int tag_get(int key, int command, int permission)`
- this system call instantiates or opens the TAG service associated with key depending on the value of command. The IPC_PRIVATE value should be used for key to instantiate the service in such a way that it could not be re-opened by this same system call. The return value should indicate the TAG service descriptor (-1 is the return error) for handling the actual operations on the TAG service. Also, the permission value should indicate whether the TAG service is created for operations by threads running a program on behalf of the same user installing the service, or by any thread.

`int tag_send(int tag, int livello, char* buffer, size_t size)`
- this service delivers to the TAG service with tag as the descriptor the message currently located in the buffer at address and made of size bytes. All the threads that are currently waiting for such a message on the corresponding value of livello should be resumed for execution and should receive the message (zero lenght messages are anyhow allowed). The service does not keep the log of messages that have been sent, hence if no receiver is waiting for the message this is simply discarded.

`int tag_receive(int tag, int livello, char* buffer, size_t size)`
- this service allows a thread to call the blocking receive operation of the message to be taken from the corresponding tag descriptor at a given livello. The operation can fail also because of the delivery of a Posix signal to the thread while the thread is waiting for the message.

`int tag_ctl(int tag, int command)`
- this system call allows the caller to control the TAG service with tag as descriptor according to command that can be either AWAKE_ALL (for awaking all the threads waiting for messages, independently of the livello), or REMOVE (for removing the TAG service from the system). A TAG service cannot be removed if there are threads waiting for messages on it). 

By default, at least 256 TAG services should be allowed to be handled by software. Also, the maximum size of the handled message should be of at least 4 KB.

Also, a device driver must be offered to check with the current state, namely the TAG service current keys and the number of threads currently waiting for messages. Each line of the corresponding device file should be therefore structured as "TAG-key TAG-creator TAG-livello Waiting-threads".

# Note
key: valore numerico da inserire nel codice di un thread che deve referenziale il particolare servizio tag. 
Viene scelta dall'user, ma poi viene restituito un intero che rappresenta il codice operativo del TAG service, utilizzabile
nelle altre API. Se utilizziamo come chiave IPC_PRIVATE (non ci interessa che nessun altro usi la chiave per chiamare l'API), lavoriamo con un 
TAG service utilizzabile da tutti quelli che ricevono il tag (processi forkati, thread figli, thread che lo leggono dalla memoria condivisa)
il TAG descriptor deve essere univoco. Più elementi possono essere associati a IPC_PRIVATE.

livello: il servizio ha 32 livelli (multiplexing attività): indichiamo istanza (tag) e livello su
cui lavorare (come se fossero code di messaggi). IN ricezione se uso il livello 0, ma ho inviato un messaggio su livello 1
il thread in ricezione non viene svegliato e non riceve nulla.
Ogni livello è 'a tenuta stagna'. La comunicazione porta a un risveglio multiplo di tutti i processi/thread in attesa sullo stesso
tag service e sullo stesso livello. Consegna uno-molti dell'informazione.

Il servizio non mantiene il log dei messaggi spediti. Se non ci sono ricevitori in attesa, il messaggio viene scartato. 
Se chiamo una send e dei thread sono in attesa di un messaggio su quel (TAG, livello) il messaggio non viene scartato.

Attenzione alla linearizzazione di arrivo tra lettori e scrittori. Dobbiamo gestire la sincronizzazione. 
I thread in attesa mostreranno la loro presenza toccando delle strutture dati condivise: riceveranno solo il primo dei messaggi degli scrittori.

command: apertura o istanziazione.

permission: permessi semplificati - il servizio può essere usato dallo stesso utente proprietario, o da tutti gli altri utenti. (o solo io, o tutti)

Due scrittori su TAG diverso e/o livello diverso possono essere concorrenti. Se sono su stesso TAG e stesso livello devono essere sincronizzati (HINT: meglio usare RCU!!!!)