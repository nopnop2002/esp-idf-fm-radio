/*
	 Example using WEB Socket.
	 This example code is in the Public Domain (or CC0 licensed, at your option.)
	 Unless required by applicable law or agreed to in writing, this
	 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	 CONDITIONS OF ANY KIND, either express or implied.
*/

#include "stdio.h"
#include "math.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "mdns.h"

#include "websocket_server.h"
#include "tea5767.h"

static QueueHandle_t client_queue;
MessageBufferHandle_t xMessageBufferMain;

const static int client_queue_size = 10;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT			 BIT1

static char *TAG = "main";
static char *PRESET_FREQ = "preset_freq";
static char *PRESET_LIST = "preset_list";
static char *SEGMENT_COLOR = "segment_color";

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
													int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

void wifi_init_sta(void)
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
																											ESP_EVENT_ANY_ID,
																											&event_handler,
																											NULL,
																											&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
																											IP_EVENT_STA_GOT_IP,
																											&event_handler,
																											NULL,
																											&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD,
			/* Setting a password implies station will connect to all security modes including WEP/WPA.
			 * However these modes are deprecated and not advisable to be used. Incase your Access point
			 * doesn't support WPA2, these mode can be enabled by commenting below line */
			 .threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				.capable = true,
				.required = false
			},
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,
		pdFALSE,
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
						 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
						 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	/* The event will not be processed after unregister */
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group);
}

void initialise_mdns(void)
{
	//initialize mDNS
	ESP_ERROR_CHECK( mdns_init() );
	//set mDNS hostname (required if you want to advertise services)
	ESP_ERROR_CHECK( mdns_hostname_set(CONFIG_MDNS_HOSTNAME) );
	ESP_LOGI(TAG, "mdns hostname set to: [%s]", CONFIG_MDNS_HOSTNAME);

#if 0
	//set default mDNS instance name
	ESP_ERROR_CHECK( mdns_instance_name_set("ESP32 with mDNS") );
#endif
}

// handles websocket events
void websocket_callback(uint8_t num,WEBSOCKET_TYPE_t type,char* msg,uint64_t len) {
	const static char* TAG = "websocket_callback";
	//int value;

	switch(type) {
		case WEBSOCKET_CONNECT:
			ESP_LOGI(TAG,"client %i connected!",num);
			break;
		case WEBSOCKET_DISCONNECT_EXTERNAL:
			ESP_LOGI(TAG,"client %i sent a disconnect message",num);
			break;
		case WEBSOCKET_DISCONNECT_INTERNAL:
			ESP_LOGI(TAG,"client %i was disconnected",num);
			break;
		case WEBSOCKET_DISCONNECT_ERROR:
			ESP_LOGE(TAG,"client %i was disconnected due to an error",num);
			break;
		case WEBSOCKET_TEXT:
			if(len) { // if the message length was greater than zero
				ESP_LOGI(TAG, "got message length %i: %s", (int)len, msg);
				//size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, msg, len, portMAX_DELAY);
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				size_t xBytesSent = xMessageBufferSendFromISR(xMessageBufferMain, msg, len, &xHigherPriorityTaskWoken);
				if (xBytesSent != len) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			}
			break;
		case WEBSOCKET_BIN:
			ESP_LOGI(TAG,"client %i sent binary message of size %i:\n%s",num,(uint32_t)len,msg);
			break;
		case WEBSOCKET_PING:
			ESP_LOGI(TAG,"client %i pinged us with message of size %i:\n%s",num,(uint32_t)len,msg);
			break;
		case WEBSOCKET_PONG:
			ESP_LOGI(TAG,"client %i responded to the ping",num);
			break;
	}
}

// serves any clients
static void http_serve(struct netconn *conn) {
	const static char* TAG = "http_server";
	const static char HTML_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/html\n\n";
	const static char ERROR_HEADER[] = "HTTP/1.1 404 Not Found\nContent-type: text/html\n\n";
	const static char JS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/javascript\n\n";
	const static char CSS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/css\n\n";
	const static char PNG_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/png\n\n";
	const static char ICO_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/x-icon\n\n";
	//const static char PDF_HEADER[] = "HTTP/1.1 200 OK\nContent-type: application/pdf\n\n";
	//const static char EVENT_HEADER[] = "HTTP/1.1 200 OK\nContent-Type: text/event-stream\nCache-Control: no-cache\nretry: 3000\n\n";
	struct netbuf* inbuf;
	static char* buf;
	static uint16_t buflen;
	static err_t err;

	// default page
	extern const uint8_t root_html_start[] asm("_binary_root_html_start");
	extern const uint8_t root_html_end[] asm("_binary_root_html_end");
	const uint32_t root_html_len = root_html_end - root_html_start;

	// main.js
	extern const uint8_t main_js_start[] asm("_binary_main_js_start");
	extern const uint8_t main_js_end[] asm("_binary_main_js_end");
	const uint32_t main_js_len = main_js_end - main_js_start;

	// seven_segment_display.js
	extern const uint8_t seven_segment_display_js_start[] asm("_binary_seven_segment_display_js_start");
	extern const uint8_t seven_segment_display_js_end[] asm("_binary_seven_segment_display_js_end");
	const uint32_t seven_segment_display_js_len = seven_segment_display_js_end - seven_segment_display_js_start;

	// speaker.png
	extern const uint8_t speaker_png_start[] asm("_binary_speaker_png_start");
	extern const uint8_t speaker_png_end[] asm("_binary_speaker_png_end");
	const uint32_t speaker_png_len = speaker_png_end - speaker_png_start;

	// main.css
	extern const uint8_t main_css_start[] asm("_binary_main_css_start");
	extern const uint8_t main_css_end[] asm("_binary_main_css_end");
	const uint32_t main_css_len = main_css_end - main_css_start;

	// favicon.ico
	extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
	extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");
	const uint32_t favicon_ico_len = favicon_ico_end - favicon_ico_start;

	// error page
	extern const uint8_t error_html_start[] asm("_binary_error_html_start");
	extern const uint8_t error_html_end[] asm("_binary_error_html_end");
	const uint32_t error_html_len = error_html_end - error_html_start;

	netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second
	ESP_LOGI(TAG,"reading from client...");
	err = netconn_recv(conn, &inbuf);
	ESP_LOGI(TAG,"read from client");
	if(err==ERR_OK) {
		netbuf_data(inbuf, (void**)&buf, &buflen);
		if(buf) {

			ESP_LOGD(TAG, "buf=[%s]", buf);
			// default page
			if		 (strstr(buf,"GET / ")
					&& !strstr(buf,"Upgrade: websocket")) {
				ESP_LOGI(TAG,"Sending /");
				netconn_write(conn, HTML_HEADER, sizeof(HTML_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, root_html_start,root_html_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			// default page websocket
			else if(strstr(buf,"GET / ")
					 && strstr(buf,"Upgrade: websocket")) {
				ESP_LOGI(TAG,"Requesting websocket on /");
				ws_server_add_client(conn,buf,buflen,"/",websocket_callback);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /main.js ")) {
				ESP_LOGI(TAG,"Sending /main.js");
				netconn_write(conn, JS_HEADER, sizeof(JS_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, main_js_start, main_js_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /seven_segment_display.js ")) {
				ESP_LOGI(TAG,"Sending /seven_segment_display.js");
				netconn_write(conn, JS_HEADER, sizeof(JS_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, seven_segment_display_js_start, seven_segment_display_js_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /main.css ")) {
				ESP_LOGI(TAG,"Sending /main.css");
				netconn_write(conn, CSS_HEADER, sizeof(CSS_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, main_css_start, main_css_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /favicon.ico ")) {
				ESP_LOGI(TAG,"Sending favicon.ico");
				netconn_write(conn,ICO_HEADER,sizeof(ICO_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn,favicon_ico_start,favicon_ico_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /speaker.png ")) {
				ESP_LOGI(TAG,"Sending speaker.png");
				netconn_write(conn,PNG_HEADER,sizeof(PNG_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn,speaker_png_start,speaker_png_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /")) {
				ESP_LOGE(TAG,"Unknown request, sending error page: %s",buf);
				netconn_write(conn, ERROR_HEADER, sizeof(ERROR_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, error_html_start, error_html_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else {
				ESP_LOGE(TAG,"Unknown request");
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}
		}
		else {
			ESP_LOGI(TAG,"Unknown request (empty?...)");
			netconn_close(conn);
			netconn_delete(conn);
			netbuf_delete(inbuf);
		}
	}
	else { // if err==ERR_OK
		ESP_LOGI(TAG,"error on read, closing connection");
		netconn_close(conn);
		netconn_delete(conn);
		netbuf_delete(inbuf);
	}
}

// handles clients when they first connect. passes to a queue
static void server_task(void* pvParameters) {
	const static char* TAG = "server_task";
	char *task_parameter = (char *)pvParameters;
	ESP_LOGI(TAG, "Start task_parameter=%s", task_parameter);
	char url[64];
	sprintf(url, "http://%s", task_parameter);
	ESP_LOGI(TAG, "Starting server on %s", url);

	struct netconn *conn, *newconn;
	static err_t err;
	client_queue = xQueueCreate(client_queue_size,sizeof(struct netconn*));
	configASSERT( client_queue );

	conn = netconn_new(NETCONN_TCP);
	netconn_bind(conn,NULL,80);
	netconn_listen(conn);
	ESP_LOGI(TAG,"server listening");
	do {
		err = netconn_accept(conn, &newconn);
		ESP_LOGI(TAG,"new client");
		if(err == ERR_OK) {
			xQueueSendToBack(client_queue,&newconn,portMAX_DELAY);
			//http_serve(newconn);
		}
	} while(err == ERR_OK);
	netconn_close(conn);
	netconn_delete(conn);
	ESP_LOGE(TAG,"task ending, rebooting board");
	esp_restart();
}

// receives clients from queue, handles them
static void server_handle_task(void* pvParameters) {
	const static char* TAG = "server_handle_task";
	struct netconn* conn;
	ESP_LOGI(TAG,"task starting");
	for(;;) {
		xQueueReceive(client_queue,&conn,portMAX_DELAY);
		if(!conn) continue;
		http_serve(conn);
	}
	vTaskDelete(NULL);
}

#if 0
esp_err_t NVS_check_key(nvs_handle_t my_handle, char * key) {
	ESP_LOGD(TAG, "NVS_KEY_NAME_MAX_SIZE=%d", NVS_KEY_NAME_MAX_SIZE);
	if (strlen(key) > NVS_KEY_NAME_MAX_SIZE-1) {
		ESP_LOGE(TAG, "Maximal key length is %d", NVS_KEY_NAME_MAX_SIZE-1);
		return ESP_ERR_INVALID_ARG;
	}

	ESP_LOGI(TAG, "Checking [%s] from NVS ... ", key);
	int16_t value = 0; // value will default to 0, if not set yet in NVS
	esp_err_t err = nvs_get_i16(my_handle, key, &value);
	switch (err) {
		case ESP_OK:
			ESP_LOGI(TAG, "Done. [%s] = %d", key, value);
			break;
		case ESP_ERR_NVS_NOT_FOUND:
			ESP_LOGW(TAG, "The value is not initialized yet!");
			break;
		default :
			ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
			break;
	}
	return err;
}
#endif


esp_err_t NVS_read_int16(nvs_handle_t my_handle, char * key, int16_t *value) {
	ESP_LOGD(TAG, "NVS_KEY_NAME_MAX_SIZE=%d", NVS_KEY_NAME_MAX_SIZE);
	if (strlen(key) > NVS_KEY_NAME_MAX_SIZE-1) {
		ESP_LOGE(TAG, "Maximal key length is %d", NVS_KEY_NAME_MAX_SIZE-1);
		return ESP_ERR_INVALID_ARG;
	}

	ESP_LOGI(TAG, "NVS_read_int16 Reading [%s] from NVS ... ", key);
	int16_t _value = 0; // value will default to 0, if not set yet in NVS
	esp_err_t err = nvs_get_i16(my_handle, key, &_value);
	switch (err) {
		case ESP_OK:
			ESP_LOGI(TAG, "NVS_read_int16 Done. [%s] = %d", key, _value);
			*value = _value;
			break;
		case ESP_ERR_NVS_NOT_FOUND:
			ESP_LOGW(TAG, "The value is not initialized yet!");
			break;
		default :
			ESP_LOGE(TAG, "Error (%s) read!", esp_err_to_name(err));
			break;
	}
	return err;
}

esp_err_t NVS_write_int16(nvs_handle_t my_handle, char * key, int16_t value) {
	ESP_LOGD(TAG, "NVS_KEY_NAME_MAX_SIZE=%d", NVS_KEY_NAME_MAX_SIZE);
	if (strlen(key) > NVS_KEY_NAME_MAX_SIZE-1) {
		ESP_LOGE(TAG, "Maximal key length is %d", NVS_KEY_NAME_MAX_SIZE-1);
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t err = nvs_set_i16(my_handle, key, value);
	ESP_LOGI(TAG, "nvs_set_i16 err=%d", err);
	if (err == ESP_OK) {
		// Commit written value.
		// After setting any values, nvs_commit() must be called to ensure changes are written
		// to flash storage. Implementations may write to storage at other times,
		// but this is not guaranteed.
		ESP_LOGI(TAG, "Committing updates in NVS ... ");
		err = nvs_commit(my_handle);
		ESP_LOGI(TAG, "nvs_commit err=%d", err);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Error (%s) commit!", esp_err_to_name(err));
		}
	} else {
		ESP_LOGE(TAG, "Error (%s) write!", esp_err_to_name(err));
	}
	return err;
}

esp_err_t NVS_read_blob(nvs_handle_t my_handle, char * key, int16_t *list, size_t *valid_elements) {
	ESP_LOGD(TAG, "NVS_KEY_NAME_MAX_SIZE=%d", NVS_KEY_NAME_MAX_SIZE);
	if (strlen(key) > NVS_KEY_NAME_MAX_SIZE-1) {
		ESP_LOGE(TAG, "Maximal key length is %d", NVS_KEY_NAME_MAX_SIZE-1);
		return ESP_ERR_INVALID_ARG;
	}

	ESP_LOGI(TAG, "NVS_read_blob Reading [%s] from NVS ... ", key);
	// Read the size of memory space required for blob
	size_t _required_size = 0;	// value will default to 0, if not set yet in NVS
	esp_err_t err = nvs_get_blob(my_handle, key, NULL, &_required_size);
	switch (err) {
		case ESP_OK:
			ESP_LOGI(TAG, "NVS_read_blob Done. [%s] = %u", key, _required_size);
			if (_required_size > 0) {
				err = nvs_get_blob(my_handle, key, list, &_required_size);
				if (err != ESP_OK) {
					*valid_elements = 0;
					ESP_LOGE(TAG, "Error (%s) read!", esp_err_to_name(err));
				} else {
					*valid_elements = _required_size/sizeof(int16_t);
					ESP_LOGI(TAG, "NVS_read_blob [%s] *valid_elements = %d", key, *valid_elements);
				}
			}
			break;
		case ESP_ERR_NVS_NOT_FOUND:
			ESP_LOGW(TAG, "The value is not initialized yet!");
			break;
		default :
			ESP_LOGE(TAG, "Error (%s) read!", esp_err_to_name(err));
			break;
	}
	return err;
}

esp_err_t NVS_write_blob(nvs_handle_t my_handle, char * key, int16_t *list, size_t valid_elements) {
	ESP_LOGD(TAG, "NVS_KEY_NAME_MAX_SIZE=%d", NVS_KEY_NAME_MAX_SIZE);
	if (strlen(key) > NVS_KEY_NAME_MAX_SIZE-1) {
		ESP_LOGE(TAG, "Maximal key length is %d", NVS_KEY_NAME_MAX_SIZE-1);
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t err = nvs_set_blob(my_handle, key, list, valid_elements*sizeof(int16_t));
	ESP_LOGI(TAG, "nvs_set_blob err=%d", err);
	if (err == ESP_OK) {
		// Commit written value.
		// After setting any values, nvs_commit() must be called to ensure changes are written
		// to flash storage. Implementations may write to storage at other times,
		// but this is not guaranteed.
		ESP_LOGI(TAG, "Committing updates in NVS ... ");
		err = nvs_commit(my_handle);
		ESP_LOGI(TAG, "nvs_commit err=%d", err);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Error (%s) commit!", esp_err_to_name(err));
		}
	} else {
		ESP_LOGE(TAG, "Error (%s) write!", esp_err_to_name(err));
	}
	return err;
}

#if 0
esp_err_t NVS_delete_key(nvs_handle_t my_handle, char * key) {
	ESP_LOGD(TAG, "NVS_KEY_NAME_MAX_SIZE=%d", NVS_KEY_NAME_MAX_SIZE);
	if (strlen(key) > NVS_KEY_NAME_MAX_SIZE-1) {
		ESP_LOGE(TAG, "Maximal key length is %d", NVS_KEY_NAME_MAX_SIZE-1);
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t err = nvs_erase_key(my_handle, key);
	ESP_LOGI(TAG, "nvs_erase_key err=%d", err);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%s) erase!", esp_err_to_name(err));
	}
	return err;
}
#endif

void app_main() {
	//Initialize NVS
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	// Open NVS
	ESP_LOGI(TAG, "Opening Non-Volatile Storage handle");
	nvs_handle_t my_handle;
	err = nvs_open("storage", NVS_READWRITE, &my_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
		while(1) { vTaskDelay(1); }
	}
	ESP_LOGI(TAG, "nvs_open Done");

	int16_t presetFrequence = 0;
	err = NVS_read_int16(my_handle, PRESET_FREQ, &presetFrequence);
	ESP_LOGI(TAG, "NVS_read_int16=%d presetFrequence=%d", err, presetFrequence);

	int16_t presetList[10];
	size_t presetListCount = 0;
	err = NVS_read_blob(my_handle, PRESET_LIST, presetList, &presetListCount);
	ESP_LOGI(TAG, "presetListCount=%u", presetListCount);
	for(int i=0;i<presetListCount;i++) {
		ESP_LOGI(TAG, "presetList[%d]=%d", i, presetList[i]);
	}

	int16_t segmentColor = 2;
	err = NVS_read_int16(my_handle, SEGMENT_COLOR, &segmentColor);
	ESP_LOGI(TAG, "NVS_read_int16=%d segmentColor=%d", err, segmentColor);

	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	wifi_init_sta();
	initialise_mdns();

	// Create Message Buffer
	xMessageBufferMain = xMessageBufferCreate(1024);
	configASSERT( xMessageBufferMain );

	// Get the local IP address
	tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
	char cparam0[64];
	sprintf(cparam0, "%s", ip4addr_ntoa(&ip_info.ip));

	// Create Task
	ws_server_start();
	xTaskCreate(&server_task, "server_task", 1024*2, (void *)cparam0, 9, NULL);
	xTaskCreate(&server_handle_task, "server_handle_task", 1024*3, NULL, 6, NULL);

	// Initialize TEA5767
	TEA5767_t ctrl_data;
	ESP_LOGI(TAG, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ESP_LOGI(TAG, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	radio_init(&ctrl_data, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);


	// Set inital frequence
	double current_freq;
#if CONFIG_FM_BAND_US
	if (presetFrequence != 0) {
		current_freq = presetFrequence / 10.0;
	} else {
		current_freq = TEA5767_US_FM_BAND_MIN; // go to station 87.5MHz
	}
#endif
#if CONFIG_FM_BAND_JP
	radio_set_japanese_band(&ctrl_data);
	if (presetFrequence != 0) {
		current_freq = presetFrequence / 10.0;
	} else {
		current_freq = TEA5767_JP_FM_BAND_MIN; // go to station 76.0MHz
	}
#endif
	ESP_LOGI(TAG, "current_freq=%f", current_freq);
	radio_set_frequency(&ctrl_data, current_freq);

	char cRxBuffer[512];
	char DEL = 0x04;
	char outBuffer[64];

	int search_mode = 0;
	int search_direction = 0;
	unsigned char radio_status[5];

	while (1) {
		size_t readBytes = xMessageBufferReceive(xMessageBufferMain, cRxBuffer, sizeof(cRxBuffer), 1000/portTICK_PERIOD_MS);
		ESP_LOGD(TAG, "readBytes=%d", readBytes);
		if (readBytes == 0) {
			//radio_read_status(&ctrl_data, radio_status);
			if (radio_read_status(&ctrl_data, radio_status) == 1) {
				//double current_freq =	floor (radio_frequency_available (&ctrl_data, radio_status) / 100000 + .5) / 10;
				current_freq = floor (radio_frequency_available (&ctrl_data, radio_status) / 100000 + .5) / 10;
				int stereo = radio_stereo(&ctrl_data, radio_status);
				int signal_level = radio_signal_level(&ctrl_data, radio_status);
				ESP_LOGI(TAG, "current_freq=%f stereo=%d signal_level=%d/15", current_freq, stereo, signal_level);

				sprintf(outBuffer,"STATUS%c%f%c%d%c%d", DEL, current_freq, DEL, stereo, DEL, signal_level);
				ESP_LOGD(TAG, "outBuffer=[%s]", outBuffer);
				ws_server_send_text_all(outBuffer,strlen(outBuffer));
			}

			if (search_mode == 1) {
				ESP_LOGI(TAG, "search_direction=%d", search_direction);
				if (radio_process_search (&ctrl_data, radio_status, search_direction) == 1) {
					ESP_LOGI(TAG, "process_search success");
					search_mode = 0;
				} else {
					ESP_LOGW(TAG, "process_search fail");
				}
			}

		} else {
			cJSON *root = cJSON_Parse(cRxBuffer);
			if (cJSON_GetObjectItem(root, "id")) {
				char *id = cJSON_GetObjectItem(root,"id")->valuestring;
				ESP_LOGI(TAG, "id=%s",id);

				if ( strcmp (id, "init") == 0) {
#if 0
					sprintf(outBuffer,"HEAD%c%s", DEL, "ESP32 RADIO");
					ESP_LOGD(TAG, "outBuffer=[%s]", outBuffer);
					ws_server_send_text_all(outBuffer,strlen(outBuffer));
#endif

					sprintf(outBuffer,"COLOR%c%d", DEL, segmentColor);
					ESP_LOGD(TAG, "outBuffer=[%s]", outBuffer);
					ws_server_send_text_all(outBuffer,strlen(outBuffer));

					ESP_LOGI(TAG, "presetListCount=%u", presetListCount);
					for(int i=0;i<presetListCount;i++) {
						ESP_LOGI(TAG, "presetList[%d]=%d", i, presetList[i]);
						if (presetList[i] == presetFrequence) {
							sprintf(outBuffer,"PRESET*10%c%d%c%d", DEL, presetList[i], DEL, 1);
						} else {
							sprintf(outBuffer,"PRESET*10%c%d%c%d", DEL, presetList[i], DEL, 0);
						}
						ESP_LOGD(TAG, "outBuffer=[%s]", outBuffer);
						ws_server_send_text_all(outBuffer,strlen(outBuffer));
					}
				}

				if ( strcmp (id, "searchup-request") == 0) {
					search_mode = 1;
					search_direction = TEA5767_SEARCH_DIR_UP;
					radio_search_up(&ctrl_data, radio_status);
				}

				if ( strcmp (id, "searchdown-request") == 0) {
					search_mode = 1;
					search_direction = TEA5767_SEARCH_DIR_DOWN;
					radio_search_down(&ctrl_data, radio_status);
				}

				if ( strcmp (id, "preset-request") == 0 && presetListCount < 9) {
					char *value = cJSON_GetObjectItem(root,"value")->valuestring;
					ESP_LOGI(TAG, "preset-request value=%s",value);
					sprintf(outBuffer,"PRESET%c%s", DEL, value);
					ESP_LOGD(TAG, "outBuffer=[%s]", outBuffer);
					ws_server_send_text_all(outBuffer,strlen(outBuffer));

					char* end;
					double request_freq = strtod(value, &end);
					if (errno == ERANGE) {
						ESP_LOGE(TAG, "preset-request strtod error");
					} else if (value == end) {
						ESP_LOGW(TAG, "preset-request can't convert");
					} else {
						int16_t int_freq = request_freq * 10;
						ESP_LOGI(TAG, "request_freq=%f int_freq=%d", request_freq, int_freq);
						ESP_LOGI(TAG, "presetListCount=%u", presetListCount);
						presetList[presetListCount] = int_freq;
						presetListCount++;
						err = NVS_write_blob(my_handle, PRESET_LIST, presetList, presetListCount);
						ESP_LOGI(TAG, "NVS_write_blob=%d", err);

						// Save to NVS
						err = NVS_write_int16(my_handle, PRESET_FREQ, int_freq);
						ESP_LOGI(TAG, "NVS_write_int16=%d, int_freq=%d", err, int_freq);
					}
				}

				if ( strcmp (id, "jump-request") == 0) {
					char *value = cJSON_GetObjectItem(root,"value")->valuestring;
					ESP_LOGI(TAG, "jump-request value=%s",value);
					char* end;
					double request_freq = strtod(value, &end);
					if (errno == ERANGE) {
						ESP_LOGE(TAG, "jump-request strtod error");
					} else if (value == end) {
						ESP_LOGW(TAG, "jump-request can't convert");
					} else {
						ESP_LOGI(TAG, "request_freq=%f", request_freq);
						radio_set_frequency(&ctrl_data, request_freq);
					}
				}

				if ( strcmp (id, "write-request") == 0) {
					char *value = cJSON_GetObjectItem(root,"value")->valuestring;
					ESP_LOGI(TAG, "write-request value=%s",value);
					char* end;
					double request_freq = strtod(value, &end);
					if (errno == ERANGE) {
						ESP_LOGE(TAG, "write-request strtod error");
					} else if (value == end) {
						ESP_LOGW(TAG, "write-request can't convert");
					} else {
						ESP_LOGI(TAG, "request_freq=%f", request_freq);
						int16_t int_freq = request_freq * 10;
						// Save to NVS
						err = NVS_write_int16(my_handle, PRESET_FREQ, int_freq);
						ESP_LOGI(TAG, "NVS_write_int16=%d, int_freq=%d", err, int_freq);
					}
				}

				if ( strcmp (id, "color-request") == 0) {
					segmentColor++;
					if (segmentColor == 7) {
						segmentColor = 1;
					}
					sprintf(outBuffer,"COLOR%c%d", DEL, segmentColor);
					ESP_LOGD(TAG, "outBuffer=[%s]", outBuffer);
					ws_server_send_text_all(outBuffer,strlen(outBuffer));

					// Save to NVS
					err = NVS_write_int16(my_handle, SEGMENT_COLOR, segmentColor);
					ESP_LOGI(TAG, "NVS_write_int16=%d, int_freq=%d", err, segmentColor);
				}

			} // end if

			// Delete a cJSON structure
			cJSON_Delete(root);
		}

	
	} // end while

	// Close NVS
	nvs_close(my_handle);
}
