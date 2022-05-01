#include <fcntl.h> // for open
#include <unistd.h> // for read and close
#include <sys/ioctl.h> // for ioctl
#include <stdio.h> // for printf, fprintf, stderr
#include <stdlib.h> // for exit
#include <string.h> // for strlen
#include <errno.h> // for errno

#include "message_slot.h"


int main(int argc, char *argv[]) {
    char* msgslot_file_path;
    unsigned long channel_id;
    int fd, num_read, num_write;
    char user_buffer[MAX_BUF_LEN];
    

    if (argc != 3) {
        printf("number of args=%d, incorrect number of arguments", argc); // change to fprintf
        exit(FAIL);
    }
    msgslot_file_path = argv[1];
    channel_id = atoi(argv[2]);

    fd = open(msgslot_file_path, O_RDWR);
    if (fd<0){
		fprintf(stderr, "ERROR: failed to open file, errorno: %s\n", strerror(errno));
        exit(FAIL);
    }
    if (ioctl(fd, MSG_SLOT_CHANNEL, channel_id) < 0){
        fprintf(stderr, "ERROR: failed to set channel to file, errorno: %s\n", strerror(errno));
        close(fd);
        exit(FAIL);
    }

    num_read = read(fd, user_buffer, MAX_BUF_LEN);
    if (num_read < 0) {
        fprintf(stderr, "ERROR: failed to read message, errorno: %s\n", strerror(errno));
        close(fd);
        exit(FAIL);
    }
		
	if (close(fd) < 0) {
        fprintf(stderr, "ERROR: failed to close file, errorno: %s\n", strerror(errno));
        exit(FAIL);
    }
    
    // write to standard output (fd of stdout is 1)
   	num_write = write(1, user_buffer, num_read);
    if (num_write != num_read || num_write < 0) {
        fprintf(stderr, "ERROR: failed write full message to stdout, errorno: %s\n", strerror(errno));
        exit(FAIL);
    }

    exit(SUCCESS);

}
