// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license.

#include "Arduino.h"
#include "AzureIotHub.h"
#include "DevKitMQTTClient.h"

#include <ArduinoJson.h>

#include "../inc/iotCentral.h"
#include "../inc/config.h"
#include "../inc/iotHubClient.h"
#include "../inc/stats.h"
#include "../inc/utility.h"
#include "../inc/wifi.h"

#define MAX_CALLBACK_COUNT 32

// forward declarations
static void receiveMessageCallback(const char *text, int length);
static void deviceTwinGetStateCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payLoad, int size);
static int deviceDirectMethodCallback(const char *methodName, const unsigned char *payLoad, int size, unsigned char **response, int *response_size);
static void deviceTwinConfirmationCallback(int status_code);

typedef struct TWIN_PROPERTY_REPORTED_TAG {
    char* name;
    char* value;
    signed int version;

    char* status;
    int statusCode;
} TWIN_PROPERTY_REPORTED;

typedef struct CALLBACK_LOOKUP_TAG {
    char* name;
    methodCallback callback;
} CALLBACK_LOOKUP;

static CALLBACK_LOOKUP methodCallbackList[MAX_CALLBACK_COUNT];
static int methodCallbackCount = 0;
static CALLBACK_LOOKUP desiredCallbackList[MAX_CALLBACK_COUNT];
static int desiredCallbackCount = 0;
static String deviceId;
static String hubName;

Queue<TWIN_PROPERTY_REPORTED, 16> queuePropertyReported;

void initIotHubClient(bool traceOn) {
    String connString = readConnectionString();
    deviceId = connString.substring(connString.indexOf("DeviceId=") + 9, connString.indexOf(";SharedAccess"));
    hubName = connString.substring(connString.indexOf("HostName=") + 9, connString.indexOf(";DeviceId="));
    hubName = hubName.substring(0, hubName.indexOf("."));

    DevKitMQTTClient_Init(true, traceOn);

    // Setting Message call back, so we can receive Commands.
    DevKitMQTTClient_SetMessageCallback(receiveMessageCallback);

    // Setting twin call back, so we can receive desired properties.
    DevKitMQTTClient_SetDeviceTwinCallback(deviceTwinGetStateCallback);

    // Setting twin direct method callback, so we can receive direct method calls.
    DevKitMQTTClient_SetDeviceMethodCallback(deviceDirectMethodCallback);

    // Setting report confirmation callback.
    DevKitMQTTClient_SetReportConfirmationCallback(deviceTwinConfirmationCallback);
}

bool sendTelemetry(const char *payload) {
    EVENT_INSTANCE* message = DevKitMQTTClient_Event_Generate(payload, MESSAGE);

    // add a timestamp to the message - illustrated for the use in batching
    time_t seconds = time(NULL);
    char *temp = ctime(&seconds);
    temp[strlen(temp) - 1] = 0;   // There is a new line character ('\n') at the end of the string which will disturb the json string, so remove it
    DevKitMQTTClient_Event_AddProp(message, "timestamp", temp);
    return DevKitMQTTClient_SendEventInstance(message);
}

bool sendReportedProperty(const char *payload) {
    EVENT_INSTANCE* message = DevKitMQTTClient_Event_Generate(payload, STATE);
    return DevKitMQTTClient_SendEventInstance(message);
}

// register callbacks for direct and cloud to device messages
bool registerMethod(const char *methodName, methodCallback callback) {
    if (methodCallbackCount < MAX_CALLBACK_COUNT) {
        methodCallbackList[methodCallbackCount].name = strdup(methodName);
        methodCallbackList[methodCallbackCount].callback = callback;
        methodCallbackCount++;
        return true;
    } else {
        return false;
    }
}

// register callbacks for desired properties
bool registerDesiredProperty(const char *propertyName, methodCallback callback) {
    if (desiredCallbackCount < MAX_CALLBACK_COUNT) {
        desiredCallbackList[desiredCallbackCount].name = strdup(propertyName);
        desiredCallbackList[desiredCallbackCount].callback = callback;
        desiredCallbackCount++;
        return true;
    } else {
        return false;
    }
}

void closeIotHubClient(void)
{
    DevKitMQTTClient_Close();
}

static void receiveMessageCallback(const char *text, int length)
{
    if (text == NULL || length < 1)
    {
        return;
    }
    char *buffer = (char *)calloc(length + 1, 1);
    memcpy(buffer, text, length);

    // message format expected:
    // {
    //     "methodName" : "<method name>",
    //     "payload" : {    
    //         "input1": "someInput",
    //         "input2": "anotherInput"
    //         ...
    //     }
    // }
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(buffer);
    const char *methodName = root["methodName"];
    int status = 0;
    char *methodResponse;
    
    // lookup if the method has been registered to a function
    for(int i = 0; i < methodCallbackCount; i++) {
        if (_stricmp(methodName, methodCallbackList[i].name) == 0) {
            const char* params = root["payload"];
            status = methodCallbackList[i].callback(params, strlen(params), &methodResponse, NULL);
            break;
        }
    }
    free(buffer);
}

static int deviceDirectMethodCallback(const char *methodName, const unsigned char *payLoad, int size, unsigned char **response, int *response_size)
{
    // message format expected:
    // {
    //     "methodName": "reboot",
    //     "responseTimeoutInSeconds": 200,
    //     "payload": {
    //         "input1": "someInput",
    //         "input2": "anotherInput"
    //         ...
    //     }
    // }

    int status = 0;
    char* methodResponse;
    
    char *buffer = (char *)calloc(size + 1, 1);
    memcpy(buffer, payLoad, size);

    // lookup if the method has been registered to a function
    for(int i = 0; i < methodCallbackCount; i++) {
        if (_stricmp(methodName, methodCallbackList[i].name) == 0) {
            status = methodCallbackList[i].callback((const char*)buffer, size + 1, &methodResponse, NULL);
            break;
        }
    }

    Serial.printf("Device Method %s called\r\n", methodName);
    free(buffer);

    return status;
}

// every desired property change gets echoed back as a reported property
void echoDesiredProperty(void) {
    while (true) {
        osEvent evt = queuePropertyReported.get(1);
        if (evt.status == osEventMessage) {
            char buff[400];
            TWIN_PROPERTY_REPORTED *propertyReported = (TWIN_PROPERTY_REPORTED*)evt.value.p;

            sprintf(buff, "{\"%s\":{\"value\":%s, \"statusCode\":%d, \"status\":\"%s\", \"desiredVersion\":%d}}", 
                propertyReported->name, propertyReported->value, propertyReported->statusCode, propertyReported->status, propertyReported->version);
            
            if (sendReportedProperty(buff)) {
                Serial.printf("Desired property %s successfully echoed back as a reported property\r\n", propertyReported->name);
                incrementReportedCount();
            } else {
                Serial.printf("Desired property %s failed to be echoed back as a reported property\r\n", propertyReported->name);
                incrementErrorCount();
            }
            free(propertyReported->name);
            free(propertyReported->value);
            free(propertyReported->status);
            free(propertyReported);
        }
        else {
            break;
        }
    }
 }

static void prepareReportedProperty(const char *propertyName, const char *value, int version, const char *status, int statusCode) {
    TWIN_PROPERTY_REPORTED *propertyReported = (TWIN_PROPERTY_REPORTED*)calloc(1, sizeof(TWIN_PROPERTY_REPORTED));
    propertyReported->name = strdup(propertyName);
    propertyReported->value = strdup(value);
    propertyReported->version = version;
    propertyReported->status = strdup(status);
    propertyReported->statusCode = statusCode;

    queuePropertyReported.put(propertyReported);
}

static void callDesiredCallback(const char *propertyName, JsonObject& jsonProperty) {
    int status = 0;
    char *methodResponse;
    
    // Parse the desired property
    char value[200];
    int version;
        
    if (jsonProperty.containsKey("desired")) {
        jsonProperty["desired"][propertyName]["value"].printTo(value, sizeof(value));
        version = jsonProperty["desired"]["$version"].as<signed int>();
    } else {
        jsonProperty[propertyName]["value"].printTo(value, sizeof(value));
        version = jsonProperty["$version"].as<signed int>();
    }

    for(int i = 0; i < desiredCallbackCount; i++) {
        if (_stricmp(propertyName, desiredCallbackList[i].name) == 0) {
            status = desiredCallbackList[i].callback(NULL, 0, &methodResponse, NULL);
            prepareReportedProperty(propertyName, value, version, methodResponse, status);
            free(methodResponse);
            break;
        }
    }
}

static void deviceTwinGetStateCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payLoad, int size)
{
    if (payLoad == NULL || size < 1)
    {
        return;
    }
    
    char *buffer = (char *)calloc(size + 1, 1);
    memcpy(buffer, payLoad, size);
    
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(buffer);
    
    if (updateState == DEVICE_TWIN_UPDATE_PARTIAL) {
        Serial.println("Processing desired property");
        for (JsonObject::iterator it = root.begin(); it != root.end(); ++it) {
            if (it->key[0] != '$') {
                callDesiredCallback(it->key, root);
            }
        }
    } else {
        // loop through all the desired properties
        // look to see if the desired property has an associated reported property
        // if so look if the versions match, if they match do nothing
        // if they don't match then call the associated callback for the desired property
        Serial.println("Processing complete twin");      
        JsonObject& desired = root["desired"];
        JsonObject& reported = root["reported"];

        for (JsonObject::iterator it = desired.begin(); it != desired.end(); ++it) {
            if (it->key[0] != '$') {
                if (reported.containsKey(it->key)) {
                    if(reported[it->key]["value"] == desired[it->key]["value"]) {
                        // Property found in reported and values match
                        Serial.printf("key: %s found in reported and values match\r\n", it->key);
                        // Move next
                        continue;
                    }
                }
                // Not exist in reported or values do not mismatch
                Serial.printf("key: %s either not found in reported or values do not match\r\n", it->key);
                callDesiredCallback(it->key, root);
            }
        }
    }
    
    free(buffer);
}

static void deviceTwinConfirmationCallback(int status_code) {
    LogInfo("DeviceTwin CallBack: Status_code = %u", status_code);
}

// scrolling text global variables
static int displayCharPos = 0; 
static int waitCount = 3;
static String displayHubName;

void displayDeviceInfo() {
    char buff[64]; 
    // code to scroll the larger hubname if it exceeds 16 characters
    if (waitCount >= 3) {
        waitCount = 0;
        if (hubName.length() > 16) {
            displayHubName = hubName.substring(displayCharPos);
            if (displayCharPos + 16 >= hubName.length())
                displayCharPos = -1;
            displayCharPos++;
        } else {
            displayHubName = hubName;
        }
    } else {
        waitCount++;
    }

    sprintf(buff, "Device:\r\n%s\r\n%.16s\r\nf/w: %s", deviceId.c_str(), displayHubName.c_str(), FW_VERSION);
    Screen.print(0, buff);
}
