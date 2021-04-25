#include "usctm.h"
#include "vtpm.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giacomo Lorenzo Rossi <giacomo.redjack@gmail.com>");
MODULE_DESCRIPTION("trova la system call table a runtime e funziona anche su kernel 5");
#define MODNAME "SYS-CALL-TABLE-FINDER"
#define LOG(msg) printk("%s: %s", MODNAME, msg);


unsigned long *hacked_ni_syscall_found = NULL;
unsigned long **hacked_syscall_tbl_found = NULL;

/**
 * Controlla se nei primi 134 elementi presenti in addr, non c'è nessun
 * indirizzo uguale a sys_ni_syscall.
 * @param addr indirizzo ipotetico della system call table.
 * @return se la condizione è vera, allora abbiamo trovato la system call (restituisce 1)
 */
int good_area(const unsigned long *addr) {
    int i;

    for (i = 1; i < FIRST_NI_SYSCALL; i++) {
        if (addr[i] == addr[FIRST_NI_SYSCALL]) goto bad_area;
    }
    return SUCCESS;
    bad_area:
    return FAILURE;
}

/* This routine checks if the page contains the begin of the syscall_table.  */
int validate_page(unsigned long *addr) {
    int i = 0;
    unsigned long page = (unsigned long) addr;
    unsigned long new_page;
    for (; i < PAGE_SIZE; i += sizeof(void *)) {
        // A volte la system call table può occupare due pagine...
        new_page = page + i + SECOND_NI_SYSCALL * sizeof(void *);
        // If the table occupies 2 pages check if the second one is materialized in a frame
        if (((page + PAGE_SIZE) == (new_page & ADDRESS_MASK)) && vtpmo(new_page) == NO_MAP)
            break;
        // go for pattern matching
        addr = (unsigned long *) (page + i);
        if (((addr[FIRST_NI_SYSCALL] & 0x3) == 0)               // allineato a 8 B
            && (addr[FIRST_NI_SYSCALL] != 0x0)                  // non punta a 0x0
            && (addr[FIRST_NI_SYSCALL] >
                0xffffffff00000000)    // non punta in una locazione (non kernel) più bassa di 0xffffffff00000000
            && (addr[FIRST_NI_SYSCALL] == addr[SECOND_NI_SYSCALL])
            && (addr[FIRST_NI_SYSCALL] == addr[THIRD_NI_SYSCALL])
            && (addr[FIRST_NI_SYSCALL] == addr[FOURTH_NI_SYSCALL])
            && (addr[FIRST_NI_SYSCALL] == addr[FIFTH_NI_SYSCALL])
            && (addr[FIRST_NI_SYSCALL] == addr[SIXTH_NI_SYSCALL])
            && (addr[FIRST_NI_SYSCALL] == addr[SEVENTH_NI_SYSCALL])
            && (good_area(addr))) {

            hacked_syscall_tbl_found = (unsigned long **) addr;
            hacked_ni_syscall_found = (unsigned long *) addr[FIRST_NI_SYSCALL];

            return SUCCESS;
        }
    }
    return FAILURE;
}


/* This routines looks for the syscall table.  */
void syscall_table_finder(unsigned long ***hacked_syscall_tbl, unsigned long **hacked_ni_syscall, int *free_entry_par) {
    int i, j;
//    int free_entries[MAX_FREE];
    unsigned long k; // current page
    unsigned long candidate; // current page
    for (k = START; k < MAX_ADDR; k += 4096) {
        candidate = k;
        // check if candidate maintains the syscall_table
        if ((vtpmo(candidate) != NO_MAP)) {
            if (validate_page((unsigned long *) (candidate))) {
                // ho trovato sys_call_table e sys_ni_syscall e sono state salvate nelle due variabili "hacked_"
                *hacked_syscall_tbl = hacked_syscall_tbl_found; // return sys_call_table address to caller
                *hacked_ni_syscall = hacked_ni_syscall_found;   // return sys_ni_syscall address to caller
                // trova le free-entries
                j = 0;
                for (i = 0; i < ENTRIES_TO_EXPLORE; i++) {
                    if ((*hacked_syscall_tbl)[i] == (*hacked_syscall_tbl)[FIRST_NI_SYSCALL]) {
                        // printk("%s: trovata sys_ni_syscall in syscall_table[%d]\n", MODNAME, i);
                        free_entry_par[j++] = i; // return free_entry to caller!!!
                        if (j >= MAX_FREE) break;
                    }
                }
                break;
            }
        }
    }
}


//default array size already known - here we expose what entries are free
unsigned long cr0; // tra le altre cose, è il registro di controllo permette di attivare o disattivare la protezione read-only.

/**
 * Scrive forzatamente su CR0. Analoga a write_cr0() definita in special_insns.h, ma non stampa nessun warning su dmesg.
 * inline: dice al compilatore di PROVARE a copiare il codice della funzione nei punti in cui viene chiamata,
 * per risparmiare overhead sulla chiamata a funzione.
 * static: può essere usata solo in questa translation unit (cioè solo in questo file)
 * _force_order: obbliga la serializzazione delle operazioni di memoria in modo performante
 * @param val
 */
static inline void write_cr0_forced(unsigned long val) {
    unsigned long _force_order;
    asm volatile("mov %0, %%cr0" : "+r"(val), "+m"(_force_order)); // output scrive da val a cr0
}

/**
 * Scrive su CR0 il valore della variabile cr0.
 * static inline: viene sostituito il codice alla chiamata di funzione, ma solo in questo file.
 */
static inline void protect_memory(void) {
    write_cr0_forced(cr0);
}

/**
 * Scrive su CR0 il valore della variabile cr0, ma non la protezione in scrittura disabilitata.
 * static inline: viene sostituito il codice alla chiamata di funzione, ma solo in questo file.
 */
static inline void unprotect_memory(void) {
    write_cr0_forced(cr0 & ~X86_CR0_WP);
}

/**
 * Permette di installare una system call nella posizione specificata
 * della sys_call_table. Se le entry disponibili sono finite, fallisce
 * @param sys_call_function puntatore a funzione della system call da inserire (inizializzato con SYSCALL_DEFINE)
 */
int install_syscall(unsigned long *sys_call_function, int position) {
    if (hacked_syscall_tbl_found[position] != hacked_ni_syscall_found) {
        printk("%s: Impossibile installare la system call %d (non è sys_ni_syscall)\n", MODNAME, position);
        return FAILURE;
    }
    cr0 = read_cr0();
    unprotect_memory();
    hacked_syscall_tbl_found[position] = sys_call_function;
    protect_memory();
    printk("%s: Installata con successo system call in posizione %d\n", MODNAME, position);
    return SUCCESS;
}

/**
 * Permette di disinstallare la system call dato l'indice a cui è stata installata.
 * Fallisce se quell'indice non è valido (si sbaglia numero, oppure non è stata installata una system call)
 * @param position indice della system call table su cui è stata installata una system call con install_syscall
 */
int uninstall_syscall(const unsigned long *sys_call_function, int position) {
    if (hacked_syscall_tbl_found[position] != sys_call_function) {
        printk("%s: Impossibile resettare la system call %d perché diversa da quella in input\n", MODNAME, position);
        return FAILURE;
    }
    cr0 = read_cr0();
    unprotect_memory();
    hacked_syscall_tbl_found[position] = (unsigned long *) hacked_ni_syscall_found;
    protect_memory();
    printk("%s: Disinstallata con successo la system call dalla posizione %d\n", MODNAME, position);
    return SUCCESS;
}
