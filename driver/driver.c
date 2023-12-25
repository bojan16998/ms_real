/* Kernel headers */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/ioport.h>
#include <asm/io.h>

/* DMA headers */

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

MODULE_AUTHOR("Vajo Bojan David Nadezda");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Driver for title_ip");

#define DRIVER_NAME "title_driver" 
#define DEVICE_NAME "title_device"
#define BUFF_SIZE 20

/* -------------------------------------- */
/* -------TITLE IP RELATED MACROS-------- */
/* -------------------------------------- */

#define LETTER_DATA_LEN			    214*2	

#define POSSITION_LEN               106*2

#define D0_LETTER_MATRIX_LEN        16602*2
#define D1_LETTER_MATRIX_LEN        22716*2
#define D2_LETTER_MATRIX_LEN        29792*2
#define D3_LETTER_MATRIX_LEN        37569*2
#define D4_LETTER_MATRIX_LEN        46423*2

#define D0_BRAM                     101
#define D1_BRAM                     67
#define D2_BRAM                     50
#define D3_BRAM                     40
#define D4_BRAM                     33

#define D0_WIDTH                    640
#define D1_WIDTH                    960
#define D2_WIDTH                    1280
#define D3_WIDTH                    1600
#define D4_WIDTH                    1920

#define MAX_PKT_LEN			        101*640*3*2

#define IP_COMMAND_LOAD_LETTER_DATA		0x0001
#define IP_COMMAND_LOAD_LETTER_MATRIX	0x0002
#define IP_COMMAND_LOAD_TEXT	        0x0004
#define IP_COMMAND_LOAD_POSSITION		0x0008
#define IP_COMMAND_LOAD_PHOTO	        0x0010
#define IP_COMMAND_PROCESSING	        0x0020
#define IP_COMMAND_SEND_FROM_BRAM	    0x0040
#define IP_COMMAND_RESET	            0x0080

/* -------------------------------------- */
/* ----------DMA RELATED MACROS---------- */
/* -------------------------------------- */

#define MM2S_CONTROL_REGISTER       	0x00
#define MM2S_STATUS_REGISTER        	0x04
#define MM2S_SRC_ADDRESS_REGISTER   	0x18
#define MM2S_TRNSFR_LENGTH_REGISTER 	0x28

#define S2MM_CONTROL_REGISTER       	0x30
#define S2MM_STATUS_REGISTER       	    0x34
#define S2MM_DST_ADDRESS_REGISTER   	0x48
#define S2MM_BUFF_LENGTH_REGISTER   	0x58

#define DMACR_RESET			    0x04
#define IOC_IRQ_FLAG			1 << 12
#define ERR_IRQ_EN			    1 << 14

#define AXI_OFFSET              0x4

/* -------------------------------------- */
/* --------FUNCTION DECLARATIONS--------- */
/* -------------------------------------- */

static int  title_probe(struct platform_device *pdev);
static int  title_remove(struct platform_device *pdev);
int         title_open(struct inode *pinode, struct file *pfile);
int         title_close(struct inode *pinode, struct file *pfile);
ssize_t     title_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t     title_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);

static int  __init title_init(void);
static void __exit title_exit(void);

static int title_mmap(struct file *f, struct vm_area_struct *vma_s);

static irqreturn_t title_command_isr(int irq, void* dev_id);
static irqreturn_t title_frame_isr(int irq, void* dev_id);
static irqreturn_t dma_MM2S_isr(int irq, void* dev_id);
static irqreturn_t dma_S2MM_isr(int irq, void* dev_id);

//irq_handler_t title_command_handler_irq = &title_command_isr;
//irq_handler_t title_frame_handler_irq = &title_frame_isr;
//irq_handler_t dma_MM2S_handler_irq = &dma_MM2S_isr;
//irq_handler_t dma_S2MM_handler_irq = &dma_S2MM_isr;

int dma_init(void __iomem *base_address);
unsigned int dma_simple_write(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address); 
unsigned int dma_simple_read(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address);

/* -------------------------------------- */
/* -----------GLOBAL VARIABLES----------- */
/* -------------------------------------- */

struct title_info
{
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
	int irq_num0;
    int irq_num1;
};

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device_title;
static struct device *my_device_dma;
static struct cdev *my_cdev;
static struct title_info *dma_p = NULL;
static struct title_info *title_p = NULL;

struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = title_open,
	.release = title_close,
	.read = title_read,
	.write = title_write,
	.mmap = title_mmap
};

static struct of_device_id title_of_match[] = {
	{ .compatible = "title_ip", },
	{ .compatible = "dma_ip", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, title_of_match);

static struct platform_driver title_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= title_of_match,
	},
	.probe		= title_probe,
	.remove		= title_remove,
};

dma_addr_t tx_phy_buffer;
u16 *tx_vir_buffer;

/* -------------------------------------- */
/* -------INIT AND EXIT FUNCTIONS-------- */
/* -------------------------------------- */

static int __init title_init(void)
{
	int ret = 0;
	int i = 0;

	// printk(KERN_INFO "[title_init] Initialize Module \"%s\"\n", DEVICE_NAME);

	ret = alloc_chrdev_region(&my_dev_id, 0, 2, "TITLE_region");
	if(ret)
	{
		printk(KERN_ALERT "[title_init] Failed CHRDEV!\n");
		return -1;
	}
	// printk(KERN_INFO "[title_init] Successful CHRDEV!\n");

	my_class = class_create(THIS_MODULE, "title_class");
	if(my_class == NULL)
	{
		printk(KERN_ALERT "[title_init] Failed class create!\n");
		goto fail_0;
	}
	// printk(KERN_INFO "[title_init] Successful class chardev1 create!\n");

	my_device_title = device_create(my_class, NULL, MKDEV(MAJOR(my_dev_id), 0), NULL, "title-ip");
	if(my_device_title == NULL)
	{
		goto fail_1;
	}
	// printk(KERN_INFO "[title_init] Device title-ip created\n");


	my_device_dma = device_create(my_class, NULL, MKDEV(MAJOR(my_dev_id), 1), NULL, "dma");
	if(my_device_dma == NULL)
	{
		goto fail_2;
	}
	// printk(KERN_INFO "[title_init] Device dma created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 2);
	if(ret)
	{
		printk(KERN_ERR "[title_init] Failed to add cdev\n");
		goto fail_3;
	}
	// printk(KERN_INFO "[title_init] Module init done\n");

	ret = dma_set_coherent_mask(my_device_dma, DMA_BIT_MASK(64));
	if(ret < 0)
	{
		printk(KERN_WARNING "[title_init] DMA coherent mask not set!\n");
	}
	else
	{
		// printk(KERN_INFO "[title_init] DMA coherent mask set\n");
	}

	tx_vir_buffer = dma_alloc_coherent(my_device_dma, MAX_PKT_LEN, &tx_phy_buffer, GFP_KERNEL);
	// printk(KERN_INFO "[title_init] Virtual and physical addresses coherent starting at %x and ending at %x\n", tx_phy_buffer, tx_phy_buffer+(uint)(MAX_PKT_LEN));
	if(!tx_vir_buffer)
	{
		printk(KERN_ALERT "[title_init] Could not allocate dma_alloc_coherent for img");
		goto fail_4;
	}
	else
	{
		// printk("[title_init] Successfully allocated memory for dma transaction buffer\n");
	}
	
	for (i = 0; i < MAX_PKT_LEN/2; i++)
	{
		tx_vir_buffer[i] = 0x0000;
	}
	
	// printk(KERN_INFO "[title_init] DMA memory reset.\n");
	return platform_driver_register(&title_driver);

	fail_4:
		cdev_del(my_cdev);
	fail_3:
		device_destroy(my_class, MKDEV(MAJOR(my_dev_id),1));
	fail_2:
		device_destroy(my_class, MKDEV(MAJOR(my_dev_id),0));
	fail_1:
		class_destroy(my_class);
	fail_0:
		unregister_chrdev_region(my_dev_id, 2);
	return -1;
} 


static void __exit title_exit(void)
{
    	/* Reset DMA memory */
	int i = 0;
	for (i = 0; i < MAX_PKT_LEN/2; i++) 
	{
		tx_vir_buffer[i] = 0x0000;
	}

	// printk(KERN_INFO "[title_exit] DMA memory reset\n");

	/* Exit Device Module */
	platform_driver_unregister(&title_driver);
	cdev_del(my_cdev);
	device_destroy(my_class, MKDEV(MAJOR(my_dev_id),0));
	device_destroy(my_class, MKDEV(MAJOR(my_dev_id),1));
	class_destroy(my_class);
	unregister_chrdev_region(my_dev_id, 2);
	dma_free_coherent(my_device_dma, MAX_PKT_LEN, tx_vir_buffer, tx_phy_buffer);
	// printk(KERN_INFO "[title_exit] Exit device module finished\"%s\".\n", DEVICE_NAME);
}

module_init(title_init);
module_exit(title_exit);  


/* -------------------------------------- */
/* -----PROBE AND REMOVE FUNCTIONS------- */
/* -------------------------------------- */

int device_fsm = 0;

static int title_probe(struct platform_device *pdev) 
{
	struct resource *r_mem;
	int rc = 0;
	const char *comp = of_get_property(pdev->dev.of_node, "compatible", NULL);
	
	// if(comp)  	printk(KERN_INFO "Probing %s\n", comp);
	// else 	printk(KERN_INFO "Not found\n");

	if(comp == "dma_ip") device_fsm = 0;
	if(comp == "title_ip") device_fsm = 1;

	switch(device_fsm)
	{
	case 0:
		
		r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if(!r_mem)
		{
			printk(KERN_ALERT "[title_probe] Failed to get reg resource.\n");
			return -ENODEV;
		}

		printk(KERN_ALERT "[title_probe] Probing dma_p\n");
		 
		dma_p = (struct title_info *) kmalloc(sizeof(struct title_info), GFP_KERNEL);
		if(!dma_p) 
		{
			printk(KERN_ALERT "[title_probe] Could not allocate DMA device\n");
			return -ENOMEM;
		}

		dma_p->mem_start = r_mem->start;
		dma_p->mem_end = r_mem->end;

		if(!request_mem_region(dma_p->mem_start, dma_p->mem_end - dma_p->mem_start + 1,	DEVICE_NAME)) 
		{
			printk(KERN_ALERT "[title_probe] Could not lock memory region at %p\n",(void *)dma_p->mem_start);
			rc = -EBUSY;
			goto error4;
		}

		dma_p->base_addr = ioremap(dma_p->mem_start, dma_p->mem_end - dma_p->mem_start + 1);
		if (!dma_p->base_addr) 
		{
			printk(KERN_ALERT "[title_probe] Could not allocate memory\n");
			rc = -EIO;
			goto error5;
		}
		
		// printk(KERN_INFO "[title_probe] dma base address start at %x\n", dma_p->base_addr);
		
        /*		
		
		dma_p->irq_num = platform_get_irq(pdev, 0);
		if(!dma_p->irq_num)
		{
			printk(KERN_ERR "[title_probe] Could not get IRQ resource\n");
			rc = -ENODEV;
			goto error5;
		}

		if (request_irq(dma_p->irq_num, dma_MM2S_isr, 0, DEVICE_NAME, dma_p)) {
			printk(KERN_ERR "[title_probe] Could not register IRQ %d\n", dma_p->irq_num);
			return -EIO;
			goto error6;
		}
		else {
			printk(KERN_INFO "[title_probe] Registered MM2S IRQ %d\n", dma_p->irq_num);
		}
		if(!dma_p->irq_num)
		{
			printk(KERN_ERR "[title_probe] Could not get IRQ resource\n");
			rc = -ENODEV;
			goto error5;
		}

		if (request_irq(dma_p->irq_num, dma_S2MM_isr, 0, DEVICE_NAME, dma_p)) {
			printk(KERN_ERR "[title_probe] Could not register IRQ %d\n", dma_p->irq_num);
			return -EIO;
			goto error6;
		}
		else {
			printk(KERN_INFO "[title_probe] Registered S2MM IRQ %d\n", dma_p->irq_num);
		}
        */
		
		dma_init(dma_p->base_addr);
		
		// printk(KERN_NOTICE "[title_probe] TITLE platform driver registered - dma\n");
		device_fsm++;	
		return 0;

		error6:
			iounmap(dma_p->base_addr);
		error5:
			release_mem_region(dma_p->mem_start, dma_p->mem_end - dma_p->mem_start + 1);
			kfree(dma_p);
		error4:
			return rc;			
	break;
	
	
	case 1:
		
		r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if(!r_mem)
		{
			printk(KERN_ALERT "[title_probe] Failed to get reg resource.\n");
			return -ENODEV;
		}
		
		printk(KERN_ALERT "[title_probe] Probing title_p\n");
		
		title_p = (struct title_info *) kmalloc(sizeof(struct title_info), GFP_KERNEL);
		if(!title_p) 
		{
			printk(KERN_ALERT "[title_probe] Could not allocate TITLE device\n");
			return -ENOMEM;
		}

		title_p->mem_start = r_mem->start;
		title_p->mem_end = r_mem->end;

		if(!request_mem_region(title_p->mem_start, title_p->mem_end - title_p->mem_start + 1, DEVICE_NAME)) 
		{
			printk(KERN_ALERT "[title_probe] Could not lock memory region at %p\n",(void *)title_p->mem_start);
			rc = -EBUSY;
			goto error1;
		}

		title_p->base_addr = ioremap(title_p->mem_start, title_p->mem_end - title_p->mem_start + 1);
		if (!title_p->base_addr) 
		{
			printk(KERN_ALERT "[title_probe] Could not allocate memory\n");
			rc = -EIO;
			goto error2;
		}
		
		// printk(KERN_INFO "[title_probe] title-ip base address start at %x\n", title_p->base_addr);
			
		title_p->irq_num0 = platform_get_irq(pdev, 0);
        title_p->irq_num1 = platform_get_irq(pdev, 1);

		if(!title_p->irq_num0 || !title_p->irq_num1)
		{
			printk(KERN_ERR "[title_probe] Could not get IRQ resource\n");
			rc = -ENODEV;
			goto error2;
		}

		if (request_irq(title_p->irq_num0, title_command_isr, IRQF_TRIGGER_RISING, DEVICE_NAME, title_p)) {
			printk(KERN_ERR "[title_probe] Could not register IRQ0 %d\n", title_p->irq_num0);
			return -EIO;
			goto error3;
		}
		else {
            printk(KERN_INFO "[title_probe] Registered IRQ0 %d\n", title_p->irq_num0);
		}
        
        if (request_irq(title_p->irq_num1, title_frame_isr, IRQF_TRIGGER_RISING, DEVICE_NAME, title_p)) {
			printk(KERN_ERR "[title_probe] Could not register IRQ1 %d\n", title_p->irq_num1);
			return -EIO;
			goto error33;
		}
		else {
			printk(KERN_INFO "[title_probe] Registered IRQ1 %d\n", title_p->irq_num1);
		}
		
		enable_irq(title_p->irq_num0);
        enable_irq(title_p->irq_num1);

		iowrite32(IP_COMMAND_RESET, title_p->base_addr);
		// printk(KERN_INFO "[title_probe] TITLE IP reset\n");

		return 0;

		error33:
            free_irq(title_p->irq_num0, title_p);           
        error3:
			iounmap(title_p->base_addr);
		error2:
			release_mem_region(title_p->mem_start, title_p->mem_end - title_p->mem_start + 1);
			kfree(title_p);
		error1:
			return rc;			
	break;
	
	default:
		// printk(KERN_INFO "[title_probe] Device FSM in illegal state.\n");
		return -1;
	break;
    }
}

static int title_remove(struct platform_device *pdev) 
{
	switch (device_fsm)
	{
	case 0: 
		printk(KERN_ALERT "[title_remove] title_p device platform driver removed\n");
		// iowrite32(0, title_p->base_addr);
		free_irq(title_p->irq_num0, title_p);
        free_irq(title_p->irq_num1, title_p);

		// printk(KERN_INFO "[title_remove] IRQ numbers for title free\n");
		
        iounmap(title_p->base_addr);
		release_mem_region(title_p->mem_start, title_p->mem_end - title_p->mem_start + 1);
		kfree(title_p);
	break;

	case 1:
		printk(KERN_ALERT "[title_remove] dma_p platform driver removed\n");
		// iowrite32(0, dma_p->base_addr);
		//free_irq(dma_p->irq_num, NULL);
		// printk(KERN_INFO "[title_remove] IRQ numbers for dma free\n");
		iounmap(dma_p->base_addr);
		release_mem_region(dma_p->mem_start, dma_p->mem_end - dma_p->mem_start + 1);
		kfree(dma_p);
		--device_fsm;
	break;

	default:
		// printk(KERN_INFO "[title_remove] Device FSM in illegal state. \n");
		return -1;
	}
	
	// printk(KERN_INFO "[title_remove] Succesfully removed driver\n");
	return 0;
}


/* -------------------------------------- */
/* ------OPEN AND CLOSE FUNCTIONS-------- */
/* -------------------------------------- */

int title_open(struct inode *pinode, struct file *pfile)
{
//	printk(KERN_INFO "TITLE FILE OPENED\n");
	return 0;
}

int title_close(struct inode *pinode, struct file *pfile)
{
//	printk(KERN_INFO "TITLE FILE CLOSE\n");
	return 0;
}


/* -------------------------------------- */
/* -------READ AND WRITE FUNCTIONS------- */
/* -------------------------------------- */

int transaction_over = 0;
volatile int ip_command_over = 0;
volatile int ip_frame_over = 0;
int input_command;
int dimension;
int offset;

ssize_t title_read(struct file *pfile, char __user *buf, size_t length, loff_t *offset)
{   
    char buff[BUFF_SIZE];
    int ret = 0;
	int minor = MINOR(pfile->f_inode->i_rdev);
    long int len;

	switch(minor)
	{
	// Reading from TITLE 
	case 0:
	    len = scnprintf(buff, BUFF_SIZE, "%d\n", ip_frame_over);
        ret = copy_to_user(buf, buff, len);
        if(ret)
            return -EFAULT;

		printk(KERN_WARNING "[title_read] Reading from TITLE is allowed\n");
		return 0;
	break;
	
	// Reading from DMA 
	case 1:
		// NOTHING TO DO HERE
		printk(KERN_WARNING "[title_read] Reading from DMA not allowed. Data should be memory mapped using mmap and memcpy functions from inside app.\n");
	break;
	
	default:
		printk(KERN_WARNING "[title_read] Invalid read command\n");
	break;
	}
	
	return 0;
}

ssize_t title_write(struct file *pfile, const char __user *buf, size_t length, loff_t *offset)
{
	char buff[BUFF_SIZE]; 
	int ret = 0;
	int minor = MINOR(pfile->f_inode->i_rdev);
	ret = copy_from_user(buff, buf, length);  
	if(ret)
	{
		printk(KERN_WARNING "[title_write] Copy from user failed\n");
		return -EFAULT;
	}  
	buff[length] = '\0';
	
	switch(minor)
	{
		// Writing into CNN 
		case 0:
			// printk(KERN_INFO "[title_write] Writing into title-ip");
			sscanf(buff, "%d,%d,%d", &input_command, &dimension, &offset);  
			
            if(offset == 0)
            {
			    // Check if command is valid //
	
			    if(input_command != IP_COMMAND_LOAD_LETTER_DATA 	&&
			       input_command != IP_COMMAND_LOAD_LETTER_MATRIX 	&&
			       input_command != IP_COMMAND_LOAD_TEXT 	        &&
			       input_command != IP_COMMAND_LOAD_POSSITION		&&
			       input_command != IP_COMMAND_LOAD_PHOTO 		    &&
			       input_command != IP_COMMAND_PROCESSING 	        &&
			       input_command != IP_COMMAND_SEND_FROM_BRAM		&&
			       input_command != IP_COMMAND_RESET)
			    {
				    printk(KERN_WARNING "[title_write] Wrong TITLE command! %d\n", input_command);
				    return 0;
			    }
					

			    switch(input_command)
			    {
			    // Write command 
			    case IP_COMMAND_LOAD_LETTER_DATA:
				    dma_simple_write(tx_phy_buffer, LETTER_DATA_LEN, dma_p->base_addr);
				    // printk(KERN_INFO "[title_write] Starting DMA transaction: LOAD LETTER_DATA\n");
		    	break;
			
			    case IP_COMMAND_LOAD_LETTER_MATRIX:
                    if(dimension == 0)
				        dma_simple_write(tx_phy_buffer, D0_LETTER_MATRIX_LEN, dma_p->base_addr);
				    else if(dimension == 1)
				        dma_simple_write(tx_phy_buffer, D1_LETTER_MATRIX_LEN, dma_p->base_addr);
                    else if(dimension == 2)
				        dma_simple_write(tx_phy_buffer, D2_LETTER_MATRIX_LEN, dma_p->base_addr);
                    else if(dimension == 3)
				        dma_simple_write(tx_phy_buffer, D3_LETTER_MATRIX_LEN, dma_p->base_addr);
                    else if(dimension == 4)
				        dma_simple_write(tx_phy_buffer, D4_LETTER_MATRIX_LEN, dma_p->base_addr);
                    
                    // printk(KERN_INFO "[title_write] Starting DMA transaction: LOAD LETTER_MATRIX\n");
			    break;
			
			    case IP_COMMAND_LOAD_TEXT:
				    dma_simple_write(tx_phy_buffer, dimension*2, dma_p->base_addr);
				    // printk(KERN_INFO "[title_write] Starting DMA transaction: LOAD TEXT\n");
			    break;
			
			    case IP_COMMAND_LOAD_POSSITION:
				    dma_simple_write(tx_phy_buffer, POSSITION_LEN, dma_p->base_addr);
				    // printk(KERN_INFO "[title_write] Starting DMA transaction: LOAD POSSITION\n");
			    break;
			
			    case IP_COMMAND_LOAD_PHOTO:
                    if(dimension == 0)
				        dma_simple_write(tx_phy_buffer, D0_BRAM*D0_WIDTH*3*2, dma_p->base_addr);
				    else if(dimension == 1)
				        dma_simple_write(tx_phy_buffer, D1_BRAM*D1_WIdTH*3*2, dma_p->base_addr);
                    else if(dimension == 2)
				        dma_simple_write(tx_phy_buffer, D2_BRAM*D2_WIDTH*3*2, dma_p->base_addr);
                    else if(dimension == 3)
				        dma_simple_write(tx_phy_buffer, D3_BRAM*D3_WIDTH*3*2, dma_p->base_addr);
                    else if(dimension == 4)
				        dma_simple_write(tx_phy_buffer, D4_BRAM*D4_WIDTH*3*2, dma_p->base_addr);
				
				    // printk(KERN_INFO "[title_write] Starting DMA transaction: LOAD PHOTO\n");
			    break;
			

			    // Read command 
			    case IP_COMMAND_SEND_FROM_BRAM:
                    if(dimension == 0)
				        dma_simple_read(tx_phy_buffer, D0_BRAM*D0_WIDTH*3*2, dma_p->base_addr);
				    else if(dimension == 1)
				        dma_simple_read(tx_phy_buffer, D1_BRAM*D1_WIDTH*3*2, dma_p->base_addr);
                    else if(dimension == 2)
				        dma_simple_read(tx_phy_buffer, D2_BRAM*D2_WIDTH*3*2, dma_p->base_addr);
                    else if(dimension == 3)
				        dma_simple_read(tx_phy_buffer, D3_BRAM*D3_WIDTH*3*2, dma_p->base_addr);
                    else if(dimension == 4)
				        dma_simple_read(tx_phy_buffer, D4_BRAM*D4_WIDTH*3*2, dma_p->base_addr);
				    
				    // printk(KERN_INFO "[title_write] Starting DMA transaction: SEND FROM BRAM\n");
			    break;

			
			    default:
				    // NOT A LOAD OR READ COMMAND
			    break;
			    }

			    // Write into TITLE IP 
			    ip_command_over = 0;
                ip_frame_over = 0;
                iowrite32((u32)input_command, title_p->base_addr);
			    
                if(input_command != IP_COMMAND_RESET && input_command != IP_COMMAND_PROCESSING)
			    {
				    while(ip_command_over != 1);
			    }
                else if(input_command == IP_COMMAND_PROCESSING)
                {
                    while(ip_command_over != 1 && ip_frame_over != 1);
                }

		        // printk(KERN_INFO "[title_write] Writing finished!");
		        ip_command_over = 0;
		        transaction_over = 0;

            }
            else if(offset == 1)
            {
                iowrite32((u32)input_command, title_p->base_addr+AXI_OFFSET);
            }


		break;
		
		// Writing into DMA 
		case 1:
			// NOTHING TO DO HERE
		//	printk(KERN_WARNING "[title_write] Writing into DMA not allowed. Data should be memory mapped using mmap and memcpy functions from inside app.\n");
		break;
		
		default:
			printk(KERN_WARNING "[title_write] Invalid write command\n");
		break;
	}
	
	return length;
}

/* -------------------------------------- */
/* ------------MMAP FUNCTION------------- */
/* -------------------------------------- */

static int title_mmap(struct file *f, struct vm_area_struct *vma_s)
{
	int ret = 0;
	long length = vma_s->vm_end - vma_s->vm_start;

	// printk(KERN_INFO "[title_dma_mmap] DMA TX Buffer is being memory mapped\n");

	if(length > MAX_PKT_LEN)
	{
		return -EIO;
		printk(KERN_ERR "[title_dma_mmap] Trying to mmap more space than it's allocated\n");
	}

	ret = dma_mmap_coherent(my_device_dma, vma_s, tx_vir_buffer, tx_phy_buffer, length);
	if(ret < 0)
	{
		printk(KERN_ERR "[title_dma_mmap] Memory map failed\n");
		return ret;
	}
	return 0;
}

/* -------------------------------------- */
/* ------INTERRUPT SERVICE ROUTINES------ */
/* -------------------------------------- */

static irqreturn_t dma_MM2S_isr(int irq, void* dev_id)
{
	unsigned int IrqStatus;  
	
	IrqStatus = ioread32(dma_p->base_addr + MM2S_STATUS_REGISTER);
	iowrite32(IrqStatus | 0x00005000, dma_p->base_addr + MM2S_STATUS_REGISTER);
	
	// Tell rest of the code that interrupt has happened 
	transaction_over = 0;
	
	// printk(KERN_INFO "[dma_MM2S_isr] Finished DMA MM2S transaction!\n");

	return IRQ_HANDLED;
}

static irqreturn_t dma_S2MM_isr(int irq, void*dev_id)
{
	unsigned int IrqStatus;  
		
	IrqStatus = ioread32(dma_p->base_addr + S2MM_STATUS_REGISTER);
	iowrite32(IrqStatus | 0x00005000, dma_p->base_addr + S2MM_STATUS_REGISTER);
	
	// Tell rest of the code that interrupt has happened 
	transaction_over = 0;
	
	// printk(KERN_INFO "[dma_S2MM_isr] Finished DMA S2MM transaction!\n");

	return IRQ_HANDLED;
}

static irqreturn_t title_command_isr(int irq, void*dev_id)
{
	ip_command_over = 1;
	//printk(KERN_INFO "[title_command_isr] IP finished operation %x\n", input_command);
	return IRQ_HANDLED;
}

static irqreturn_t title_frame_isr(int irq, void*dev_id)
{
	ip_frame_over = 1;
	//printk(KERN_INFO "[title_frame_isr] IP finished operation %x\n", input_command);
	return IRQ_HANDLED;
}

/* -------------------------------------- */
/* ------------DMA FUNCTIONS------------- */
/* -------------------------------------- */

int dma_init(void __iomem *base_address)
{
	/* 
	 * In order for DMA to work proprely, it's internal control registers should be configurated first 
	 * There is a series of steps needed to be complited before every DMA transcation
	 * This one is the initial step that does the following inside a memory-mapped MM2S_DMACR and S2MM_DMACR register:
	 *  - Reset DMA by setting bit 3
	 *  - Allow interrupts by setting bits 12 and 14 (these interrupts will signal the CPU when the transaction is complited or an error has accured)
	*/
	
	u32 MM2S_DMACR_reg = 0;
	u32 S2MM_DMACR_reg;
	u32 en_interrupt = 0;
	u32 temp = 0;
	
	// For debug purpose first we read status register
	temp = ioread32(base_address + 4);
	// printk(KERN_INFO "Initial state of STATUS reg is %u\n", temp);	

	// Writing to MM2S_DMACR register. Setting reset bit (3rd bit) 
	iowrite32(DMACR_RESET, base_address + MM2S_CONTROL_REGISTER);

	// printk(KERN_INFO "[dma_init] Writing %d into %x", DMACR_RESET, base_address+MM2S_CONTROL_REGISTER);
	temp = ioread32(base_address + 0);
	// printk(KERN_INFO "[debug - ioread] After reseting control reg is %u\ [should be 65538 probably]\n", temp);

	// Reading from MM2S_DMACR register inside DMA 
	MM2S_DMACR_reg = ioread32(base_address + MM2S_CONTROL_REGISTER); 
	// printk(KERN_INFO "[debug - ioread] Reading control reg is %u [probably should be still 65538]\n", MM2S_DMACR_reg);
	

	// Setting 13th and 15th bit in MM2S_DMACR to enable interrupts 
	en_interrupt = MM2S_DMACR_reg | IOC_IRQ_FLAG | ERR_IRQ_EN;
	// printk(KERN_INFO "[debug] int flag is %u\n", en_interrupt);

	iowrite32(en_interrupt, base_address + MM2S_CONTROL_REGISTER);
	// printk(KERN_INFO "[dma_init] To enable interrupt and error check, writing %d into %x", en_interrupt, base_address+MM2S_CONTROL_REGISTER);
	temp = ioread32(base_address + 0);
	// printk(KERN_INFO "[debug - iowrite/ioread] After enabling interrupt and error check, control reg is %u [should be 86018]\n", temp);
	
	// Same steps should be taken for S2MM_DMACR register 

	// Writing to S2MM_DMACR register. Setting reset bit (3rd bit) 
	iowrite32(DMACR_RESET, base_address + S2MM_CONTROL_REGISTER);

	// Reading from S2MM_DMACR register inside DMA 
	S2MM_DMACR_reg = ioread32(base_address + S2MM_CONTROL_REGISTER); 
	
	// Setting 13th and 15th bit in S2MM_DMACR to enable interrupts 
	en_interrupt = S2MM_DMACR_reg | IOC_IRQ_FLAG | ERR_IRQ_EN;
	iowrite32(en_interrupt, base_address + S2MM_CONTROL_REGISTER);

	// printk(KERN_INFO "[dma_init] DMA init done\n");
	return 0;
}

unsigned int dma_simple_write(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) 
{
	u32 MM2S_DMACR_reg = 0;
	u32 temp = 0;
	u32 en_interrupt;	
	
	MM2S_DMACR_reg = ioread32(base_address + MM2S_CONTROL_REGISTER); 
	
	en_interrupt = MM2S_DMACR_reg | IOC_IRQ_FLAG | ERR_IRQ_EN;
	// printk(KERN_INFO "[debug] int flag is %u\n", en_interrupt);
	
    iowrite32(en_interrupt, base_address + MM2S_CONTROL_REGISTER);
	// printk(KERN_INFO "[dma_init] To enable interrupt and error check, writing %d into %x", en_interrupt, base_address+MM2S_CONTROL_REGISTER);


	// READ from MM2S_DMACR register 
	MM2S_DMACR_reg = ioread32(base_address + MM2S_CONTROL_REGISTER);	

	// printk(KERN_INFO "[debug - ioread] Initial control register before any changes inside dma_simple_write: %u [should be 86018]\n", MM2S_DMACR_reg);


	temp = ioread32(base_address + 4);
	// printk(KERN_INFO "[debug] Before starting DMA, STATUS reg LSB bit is %u [should be 1 - halted]\n", temp & 0x1);


	// Set RS bit in MM2S_DMACR register (this bit starts the DMA) 
	iowrite32(0x1 |  MM2S_DMACR_reg, base_address + MM2S_CONTROL_REGISTER);
	
	// printk(KERN_INFO "[dma_simple_write] Writing %d at address %x\n", 0x1 | MM2S_DMACR_reg, base_address + MM2S_CONTROL_REGISTER);
	temp = ioread32(base_address + 0);
	// printk(KERN_INFO "[debug - iowrite/ioread] After starting RS bit, control register is %u [should be 68019]\n", temp);


	temp = ioread32(base_address + 4);
	// printk(KERN_INFO "[debug] After starting DMA, STATUS reg LSB bit is %u [should be 0 - running]\n", temp & 0x1);

    
	/* Write into MM2S_SA register the value of TxBufferPtr. 
	 * With this, the DMA knows from where to start - this is the first address of data that needs to be transfered. 
	*/
    
	iowrite32((u32)TxBufferPtr, base_address + MM2S_CONTROL_REGISTER + MM2S_SRC_ADDRESS_REGISTER);

	// printk(KERN_INFO "[dma_simple_write] Writing starting buffer address %x at address %x\n", (int)TxBufferPtr, base_address + MM2S_CONTROL_REGISTER + MM2S_SRC_ADDRESS_REGISTER);
	temp = ioread32(base_address + 0x18);
	// printk(KERN_INFO "[debug - iowrite/ioread] After writing starting address: %u [should be value from previous message]\n", temp);
	


	// Write into MM2S_LENGTH register. This is the length of a tranaction. 
	iowrite32(pkt_len, base_address + MM2S_CONTROL_REGISTER + MM2S_TRNSFR_LENGTH_REGISTER);
	// printk(KERN_INFO "[dma_simple_write] Writing length of transaction %d at address %x\n", pkt_len, base_address + MM2S_CONTROL_REGISTER + MM2S_TRNSFR_LENGTH_REGISTER);
	temp = ioread32(base_address + 0x28);
	// printk(KERN_INFO "[debug - iowrite/ioread] After writing length: %u [should be 128 for bias]\n", temp);
	return 0;
}


unsigned int dma_simple_read(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) 
{
	u32 S2MM_DMACR_reg;

	// READ from S2MM_DMACR register 
	S2MM_DMACR_reg = ioread32(base_address + S2MM_CONTROL_REGISTER);

	// Set RS bit in S2MM_DMACR register (this bit starts the DMA) 
	iowrite32(0x1 |  S2MM_DMACR_reg, base_address + S2MM_CONTROL_REGISTER);
    
	/* Write into S2MM_SA register the value of TxBufferPtr. 
	 * With this, the DMA knows from where to start writing into - this is the first address of data that needs to be transfered. 
	*/
    
	iowrite32((u32)TxBufferPtr, base_address + S2MM_DST_ADDRESS_REGISTER); 

	/* NOTE: no need for: base_address + S2MM_DST_ADDRESS_REGISTER + S2MM_CONTROL_REGISTER since 
	 * S2MM_CONTROL_REGISTER address is alreay accounted for inside S2MM_DST_ADDRESS_REGISTER
	*/

	// Write into S2MM_LENGTH register. This is the length of a tranaction.
    
	iowrite32(pkt_len, base_address + S2MM_BUFF_LENGTH_REGISTER);
	return 0;
}
