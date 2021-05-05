//
// Created by giacomo on 16/04/21.
//

#ifndef TAG_SERVICE_TAG_DEVICE_DRIVER_H
#define TAG_SERVICE_TAG_DEVICE_DRIVER_H

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include "commands.h"
#include "tag_service.h"

int init_device_driver(ts_management *tsm);

int ts_create_char_device_file(int tag_minor);

void ts_destroy_char_device_file(int minor_tag);

void destroy_driver_and_all_devices(void);

void change_epoch(int tag_minor);


#endif //TAG_SERVICE_TAG_DEVICE_DRIVER_H
