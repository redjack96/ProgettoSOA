//
// Created by giacomo on 15/03/21.
//

#ifndef TAG_SERVICE_COMMANDS_H
#define TAG_SERVICE_COMMANDS_H

#define MAX_TAG_SERVICES 512
#define MAX_LEVELS 32
// commands
#define CREATE_TAG 0
#define OPEN_TAG 1
#define REMOVE_TAG 2
#define AWAKE_ALL 3
// permission
#define EVERYONE 0
#define ONLY_OWNER 1
// messages
#define NOT_READY 0
#define READY 1
#define MAX_MESSAGE_SIZE 4096


#endif //TAG_SERVICE_COMMANDS_H
