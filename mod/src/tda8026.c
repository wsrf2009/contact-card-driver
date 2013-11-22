
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <plat/clock.h>

#include "pwm.c"

#define TDA8026_DEVICE_NAME		"tda8026"
#define TDA8026_RESET_GPIO		13//161
#define TDA8026_IRQ_GPIO		157//156

#define BANK0_ADDR			0x48
#define BANK1_REG0			0x40
#define BANK1_REG1			0x42

#define SLOT0				0
#define SLOT1				1
#define SLOT2				2
#define SLOT3				3
#define SLOT4				4
#define SLOT5				5
#define SLOT6				6
#define REG0				(0<<4)
#define REG1				(1<<4)
#define REG2				(2<<4)
#define REG3				(3<<4)
#define REG_MASK			(3<<4)

#define	ACTIVE				(1<<7)
#define	EARLY				(1<<6)
#define	MUTE				(1<<5)
#define	PROT				(1<<4)
#define	SUPL				(1<<3)
#define CLKSW				(1<<2)
#define	PRESL				(1<<1)
#define PRES				(1<<0)

//register0
#define VCC1V8          (1<<7)
#define IOEN            (1<<6)
#define REG1_0          (0<<4)
#define REG1_1          (1<<4)
#define REG1_2          (2<<4)
#define REG1_3          (3<<4)
#define PDWN            (1<<3)
#define VCC5V           (1<<2)
#define VCC3V           (0<<2)
#define WARM            (1<<1)
#define START           (1<<0)

//register1
#define RSTIN           (1<<6)
#define CLKDIV_0        (0<<0)
#define CLKDIV_2        (1<<0)
#define CLKDIV_4        (2<<0)
#define CLKDIV_5        (3<<0)

// interrupt register
#define	irq_slot5		(1<<4)
#define	irq_slot4		(1<<3)
#define	irq_slot3		(1<<2)
#define	irq_slot2		(1<<1)
#define	irq_slot1		(1<<0)



static u8 			slot1_reg0 = 0;
static u8 			slot2_reg0 = 0;
static u8 			slot3_reg0 = 0;
static u8 			slot4_reg0 = 0;
static u8 			slot5_reg0 = 0;


struct tda8026_common
{
    struct i2c_client	*i2c_client;
	struct i2c_driver	*i2c_driver;

	struct i2c_msg 		msg;
    
    struct work_struct wk_tda8026;
    struct workqueue_struct *wq_tda8026;

	struct clk *sys_clkout2;
	struct clk *sys_clkout2_src;
	struct clk *func96m_clk;


    
    int reset_pin;
	int irq_pin;

};



static struct tda8026_common *tda8026;



int tda8026_write(u8 reg, u8 value) 
{
	struct i2c_msg *msg;
	int ret = 0;


//    TRACE_TO("enter %s with Cmd = %d\n", __func__, cmd);

	if(!tda8026->i2c_client)
	{
		ERROR_TO("have not connect to i2c client\n");
		ret = -EFAULT;
		goto err;
	}

	msg = &tda8026->msg;
	msg->addr= reg>>1;	
	msg->len = 1;	
	msg->flags = 0;
	msg->buf = &value;

	ret = i2c_transfer(tda8026->i2c_client->adapter, &tda8026->msg, 1);
	if(ret != 1)
		ret = -ICC_ERRORCODE_HW_ERROR;
	else
		ret = 0;

err:
	
//    TRACE_TO("exit %s\n", __func__);
	
    return	ret;
}

int tda8026_read(u8 reg, u8 *value)
{
	struct i2c_msg *msg;
	int ret = 0;

	
//	TRACE_TO("enter %s with Cmd = %d\n", __func__, Cmd);

	if(!tda8026->i2c_client)
	{
		ERROR_TO("have not connect to i2c client\n");
		ret = -EFAULT;
		goto err;
	}

	msg = &tda8026->msg;
	msg->addr= reg>>1;	
	msg->len = 1;	
	msg->flags = 1;
	msg->buf = value;

	ret = i2c_transfer(tda8026->i2c_client->adapter, &tda8026->msg, 1);
	if(ret != 1)
		ret = -ICC_ERRORCODE_HW_ERROR;
	else
		ret = 0;

err:

//    TRACE_TO("exit %s\n", __func__);
	
    return	ret;
}

int tda8026_card_cold_reset(u8 slot, u8 cvcc)
{
	u8 temp;
	int ret;
	

	tda8026_write(BANK0_ADDR, slot);
//	tda8026_write(BANK1_REG0, 0x00);
	tda8026_write(BANK1_REG1, 0x7c);
	tda8026_write(BANK1_REG0, 0x00);
	
	tda8026_read(BANK1_REG0, &temp);
	TRACE_TO("%s [%02X]=%02X temp=%02X\n", __func__, slot, cvcc, temp);
//	if(temp&PRES)
//	{
		switch(cvcc)
		{
			case V_CLASS_C:
				ret = tda8026_write(BANK1_REG0, VCC1V8|IOEN|START);
				break;
			case V_CLASS_B:
				ret = tda8026_write(BANK1_REG0, VCC3V|IOEN|START);
				break;
			case V_CLASS_A:
				ret = tda8026_write(BANK1_REG0, VCC5V|IOEN|START);
				break;
			default:
				ret = -ICC_ERRORCODE_POWERSELECT_NO_SUPPORTED;
				break;
		}
//		tda8026_write(BANK1_REG0, START);

//	}
//	else
//		ret = -ICC_ERRORCODE_ICC_MUTE;

	mdelay(8);
	
	return ret;
}


int tda8026_card_warm_reset(u8 slot, u8 cvcc)
{
	int ret = 0;
	u8	temp;
	
	tda8026_write(BANK0_ADDR, slot);
	tda8026_write(BANK1_REG1, 0x7c);
	
	tda8026_read(BANK1_REG0, &temp);
	if(temp & PRES)
	{
		switch(cvcc)
		{
			case V_CLASS_C:
				ret = tda8026_write(BANK1_REG0, VCC1V8|IOEN|WARM|START); 
				break;
			case V_CLASS_B:
				ret = tda8026_write(BANK1_REG0, VCC3V|IOEN|WARM|START); 
				break;
			case V_CLASS_A:
				ret = tda8026_write(BANK1_REG0, VCC5V|IOEN|WARM|START); 
				break;
			default:
				ret = -ICC_ERRORCODE_POWERSELECT_NO_SUPPORTED;
				break;
		}
	}
	else
		ret = -ICC_ERRORCODE_ICC_MUTE;

	mdelay(8);
	
	return ret;
}

int tda8026_card_power_off(u8 slot)
{
	tda8026_write(BANK0_ADDR, slot);
	tda8026_write(BANK1_REG1, 0x7c);

	tda8026_write(BANK1_REG0, 0x00); 

	return 0;
}

int tda8026_check_card_status(u8 slot)
{
	unsigned char tmp;

	tda8026_write(BANK0_ADDR, slot);
	tda8026_read(BANK1_REG0, &tmp);

	return (tmp&0x01);
}

static void tda8026_wq_func(struct work_struct *work)
{
	u8	temp;
	u8	irq_status;


	// slot0
	tda8026_write(BANK0_ADDR, SLOT0);
	tda8026_read(BANK1_REG1, &irq_status);
	TRACE_TO("interrupt status: %02X\n", irq_status);

	// slot1
	if(irq_status & irq_slot1)
	{
		tda8026_write(BANK0_ADDR, SLOT1);
		tda8026_read(BANK1_REG0, &temp);
		TRACE_TO("slot1 status: %02X\n", temp);
		if(temp&PRESL)
		{
			if(temp&PRES)	// a card is detected
			{
				icc_status_changed(1);
				INFO_TO("card insert\n");
			}
			else
			{
				icc_status_changed(0);
				INFO_TO("card removed\n");
			}
		}
		
//		if(temp&EARLY)
//			uart_clear_fifo();
	}

	// slot2
	if(irq_status & irq_slot2)
	{
		tda8026_write(BANK0_ADDR, SLOT2);
		tda8026_read(BANK1_REG0, &temp);
		TRACE_TO("slot2 status: %02X\n", temp);
	}

	// slot3
	if(irq_status & irq_slot3)
	{
		tda8026_write(BANK0_ADDR, SLOT3);
		tda8026_read(BANK1_REG0, &temp);
		TRACE_TO("slot3 status: %02X\n", temp);
	}

	// slot4
	if(irq_status & irq_slot4)
	{
		tda8026_write(BANK0_ADDR, SLOT4);
		tda8026_read(BANK1_REG0, &temp);
		TRACE_TO("slot4 status: %02X\n", temp);
	}

	// slot5
	if(irq_status & irq_slot5)
	{
		tda8026_write(BANK0_ADDR, SLOT5);
		tda8026_read(BANK1_REG0, &temp);
		TRACE_TO("slot5 status: %02X\n", temp);
	}
	
	// slot0
//	tda8026_write(BANK0_ADDR, SLOT0);
//	tda8026_read(BANK1_REG1, &temp);
//	TRACE_TO("interrupt status: %02X\n", temp);

}

static irqreturn_t tda8026_irq(int irq, void *dev_id)
{
    struct tda8026_common *tda8026 = dev_id;


//	TRACE_TO("enter %s\n", __func__);
	
    schedule_work(&tda8026->wk_tda8026);

//	TRACE_TO("exit %s\n", __func__);
    
    return(IRQ_HANDLED);
}

static int tda8026_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    struct tda8026_common *tda8026 = (struct tda8026_common *)(id->driver_data);
 	u8 temp;
	struct device *dev;
	
    
    TRACE_TO("enter %s\n", __func__);
    
    tda8026->i2c_client = client;

	dev = &client->dev;
#if 0
	tda8026->sys_clkout2_src = clk_get(dev, "clkout2_src_ck");
	if (IS_ERR(tda8026->sys_clkout2_src)) {
		dev_err(dev, "Could not get clkout2_src_ck clock\n");
		ret = PTR_ERR(tda8026->sys_clkout2_src);
		goto err;
	}

	tda8026->sys_clkout2 = clk_get(dev, "sys_clkout2");
	if (IS_ERR(tda8026->sys_clkout2)) {
		dev_err(dev, "Could not get sys_clkout2\n");
		ret = PTR_ERR(tda8026->sys_clkout2);
		goto err1;
	}

	tda8026->func96m_clk = clk_get(dev, "cm_96m_fck");
	if (IS_ERR(tda8026->func96m_clk)) {
		dev_err(dev, "Could not get func cm_96m_fck clock\n");
		ret = PTR_ERR(tda8026->func96m_clk);
		goto err2;
	}

	clk_set_parent(tda8026->sys_clkout2_src, tda8026->func96m_clk);
	clk_set_rate(tda8026->sys_clkout2, tda8026->func96m_clk->rate);
	clk_enable(tda8026->sys_clkout2);
#else
	if(Init_PWM())
	{
		printk("Init_PWM Fail!!!\n");
		return -1;
	}
#endif
	ret = gpio_request(tda8026->reset_pin, "tda8026_reset_pin");
	if(ret)
	{
		ERROR_TO("fail to requesting gpio%d\n", tda8026->reset_pin);
		goto err3;
	}

	// tda8026 hardware reset
	gpio_direction_output(tda8026->reset_pin, 1);
	udelay(400);
	gpio_set_value(tda8026->reset_pin, 0);
	udelay(200);
	gpio_set_value(tda8026->reset_pin, 1);
	udelay(400);

	// slot0	
	tda8026_write(BANK0_ADDR, SLOT0);
	tda8026_read(BANK1_REG0, &temp);
	INFO_TO("tda8026 hardware version: %02X\n", temp);

	// slot1
	tda8026_write(BANK0_ADDR, SLOT1);
	tda8026_write(BANK1_REG0, slot1_reg0);
	tda8026_write(BANK1_REG1, 0);

	// slot2
	tda8026_write(BANK0_ADDR, SLOT2);
	tda8026_write(BANK1_REG0, slot2_reg0);
	tda8026_write(BANK1_REG1, 0);

	// slot3
	tda8026_write(BANK0_ADDR, SLOT3);
	tda8026_write(BANK1_REG0, slot3_reg0);
	tda8026_write(BANK1_REG1, 0);

	// slot4
	tda8026_write(BANK0_ADDR, SLOT4);
	tda8026_write(BANK1_REG0, slot4_reg0);
	tda8026_write(BANK1_REG1, 0);

	// slot5
	tda8026_write(BANK0_ADDR, SLOT5);
	tda8026_write(BANK1_REG0, slot5_reg0);
	tda8026_write(BANK1_REG1, 0);

	// slot0
//	tda8026_write(BANK0_ADDR, SLOT0);
//	tda8026_read(BANK1_REG1, &temp);
//	TRACE_TO("interrupt status: %02X\n", temp);

	    
    INIT_WORK(&tda8026->wk_tda8026, tda8026_wq_func);
	tda8026_wq_func(NULL);
	
#if 1
    ret = request_irq(client->irq, tda8026_irq, IRQF_TRIGGER_FALLING ,
						"tda8026_interrupt", tda8026);
    if(ret)
    {
       ERROR_TO("fail to requesting irq for tda8026\n");
       goto err6;
    }
#endif

	// slot0
	tda8026_write(BANK0_ADDR, SLOT0);
	tda8026_read(BANK1_REG1, &temp);
	TRACE_TO("interrupt status: %02X\n", temp);


	TRACE_TO("exit %s\n", __func__);
	
	return 0;

err6:
	destroy_work_on_stack(&tda8026->wk_tda8026);
err5:
//	gpio_free(tda8026->irq_pin);
//err4:
	gpio_free(tda8026->reset_pin);
err3:
	clk_disable(tda8026->sys_clkout2);
	clk_put(tda8026->func96m_clk);
err2:
	clk_put(tda8026->sys_clkout2);
err1:
	clk_put(tda8026->sys_clkout2_src);
err:
	tda8026->i2c_client = NULL;
	
	TRACE_TO("exit %s\n", __func__);
	
    return (ret);
}

static int tda8026_remove(struct i2c_client *client)
{
//    int ret;


    TRACE_TO("enter %s\n", __func__);

    disable_irq_nosync(client->irq);
    free_irq(client->irq, tda8026);

	destroy_work_on_stack(&tda8026->wk_tda8026);

	gpio_free(tda8026->reset_pin);


	clk_disable(tda8026->sys_clkout2);
	clk_put(tda8026->func96m_clk);

	clk_put(tda8026->sys_clkout2);

	clk_put(tda8026->sys_clkout2_src);

	tda8026->i2c_client = NULL;

	TRACE_TO("exit %s\n", __func__);
	
    return(0);
}

static struct i2c_device_id tda8026_id[] = {  
    {TDA8026_DEVICE_NAME, 0 },
    {}  
};

static struct i2c_driver tda8026_driver =
{
    .probe = tda8026_probe,
    .remove= tda8026_remove,
    .driver=
    {
        .owner = THIS_MODULE,
        .name  = TDA8026_DEVICE_NAME,
    },
    
//    .id_table = tda8026_id, 
};



static int tda8026_init(void)
{
    int ret;

    
    TRACE_TO("enter %s\n", __func__);

	tda8026 = kzalloc(sizeof *tda8026, GFP_KERNEL);
	if (!tda8026)
	{
		ret = -ENOMEM;
		goto err1;
	}

	tda8026->reset_pin = TDA8026_RESET_GPIO;
	tda8026->irq_pin = TDA8026_IRQ_GPIO;

	tda8026_id[0].driver_data = (kernel_ulong_t)tda8026;
	tda8026->i2c_driver = &tda8026_driver;
	tda8026->i2c_driver->id_table = tda8026_id;

    // config AT83C26 address at i2c bus    
    if((ret = i2c_add_driver(tda8026->i2c_driver)) < 0)
    {
        ERROR_TO("adding i2c device fail\n");
        goto err2;
    }

	TRACE_TO("exit %s\n", __func__);

    return (0);

err2:
    kfree(tda8026);
err1:
	
	TRACE_TO("exit %s\n", __func__);

    return(ret);
}

static int tda8026_uninit(void)
{
    i2c_del_driver(tda8026->i2c_driver);
    
	kfree(tda8026);

    return(0);
}

