/*
 *  Example of working sensor DHT22 (temperature and humidity) and send data on the service thingspeak.com
 *  https://thingspeak.com
 *
 *  For a single device, connect as follows:
 *  DHT22 1 (Vcc) to Vcc (3.3 Volts)
 *  DHT22 2 (DATA_OUT) to ESP Pin GPIO2
 *  DHT22 3 (NC)
 *  DHT22 4 (GND) to GND
 *
 *  Between Vcc and DATA_OUT need to connect a pull-up resistor of 10 kOh.
 *
 *  (c) 2015 by Mikhail Grigorev <sleuthhound@gmail.com>
 *
 */

#include <user_interface.h>
#include <osapi.h>
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include "httpclient.h"
#include "driver/uart.h"
#include "driver/dht22.h"
#include "user_config.h"

/////////////////////////////////////////////////////////////////

const char *FlashSizeMap[] =
{
		"512 KB (256 KB + 256 KB)",	// 0x00
		"256 KB",			// 0x01
		"1024 KB (512 KB + 512 KB)", 	// 0x02
		"2048 KB (512 KB + 512 KB)"	// 0x03
		"4096 KB (512 KB + 512 KB)"	// 0x04
		"2048 KB (1024 KB + 1024 KB)"	// 0x05
		"4096 KB (1024 KB + 1024 KB)"	// 0x06
};

const char *WiFiMode[] =
{
		"NULL",		// 0x00
		"STATION",	// 0x01
		"SOFTAP", 	// 0x02
		"STATIONAP"	// 0x03
};

extern int ets_uart_printf(const char *fmt, ...);
int (*console_printf)(const char *fmt, ...) = ets_uart_printf;

// Debug output.
#ifdef DHT22_DEBUG
#undef DHT22_DEBUG
#define DHT22_DEBUG(...) console_printf(__VA_ARGS__);
#else
#define DHT22_DEBUG(...)
#endif

/////////////////////////////////////////////////////////////////

unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;

extern uint16_t readvdd33(void);

LOCAL void ICACHE_FLASH_ATTR setup_wifi_st_mode(void);
static struct ip_info ipConfig;
static ETSTimer WiFiLinker;

static ETSTimer sleep_timer;
LOCAL void ICACHE_FLASH_ATTR sleep_cb(void *arg)
{
DHT22_DEBUG("sleep_cb start.\n");

    os_timer_disarm(&sleep_timer);
    system_deep_sleep_set_option( 1 );
    system_deep_sleep(DATA_SEND_DELAY);//second*1000*1000
}

LOCAL void ICACHE_FLASH_ATTR thingspeak_http_callback(char * response, int http_status, char * full_response)
{
	DHT22_DEBUG("Answers: \r\n");

	if (http_status == 200)
	{
		DHT22_DEBUG("response=%s<EOF>\n", response);

        os_timer_disarm(&WiFiLinker);

        os_timer_disarm(&sleep_timer);
        os_timer_setfn(&sleep_timer, sleep_cb, NULL);
        os_timer_arm(&sleep_timer, 500, 1); 
	}
}

LOCAL void ICACHE_FLASH_ATTR dht22_func()
{
	static char data[256];
	static char temp[10];
	static char hum[10];
	struct dht_sensor_data* r;
	float lastTemp, lastHum;
    int iter = 10; // loop 

    r = DHTRead();
    lastTemp = r->temperature;
    lastHum = r->humidity;
    unsigned int vdd = readvdd33();
    if(r->success)
    {
        os_sprintf(temp, "%d.%d",(int)(lastTemp),(int)((lastTemp - (int)lastTemp)*100));
        os_sprintf(hum, "%d.%d",(int)(lastHum),(int)((lastHum - (int)lastHum)*100));
DHT22_DEBUG("Temperature: %s *C, Humidity: %s %%\r\n", temp, hum);

        // Start the connection process
        os_sprintf(data, "http://%s/update?key=%s&field4=%s&field2=%s&field6=%d", THINGSPEAK_SERVER, THINGSPEAK_API_KEY, temp, hum, vdd);
DHT22_DEBUG("Request: %s\r\n", data);

        http_get(data, "", thingspeak_http_callback);
        return;
    }
}

static void ICACHE_FLASH_ATTR wifi_check_ip(void *arg)
{
DHT22_DEBUG("wifi_check_ip\r\n");

	os_timer_disarm(&WiFiLinker);
    if (wifi_station_get_connect_status()==STATION_GOT_IP)
    {
DHT22_DEBUG("WiFi connected, has IP...\r\n");

        wifi_get_ip_info(STATION_IF, &ipConfig);
        if(ipConfig.ip.addr != 0) 
        {
DHT22_DEBUG("WiFi connected, IP is not empty - wait for DHT22...\r\n");

            dht22_func();
        }
    }
DHT22_DEBUG("WiFi connected, wait DHT22 timer...\r\n");

	os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
	os_timer_arm(&WiFiLinker, WIFI_CHECK_DELAY, 0);
}


LOCAL void ICACHE_FLASH_ATTR setup_wifi_st_mode(void)
{
	wifi_set_opmode(STATION_MODE);
	struct station_config stconfig;
	wifi_station_disconnect();
	wifi_station_dhcpc_stop();
	if(wifi_station_get_config(&stconfig))
	{
		os_memset(stconfig.ssid, 0, sizeof(stconfig.ssid));
		os_memset(stconfig.password, 0, sizeof(stconfig.password));
		os_sprintf(stconfig.ssid, "%s", WIFI_CLIENTSSID);
		os_sprintf(stconfig.password, "%s", WIFI_CLIENTPASSWORD);
		wifi_station_set_config(&stconfig);
	}
	wifi_station_connect();
	wifi_station_dhcpc_start();
	wifi_station_set_auto_connect(1);

	DHT22_DEBUG("ESP8266 in STA mode configured.\r\n");
}

void user_rf_pre_init(void)
{
}

void user_init(void)
{
	// Configure the UART
	uart_init(BIT_RATE_115200, BIT_RATE_115200);

	system_set_os_print(1);
	os_delay_us(10000);
	DHT22_DEBUG("System init...\r\n");


//	os_delay_us(10000);

	if(wifi_get_opmode() != STATION_MODE)
	{
		setup_wifi_st_mode();
	}
	if(wifi_get_phy_mode() != PHY_MODE_11N)
		wifi_set_phy_mode(PHY_MODE_11N);
	if(wifi_station_get_auto_connect() == 0)
		wifi_station_set_auto_connect(1);

	// Init DHT22 sensor
	DHTInit(DHT22);

	// Wait for Wi-Fi connection
	os_timer_disarm(&WiFiLinker);
	os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
	os_timer_arm(&WiFiLinker, WIFI_CHECK_DELAY, 0);

DHT22_DEBUG("System init done.\n");
}
