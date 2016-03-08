#include <user_interface.h>
#include <osapi.h>
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include "httpclient.h"
#include "user_config.h"
#include "driver/ds18b20.h"

typedef enum {
	WIFI_CONNECTING,
	WIFI_CONNECTING_ERROR,
	WIFI_CONNECTED,
} tConnState;

unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;

extern uint16_t readvdd33(void);

LOCAL void ICACHE_FLASH_ATTR setup_wifi_st_mode(void);
static struct ip_info ipConfig;
static ETSTimer WiFiLinker;

int ds18b20();

static ETSTimer sleep_timer;
LOCAL void ICACHE_FLASH_ATTR sleep_cb(void *arg)
{
    os_timer_disarm(&sleep_timer);
    system_deep_sleep_set_option( 1 );
    system_deep_sleep(60*1000*1000);//second*1000*1000
}

LOCAL void ICACHE_FLASH_ATTR thingspeak_http_callback(char * response, int http_status, char * full_response)
{
	if (http_status == 200)
	{
        os_timer_disarm(&sleep_timer);
        os_timer_setfn(&sleep_timer, sleep_cb, NULL);
        os_timer_arm(&sleep_timer, 2000, 1); //5s
	}
}

static void ICACHE_FLASH_ATTR wifi_check_ip(void *arg)
{
	os_timer_disarm(&WiFiLinker);
	if (wifi_station_get_connect_status() == STATION_GOT_IP)
	{
        wifi_get_ip_info(STATION_IF, &ipConfig);
        if(ipConfig.ip.addr != 0) {
            ds18b20();
        }
	}
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
	}
	wifi_station_connect();
	wifi_station_dhcpc_start();
	wifi_station_set_auto_connect(1);
}

void user_rf_pre_init(void)
{
}

int ICACHE_FLASH_ATTR ds18b20()
{
	int r, i;
	uint8_t data[12];
	uint8_t addr[] = "\x28\xff\x78\x01\x01\x15\x03\xce";

	ds_init();
	reset();

	select(addr);
	write(DS1820_CONVERT_T, 1); // perform temperature conversion

	os_delay_us(1000*1000); // sleep 1s

	reset();

	select(addr);
	write(DS1820_READ_SCRATCHPAD, 0); // read scratchpad
	
	for(i = 0; i < 9; i++)
	{
		data[i] = read();
	}

	int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;
	LowByte = data[0];
	HighByte = data[1];
	TReading = (HighByte << 8) + LowByte;
	SignBit = TReading & 0x8000;  // test most sig bit
	if (SignBit) // negative
		TReading = (TReading ^ 0xffff) + 1; // 2's comp
	
	Whole = TReading >> 4;  // separate off the whole and fractional portions
	Fract = (TReading & 0xf) * 100 / 16;
    
    unsigned int vdd = readvdd33();

    wifi_get_ip_info(STATION_IF, &ipConfig);
    static char temp[10];
    static char http_data[256];

    os_sprintf(temp, "%d.%d", Whole, Fract < 10 ? 0 : Fract);
    // Start the connection process
    os_sprintf(http_data, "http://%s/update?key=%s&field1=%s&field3=%d", THINGSPEAK_SERVER, THINGSPEAK_API_KEY, temp, vdd);
    http_get(http_data, "", thingspeak_http_callback);

    return r;
}

void user_init(void)
{
    system_set_os_print(0);

	if(wifi_get_opmode() != STATION_MODE)
	{
		setup_wifi_st_mode();
	}
	if(wifi_get_phy_mode() != PHY_MODE_11N)
		wifi_set_phy_mode(PHY_MODE_11N);
	if(wifi_station_get_auto_connect() == 0)
		wifi_station_set_auto_connect(1);

	// Wait for Wi-Fi connection
	os_timer_disarm(&WiFiLinker);
	os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
	os_timer_arm(&WiFiLinker, WIFI_CHECK_DELAY, 0);
}

