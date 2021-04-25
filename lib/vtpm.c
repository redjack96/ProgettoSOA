//
// Created by giacomo on 21/11/20.
//
#include "vtpm.h"

#define MODNAME "VIRTUAL-TO-PHYSICAL"

//auxiliary stuff
static inline unsigned long _read_cr3(void) {
    unsigned long val;
    asm volatile("mov %%cr3,%0":  "=r" (val) : );
    return val;
}

/**
 * @param vaddr indirizzo logico.
 * @return IL numero del frame fisico che corrisponde a vaddr.
 */
long vtpmo(unsigned long vaddr) {
    void *target_address; // copia dell'indirizzo fisico passato in input
    // pte|pmd|pud|pgd
    pgd_t *pml4;    // PGD/PML4 contiene l'indirizzo (fisico) della page table di liv 0
    pud_t *pdp;     // PUD/PDP  contiene l'indirizzo (fisico) della page table di liv 1
    pmd_t *pde;     // PMD/PDE  contiene l'indirizzo (fisico) della page table di liv 2
    pte_t *pte;     // PTE      contiene l'indirizzo (fisico) della page table di liv 3

    unsigned long frame_number;
    unsigned long frame_addr;

    target_address = (void *) vaddr;
    /* fixing the address to use for page table access */


    // Ricava l'indirizzo della PML4
    pml4 = PAGE_TABLE_ADDRESS; // ATTENZIONE: potrebbe essere acceduta in concorrenza!!! NON COMPLIANT CON REQUISITO DI ATOMICITA'
    // Per rendere l'esempio ATOMICO: dobbiamo usare le strutture current->mm (Thread control block-> Memory Management)
    // pml4 = current->mm->pgd; // va usato questo!!! Ma non e' disponibile su kernel 5...

    /*
     * pml4 = indirizzo della page table: scegliamo un suo elemento
     * PML4_indexof(target_address): dato un target_address logico, ricaviamo i 9 bit con l'indice della page table corrispondente
     * .pgd: contenuto a 64 bit della ENTRY della page table corrispondente a all'indirizzo logico. E' l'indirizzo fisico.
     */
    //La macro PML4_indexof ci fa ottenere i 9 bit per spiazzarci sulla page table PML4
    if (((ulong) (pml4[PML4_indexof(target_address)].pgd)) & VALID) {
        // la entry pml4 relativa a vaddr è valida. Continua.
    } else {
        // L'elemento della PML4 non mappa in memoria fisica
        return NO_MAP; //-1
    }

    // Ricavo l'indirizzo logico a partire dall'indirizzo fisico contenuto in .pgd. Escludo alcuni bit.
    pdp = __va((ulong) (pml4[PML4_indexof(target_address)].pgd) & PT_ADDRESS_MASK);

    if ((ulong) (pdp[PDP_indexof(target_address)].pud) & VALID) { //.pud
        // la entry pdp relativa a vaddr è valida. Continua.
    } else {
        return NO_MAP;
    }

    // Ricavo l'indirizzo logico a partire dall'indirizzo fisico contenuto in .pud (pdp)
    pde = __va((ulong) (pdp[PDP_indexof(target_address)].pud) & PT_ADDRESS_MASK);

    if ((ulong) (pde[PDE_indexof(target_address)].pmd) & VALID) {
        // la entry pde relativa a vaddr è valida. Continua.
    } else {
        return NO_MAP;
    }
    // Large/Huge pages: la pagina puntata dalla pde potrebbe essere frame fisico da 2 MB non una pte.
    if ((ulong) pde[PDE_indexof(target_address)].pmd &
        LH_MAPPING) { // .pmd L'ottavo bit (7 perché parte da 0) verifica LH pages

        // Ricaviamo l'indirizzo fisico del frame (i primi 12 bit sono a 0)
        frame_addr = (ulong) (pde[PDE_indexof(target_address)].pmd) & PT_ADDRESS_MASK; //.pmd

        // shift di 12 a destra per eliminare gli 0 iniziali.
        frame_number = frame_addr >> 12;

        return frame_number;
    }

    // Se invece è una PTE,  ricavo il suo indice.
    // ricavo l'indirizzo logico dall'indirizzo fisico della PTE
    pte = __va((ulong) (pde[PDE_indexof(target_address)].pmd) & PT_ADDRESS_MASK); //.pmd

    if ((ulong) (pte[PTE_indexof(target_address)].pte) & VALID) {
    } else {
        return NO_MAP;
    }
    // Ricaviamo l'indirizzo fisico del frame (i primi 12 bit sono a 0)
    frame_addr = (ulong) (pte[PTE_indexof(target_address)].pte) & PT_ADDRESS_MASK;

    // shift di 12 a destra per eliminare gli 0 iniziali.
    frame_number = frame_addr >> 12;

    return frame_number; // numero del frame (senza offset)
}