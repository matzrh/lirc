/*      $Id: lirc_i2c.c,v 1.1 2000/09/30 19:32:40 columbus Exp $      */

/*
 * i2c IR lirc plugin for Hauppauge and Pixelview cards - new 2.3.x i2c stack
 * (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 * modified for PixelView (BT878P+W/FM) by
 *      Michal Kochanowicz <mkochano@pld.org.pl>
 *      Christoph Bartelmus <lirc@bartelmus.de>
 *
 * parts are cut&pasted from the old lirc_haup.c driver
 *
 */

#include <linux/version.h>
#if LINUX_VERSION_CODE < 0x020200
#error "--- Sorry, this driver needs kernel version 2.2.0 or higher. ---"
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/i2c.h>
#include <asm/semaphore.h>

#include "../lirc_dev/lirc_dev.h"

/* Addresses to scan */
static unsigned short normal_i2c[] = {I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {0x18,0x1a,I2C_CLIENT_END};
static unsigned short probe[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short probe_range[2]  = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore[2]       = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore_range[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short force[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static struct i2c_client_address_data addr_data = {
	normal_i2c, normal_i2c_range, 
	probe, probe_range, 
	ignore, ignore_range, 
	force
};

struct IR {
	struct lirc_plugin l;
	struct i2c_client  c;
	int nextkey;
};

/* ----------------------------------------------------------------------- */
/* insmod parameters                                                       */

static int debug   = 0;    /* debug output */
static int minor   = -1;   /* minor number */

MODULE_PARM(debug,"i");
MODULE_PARM(minor,"i");

#define dprintk if (debug) printk

/* ----------------------------------------------------------------------- */

#define DEVICE_NAME "lirc_i2c"

/*
 * If this key changes, a new key was pressed.
 */
#define REPEAT_TOGGLE_0      192
#define REPEAT_TOGGLE_1      224

/* ----------------------------------------------------------------------- */

static int get_key_haup(void* data, unsigned char* key, int key_no)
{
	struct IR *ir = data;
        unsigned char b[3];
	__u16 repeat_bit, code;

	if (ir->nextkey != -1) {
		/* pass second byte */
		*key = ir->nextkey;
		ir->nextkey = -1;
		return 0;
	}

	/* poll IR chip */
	if (3 != i2c_master_recv(&ir->c,b,3)) {
		dprintk(KERN_DEBUG DEVICE_NAME ": read error\n");
		return -1;
	}

	/* key pressed ? */
	if (b[0] != REPEAT_TOGGLE_0 && b[0] != REPEAT_TOGGLE_1)
		return -1;
		
	/* look what we have */
	dprintk(KERN_DEBUG DEVICE_NAME ": key (0x%02x/0x%02x)\n", b[0], b[1]);
	repeat_bit=(b[0]&0x20) ? 0x800:0;
	code = (0x1000 | repeat_bit | (b[1]>>2));

	/* return it */
	*key        = (code >> 8) & 0xff;
	ir->nextkey =  code       & 0xff;
	return 0;
}

static int get_key_pixelview(void* data, unsigned char* key, int key_no)
{
	struct IR *ir = data;
        unsigned char b;
	
	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		dprintk(KERN_DEBUG DEVICE_NAME ": read error\n");
		return -1;
	}
	dprintk(KERN_DEBUG DEVICE_NAME ": key %02x\n", b);
	*key = b;
	return 0;
}

static void set_use_inc(void* data)
{
	struct IR *ir = data;

	/* lock bttv in memory while /dev/lirc is in use  */
	if (ir->c.adapter->inc_use) 
		ir->c.adapter->inc_use(ir->c.adapter);

	MOD_INC_USE_COUNT;
}

static void set_use_dec(void* data)
{
	struct IR *ir = data;

	if (ir->c.adapter->dec_use) 
		ir->c.adapter->dec_use(ir->c.adapter);
	MOD_DEC_USE_COUNT;
}

static struct lirc_plugin lirc_template = {
	name:        "lirc_i2c",
	get_key:     get_key_haup,
	set_use_inc: set_use_inc,
	set_use_dec: set_use_dec
};

/* ----------------------------------------------------------------------- */

static int ir_attach(struct i2c_adapter *adap, int addr,
		      unsigned short flags, int kind);
static int ir_detach(struct i2c_client *client);
static int ir_probe(struct i2c_adapter *adap);
static int ir_command(struct i2c_client *client, unsigned int cmd, void *arg);

static struct i2c_driver driver = {
        "i2c ir driver",
        /* FIXME */ I2C_DRIVERID_EXP3,
        I2C_DF_NOTIFY,
        ir_probe,
        ir_detach,
        ir_command,
};

static struct i2c_client client_template = 
{
        "ir",
        -1,
        0,
        0,
        NULL,
        &driver
};

static int ir_attach(struct i2c_adapter *adap, int addr,
		     unsigned short flags, int kind)
{
        struct IR *ir;
	
        client_template.adapter = adap;
        client_template.addr = addr;
	
        if (NULL == (ir = kmalloc(sizeof(struct IR),GFP_KERNEL)))
                return -ENOMEM;
        memcpy(&ir->l,&lirc_template,sizeof(struct lirc_plugin));
        memcpy(&ir->c,&client_template,sizeof(struct i2c_client));
	
	ir->c.adapter = adap;
	ir->c.addr    = addr;
	ir->c.data    = ir;
	ir->l.data    = ir;
	ir->l.minor   = minor;
	ir->l.sample_rate = 10;
	switch(addr)
	{
	case 0:
		ir->l.code_length = 8;
		ir->l.get_key=get_key_pixelview;
		break;
	case 1:
		ir->l.code_length = 13;
		ir->l.get_key=get_key_haup;
		break;
	}
	ir->nextkey = -1;
	
	/* register device */
	i2c_attach_client(&ir->c);
	ir->l.minor = lirc_register_plugin(&ir->l);
	if (ir->c.adapter->inc_use)
		ir->c.adapter->inc_use(ir->c.adapter);
	
	return 0;
}

static int ir_detach(struct i2c_client *client)
{
        struct IR *ir = client->data;
	
	/* unregister device */
	if (ir->c.adapter->dec_use)
		ir->c.adapter->dec_use(ir->c.adapter);
	lirc_unregister_plugin(ir->l.minor);
	i2c_detach_client(&ir->c);

	/* free memory */
	kfree(ir);
	return 0;
}

static int ir_probe(struct i2c_adapter *adap)
{
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, ir_attach);
	return 0;
}

static int ir_command(struct i2c_client *client,unsigned int cmd, void *arg)
{
	/* nothing */
	return 0;
}

/* ----------------------------------------------------------------------- */

#ifdef MODULE
int init_module(void)
#else
int lirc_i2c_init(void)
#endif
{
	request_module("bttv");
	i2c_add_driver(&driver);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	i2c_del_driver(&driver);
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */