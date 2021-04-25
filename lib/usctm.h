//
// Created by giacomo on 07/03/21.
//

#ifndef TAG_SERVICE_USCTM_H
#define TAG_SERVICE_USCTM_H

#define ADDRESS_MASK        0xfffffffffffff000          // prende tutto tranne l'offset dell'indirizzo
#define START               0xffffffff00000000ULL       // use this as starting address --> this is a biased search since does not start from 0xffff000000000000
#define MAX_ADDR            0xfffffffffff00000ULL
#define FIRST_NI_SYSCALL    134
#define SECOND_NI_SYSCALL   174
#define THIRD_NI_SYSCALL    182  // in realtà anche 177, 178, 180, 181 sono sys_ni_syscall
#define FOURTH_NI_SYSCALL   183
#define FIFTH_NI_SYSCALL    214
#define SIXTH_NI_SYSCALL    215
#define SEVENTH_NI_SYSCALL  236
#define MAX_FREE            15
#define ENTRIES_TO_EXPLORE  256
#define SUCCESS 1
#define FAILURE 0

void syscall_table_finder(unsigned long ***hacked_syscall_tbl, unsigned long **hacked_ni_syscall, int *free_entry_par);
int install_syscall(unsigned long *sys_call_function, int position);
int uninstall_syscall(const unsigned long *sys_call_function, int position);

#endif //TAG_SERVICE_USCTM_H
