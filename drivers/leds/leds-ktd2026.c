
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <asm/errno.h> 
#include <linux/cdev.h>
#include <linux/leds.h>

#define KTD_I2C_NAME			"ktd2026"

static DEFINE_MUTEX(leds_mutex);

#define  KTD_DEBUG 
struct i2c_client *ktd20xx_client;
enum cust_led_id
{
	CUST_LED_RED,
	CUST_LED_GREEN,
	CUST_LED_BLUE,	
	CUST_LED_TOTAL
};

struct cust_led_data{
	char                 *name;
	enum cust_led_id  led_id;
};

struct ktd_led_data {
	struct led_classdev     cdev;
	struct cust_led_data  cust_data;
	struct work_struct	 work;
	int level;
	int delay_on;
	int delay_off;
};

#define  LED_COUNT   2
struct cust_led_data cust_led_list[LED_COUNT] =
{
	{"red", CUST_LED_RED},
	{"green", CUST_LED_GREEN},
};

struct ktd_led_data *g_ktd_data[LED_COUNT];

int  const  leds_count = sizeof(cust_led_list);

int led_status = 0;
int g_led_suspend = 0;

#define TOTAL_TIME_START_COUNT       0x01
#define TOTAL_TIME_START       348
#define TOTAL_TIME_STEP         128

#define TIME_PERCENT_START_COUNT   0X00
#define TIME_PERCENT_STEP         (4)

#define  LED_CURRENT  0x28		//<REQ><JABALL-1500><20150526>modify led current;xiongdajun

int  ktd22xx_translate_timer(unsigned long delay_on, unsigned long delay_off)
{
	int time_count = 0;
	int percent_count = 0;	
	if(delay_on==0 && delay_off ==0)
		return -1;
	
	if((delay_on + delay_off)< 348)
	{
		time_count = 1;
	}
	else
	{
		time_count = (delay_on + delay_off - TOTAL_TIME_START)/TOTAL_TIME_STEP + TOTAL_TIME_START_COUNT + 1;
	}

	percent_count = (delay_on*100/(delay_on + delay_off))*10 /TIME_PERCENT_STEP + TIME_PERCENT_START_COUNT + 1;

	i2c_smbus_write_byte_data(ktd20xx_client, 0x01, time_count);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, percent_count);
	
	#ifdef KTD_DEBUG
		pr_info("wangyanhui   ktd22xx_translate_timer   time_count = %x      percent_count = %x   \n",  time_count, percent_count);	
	#endif
	
	return 0;
}

void ktd22xx_lowbattery_breath_leds(void){
	/*
	 * flash time period: 2.5s, rise/fall 1s, sleep 0.5s 
	 * reg5 = 0xaa, reg1 = 0x12
	 */
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x20);// initialization LED off
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);// initialization LED off
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, 0xbf);//set current is 24mA
	i2c_smbus_write_byte_data(ktd20xx_client, 0x05, 0xaa);//rase time
	i2c_smbus_write_byte_data(ktd20xx_client, 0x01, 0x12);//dry flash period
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0x00);//reset internal counter
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x02);//allocate led1 to timer1
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0x56);//led flashing(curerent ramp-up and down countinuously)
}

void ktd22xx_events_breath_leds(void){
	/* 
	 * flash time period: 5s, rise/fall 2s, sleep 1s 
	 * reg5 = 0xaa, reg1 = 0x30	
	 */
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x20);// initialization LED off
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);// initialization LED off
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, 0xbf);//set current is 24mA
	i2c_smbus_write_byte_data(ktd20xx_client, 0x05, 0xaa);//rase time
	i2c_smbus_write_byte_data(ktd20xx_client, 0x01, 0x30);//dry flash period
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0x00);//reset internal counter
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x02);//allocate led1 to timer1
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0x56);//led flashing(curerent ramp-up and down countinuously)
}


void ktd2xx_led_on(void){
	//turn on led when 0x00 is 0x00
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, 0x2f);//set current is 10mA
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x01);//turn om all of leds
}

 void ktd2xx_led_off(void){
	//turn on led when 0x02 is not 0x00,there is set to same as breath leds
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, 0x00);//set current is 0.125mA
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);//turn off leds
}

void ktd2xx_led_sleep_mode(void){
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x08);
}

void ktd2xx_led_enable_mode(void){
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x20);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);
}
  
void ktd22xx_breath_leds_time(int blink){
	//set breath led flash time from blink
	int period, flashtime;
	period = (blink >> 8) & 0xff;
	flashtime = blink & 0xff;
	printk("ktd20xx led write blink = %x, period = %x, flashtime = %x\n", blink, period, flashtime);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x20);// initialization LED off
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);// initialization LED off
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, 0xbf);//set current is 24mA
	i2c_smbus_write_byte_data(ktd20xx_client, 0x05, period);//rase time
	i2c_smbus_write_byte_data(ktd20xx_client, 0x01, flashtime);//dry flash period
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0x00);//reset internal counter
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x02);//allocate led1 to timer1
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0x56);//led flashing(curerent ramp-up and down countinuously)

}

void ktd2xx_red_led_on(void)
{
	led_status = led_status|0x04 ;
	led_status = led_status & (~0x08) ;
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);	//Line<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x18);	
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0XFF);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x07, LED_CURRENT);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, led_status);

	#ifdef KTD_DEBUG
		pr_info("wangyanhui  ktd2xx_red_led_on      led_status = %x  \n",  led_status);
	#endif
}
void ktd2xx_red_led_off(void)
{
	led_status = led_status & (~0x0C) ;

	i2c_smbus_write_byte_data(ktd20xx_client, 0x07, 0x00);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, led_status);
	//Begin<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x08);
	mdelay(1);
	//End<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	#ifdef KTD_DEBUG
		pr_info("wangyanhui  ktd2xx_red_led_off     led_status = %x  \n",  led_status);
	#endif
}

void ktd2xx_green_led_on(void)
{
	led_status = led_status | 0x01; 
	led_status = led_status & (~0x02) ;
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);	//Line<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x18);	
	i2c_smbus_write_byte_data(ktd20xx_client, 0x02, 0XFF);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, LED_CURRENT);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, led_status);

	#ifdef KTD_DEBUG
		pr_info("wangyanhui  ktd2xx_green_led_on     led_status  = %x  \n",  led_status);
	#endif
	
}

void ktd2xx_green_led_off(void)
{
	led_status = led_status & (~0x03); 
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, 0x00);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, led_status);
	//Begin<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x08);
	mdelay(1);
	//End<REQ><JABALL-1500><20150526>modify led current;xiongdajun

	#ifdef KTD_DEBUG
		pr_info("wangyanhui  ktd2xx_green_led_off      led_status = %x  \n",  led_status);
	#endif
}

void ktd2xx_red_led_blink(unsigned long delay_on, unsigned long delay_off)
{
	int ret=0;
	led_status = 0;
	led_status = led_status | 0x08;
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);	//Line<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	ret = ktd22xx_translate_timer(delay_on, delay_off);
	if(ret < 0)
	{
		ktd2xx_red_led_off();
		return;
	}
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x18);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x05, 0x00);	
	i2c_smbus_write_byte_data(ktd20xx_client, 0x07, LED_CURRENT);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, led_status);

}

void ktd2xx_green_led_blink(unsigned long delay_on, unsigned long delay_off)
{
	int ret=0;
	led_status = 0;
	led_status = led_status | 0x02;
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);	//Line<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	ret = ktd22xx_translate_timer(delay_on, delay_off);
	if(ret < 0)
	{
		ktd2xx_green_led_off();
		return;
	}
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x18);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x05, 0x00);		
	i2c_smbus_write_byte_data(ktd20xx_client, 0x06, LED_CURRENT);
	i2c_smbus_write_byte_data(ktd20xx_client, 0x04, led_status);
}

void ktd_cust_led_set_cust(struct cust_led_data *cust, int level)
{
#ifdef KTD_DEBUG
	pr_info("wangyanhui  ktd_cust_led_set_cust    level = %d  cust->led_id = %d  \n", level , cust->led_id);
#endif
	if(level > 0)
	{
		switch(cust->led_id)
		{
			case  CUST_LED_RED:
				ktd2xx_red_led_on();
				break;
			case  CUST_LED_GREEN:
				ktd2xx_green_led_on();
				break;

			default:
				break;
		}
	}
	else
	{
		switch(cust->led_id)
		{
			case  CUST_LED_RED:
				ktd2xx_red_led_off();
				break;
			case  CUST_LED_GREEN:
				ktd2xx_green_led_off();
				break;

			default:
				break;
		}
	}
}
static void ktd_led_set(struct led_classdev *led_cdev, enum led_brightness level)
{
	struct ktd_led_data *led_data =
		container_of(led_cdev, struct ktd_led_data, cdev);

	if (level < LED_OFF) {
		return;
	}

	if (level > led_data->cdev.max_brightness)
		level = led_data->cdev.max_brightness;
	
	led_data->cdev.brightness = level;
	schedule_work(&led_data->work);
}

void ktd_led_blink_set(struct ktd_led_data *led_data)
{

#ifdef KTD_DEBUG
	pr_info("wangyanhui  ktd_blink_set  led_data->cust_data.led_id = %d \n" ,led_data->cust_data.led_id);
	pr_info("wangyanhui  ktd_blink_set  led_data->delay_on = %d  led_data->delay_off = %d\n" ,led_data->delay_on ,led_data->delay_off);
#endif
	switch(led_data->cust_data.led_id)
	{
		case CUST_LED_RED:
			ktd2xx_red_led_blink(led_data->delay_on, led_data->delay_off);
			break;
		case CUST_LED_GREEN:
			ktd2xx_green_led_blink(led_data->delay_on, led_data->delay_off);
			break;
		default:
			break;
	}
}

static int  ktd_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct ktd_led_data *led_data =
		container_of(led_cdev, struct ktd_led_data, cdev);

	led_data->delay_on = *delay_on;
	led_data->delay_off = *delay_off;
	//pr_info("wangyanhui  ktd_blink_set  delay_on = %x  delay_off = %x  ",led_data->delay_on ,led_data->delay_off);	
	ktd_led_blink_set(led_data);
	
	return 0;
}


void ktd_led_work(struct work_struct *work)
{
	struct ktd_led_data *led_data =
		container_of(work, struct ktd_led_data, work);

	mutex_lock(&leds_mutex);
	ktd_cust_led_set_cust(&led_data->cust_data, led_data->cdev.brightness);
	mutex_unlock(&leds_mutex);
}

static int ktd20xx_probe(struct i2c_client *client, const struct i2c_device_id *id){

	int ret;
	int err = 0;
	int i = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev,
				"%s: check_functionality failed.", __func__);
		err = -ENODEV;
		goto exit0;
	}
	
	ktd20xx_client = client;
	
	//ret = i2c_smbus_write_byte_data(ktd20xx_client, 0x06, 0x00);//set current is 0.125mA
	//Begin<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	i2c_smbus_write_byte_data(ktd20xx_client, 0x00, 0x07);
	mdelay(1);
	//End<REQ><JABALL-1500><20150526>modify led current;xiongdajun
	ret = i2c_smbus_write_byte_data(ktd20xx_client, 0x04, 0x00);//turn off leds	

	if(ret < 0){
		printk("can't find ktd2026 led control ic!");
		//goto exit0;//Line<REQ><JABALL-1500><20150709>ignore NACK when probe IC;xiongdajun

	}

	for(i = 0; i < LED_COUNT; i++)
	{
		g_ktd_data[i] = kzalloc(sizeof(struct ktd_led_data), GFP_KERNEL);
		if (!g_ktd_data[i]) {
			dev_err(&client->dev,
					"%s: memory allocation failed.", __func__);
			err = -ENOMEM;
			goto exit0;
		}

		g_ktd_data[i]->cust_data.name = cust_led_list[i].name;
		g_ktd_data[i]->cust_data.led_id = cust_led_list[i].led_id;
		g_ktd_data[i]->cdev.name = cust_led_list[i].name;
		g_ktd_data[i]->cdev.brightness_set = ktd_led_set;
		g_ktd_data[i]->cdev.blink_set =ktd_blink_set;

		INIT_WORK(&g_ktd_data[i]->work, ktd_led_work);

		ret = led_classdev_register((struct device *)&client->dev , &g_ktd_data[i]->cdev);
	}

exit0:
	return err;
}

static int ktd20xx_remove(struct i2c_client *client){

	int i;
	for (i = 0; i < LED_COUNT; i++) {
		led_classdev_unregister(&g_ktd_data[i]->cdev);
		cancel_work_sync(&g_ktd_data[i]->work);
		kfree(g_ktd_data[i]);
		g_ktd_data[i] = NULL;
	}
		
	return 0;
}
	

void ktd20xx_shutdown(struct i2c_client *client)
{
	mutex_lock(&leds_mutex);
	ktd2xx_led_off();
	mutex_unlock(&leds_mutex);
}


static int ktd20xx_suspend(struct i2c_client *client, pm_message_t mesg)
{
#ifdef KTD_DEBUG
	pr_info("wangyanhui  ktd20xx_suspend    led_status = %x  \n", led_status);
#endif
	if(led_status == 0)
	{
		ktd2xx_led_sleep_mode();
		g_led_suspend = 1;
	}
	return 0;
}

static int ktd20xx_resume(struct i2c_client *client)
{
	if(g_led_suspend)
	{
		ktd2xx_led_enable_mode();
		g_led_suspend = 0;
	}
	return 0;
}



static const struct i2c_device_id ktd2xx_id[] = {
	{KTD_I2C_NAME, 0},
	{ }
};


static struct of_device_id ktd20xx_match_table[] = {
        { .compatible = "ktd,ktd2026",},
        { },
};

/* 1. 分配一个i2c_driver结构体 */
/* 2. 设置i2c_driver结构体 */
static struct i2c_driver ktd20xx_driver = {
	.driver = {
		.name	= KTD_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ktd20xx_match_table,
	},
	.probe = ktd20xx_probe,
	.remove = ktd20xx_remove,
	.suspend    = ktd20xx_suspend,
	.resume     = ktd20xx_resume,	
	.shutdown = ktd20xx_shutdown,
	.id_table = ktd2xx_id,
};

static int __init ktd20xx_init(void)
{
	int err;
	printk("%s\n",__func__);
	err = i2c_add_driver(&ktd20xx_driver);
	if (err) {
		printk(KERN_WARNING "ktd20xx driver failed "
		       "(errno = %d)\n", err);
	} else {
		printk( "Successfully added driver %s\n",
		          ktd20xx_driver.driver.name);
	}
	return err;
}

static void __exit ktd20xx_exit(void)
{
	printk("%s\n",__func__);
	i2c_del_driver(&ktd20xx_driver);
}

module_init(ktd20xx_init);
module_exit(ktd20xx_exit);

MODULE_AUTHOR("tinno.com");
MODULE_LICENSE("GPL");

