#include <stdio.h>
#include <string.h>

#include <math.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 

#define gyro_threshold 35.0

//State machine states
enum state { IDLE=1, READING_DATA, SENDING_DATA};
volatile enum state programState = IDLE;

//Global variables here
int position;
uint8_t space = 0;
//Uncomment if we want to transfer to using this over position
//char symbol;

//Button interrupt function for both buttons
//The led toggles might be unnecessary
static void btn_fxn(uint gpio, uint32_t eventMask) {
    //If button 1 is pressed, the code continues to read the sensor data and send it
    if (gpio == BUTTON1) {
        toggle_led();
        programState = READING_DATA;
        toggle_led();
    }
    
    //If button 2 is pressed, the Pico sends a space
    else if (gpio == BUTTON2) {
        toggle_led();
        space = 1;
        toggle_led();
    }
}




//Do the sensor task
static void sensor_task(void *pvParameters){
    (void)pvParameters;

    //Introduce the variables
    float ax, ay, az, gx, gy, gz, t;

    //Initialize the IMU sensor
    if (init_ICM42670() == 0) {
        printf("ICM-42670P initialized successfully!\n");
        if (ICM42670_start_with_default_values() != 0){
            printf("ICM-42670P could not initialize accelerometer or gyroscope");
        }
        int _accel = ICM42670_startAccel(ICM42670_ACCEL_ODR_DEFAULT, ICM42670_ACCEL_FSR_DEFAULT);
        printf ("Accel return:  %d\n", _accel);
    }
        
    else {
        printf("Failed to initialize ICM-42670P.\n");
    }

    while(1){
            
        if (programState == READING_DATA) {
            if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0) {
                //printf("Accel: X=%f, Y=%f, Z=%f | Gyro: X=%f, Y=%f, Z=%f| Temp: %2.2f°C\n", ax, ay, az, gx, gy, gz, t);

                //Check if the Pico is still rotating using gyros absolute values
                //This check continues as long as the Pico is rotating and then continues to check the position
                if ((fabsf(gx) <= gyro_threshold) && (fabsf(gy) <= gyro_threshold) && (fabsf(gz) <= gyro_threshold)) {

                

                    //In case any of the following positions isn't detected it will default to zero which we can check in send task
                    position = 0;

                    /*SHOULD WE USE THE SYMBOL VARIABLE OR THE POSITION
                    CHECK SENDTASK ALSO
                    THE SYMBOL COULD BE DETERMINED HERE IN SENSORTASK AND THEN THE SENDTASK WOULD JUST SEND
                    */

                    if ((az >= 0.7) || (az <= -0.7)) {
                        position = 1; //Lying on its side
                        //symbol = '.';
                    }
                    else if ((ay >= 0.7) || (ay <= -0.7)) {
                        position = 2; //Standing up
                        //symbol = '-';
                    }
                    else if ((ax >= 0.7) || (ax <= -0.7)) {
                        position = 3; //Lying on its side too. this is for testing purposes and should be combined with pos1 later
                        //symbol = '.';
                    }
                    
                    programState = SENDING_DATA;
                }
            }
            else {
                printf("Sensor rading failed\n");
                programState = IDLE;
            }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
}

//Do the task that sends data
static void send_task(void *arg){
    (void)arg;

    for(;;){
        if (space == 1) {
            printf(" \n");
            space = 0;
        }
        else if (programState == SENDING_DATA) {
            //For testing, should be removed later
            //printf("Sending position: %d\n", position);


            /*SHOULD WE USE THE SYMBOL VARIABLE OR THE POSITION
            CHECK SENSORTASK ALSO
            THE SYMBOL COULD BE DETERMINED IN SENSORTASK AND THE CODE HERE COULD BE LIKE:
            printf("%c", symbol);
            fflush(stdout);
            */
            if ((position == 1) || (position == 3)) {
                printf("-\n");
            }
            else if (position == 2) {
                printf(".\n");
            }
            else {
                printf("Unknown position \n");
            }
            space = 0;


            programState = IDLE;
        }
        //tight_loop_contents(); // Modify with application code here.
        vTaskDelay(pdMS_TO_TICKS(100));
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

    //Initialize red led and button1
    init_red_led();
    init_sw1();
    init_sw2();

    //Set button1 interrupt
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_fxn);
    gpio_set_irq_enabled(BUTTON2, GPIO_IRQ_EDGE_FALL, true);

    //Create handle
    TaskHandle_t hSensorTask = NULL;
    TaskHandle_t hSendTask = NULL;
    
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
    result = xTaskCreate(send_task,  // (en) Task function
                "send",              // (en) Name of the task 
                DEFAULT_STACK_SIZE,   // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                 // (en) Arguments of the task 
                2,                    // (en) Priority of this task
                &hSendTask);         // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Send Task creation failed\n");
        return 0;
    }

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}






//IDEAS




// button press to retrieve accelerometer data and...

// After button is pressed before accel. data is retrieved, a check for gyroscope data is done. If gyro data is high, accel. data is not retrieved. 
// If gyro data is low, accel. data is retrieved and...


/*
03.11.2026
Completed the tier 1 requirements
Completed sensor task and send task
Added global variables
Sensor task check if Pico is moving with gyro data before continuing to check accel data
Added button interrupts for button 1 and 2
Button1 starts the sensor and send tasks ending in a sent symbol ("-" or ".")
Button2 sends a space (" ")
Should the sending have always a new line or not? If not, we can remove the \n in the prints
Defined gyro_threshold from testing what it returns when Pico is fairly stationary
Not sure if we should use position or symbol variable approach

Serial monitor when pressing button1 thrice when Pico is lying on any of its sides, Button2 once, Button1 thrice when Pico standing up either way,
Button2 once, button1 thrice when Pico is lying on any of its sides:
-
-
-
 
.
.
.
 
-
-
-
T. Aatu
*/