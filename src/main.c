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

#define CHARS_PER_LINE 16 // Number of characters per line on the display

//State machine states
enum state { IDLE=1, READING_DATA, SENDING_DATA};
volatile enum state programState = IDLE;

//Global variables here
int position;
uint8_t space = 0;
//Uncomment if we want to transfer to using this over position
//char symbol;

volatile char receivedSymbol = 0; // Store the received symbol

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

// Blink for received symbols
static void blink_received_symbol(char symbol) {
    if ((symbol == '.') || (symbol == '-')) {
        set_red_led_status(1);

        if (symbol == '.') {
            vTaskDelay(pdMS_TO_TICKS(100)); // Dot, short blink

        } else {
            vTaskDelay(pdMS_TO_TICKS(300)); // Dash, long blink
        } 
        set_red_led_status(0);
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between blinks
    }

    else if (symbol == ' ') {
        // Delay between words (space)
        vTaskDelay(pdMS_TO_TICKS(200));

    }
}

//Screen functionality (Otso)
static void screen_task(void *arg){
    (void)arg;
    char text[CHARS_PER_LINE + 1] = "";
    int length = 0;
    uint8_t newMessage = 0;
    char symbol;

    clear_display();

    for(;;){
        if (receivedSymbol != 0) {
            symbol = receivedSymbol;
            receivedSymbol = 0;

            if (symbol == '\n') {
                newMessage = 1; // End message
            }
            
            else {
                if ((newMessage == 1) || (length >= CHARS_PER_LINE)) { // new message or line full, clear display
                    clear_display();
                    length = 0;
                    newMessage = 0;
                }
            text[length] = symbol; // Store received symbol in array
            length++; // Add space for next symbol
            text[length] = '\0'; // Terminate string
            write_text_xy(0, 0, text); // Write text to display
            blink_received_symbol(symbol); // Blink LED for received symbol
            }


            
        }
        //tight_loop_contents(); // Modify with application code here.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//TEST FOR RECEIVING DATA FROM SERIAL MONITOR (made by claude, delete when not needed)
static void fake_receive_task(void *arg){
    (void)arg;

    const char *message = ".... . .-.. .-.. ---  .-- --- .-. .-.. -..  \n";

    for(;;){
        for (int i = 0; message[i] != '\0'; i++) {
            while (receivedSymbol != 0) {
                vTaskDelay(pdMS_TO_TICKS(10)); //Wait until the last symbol has been handled
            }
            receivedSymbol = message[i];
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

//Note for music functionality

typedef struct {
    uint32_t frequency; // Hz
    uint32_t duration; // ms
} note_t;

static const note_t melody[] = {
    {440, 500},{494, 500}, {523, 500}, {587, 500}, 
    {659, 500}, {698, 500}, {784, 500}, 
};

static void play_melody(const note_t *melody, int count) {
    for (int i = 0; i < count; i++) {
        if (melody[i].frequency == 0) {
            vTaskDelay(pdMS_TO_TICKS(melody[i].duration)); //No note
        } else {
            buzzer_play_tone(melody[i].frequency, melody[i].duration); //Play note
            vTaskDelay(pdMS_TO_TICKS(30)); //Delay between notes
        }
    }
}

//Task for music functionality
static void music_task(void *arg){
    (void)arg;

    play_melody(melody, sizeof(melody) / sizeof(melody[0]));
    vTaskDelete(NULL); // Delete task after playing the melody
}


// Able to receive commands from serial monitor.
// Maybe have a infinite while loop to scan for right commands and then execute them?
// Command to make: Translate
// Make the commands in functions.c ???

// Need a Menu structure to select the sensors we want to use?






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

    //initialize display
    init_display();

    //initialize buzzer
    init_buzzer();

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

    //Task for screen functionality
    result = xTaskCreate(screen_task,  // (en) Task function
                "screen",              // (en) Name of the task 
                DEFAULT_STACK_SIZE,   // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                 // (en) Arguments of the task 
                2,                    // (en) Priority of this task
                NULL);         // (en) A handle to control the execution of this task

    if (result != pdPASS) {
        printf("Screen Task creation failed\n");
        return 0;
    }

    //Task for playing music
    result = xTaskCreate(music_task, "music", DEFAULT_STACK_SIZE, NULL, 2, NULL);

    if(result != pdPASS) {
        printf("Music Task creation failed\n");
        return 0;
    }

    //TEST ONLY: remove when the real receive task exists
    result = xTaskCreate(fake_receive_task, "fake_receive", DEFAULT_STACK_SIZE, NULL, 2, NULL);

    if(result != pdPASS) {
        printf("Fake receive Task creation failed\n");
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

/*
15.11.2026
Added screen funtionalities to display received symbols, the receiving logic is not yet ready, so I implemented a test receive task
Also added the led blinking for received symbols, the led blinks for a dot or dash and has a delay for a space
Added the melody functionality, the melody is played once when the program starts, the task is deleted after playing the melody
T. Otso

*/

