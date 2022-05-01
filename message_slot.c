/* Assumptions:
    - minor numbers are in the range of 0 to 255
*/

// errno - https://stackoverflow.com/questions/24567584/how-to-set-errno-in-linux-device-driver
// The c library interprets this and gives a -1 return code and sets errno to the positive error
// f_inode - https://elixir.bootlin.com/linux/latest/source/include/linux/fs.h#L956

// TODO: decide if to change file->private_data to something else (the node) or add .release to FOPS.
// TODO: make sure when getting a variable that could be NULL - wont return null-pointer-error when referring to it.

//Defining __KERNEL__ and MODULE allows us to access kernel-level
// code not usually available to userspace programs.
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE


#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/errno.h>
#include <linux/slab.h>  /* for kmalloc. use GFP_KERNEL flag*/
#include <linux/types.h> /*for MSG_SLOT_CHANNEL command type*/


#include "message_slot.h"

MODULE_LICENSE("GPL");

/*
devices_arr - array of lenth 256 - of type channel_node* (array of pointers channel_node). array[x] == points to the first channel created for minor x (or to null if none was created yet)
channel_node -  (struct with fields next, channel_id, msg, msg_len?)
*/

// TODO: does it need to be * or **? Depends if I want it to be a POINTER to channel_node or a channel_node itself.
static struct channel_node* devices_arr[MAX_DEVICES]; // array of channel_nodes (devices_arr[x] is initialized == NULL if no channel was created yet in minor x)
int minor_num;
unsigned long channel_id;
int i;

channel_node* create_channel_node(int minor_num, unsigned long channel_id);
channel_node* get_channel_node(int minor_num, unsigned long channel_id, int is_read);
static int device_open( struct inode* inode, struct file*  file );
static int device_close(struct inode* inode, struct file*  file );
static long device_ioctl( struct   file* file, unsigned int   ioctl_command_id, unsigned long  ioctl_param );
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset );
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length, // length of message
                             loff_t*            offset);

//================== HELPER FUNCTIONS ===========================

channel_node* create_channel_node(int minor_num, unsigned long channel_id){
    channel_node* _channel_node = (channel_node*)kmalloc(sizeof(channel_node), GFP_KERNEL);
    _channel_node->channel_id = channel_id;
    _channel_node->next = NULL;
    return _channel_node;
}

/* if channel_node exists - returns it
    else:
    if is read:
        return NULL
    if is write:
        create new node to last node in list
*/
channel_node* get_channel_node(int minor_num, unsigned long channel_id, int is_read){
    channel_node* curr_channel_node = devices_arr[minor_num];
    channel_node* prev_channel_node;

    if (devices_arr[minor_num] == NULL){ // device doesn't have any nodes yet - so create one
        printk("There are no nodes of minor number %d.\n", minor_num);
        printk("Creating first node with channel id %ld\n", channel_id);
        devices_arr[minor_num] = create_channel_node(minor_num, channel_id);

        if (devices_arr[minor_num] == NULL){ // allocation failed
            return NULL; // TODO: change to error of type allocation failure
        }
        return devices_arr[minor_num];
    }

    while (curr_channel_node != NULL){
        if (curr_channel_node->channel_id == channel_id) {
            return curr_channel_node;
        }
        prev_channel_node = curr_channel_node; // TODO: not sure this will work (might be pointing at same object) - maybe have to do some deep copy?
        curr_channel_node = curr_channel_node->next; // Advance node on linked_list
    }
    printk("There is no node of minor number %d with channel %ld.\n", minor_num, channel_id);

    if (is_read){
        return NULL;
    }

    printk("Creating a new node\n");
    curr_channel_node = create_channel_node(minor_num, channel_id);
    prev_channel_node->next = curr_channel_node;
    return curr_channel_node;
}

//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
                        struct file*  file )
{
    printk("Invoking device_open: (%p)\n", file);

    // Initialize file metadata (for now only minor_num)
    file->private_data = (file_meta*)kmalloc(sizeof(file_meta), GFP_KERNEL); // Initialize private_data field
    if (file->private_data == NULL){ // allocation failed
        return -1; // TODO: change to error of type allocation failure
    }
    ((file_meta*)file->private_data)->minor_num = iminor(inode); // TODO: maybe its enough: file->private_data->minor_num ?

    return SUCCESS;
}

//----------------------------------------------------------------
static int device_close(struct inode* inode, struct file*  file ){
    if (file->private_data != NULL){ // Checking that device_open has been called and there is what to free
        kfree(file->private_data);
    }

    return SUCCESS;
}

//----------------------------------------------------------------
static long device_ioctl( struct   file* file,
                          unsigned int   ioctl_command_id,
                          unsigned long  ioctl_param )
{
    if (ioctl_command_id==MSG_SLOT_CHANNEL && ioctl_param != 0) {
        printk("Invoking ioctl: setting channel_id to %ld\n", ioctl_param);
        if (file->private_data == NULL) { // device_open hasn't been called
            return -EINVAL; // TODO: make sure this is the correct error to return
        }
        // Initialize channel_id
        ((file_meta*)file->private_data)->channel_id = ioctl_param;
        return SUCCESS;
    }
    return -EINVAL; // wrong command or wrong ioctl_param
}

//---------------------------------------------------------------
/* Assumptions - the following functions were already called:
    device_open -  file->private_data->minor_num was set to minor number of device [error: EINVAL]
    device_ioctl - file->private_data->channel_id was set to channel_id (according to user parameter) [error: EINVAL]
    device_write - message has been set to the files channel_id [error: EWOULDBLOCK]
                   devices_arr[minor_num] - has a node (in linked_list) with channel_id and a msg
*/

static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset )
{
	channel_node* _channel_node;
    printk( "Invoking device_read: (%p,%ld)\n", file, length);
    if (file->private_data == NULL || ((file_meta*)file->private_data)->channel_id == 0) { // device_open or device_ioctl hasn't been called
        return -EINVAL;
    }

    channel_id = ((file_meta*) file->private_data)->channel_id;
    minor_num = ((file_meta*) file->private_data)->minor_num;
    printk("channel_id=%ld, minor_num=%d", channel_id, minor_num);

    _channel_node = get_channel_node(minor_num, channel_id, 1);

    if (_channel_node == NULL) { // no message set to channel_id
        return -EWOULDBLOCK;
    }

    if (length < _channel_node->msg_len) {
        return -ENOSPC;
    }

    // read message from message slot file to user's buffer
    printk("starting to copy_to_user");
    if (copy_to_user(buffer, _channel_node->msg, _channel_node->msg_len) < 0){
    	return -1; // TODO: change to relevant error type (use strerror)
    }
    printk("buffer=%s", buffer);
    printk("_channel_node->msg=%s", _channel_node->msg);
    printk("finished read");
    //for (i=0; i<_channel_node->msg_len; i++){
      //  put_user(_channel_node->msg[i], &buffer[i]);
    //}
	
    return SUCCESS;
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length, // length of message
                             loff_t*            offset)
{
	channel_node* _channel_node;
    char temp_msg[MAX_BUF_LEN]; // to not write into real node until making sure the user msg is legal
    
    printk("Invoking device_write: (%p,%ld)\n", file, length);
    if (file->private_data == NULL || ((file_meta*)file->private_data)->channel_id == 0) { // device_open or device_ioctl hasn't been called
        return -EINVAL;
    }

    minor_num = ((file_meta*)file->private_data)->minor_num;
    channel_id = ((file_meta*)file->private_data)->channel_id;

    _channel_node = get_channel_node(minor_num, channel_id, 0);
    if (_channel_node == NULL) { // function get_channel_node failed
    	return -1; // TODO: change to relevant error type (use strerror)
    }

    if (length > MAX_BUF_LEN || length == 0){
        return -EMSGSIZE;
    }

    // first write to temp_message to make sure there are no errors (in user space buffer)
    // TODO: Don’t include the terminating null character of the C string as part of the message:
    //  does this mean i have to set length = length-1 ?
    for (i=0; i<length; i++){
        if (get_user(temp_msg[i], &buffer[i]) != 0) { // error
            return -1; // TODO: change to relevant error type (use strerror)
        }
    }

    // upon success - write to real message
    for (i=0; i<length; i++){
        _channel_node->msg[i] = temp_msg[i];
    }
    _channel_node->msg_len = i; // i or length? anyways: i==length should always be true at this point (unless i should check it?)
	
	printk("_channel_node->msg_len=%d\n", _channel_node->msg_len);
	printk("_channel_node->msg=%s\n", _channel_node->msg);
	
    return i;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
{
  .owner	  = THIS_MODULE,
  .read           = device_read,
  .write          = device_write,
  .open           = device_open,
  .unlocked_ioctl = device_ioctl,
  .release        = device_close // why isnt this called close?
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
    int rc = -1; // init
    rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops ); // returns 0 upon success

    // Negative values signify an error
    if( rc < 0 ) {
        printk( KERN_ALERT "%s registration failed for  %d\n",
                       DEVICE_RANGE_NAME, MAJOR_NUM );
        return rc;
    }

    // Initializing devices_arr
    for (i=0; i < MAX_DEVICES; i++) {
      devices_arr[i] = NULL;
    }

    printk( "Registeration is successful. ");
    return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void) {
    printk("Unregistering device.");
    // Unregister the device - should always succeed
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);

    // free allocated memory
    for (i=0; i<MAX_DEVICES; i++){
        channel_node* curr_channel_node = devices_arr[i];
        channel_node* prev_channel_node;
        while (curr_channel_node != NULL) {
                prev_channel_node = curr_channel_node;
                curr_channel_node = curr_channel_node->next;
                kfree(prev_channel_node);
            }
        }
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
