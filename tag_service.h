//
// Created by giacomo on 04/03/21.
//

#ifndef TAG_SERVICE_TAG_SERVICE_H
#define TAG_SERVICE_TAG_SERVICE_H

/* Needed libraries */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/kprobes.h>
#include <linux/syscalls.h>
#include "lib/usctm.h"
#include "commands.h"

/* Macro definitions */
#define AUDIT if(1)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define UID current->cred->uid.val
#define EUID current->cred->euid.val
#else
#define UID current_cred()->uid.val
#define EUID current_cred()->euid.val
#endif
#define MOUNT_FAILURE (-1)
#define MOUNT_SUCCESS 0
#define FORMAT_STRING "%s: thread %d - %s\n"
#define FORMAT_STRING1 "%s: thread %d - %s %d\n"
#define _LOG(msg, modname, mode) printk(mode FORMAT_STRING, modname, current->pid, msg)
#define _LOG1(msg, modname, mode, arg) printk(mode FORMAT_STRING1, modname, current->pid, msg, (int) (arg))
#define printk AUDIT printk

/* Struct definitions*/
typedef struct the_level {
    int tag;
    int message_ready;
    unsigned long thread_waiting;
    char *message;
    unsigned long size;
    struct rcu_head rcu;
} tag_level;

typedef struct my_tag_service {
    tag_level *level;
    unsigned long thread_waiting_message_count; // incrementato e decrementato atomicamente: numero di thread in attesa su questo tag service
    int key;                                    /* chiave fornita dall'utente che permette di generare il descrittore del tag univocamente */
    int tag;                                    /* descrittore del tag_service e minor number del device driver associato */
    int permission;                             /* permessi per il tag_service: ONLY_OWNER o EVERYONE */
    int owner_euid;
    int owner_uid;
    int awake_request;
    struct rcu_head tag_rcu;
    int lazy_deleted;                           // Impostato a YES quando si vuole eliminare tutto il tag. Tutte le istruzioni falliscono se e' impostato a YES
} tag_service;

typedef struct my_ts_management {
    int first_free_entry;
    unsigned long remaining_entries;            // incrementato e decrementato atomicamente
    struct mutex access_lock[MAX_TAG_SERVICES]; // per rimuovere l'intero tag, svegliare o accedere a qualsiasi altra info in scrittura!!!
    unsigned int major;                         // major number del device driver
    tag_service **all_tag_services;
} ts_management;

#endif //TAG_SERVICE_TAG_SERVICE_H
