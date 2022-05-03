#ifndef MESSAGE_SLOT_H
#define MESSAGE_SLOT_H

#include <linux/ioctl.h>

#define MAJOR_NUM 235
#define DEVICE_RANGE_NAME "message_slot"
#define MAX_BUF_LEN 128
#define MAX_DEVICES 256
#define SUCCESS 0
#define FAIL 1
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned int)


typedef struct channel_node {
    unsigned long channel_id; // Default value is 0
    struct channel_node* next; // Default value should be NULL
    char msg[MAX_BUF_LEN]; // TODO: make sure this works with the O(C*M) space. If not - need to allocate memory each time according to user buffer length
    int msg_len;
} channel_node;

typedef struct file_meta {
    unsigned long channel_id;
    int minor_num;
} file_meta;


#endif //MESSAGE_SLOT_H
