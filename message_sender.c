#include <fcntl.h> // for open
#include <unistd.h> // for write and close
#include <sys/ioctl.h> // for ioctl
#include <stdio.h> // for printf, fprintf, stderr
#include <stdlib.h> // for exit
#include <string.h> // for strlen
#include <errno.h> // for errno

#include "message_slot.h"



int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("number of args=%d, incorrect number of arguments", argc); // change to fprintf
        exit(FAIL);
    }
    char* msgslot_file_path = argv[1];
    unsigned long channel_id = atoi(argv[2]);
    char* msg = argv[3];
    int msg_len = strlen(msg);

    int fd = open(msgslot_file_path, O_RDWR);
    if (fd<0){
		fprintf(stderr, "ERROR: failed to open file, errorno: %s\n", strerror(errno));
        exit(FAIL);
    }
    if (ioctl(fd, MSG_SLOT_CHANNEL, channel_id) < 0){
        fprintf(stderr, "ERROR: failed to set channel to file, errorno: %s\n", strerror(errno));
        close(fd);
        exit(FAIL);
    }

    int num_write = write(fd, msg, msg_len); // make sure strlen doesnt include last null char.
    if (num_write != msg_len) {
        fprintf(stderr, "ERROR: wrote %d out of %d bytes. Failed to write full message, errorno: %s\n", num_write, msg_len, strerror(errno));
        close(fd);
        exit(FAIL);
    }

    if (close(fd) < 0) {
        fprintf(stderr, "ERROR: failed to close file, errorno: %s\n", strerror(errno));
        exit(FAIL);
    }

    exit(SUCCESS);
}
