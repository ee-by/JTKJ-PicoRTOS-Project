#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 

//State machine states
enum state { IDLE=1, READING_DATA, SENDING_DATA};
enum state programState = IDLE;

//Global variables here




//Initialization of the IMU sensor and reading of data


static void btn_fxn(uint gpio, uint32_t eventMask) {
    toggle_led();
}
//Do teh sensor task
static void sensor_task(void *arg){
    (void)arg;
    init_ICM42670();
    while(1){
        
        if (programState == IDLE) {
            position = ICM42670_read_sensor_data();
            programState = SENDING_DATA;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//Do the task that sends data
static void send_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // Modify with application code here.
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}





int main() {
    //Initialize everything first
    stdio_init_all();
    
    
    // Uncomment this lines if you want to wait till the serial monitor is connected
    /*while (!stdio_usb_connected()){
        sleep_ms(10);
    }*/ 
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.

    //Create handle
    TaskHandle_t hSensorTask, hSendTask = NULL;
    
    // Create the tasks with xTaskCreate
    //Sensortask for reading sensors
    BaseType_t result = xTaskCreate(sensor_task,       // (en) Task function
                "sensor",              // (en) Name of the task 
                DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,               // (en) Arguments of the task 
                2,                  // (en) Priority of this task
                &hSensorTask);    // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Sensor Task creation failed\n");
        return 0;
    }
    //Task for sending data
    result = xTaskCreate(sending_task,  // (en) Task function
                "send",              // (en) Name of the task 
                DEFAULT_STACK_SIZE,   // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                 // (en) Arguments of the task 
                2,                    // (en) Priority of this task
                &hSendTask);         // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Send Task creation failed\n");
        return 0;

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}






//IDEAS




// button press to retrieve accelerometer data and...

// After button is pressed before accel. data is retrieved, a check for gyroscope data is done. If gyro data is high, accel. data is not retrieved. 
// If gyro data is low, accel. data is retrieved and...