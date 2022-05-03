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

// devices_arr: array of lenth 256 of type channel_node*. devices_arr[x] == first channel node created for minor x (or to null if none was created yet)
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

// if channel_node exists - returns it. Else: if in reading mode - returns None, else created new node
channel_node* get_channel_node(int minor_num, unsigned long channel_id, int is_read){
    channel_node* curr_channel_node = devices_arr[minor_num];
    channel_node* prev_channel_node;

    if (devices_arr[minor_num] == NULL){ // device doesn't have any nodes yet - so create one
        printk("There are no nodes of minor number %d.\n", minor_num);
        printk("Creating first node with channel id %ld\n", channel_id);
        devices_arr[minor_num] = create_channel_node(minor_num, channel_id);
        return devices_arr[minor_num];
    }

    while (curr_channel_node != NULL){
        if (curr_channel_node->channel_id == channel_id) {
            return curr_channel_node;
        }
        prev_channel_node = curr_channel_node;
        curr_channel_node = curr_channel_node->next; // Advance node on linked_list
    }
    printk("There is no node of minor number %d with channel %ld.\n", minor_num, channel_id);
    
    if (is_read){
    	printk("Read mode: returning NULL bc there is no channel_node of the specified channel\n");
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
        return -ENOMEM;
    }
    ((file_meta*)file->private_data)->minor_num = iminor(inode);

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
            return -EINVAL;
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

    _channel_node = get_channel_node(minor_num, channel_id, 1);

    if (_channel_node == NULL) { // no message set to channel_id
    	printk("There is no node with specified channel - returning NULL\n");
        return -EWOULDBLOCK;
    }

    if (length < _channel_node->msg_len) {
    	printk("Error: length of buffer is shorter than channel msg len\n");
        return -ENOSPC;
    }

    for (i=0; i<_channel_node->msg_len; i++){
        if (put_user(_channel_node->msg[i], &buffer[i]) < 0 ){
        	return -EFAULT;
        }
    }
	
    return _channel_node->msg_len;
}

//---------------------------------------------------------------
// a processs which has already opened the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length, // length of message
                             loff_t*            offset)
{
	channel_node* _channel_node;
    char temp_msg[MAX_BUF_LEN];
    
    printk("Invoking device_write: (%p,%ld)\n", file, length);
    if (file->private_data == NULL || ((file_meta*)file->private_data)->channel_id == 0) { // device_open or device_ioctl hasn't been called
        return -EINVAL;
    }

    minor_num = ((file_meta*)file->private_data)->minor_num;
    channel_id = ((file_meta*)file->private_data)->channel_id;

	if (length > MAX_BUF_LEN || length == 0){
        return -EMSGSIZE;
    }

	// gets or creates channel_node according to specified minor and channel
    _channel_node = get_channel_node(minor_num, channel_id, 0);
    if (_channel_node == NULL) { // allocation failed
    	return -ENOMEM;
    }

    // first write to temp_message to make sure there are no errors (in user space buffer)
    for (i=0; i<length; i++){
        if (get_user(temp_msg[i], &buffer[i]) != 0) { // error
            return -EFAULT;
        }
    }

    // upon success - write to real message
    for (i=0; i<length; i++){
        _channel_node->msg[i] = temp_msg[i];
    }
    _channel_node->msg_len = i; // i==length should always be true
	
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
