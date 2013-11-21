


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h> 
#include <linux/err.h>     //IS_ERR()
#include <linux/device.h>
#include <linux/cdev.h>



#include "icc.h"
#include "debug.h"



#define	IFD_MAX_SLOT_NUMBER		3	

enum	ifd_command
{
	ICC_POWER_ON = 1,
	ICC_POWER_OFF,
	ICC_XFR_APDU,
};


struct ifd_param
{
    u8 *p_iBuf;
    u8 *p_oBuf;
    u32  iDataLen;
    u32  oDataLen;
};



struct ifd_common
{
	struct icc_info		icc[IFD_MAX_SLOT_NUMBER];

	struct semaphore	mutex;
	
	struct timer_list waiting_timer;
	
	u8		sem_inc;

	u8		previous_slot;
	bool	time_out;
	u32		freq;
};

struct ifd_common	*common;



#include	"icc.c"


static long ifd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) 
{
	struct	ifd_common	*common = filp->private_data;
	struct	icc_info	*icc;
    u8	slot = cmd & 0x0F;
    u8	ifd_cmd = (cmd >> 4) & 0x0F;
    struct ifd_param kernel_param;
    struct ifd_param *user_param = (struct ifd_param *)arg;
    u8	*p_iData = NULL;
    u8	*p_oData = NULL;
    long ret = 0;


//    TRACE_TO("enter %s, slot = %d\n", __func__, slot);
	
    if(down_interruptible(&common->mutex))    // acquire the semaphore
    {
    	ERROR_TO("ifd is busy\n");
        ret = -ERESTARTSYS;
        goto	done;
    }

    if((!user_param) || (copy_from_user(&kernel_param, user_param, sizeof(kernel_param))))
    {
    	ERROR_TO("bad address\n");
        ret = -EFAULT;          // bad address
		goto	err1;
    }

	if(slot > IFD_MAX_SLOT_NUMBER)
	{
		ERROR_TO("bad slot\n");
		ret = -EINVAL;
		goto	err1;
	}

	icc = &common->icc[slot];
	
    if(common->previous_slot >= IFD_MAX_SLOT_NUMBER)
    {
        ret = at83c26_CIOx(icc->slot, CIO_CON, CIO_LOW);    // connect IO1 to CardIdx
        if(ret)
			goto	err1;
		
        common->previous_slot = slot;
    }
    else if(slot != common->previous_slot)
    {
		
        ret = at83c26_CIOx(common->previous_slot, CIO_DISCON, CIO_HIGH);    // save previous card status
        if(ret)
			goto	err1;

        ret = at83c26_CIOx(slot, CIO_CON, CIO_LOW);    // connect IO1 to CardIdx
        if(ret)
			goto	err1;
		
        common->previous_slot = slot;
    }

    switch(ifd_cmd)
    {
        case ICC_POWER_ON:
        {
            if(!kernel_param.p_oBuf)
            {
                ret = -EFAULT;       // bad address
                goto	err1;
            }
            p_oData = kzalloc(kernel_param.oDataLen, GFP_KERNEL);
            if(!p_oData)
            {
                ret = -EFAULT;       // bad address
                goto	err1;                
            }

			ret = icc_power_on(icc, p_oData, &kernel_param.oDataLen);
            if(ret)
            {
                goto	err2;
            }
            if(copy_to_user(kernel_param.p_oBuf, p_oData, kernel_param.oDataLen))
            {
                ret = -EFAULT;       // bad address
                goto	err2;
            }
            if(copy_to_user(&user_param->oDataLen, &kernel_param.oDataLen, sizeof(kernel_param.oDataLen)))
            {
                ret = -EFAULT;       // bad address
                goto	err2;
            }
            break; 
        }

        case ICC_POWER_OFF:
        {
			ret = icc_power_off(icc);
            if(ret)
            {
                goto	err2;
            }
            break;
        }

        case ICC_XFR_APDU:
        {
            if((kernel_param.iDataLen <= 0) || (kernel_param.oDataLen <= 0) || (!kernel_param.p_iBuf) || (!kernel_param.p_oBuf))
            {
                ret = -EFAULT;       // bad address
                goto	err1;
            }

			p_oData = kzalloc(kernel_param.oDataLen, GFP_KERNEL);
			if(!p_oData)
			{
				ret = -EFAULT;       // bad address
                goto	err1;
			}
			
            p_iData = kzalloc(kernel_param.iDataLen, GFP_KERNEL);
            if(!p_iData)
            {
				ret = -EFAULT;       // bad address
                goto	err2;
			}
            if(copy_from_user(p_iData, kernel_param.p_iBuf, kernel_param.iDataLen))
            {
                ret = -EFAULT;       // bad address
                goto	err3;
            }

			ret = icc_xfr_apdu(icc, p_iData, kernel_param.iDataLen, p_oData, &kernel_param.oDataLen);
            if(ret)
            {
                goto	err3;
            }
            if((kernel_param.oDataLen <= 0) || (copy_to_user(kernel_param.p_oBuf, p_oData, (unsigned long)kernel_param.oDataLen)))
            {
                ret = -EFAULT;       // bad address
                goto	err3;
            }
			
            if(copy_to_user(&user_param->oDataLen, &kernel_param.oDataLen, sizeof(kernel_param.oDataLen)))
            {
                ret = -EFAULT;       // bad address
                goto	err3;
            }

			kfree(p_iData);
			kfree(p_oData);

            break;
        }

        default:
            break;
    }

	up(&common->mutex);
	
	goto	done;

err3:
	kfree(p_iData);
err2:
	kfree(p_oData);
err1:
    up(&common->mutex);                    // release the semaphore
done:
	
//	TRACE_TO("exit %s, ret = %d\n", __func__, ret);
	
    return(ret);
}

int ifd_open(struct inode *inode,struct file *filp)
{
	int	ret;


    TRACE_TO("enter %s\n", __func__);
	
    if(common->sem_inc > 0)
	{
		ERROR_TO("ifd has been open\n");
		ret = -ERESTARTSYS;
		goto	done;
    }
    common->sem_inc++;

    filp->private_data = common;

    ret = 0;

done:

	TRACE_TO("exit %s\n", __func__);

	return	ret;
}
int ifd_release(struct inode *inode,struct file *filp)
{
	struct ifd_common	*common = filp->private_data;

	
    common->sem_inc--;
	
    return(0);
}

static struct file_operations ifd_fops=
{
    .owner = THIS_MODULE,
    .open = ifd_open,
    .unlocked_ioctl = ifd_ioctl,
    .release = ifd_release
};

static struct miscdevice ifd_misc=
{
    .minor = 220,
    .name = "CONTACT_CARD",
    .fops = &ifd_fops
};


static int ifd_init(void)
{
	int ret;

	
    TRACE_TO("enter %s\n", __func__);

	common = kzalloc(sizeof *common, GFP_KERNEL);
	if(!common)
	{
		ERROR_TO("fail to request memory\n");
		ret = -EFAULT;
		goto done;
	}
	
    sema_init(&common->mutex, 0);    // initial a semaphore, and lock it

	ret = icc_init(common);
    if(ret)
		goto err1;

	common->previous_slot = 0xFF;
	common->freq = 4000000;
	common->sem_inc = 0;

	ret = misc_register(&ifd_misc);
    if(ret)
		goto err2;

    up(&common->mutex);                  // release the semaphore
    
    TRACE_TO("initial ifd module sucessfully\n");
	
	goto done;

err2:
	icc_uninit(common);
err1:
	up(&common->mutex);
	kfree(common);
done:

	TRACE_TO("exit %s\n", __func__);

	return ret;
}

static void ifd_exit(void)
{
	TRACE_TO("enter %s\n", __func__);
	
    if(down_interruptible(&common->mutex))
    {
		
	}
    misc_deregister(&ifd_misc);
    icc_uninit(common);
    up(&common->mutex);
	kfree(common);
	
	TRACE_TO("exit %s\n", __func__);
}

module_init(ifd_init);
module_exit(ifd_exit);

MODULE_DESCRIPTION("Contact Card Driver");
MODULE_AUTHOR("Alex Wang");
MODULE_LICENSE("GPL");
