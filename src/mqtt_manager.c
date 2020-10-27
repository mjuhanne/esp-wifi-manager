#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "mqtt_client.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_sync.h"

#include "esp_wifi.h"
#include "esp_log.h"

#include "wifi_manager.h"
#include "mqtt_manager.h"

static const char *TAG = "MQTT";

const char mqtt_manager_nvs_namespace[] = "espmqttmgr";

mqtt_config_t mqtt_config;

EventGroupHandle_t mqtt_conn_event_group;

/* @brief software timer to wait between each connection retry. */
TimerHandle_t mqtt_manager_retry_timer = NULL;


// connection status event bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_GOT_IP_BIT  BIT1
#define MQTT_CONNECTED_BIT  BIT2
#define MQTT_DISCONNECT_REQ_BIT BIT3


#define MAX_NODE_NAME_LEN 32
#define MAX_ERROR_STRING_LEN 64

static char mqtt_error_string[MAX_ERROR_STRING_LEN];

/* objects used to manipulate the main queue of events */
QueueHandle_t mqtt_manager_queue;

SemaphoreHandle_t mqtt_manager_json_mutex = NULL;

static esp_mqtt_client_handle_t mqtt_client;

// NVS handle
extern nvs_handle storage_handle;

TaskHandle_t task_mqtt_manager;

/* @brief Array of callback function pointers */
static void (**cb_ptr_arr)(void*) = NULL;


char *mqtt_info_json = NULL;


void mqtt_manager_subscribe( const char * topic ) {
	esp_mqtt_client_subscribe(mqtt_client, topic, 0);
}

void mqtt_manager_unsubscribe( const char * topic ) {
	esp_mqtt_client_unsubscribe(mqtt_client, topic);
}

int mqtt_manager_publish(  const char *topic, const char *data, int len, int qos, int retain ) {
    return esp_mqtt_client_publish(mqtt_client, topic, data, len, qos, retain);   
}

const char * mqtt_manager_get_uri() {
	return mqtt_config.uri;
}

void mqtt_manager_set_uri(const char * uri) {
	ESP_LOGI(TAG,"URI: %s", uri);
	strncpy(mqtt_config.uri, uri, MQTT_MAX_HOST_LEN);
}
void mqtt_manager_set_username(const char * username) {
	ESP_LOGI(TAG,"username: %s", username);
	strncpy(mqtt_config.username, username, MQTT_MAX_USERNAME_LEN);
}

void mqtt_manager_set_password(const char * pwd) {
	ESP_LOGI(TAG,"password: %s", pwd);
	strncpy(mqtt_config.password, pwd, MQTT_MAX_PASSWORD_LEN);
}


bool mqtt_manager_do_fetch_config(mqtt_config_t * target_config) {
	nvs_handle handle;
	esp_err_t esp_err;

	esp_err = nvs_open(mqtt_manager_nvs_namespace, NVS_READONLY, &handle);

	if(esp_err != ESP_OK){
		return false;
	}

	size_t sz = sizeof(mqtt_config_t);
	esp_err = nvs_get_blob(handle, "mqtt_config", target_config, &sz);
	if(esp_err == ESP_OK) {
		ESP_LOGI(TAG, "mqtt_manager_fetch_config: URI:'%s' username:'%s' password:'%s' auto-reconnect: %d",
				target_config->uri, target_config->username, target_config->password, target_config->auto_reconnect);
		if (strcmp(mqtt_config.uri,"")==0)
			esp_err = ESP_ERR_NOT_FOUND;
	} else {
		esp_err = ESP_ERR_NOT_FOUND;
	}

	if (esp_err != ESP_OK)
		memset(&mqtt_config, 0x00, sizeof(sz));

	nvs_close(handle);
	return (esp_err == ESP_OK);
}



bool mqtt_manager_fetch_config(){
	bool res = false;
	if(nvs_sync_lock( portMAX_DELAY )){
		res = mqtt_manager_do_fetch_config(&mqtt_config);
		nvs_sync_unlock();
	} 	
	return res;

}

bool mqtt_manager_config_changed() {
	bool changed = false;
	if ( nvs_sync_lock( portMAX_DELAY )) {

		// lets check if write is really needed 
		mqtt_config_t tmp_config;
		if (mqtt_manager_do_fetch_config(&tmp_config)) {
			ESP_LOGI(TAG,"config changed: compare %d bytes, addr %d %d ", sizeof(mqtt_config), (int)&tmp_config, (int)&mqtt_config);
			if (memcmp(&tmp_config, &mqtt_config, sizeof(mqtt_config))!=0) 
				changed = true;
		} else {
			// Could not fetch config -> assume changed
			changed = true;
		}
		nvs_sync_unlock();
	}
	return changed;
}


esp_err_t mqtt_manager_save_config(){

	nvs_handle handle;
	esp_err_t esp_err;
	//size_t sz;

	bool change = mqtt_manager_config_changed();
	if (!change) {		
		ESP_LOGI(TAG, "MQTT config was not saved to flash because no change has been detected.");
		return ESP_OK;
	}

	ESP_LOGI(TAG, "About to save config to flash!!");

	if ( nvs_sync_lock( portMAX_DELAY )){

		esp_err = nvs_open(mqtt_manager_nvs_namespace, NVS_READWRITE, &handle);
		if (esp_err != ESP_OK){
			nvs_sync_unlock();
			return esp_err;
		}

		esp_err = nvs_set_blob(handle, "mqtt_config", &mqtt_config, sizeof(mqtt_config));
		if (esp_err != ESP_OK){
			nvs_sync_unlock();
			return esp_err;
		}
		if ( nvs_commit(handle) == ESP_OK ) {
			ESP_LOGD(TAG, "mqtt_manager wrote settings: URI:%s username:%s password:%s",
				mqtt_config.uri, mqtt_config.username, mqtt_config.password);
		} else {
			ESP_LOGE(TAG,"Commit failed!");
		}

		nvs_close(handle);
		nvs_sync_unlock();

	}
	else{
		ESP_LOGE(TAG, "mqtt_manager_save_config failed to acquire nvs_sync mutex");
	}

	return ESP_OK;
}



void mqtt_manager_destroy(){

	vTaskDelete(task_mqtt_manager);
	task_mqtt_manager = NULL;

	/* RTOS objects */
	vSemaphoreDelete(mqtt_manager_json_mutex);
	mqtt_manager_json_mutex = NULL;
	vEventGroupDelete(mqtt_conn_event_group);
	mqtt_conn_event_group = NULL;
	vQueueDelete(mqtt_manager_queue);
	mqtt_manager_queue = NULL;

}

BaseType_t mqtt_manager_send_message(mqtt_message_code_t code, void *param){
	mqtt_queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSend( mqtt_manager_queue, &msg, portMAX_DELAY);
}


bool mqtt_manager_lock_json_buffer(TickType_t xTicksToWait){
	if(mqtt_manager_json_mutex){
		if( xSemaphoreTake( mqtt_manager_json_mutex, xTicksToWait ) == pdTRUE ) {
			return true;
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}

}
void mqtt_manager_unlock_json_buffer(){
	xSemaphoreGive( mqtt_manager_json_mutex );
}

void mqtt_manager_clear_json(){
	strcpy(mqtt_info_json, "{}\n");
}

char* mqtt_manager_get_info_json() {
	return mqtt_info_json;
}


void mqtt_manager_generate_json(mqtt_update_reason_code_t update_reason_code, const char * error_string){

	const char *json_format = "{\"uri\":\"%s\",\"urc\":%d, \"error\":\"%s\"}\n";
	const char empty_str[1] = "";
	const char * error_str;

	if (error_string)
		error_str = error_string; 
	else
		error_str = empty_str;

	memset(mqtt_info_json, 0x00, JSON_MQTT_INFO_SIZE);

	snprintf( mqtt_info_json, JSON_MQTT_INFO_SIZE, json_format,
			mqtt_config.uri,
			(int)update_reason_code,
			error_str);
	ESP_LOGI(TAG,"json %s", mqtt_info_json);
}


/**
 * @brief Standard wifi event handler
 */
static void mqtt_manager_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){

	if (event_base == WIFI_EVENT){

		switch(event_id){

		case WIFI_EVENT_STA_CONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
            xEventGroupSetBits(mqtt_conn_event_group, WIFI_CONNECTED_BIT);
			mqtt_manager_send_message( MM_EVENT_STA_CONNECTED, NULL );
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
			ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
            xEventGroupClearBits(mqtt_conn_event_group, WIFI_CONNECTED_BIT);
			mqtt_manager_send_message( MM_EVENT_STA_DISCONNECTED, NULL );
			break;
		default:
			break;
		}

	}
	else if(event_base == IP_EVENT){

		switch(event_id){

		/* This event arises when the DHCP client successfully gets the IPV4 address from the DHCP server,
		 * or when the IPV4 address is changed. The event means that everything is ready and the application can begin
		 * its tasks (e.g., creating sockets).
		 * The IPV4 may be changed because of the following reasons:
		 *    The DHCP client fails to renew/rebind the IPV4 address, and the station’s IPV4 is reset to 0.
		 *    The DHCP client rebinds to a different address.
		 *    The static-configured IPV4 address is changed.
		 * Whether the IPV4 address is changed or NOT is indicated by field ip_change of ip_event_got_ip_t.
		 * The socket is based on the IPV4 address, which means that, if the IPV4 changes, all sockets relating to this
		 * IPV4 will become abnormal. Upon receiving this event, the application needs to close all sockets and recreate
		 * the application when the IPV4 changes to a valid one. */
		case IP_EVENT_STA_GOT_IP:
			ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
	        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

	        if ((xEventGroupGetBits(mqtt_conn_event_group) & WIFI_GOT_IP_BIT) && (event->ip_changed)) {
	            ESP_LOGW(TAG,"IP address changed!");
    	        // signal this to MQTT thread
        	    mqtt_manager_send_message( MM_EVENT_STA_IP_CHANGED, NULL);
	        } else {
	            xEventGroupSetBits(mqtt_conn_event_group, WIFI_GOT_IP_BIT);
				mqtt_manager_send_message( MM_EVENT_STA_GOT_IP, NULL );
	        }

			break;
		}
	}

}


void mqtt_manager_set_callback(mqtt_message_code_t message_code, void (*func_ptr)(void*) ){

	if(cb_ptr_arr && message_code < MM_MESSAGE_CODE_COUNT){
		cb_ptr_arr[message_code] = func_ptr;
	}
}


static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    //esp_mqtt_client_handle_t client = event->client;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
			ESP_LOGW(TAG," Heap: %d", esp_get_free_heap_size());
			mqtt_manager_send_message( MM_EVENT_MQTT_CONNECTED, NULL );
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
			mqtt_manager_send_message( MM_EVENT_MQTT_DISCONNECTED, NULL );
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
#ifdef ESP32
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN,"MQTT TLS ERROR. Code 0x%x", event->error_handle->esp_tls_last_esp_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                switch (event->error_handle->connect_return_code) {
                case MQTT_CONNECTION_REFUSE_PROTOCOL:
                	snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN, "Connection refused, bad protocol");
                    break;
                case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
                	snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN, "Connection refused, server unavailable");
                    break;
                case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
                	snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN, "Connection refused, bad username or password");
                    break;
                case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
                	snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN, "Connection refused, not authorized");
                    break;
                default:
                	snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN, "Connection refused, Unknown reason");
                    break;
                }
                ESP_LOGE(TAG,"MQTT ERROR: %s", mqtt_error_string);
            } else {
                ESP_LOGE(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
                snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN,"MQTT Unknown error. Code 0x%x", event->error_handle->error_type);
            }
#else
                snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN, "MQTT unspecified error");
#endif
			mqtt_manager_send_message( MM_EVENT_MQTT_ERROR, mqtt_error_string );
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
			ESP_LOGW(TAG," Heap: %d", esp_get_free_heap_size());
            break;
    }

	/* callback */
	if(cb_ptr_arr[ MM_EVENT_MQTT_EVENT ]) (*cb_ptr_arr[ MM_EVENT_MQTT_EVENT ])( event );

    return ESP_OK;
}


#ifdef ESP32
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}
#else
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
	return mqtt_event_handler_cb(event);
}
#endif

void mqtt_manager_connect_async() {
	mqtt_manager_send_message(MM_ORDER_CONNECT, NULL);
}

void mqtt_manager_disconnect_async() {
	mqtt_manager_send_message(MM_ORDER_DISCONNECT, NULL);
}

/*
void mqtt_manager_set_auto_reconnect(bool reconnect) {
	if (mqtt_config.disable_auto_reconnect == reconnect) {
		// setting changed	
		mqtt_config.disable_auto_reconnect = !reconnect;
		if (xEventGroupGetBits(mqtt_conn_event_group) & MQTT_CONNECTED_BIT) {

			// cannot change configuration while connected, have to reconnect
			mqtt_manager_disconnect_async();
			mqtt_manager_connect_async();
		}
	}
}
*/
void mqtt_manager_set_auto_reconnect(bool reconnect) {
	mqtt_config.auto_reconnect = reconnect;
	/*
	if (mqtt_config.auto_reconnect != reconnect) {
		// setting changed	
		mqtt_config.auto_reconnect = reconnect;
		if (xEventGroupGetBits(mqtt_conn_event_group) & MQTT_CONNECTED_BIT) {

			// cannot change configuration while connected, have to reconnect
			mqtt_manager_disconnect_async();
			mqtt_manager_connect_async();
		}
	}
	*/
}



void mqtt_manager_timer_retry_cb( TimerHandle_t xTimer ){

	ESP_LOGI(TAG, "Retry Timer Tick! Sending MM_ORDER_CONNECT");

	/* stop the timer */
	xTimerStop( xTimer, (TickType_t) 0 );

	if (!(xEventGroupGetBits(mqtt_conn_event_group) & MQTT_CONNECTED_BIT)) {
		/* Attempt to reconnect */
		mqtt_manager_send_message(MM_ORDER_CONNECT, NULL);
	}
}


void mqtt_manager_task( void * pvParameters ) {
	mqtt_queue_message msg;
	BaseType_t xStatus;

    ESP_LOGI(TAG,"[MQTT] Running on core #%d", xPortGetCoreID());


    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &mqtt_manager_wifi_event_handler, NULL));	
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &mqtt_manager_wifi_event_handler, NULL));


	/* main processing loop */
	for(;;){
		xStatus = xQueueReceive( mqtt_manager_queue, &msg, portMAX_DELAY );

		ESP_LOGW(TAG," Heap: %d", esp_get_free_heap_size());

		if( xStatus == pdPASS ){
			switch(msg.code){

				case MM_EVENT_STA_CONNECTED:{
					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;


				case MM_EVENT_STA_GOT_IP:{
					if (strcmp(mqtt_config.uri,"")!=0) {
						mqtt_manager_send_message(MM_ORDER_CONNECT, NULL);
					}
					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;

				case MM_EVENT_STA_IP_CHANGED:{
		            ESP_LOGI(TAG,"WiFi IP changed. ");

			        if (xEventGroupGetBits(mqtt_conn_event_group) & MQTT_CONNECTED_BIT) {
			        	xEventGroupClearBits(mqtt_conn_event_group, MQTT_CONNECTED_BIT);
		                // at this stage MQTT server is disconnected. Clean up 
		                if (esp_mqtt_client_stop(mqtt_client) != ESP_OK) {
		                    ESP_LOGE(TAG,"Warning. Could not stop MQTT client");
		                }
		                esp_mqtt_client_destroy(mqtt_client);
		            }
	                // Force new connection
					mqtt_manager_send_message(MM_ORDER_CONNECT, NULL);
					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;


				case MM_EVENT_STA_DISCONNECTED:{
		            ESP_LOGI(TAG,"WiFi disconnected.. ");

			        if (xEventGroupGetBits(mqtt_conn_event_group) & MQTT_CONNECTED_BIT) {
			        	xEventGroupClearBits(mqtt_conn_event_group, MQTT_CONNECTED_BIT);
		                // at this stage WIFI and MQTT is disconnected. Clean up 
		                if (esp_mqtt_client_stop(mqtt_client) != ESP_OK) {
		                    ESP_LOGE(TAG,"Warning. Could not stop MQTT client");
		                }
		                esp_mqtt_client_destroy(mqtt_client);
			        }
					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;

				case MM_ORDER_CONNECT: {
					if (!(xEventGroupGetBits(mqtt_conn_event_group) & MQTT_CONNECTED_BIT)) {
						if (strcmp(mqtt_config.uri,"") != 0) {					
				            ESP_LOGI(TAG,"Connecting to the MQTT server (%s) (reconnect=%d)..",mqtt_config.uri, mqtt_config.auto_reconnect );
				            esp_mqtt_client_config_t cfg;
				            memset((void*)&cfg, 0x00, sizeof(esp_mqtt_client_config_t));
				            cfg.uri = mqtt_config.uri;
				            cfg.username = mqtt_config.username;
				            cfg.password = mqtt_config.password;
				            cfg.disable_auto_reconnect = !mqtt_config.auto_reconnect;
							#ifndef ESP32
								cfg.event_handle = mqtt_event_handler;
							#endif
				            mqtt_client = esp_mqtt_client_init(&cfg);

				            if (mqtt_client == NULL) {
				                ESP_LOGE(TAG,"Could not init MQTT client!");
				                snprintf(mqtt_error_string, MAX_ERROR_STRING_LEN, "Could not init MQTT client!");
				                mqtt_manager_send_message(MM_EVENT_MQTT_ERROR, mqtt_error_string);
				            } else {
				            	#ifdef ESP32
									esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
								#endif
								ESP_LOGW(TAG," Heap: %d", esp_get_free_heap_size());
					            ESP_LOGI(TAG,"Starting MQTT client ..");
				                esp_mqtt_client_start(mqtt_client);
				            }
						} else {
							ESP_LOGE(TAG,"MQTT URI not set!");
						}
						/* callback */
						if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
					} else {
						ESP_LOGE(TAG,"Already connected!");
					}
				}
				break;

				case MM_ORDER_DISCONNECT: {
		            ESP_LOGI(TAG,"Discconnecting from the MQTT server ..");
	                if (esp_mqtt_client_stop(mqtt_client) != ESP_OK) {
	                    ESP_LOGE(TAG,"Warning. Could not stop MQTT client");
	                }
	                esp_mqtt_client_destroy(mqtt_client);

					// We don't get any DISCONNECTED event from MQTT client, so assume disconnect will succeed
					xEventGroupClearBits(mqtt_conn_event_group, MQTT_CONNECTED_BIT);
					mqtt_manager_generate_json(UPDATE_MQTT_USER_DISCONNECT,NULL);

					// Cancel also AP shutdown
					wifi_manager_set_auto_ap_shutdown(false);

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;

				case MM_EVENT_MQTT_CONNECTED:{
	                ESP_LOGI(TAG,"MQTT server connected!");

		            xEventGroupSetBits(mqtt_conn_event_group, MQTT_CONNECTED_BIT);
					if(mqtt_manager_lock_json_buffer( portMAX_DELAY )){
		                mqtt_manager_generate_json(UPDATE_MQTT_CONNECTION_OK,NULL);
						mqtt_manager_unlock_json_buffer();
					}

					// Now that we have successful connection, turn on auto reconnect and save settings to flash. 
					mqtt_manager_set_auto_reconnect(true); 
					mqtt_manager_save_config();
					
	                // It's OK to start ap shutdown timer now
					wifi_manager_set_auto_ap_shutdown(true);

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;

				case MM_EVENT_MQTT_DISCONNECTED:{

					EventBits_t uxBits;
					uxBits = xEventGroupGetBits(mqtt_conn_event_group);

					if (mqtt_manager_lock_json_buffer( portMAX_DELAY )) {
						if (uxBits & MQTT_CONNECTED_BIT) {
			                mqtt_manager_generate_json(UPDATE_MQTT_LOST_CONNECTION,NULL);
						} // Failed attempt will be handled below at MM_EVENT_MQTT_ERROR
						mqtt_manager_unlock_json_buffer();
					}

		            xEventGroupClearBits(mqtt_conn_event_group, MQTT_CONNECTED_BIT);

					ESP_LOGI(TAG,"MQTT server disconnected! Cancel auto ap shutdown..");
	        		wifi_manager_set_auto_ap_shutdown(false);

	                if (mqtt_config.auto_reconnect) {
	                	ESP_LOGI(TAG,"Setting timer to reconnect..");
						/* Start the timer that will try to connect again */
						xTimerStart( mqtt_manager_retry_timer, (TickType_t)0 );
	                }

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;

				case MM_EVENT_MQTT_ERROR:{
	                ESP_LOGI(TAG,"MQTT error!");

	                mqtt_manager_generate_json(UPDATE_MQTT_FAILED_ATTEMPT,(char*)msg.param);

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				}
				break;


				default:
				break;
			}
		}


    ESP_LOGW(TAG, "Stack: %d", uxTaskGetStackHighWaterMark(NULL));

    }
}


void mqtt_manager_start() {

	ESP_LOGI(TAG,"MQTT manager start");
	//esp_log_level_set("httpd_parse", ESP_LOG_DEBUG);

	mqtt_manager_queue = xQueueCreate( 3, sizeof( mqtt_queue_message) );
	mqtt_conn_event_group = xEventGroupCreate();

	mqtt_manager_json_mutex = xSemaphoreCreateMutex();
	mqtt_info_json = (char*)malloc(sizeof(char) * JSON_MQTT_INFO_SIZE);
	mqtt_manager_clear_json();

	if (mqtt_manager_fetch_config()) {
		ESP_LOGI(TAG,"MQTT config reloaded succesfully. Starting AP temporarily...");
		wifi_manager_set_auto_ap_shutdown(true);
		wifi_manager_send_message(WM_ORDER_START_AP, NULL);
	} else {
		ESP_LOGW(TAG,"No MQTT config found. Starting AP..");
	    memset(&mqtt_config, 0x00, sizeof(esp_mqtt_client_config_t));
		// We don't want to reconnect before making sure that this config is valid
	    mqtt_config.auto_reconnect = false;	
		wifi_manager_send_message(WM_ORDER_START_AP, NULL);
		// Let's keep AP alive for now
		wifi_manager_set_auto_ap_shutdown(false);
	}

	/* create timer for to keep track of retries */
	mqtt_manager_retry_timer = xTimerCreate( NULL, pdMS_TO_TICKS(MQTT_MANAGER_RETRY_TIMER), pdFALSE, ( void * ) 0, mqtt_manager_timer_retry_cb);


	cb_ptr_arr = malloc(sizeof(void (*)(void*)) * MM_MESSAGE_CODE_COUNT);
	for(int i=0; i<MM_MESSAGE_CODE_COUNT; i++){
		cb_ptr_arr[i] = NULL;
	}

    ESP_LOGI(TAG,"Create MQTT manager task..");
	xTaskCreate(&mqtt_manager_task, "task_mqtt_manager", 4096, NULL, MQTT_MANAGER_TASK_PRIORITY, &task_mqtt_manager);
}
