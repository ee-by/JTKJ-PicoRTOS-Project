#include <stdio.h>
#include <string.h>
#include <ctype.h>
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

#define VEML_THRESHOLD 20 // Threshold for light sensor to be considered covered

//State machine states
enum state { IDLE=1, READING_DATA, SENDING_DATA, MENU, SELECTED_SENSOR, TRANSLATOR};
volatile enum state programState = MENU;

//Create a table for the menu items
static const char *menu_items[] = {"Accelerometer", "Light sensor", "Translator"};
#define MENU_COUNT (sizeof(menu_items) / sizeof(menu_items[0]))

//Global variables here
volatile int position;
volatile uint8_t space = 0;
volatile int spaceCounter = 0; //Count how many spaces have been sent

volatile uint8_t sensor = 1; //Specifies which sensor the user wants to use to send morse code. 1 is default for accelerometer and 2 should be for lightsensor
//We could use this in the menu to choose which sensor to use

//Uncomment if we want to transfer to using this over position
//char symbol;

volatile char receivedSymbol = 0; // Store the received symbol

//Button interrupt function for both buttons
//The led toggles might be unnecessary
static void btn_fxn(uint gpio, uint32_t eventMask) {
    //printf("Button pressed: %d, State: %d, Sensor: %d\n",
    //       gpio, programState, sensor);
    //Check if we are in menu
    if (programState == MENU) {
        //Change selected sensor in menu
        if (gpio == BUTTON1) {
            sensor = (sensor % MENU_COUNT) + 1;   // 1 -> 2 -> 3 -> 1
        }
        else if (gpio == BUTTON2) {
            programState = SELECTED_SENSOR;
        }
    }
    else if (programState == TRANSLATOR) {      // If we are in translator mode, quick exit to menu with button 2
        if (gpio == BUTTON2) {                  // Consider making it possible to exit any mode with e.g. long press on button 2
            programState = MENU;                  // exit back to the menu
        }
    }

    //If we are not in menu
    else {
        //If button 1 is pressed, the code continues to read the sensor data and send it
        if (gpio == BUTTON1) {
            //toggle_led();
            programState = READING_DATA;
            //toggle_led();
        }
        
        //If button 2 is pressed, the Pico sends a space
        else if (gpio == BUTTON2) {
            //toggle_led();
            space = 1;
            //toggle_led();
        }
    }
}




//Do the sensor task
static void sensor_task(void *pvParameters){
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    (void)pvParameters;

    //Introduce the variables
    float ax, ay, az, gx, gy, gz, t;

    
    //Initialize the IMU sensor
    vTaskDelay(pdMS_TO_TICKS(1000));
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

    printf("Reading accelerometer...\n");
    while(1){
        if ((sensor == 1) && (programState == READING_DATA)) {
            if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0) {
                printf("Accel: X=%f, Y=%f, Z=%f | Gyro: X=%f, Y=%f, Z=%f| Temp: %2.2f°C\n", ax, ay, az, gx, gy, gz, t);

                //Check if the Pico is still rotating using gyros absolute values
                //This check continues as long as the Pico is rotating and then continues to check the position
                if ((fabsf(gx) <= gyro_threshold) && (fabsf(gy) <= gyro_threshold) && (fabsf(gz) <= gyro_threshold)) {
                //if (1 == 1) {

                

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
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    //vTaskDelay(pdMS_TO_TICKS(100));
    
}



//Beep when symbol is sent
static void beep(void) {
    buzzer_play_tone(1000, 50);
}

//Feedback when message is sent
static void feedback(void) {
    buzzer_play_tone(1000, 100);
    buzzer_play_tone(1500, 200);
}


//Task for light sensor functionality (second option for sending data)
static void light_sensor_task(void *arg){

    //Important for initialization
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)arg;

    init_veml6030();
    vTaskDelay(pdMS_TO_TICKS(500)); // Wait for the sensor to initialize
    uint32_t lux;
    int cover = 0; // Variable to track if the sensor is covered

    for(;;){
        if ((sensor == 2) && (programState != MENU) && (programState != SELECTED_SENSOR)) {
            lux = veml6030_read_light();

            if (lux < VEML_THRESHOLD) {
                //Check for unnecessary big cover value
                if (cover < 5) {
                    cover++; // If sensor is covered, add to the cover count
                }
                
            }
            else {
                if (cover >= 5) {
                    printf("-"); // If sensor is covered for 500 ms, send line
                    fflush(stdout);
                    beep();
                }
                else if (cover > 0) {
                    printf("."); // Else, send dot
                    fflush(stdout);
                    beep();
                }
                cover = 0; // Reset the cover count if the sensor is not covered
            }
        } else {
            cover = 0; // Reset the cover count if the sensor is not selected or in menu
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}




//Do the task that sends data
static void send_task(void *arg){
    (void)arg;

    for(;;){
        if (space == 1) {
            if (spaceCounter >= 2) { // If space has been pressed 3 times, send message
                printf("\n");
                fflush(stdout);
                feedback();
                spaceCounter = 0;
            }
            else { //Else print space
                printf(" ");
                fflush(stdout);
                beep();
                spaceCounter++;
            }
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
                printf("-");
                fflush(stdout);
                beep();
            }
            else if (position == 2) {
                printf(".");
                fflush(stdout);
                beep();
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

            if ((programState == MENU) || (programState == SELECTED_SENSOR)  || (programState == TRANSLATOR)) {
                // Ignore receive_task when in these states
                continue;
            }

            else if (symbol == '\n') {
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






/*
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
*/





//Note for music functionality

typedef struct {
    uint32_t frequency; // Hz
    uint32_t duration; // ms
} note_t;

// Melody
static const note_t melody[] = {
    {554, 500}, 
    /*{523, 500}, {466, 500},
    {523, 1500}, {415, 500},
    {415, 500}, {466, 1200}, {0, 50}, {415, 200}, {0, 50},
    {415, 2500},*/
};
// Play melody
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





//Function to show the menu on the oled indicating which of the sensors is being selected
static void show_menu(void) {

    //First let's clear the display, in case there are any symbols from other tasks
    clear_display();

    // Display the menu items and indicate the selected one.
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        char line[CHARS_PER_LINE + 1];
        // snprintf drops trailing characters if the string is too long
        snprintf(line, sizeof(line), "%c %s", (sensor == i + 1) ? '>' : ' ', menu_items[i]);
        // Print each row 10 pixels lower
        write_text_xy(0, i * 10, line); 
}       // New menu items can be added to the menu array and they will automatically be displayed.

}

//Task for showing the menu and selecting the sensor from it
static void menu_task(void *arg) {
    //Important so that the initialization isn't interfered with
    vTaskDelay(pdMS_TO_TICKS(1000));
    (void)arg;

    //For checking ifwe need to update the screen based on if the selected sensor has changed
    uint8_t previous_sensor = 0;

    //Infinite loop
    while (1) {
        //Check state
        if (programState == MENU) {
            //Check if selected sensor has changed and update the screen as well as the previous sensor variable
            if (sensor != previous_sensor) {
                show_menu();
                previous_sensor = sensor;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        //Check for different state (when the sensor has been selected)
        else if (programState == SELECTED_SENSOR) {
            clear_display();
            // Show the selected sensor on the screen
            write_text_xy(0, 0, "Selected:");
            write_text_xy(0, 10, menu_items[sensor - 1]);

            // Delay to show the selected sensor briefly
            vTaskDelay(pdMS_TO_TICKS(1500));
            // clear display for the tasks
            clear_display();

            if (sensor == 3) {
                write_text_xy(0, 0, "Translator");
                write_text_xy(0, 10, "BTN2: exit"); // Assigned BTN2 to exit, so it doesn't printf ' '
                                                    // Change this later if we establish a universal exit method for all tasks.
                programState = TRANSLATOR;
            } else {

                // set programstate
                programState = IDLE;
            }

            previous_sensor = 0;   // Reset so if the menu is accessed again, it shows correctly
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }    
}


// ---------------- MORSE TRANSLATOR -------

// Create the table
typedef struct {
    char ch;
    const char *code;
} MorseChar;

static const MorseChar table[] = {
    {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
    {'E', "."},    {'F', "..-."}, {'G', "--."},  {'H', "...."},
    {'I', ".."},   {'J', ".---"}, {'K', "-.-"},  {'L', ".-.."},
    {'M', "--"},   {'N', "-."},   {'O', "---"},  {'P', ".--."},
    {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
    {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"},
    {'Y', "-.--"}, {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, 
    {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."}, 
    {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}
};

// Calculate the size of the table
#define TABLE_SIZE (sizeof(table) / sizeof(table[0])) 

// encode character
static const char *encode_char(char c) { // const to not modify the returned string
    c = (char)toupper((unsigned char)c); // Convert to uppercase
    for (size_t i = 0; i < TABLE_SIZE; i++) { // Go through the table
        if (table[i].ch == c) { 
            return table[i].code; // If found, return the Morse code
        }
    }
    return NULL; // Return NULL if character is not found
}

// decode Morse code
static char decode_symbol(const char *code) { 
    for (size_t i = 0; i < TABLE_SIZE; i++) { // go through again
        if (strcmp(table[i].code, code) == 0) {
            return table[i].ch; // If found, return the character
        }
    }
    return '?'; // Return '?' if Morse code is not found
}

// encode string
static void encode_string(const char *string) { // won't modify the input
    for (size_t i = 0; string[i] != '\0'; i++) { // Go through the string until null terminator
        if (string[i] == ' ') {
            printf("/ "); // Use "/" to represent a space between words for now
        } else {
            const char *code = encode_char(string[i]); // Get the Morse code for the character
            if (code != NULL) {
                printf("%s ", code); // Print the Morse code followed by a space
            } else {
                printf("? "); // If character is not found, print "?"
            }
        }
    }
    printf("\n");
}

// decode code to string
static void decode_string(char *morse) { // Modifies the input string to tokenize it
    char *saveptr; // For strtok_r to keep track of the next token
    char *token = strtok_r(morse, " ", &saveptr); // Tokenize the Morse code string by spaces
    while (token != NULL) {
        if (strcmp(token, "/") == 0) { // If token is "/", print a space to represent a word separation
            putchar(' ');
        } else {
            putchar(decode_symbol(token)); // Decode the Morse code and print the character
        }
        token = strtok_r(NULL, " ", &saveptr); // Get the next token
    }
    printf("\n");
}

// Helper function to get a line of input from the user
static bool get_line(char *buf, size_t size) {
    static size_t len = 0;

    for (;;) {
        if (programState != TRANSLATOR) { len = 0; return false; }

        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(10));   // yields, so it doesn't starve anything
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (len == 0) continue;          // ignore empty lines
            buf[len] = '\0';
            len = 0;
            return true;
        }
        if (c == 0x08 || c == 0x7F) {        // backspace
            if (len > 0) len--;
            continue;
        }
        if (len < size - 1) buf[len++] = (char)c;
    }
}

// Helper to clear inputbuffer, so no leftover characters are read
static void clear_input(void) {
    while (getchar_timeout_us(0) != PICO_ERROR_TIMEOUT) {}
}

// Task for the translator functionality
static void translate_task(void *arg) {
    (void)arg;
    char line[128];         // Buffer for user input
    bool wasActive = false; // Detects the moment the translator is entered, so we can clear the input buffer and print the prompt

    for (;;) {
        if (programState != TRANSLATOR) {
            wasActive = false;      // Sets the flag if machine is in any other state
        } else {
            if (!wasActive) {       // If the translator is entered, clears the input buffer and print the prompt
                clear_input();
                printf("Do you want to encode or decode? Type: (e / d) \n");
                wasActive = true;
            }

            if (get_line(line, sizeof(line)) && programState == TRANSLATOR) { // waits for an answer
                char mode = tolower((unsigned char)line[0]);    // Takes the first character and converts it to lowercase
                                                                // Lets the user type more characters.
                                                                // Might even let wrong words through, but we only care about the first character anyway
                if (mode == 'e' || mode == 'd') {
                    printf("Enter text or code:\n");
                    // If e or d is selected, asked for the text or code to be handled.
                    if (get_line(line, sizeof(line)) && programState == TRANSLATOR) { 
                        if (mode == 'e') encode_string(line);   // encodes
                        else             decode_string(line);   // decodes
                        printf("\nEncode or decode? (e/d)\n");    // ask again for new input
                    }
                } else {
                    printf("Please enter e or d\n"); // Hint if user enters something else than e or d
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));   // Gives time for other tasks to run, since using fgets
    }
}


//Task for receiving symbols from serial monitor.
static void receive_task(void *arg) {
    (void)arg;

    for (;;) {
        if (programState != TRANSLATOR) {
            int c = getchar_timeout_us(0); // don't wait for input if not in translator mode

            if ((c != PICO_ERROR_TIMEOUT) && (c != '\r')) { // Ignore newline characters
                while (receivedSymbol != 0) { // Wait until last symbol is processed
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                receivedSymbol = (char)c; // Store character
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
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
    TaskHandle_t hMenuTask = NULL;
    
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
    //Task for menu
    result = xTaskCreate(menu_task,  // (en) Task function
                "menu",              // (en) Name of the task 
                DEFAULT_STACK_SIZE,   // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                 // (en) Arguments of the task 
                2,                    // (en) Priority of this task
                &hMenuTask);         // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Send Task creation failed\n");
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

    //Task for light sensor functionality
    result = xTaskCreate(light_sensor_task, "light_sensor", DEFAULT_STACK_SIZE, NULL, 2, NULL);

    if(result != pdPASS) {
        printf("Light Sensor Task creation failed\n");
        return 0;
    }

    // Task for translator
    result = xTaskCreate(translate_task, "translate", DEFAULT_STACK_SIZE, NULL, 1, NULL);
    if (result != pdPASS) {
        printf("Translate Task creation failed\n");
        return 0;
    }

    //Task for receiving symbols from serial monitor
    result = xTaskCreate(receive_task, "receive", DEFAULT_STACK_SIZE, NULL, 2, NULL);

    if (result != pdPASS) {
        printf("Receive Task creation failed\n");
        return 0;
    }



    /*
    //TEST ONLY: remove when the real receive task exists
    result = xTaskCreate(fake_receive_task, "fake_receive", DEFAULT_STACK_SIZE, NULL, 2, NULL);

    if(result != pdPASS) {
        printf("Fake receive Task creation failed\n");
        return 0;
    }
    */





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

17.11.2026
Added light sensor functionality as a second option for sending symbols.
If sensor is covered for 500 ms, a dash is sent, if it is covered for less than 500 ms, a dot is sent.
Added beep functionality when sending symbols.

IDEA FOR LATER: Create a menu to select the sensor. Press both buttons to enter menu, button 1 move, button 2 select. How to create on screen?
T. Otso

*/



/*
18.11.2026
Added menu-related states to statemachine
Changed some variables to volatile
Added uint_8t sensor variable to indicate which sensor to use
Also added checks for that in sensortask and lightsensortask
Modified btn_fxn to check for menu. Works differently in menu and in other states
Added a threshold (5) in the lightsensortask so it doesn't increase coutner too high
Made showmenu function to aid menutask in showing the menu on oled
Made menu task. At start, gives user the option to switch sensor(BUTTON1) and select a sensor(BUTTON2).
Menu shows the selected sensor for 1,5s after selection

Had to comment out some of the screen functions for receiving symbols. We should work on integrating those back

T. Aatu 


*/


/*
19.11.2026
Added morse translator functionality. Can encode and decode morse code.
Made a table for the menu. New items can easily be added. Added Translator.
Changed the logic of the buttons in the menu
Assigned BTN2 to exit if the mode is translator. This way it doesn't print a space if pressed accidentally.
Changed show_menu and menu_task to work with the new menu table and the new menu items.
Made the menu able to handle adding new items automatically.
Added translate_task to handle the translator functionality. 
    Translate task uses fgets, which blocks until Enter is pressed. Maybe need to change the logic if it disrupts other tasks.
Changed the melody of the music. Old melody is attached below if needed:

 static const note_t melody[] = {
    {440, 500},{494, 500}, {523, 500}, {587, 500}, 
    {659, 500}, {698, 500}, {784, 500}, 
};
T. Eero



*/