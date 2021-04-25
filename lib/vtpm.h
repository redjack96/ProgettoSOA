//
// Created by giacomo on 21/11/20.
//

#ifndef MESSAGE_EXCHANGE_SERVICE_PARAM_VTPM_H
#define MESSAGE_EXCHANGE_SERVICE_PARAM_VTPM_H

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/kprobes.h>
#include <asm/apic.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <linux/syscalls.h>

#define ADDRESS_MASK 0xfffffffffffff000 // escludo l'offset fisico
#define PT_ADDRESS_MASK 0x7ffffffffffff000 // escludo il bit più significativo e l'offset.
#define VALID 0x1 // BIT DI PRESENZA PAGE TABLE ENTRY
#define PAGE_TABLE_ADDRESS phys_to_virt(_read_cr3() & ADDRESS_MASK)
#define LH_MAPPING 0x80 // Pagine Large/Huge
// queste macro restituiscono l'indice corrispondente alla page table indicata di un indirizzo logico.
#define PML4_indexof(addr) (((long long)(addr) >> 39) & 0x1ff) // indice pml4 nei bit 39-47 spostato nei 9 bit meno significativi.
#define PDP_indexof(addr)  (((long long)(addr) >> 30) & 0x1ff) // indice pdp nei bit 30-38 spostato nei 9 bit meno significativi.
#define PDE_indexof(addr)  (((long long)(addr) >> 21) & 0x1ff) // indice pde nei bit 21-29 spostato nei 9 bit meno significativi.
#define PTE_indexof(addr)  (((long long)(addr) >> 12) & 0x1ff) // indice pte nei bit 12-20 spostato nei 9 bit meno significativi.
#define NO_MAP (-1)

long vtpmo(unsigned long vaddr);

#endif //MESSAGE_EXCHANGE_SERVICE_PARAM_VTPM_H
