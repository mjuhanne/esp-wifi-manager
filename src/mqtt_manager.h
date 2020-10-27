/*
Copyright (c) 2020 Marko Juhanne

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

@file mqtt_manager.h
@author Marko Juhanne
@brief Defines all functions necessary for esp32/8266 to connect to a MQTT server
*/

/*
#define MAX_MQTT_URI_SIZE 64
#define MAX_MQTT_USERNAME_SIZE 32
#define MAX_MQTT_PWD_SIZE 32
*/
#include "mqtt_config.h"

#define MQTT_MANAGER_TASK_PRIORITY			CONFIG_MQTT_MANAGER_TASK_PRIORITY

#define MQTT_MANAGER_RETRY_TIMER			CONFIG_MQTT_MANAGER_RETRY_TIMER


/**
 * @brief Defines the maximum length in bytes of a JSON representation of the MQTT information
 * assuming all ips are 4*3 digits, and all characters in the ssid require to be escaped.
 * example: {"ssid":"abcdefghijklmnopqrstuvwxyz012345","ip":"192.168.1.119","netmask":"255.255.255.0","gw":"192.168.1.1","urc":99}
 * Run this JS (browser console is easiest) to come to the conclusion that 159 is the worst case.
 * ```
 * var a = {"ssid":"abcdefghijklmnopqrstuvwxyz012345","ip":"255.255.255.255","netmask":"255.255.255.255","gw":"255.255.255.255","urc":99};
 * // Replace all ssid characters with a double quote which will have to be escaped
 * a.ssid = a.ssid.split('').map(() => '"').join('');
 * console.log(JSON.stringify(a).length); // => 158 +1 for null
 * console.log(JSON.stringify(a)); // print it
 * ```
 */
// TODO
#define JSON_MQTT_INFO_SIZE 					256


/**
 * @brief simplified reason codes for a MQTT connection status update
 *
 */
typedef enum mqtt_update_reason_code_t {
	UPDATE_MQTT_NO_CONFIG = 0,
	UPDATE_MQTT_CONNECTION_OK = 1,
	UPDATE_MQTT_USER_DISCONNECT = 2,
	UPDATE_MQTT_FAILED_ATTEMPT = 3,
	UPDATE_MQTT_LOST_CONNECTION = 4
} mqtt_update_reason_code_t;

/**
 * @brief Defines the complete list of all messages that the mqtt_manager can process.
 *
 * Some of these message are events ("EVENT"), and some of them are action ("ORDER")
 * Each of these messages can trigger a callback function and each callback function is stored
 * in a function pointer array for convenience. Because of this behavior, it is extremely important
 * to maintain a strict sequence and the top level special element 'MESSAGE_CODE_COUNT'
 *
 * @see mqtt_manager_set_callback
 */
typedef enum mqtt_message_code_t {
	MM_NONE = 0,
	MM_EVENT_STA_CONNECTED = 1,
	MM_EVENT_STA_GOT_IP = 2,
	MM_EVENT_STA_IP_CHANGED = 3,
	MM_EVENT_STA_DISCONNECTED = 4,
	MM_ORDER_CONNECT = 5,
	MM_ORDER_DISCONNECT = 6,
	MM_EVENT_MQTT_CONNECTED = 7,
	MM_EVENT_MQTT_DISCONNECTED = 8,
	MM_EVENT_MQTT_ERROR = 9,
	MM_EVENT_MQTT_EVENT = 10, 
	MM_MESSAGE_CODE_COUNT = 11 /* important for the callback array */
} mqtt_message_code_t;


/**
 * @brief Structure used to store one message in the queue.
 */
typedef struct{
	mqtt_message_code_t code;
	void *param;
} mqtt_queue_message;

typedef struct mqtt_config_t{
	char uri[MQTT_MAX_HOST_LEN];
	char username[MQTT_MAX_USERNAME_LEN];
	char password[MQTT_MAX_PASSWORD_LEN];
	bool auto_reconnect;
} mqtt_config_t;
//extern struct mqtt_settings_t mqtt_settings;

/**
 * @brief Tries to get access to json buffer mutex.
 *
 * The HTTP server can try to access the json to serve clients while the mqtt manager thread can try
 * to update it. These two tasks are synchronized through a mutex.
 *
 * The mutex is used by the MQTT connection status json.\n
 *
 * This is a simple wrapper around freeRTOS function xSemaphoreTake.
 *
 * @param xTicksToWait The time in ticks to wait for the semaphore to become available.
 * @return true in success, false otherwise.
 */
bool mqtt_manager_lock_json_buffer(TickType_t xTicksToWait);

/**
 * @brief Releases the json buffer mutex.
 */
void mqtt_manager_unlock_json_buffer();


void mqtt_manager_set_callback(mqtt_message_code_t message_code, void (*func_ptr)(void*) );

int mqtt_manager_publish(  const char *topic, const char *data, int len, int qos, int retain );
void mqtt_manager_subscribe( const char * topic );
void mqtt_manager_unsubscribe( const char * topic );

void mqtt_manager_set_auto_reconnect(bool reconnect);

void mqtt_manager_connect_async();
void mqtt_manager_disconnect_async();

const char * mqtt_manager_get_uri();

void mqtt_manager_set_uri(const char * uri);
void mqtt_manager_set_username(const char * username);
void mqtt_manager_set_password(const char * pwd);

bool mqtt_manager_fetch_config();
esp_err_t mqtt_manager_save_config();

char* mqtt_manager_get_info_json();

void mqtt_manager_start();



