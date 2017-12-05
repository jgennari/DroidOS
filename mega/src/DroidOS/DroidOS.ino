// DroidOS v1.04 by Joey Gennari
// This is the operating system that powers our R2 unit. It includes an MP3
// player from DFPlayer and an SBUS receiver to initiate responses based on
// the RC controller. This sketch is designed for a Particle Electron to take
// full use of the cloud connectivity as well as 3 serial buses.

#include "DFRobotDFPlayerMini.h"         // MP3 Player Library
#include "SBUS.h"                        // SBUS Library
#include "Queue.h"                       // Queue for log management

DFRobotDFPlayerMini mp3player;           // Declare MP3 player_active
SBUS x4r(Serial2);                       // Declare RC recv, assign to S4
Queue<String> logs = Queue<String>(20);  // Queue of logs

// System and cloud variables
String dosversion = "v1.17";             // DroidOS version
bool armed = false;                      // Is the Droid armed?
String readlog;                          // Last debug message for cloud
String systemstatus;                     // System status for cloud
String systemstatus_last;                // Last system status displayed
int systemstatus_time;                   // Time (in millis) since last
const int reset_timeout = 5000;          // Start reset after value, in millis
const int sound_threshold = 35;          // Percent before activating sound
bool is_connected = false;               // Cloud connection status
bool show_activity = false;               // Show the loops in the consoleupdate
bool show_changes = true;                // Show RC changes in console
double battcharge;                       // Charge % of internal battery
int log_length;                          // Number of log entries unread
int freememory;                          // Free memory available
int systemloop;                          // Time (in millis) since sys update
const int board_led = 13;                // LED on the board
const int status_led_duration = 100;     // Length of time to flash the LED
const int status_led_off = 4000;         // Time of off period
int status_led_last = 0;                 // Last flash time
int status_led_state = LOW;              // Current state
const int mp3_timer_interval = 10;       // Time in between MP3 polls
int mp3_timer = 0;                       // Last MP3 poll
const int sbus_timer_interval = 1;       // Time in between SBUS polls
int sbus_timer = 0;                      // Last SBUS poll

// Keep track of the status of the MP3 player, including the last song played
// and the state of the player to avoid errors
bool player_active = false;              // Is the MP3 player initialized
bool player_card = false;                // Is the card mounted yet
int song_index[] = { 0, 0 };             // The index of the happy/sad songs
int song_count[] = { 0, 0 };             // The count of the happy/sad songs
bool is_playing = false;                 // Debounce playing efforts
int is_playing_last;                     // Time (in millis) since last playing
int last_finished;                       // Only reset for each play
int is_playing_timeout = 800;            // Debounce timeout
int last_volume;                         // Last volume

// SBUS channel positions, refreshed in the sbus_update proc, 1-based index
int channels[17];                         // 16ch and their -100 to 100 value
int sbus_inactive_value = -100;           // Value of inactivated SBUS channels
int sbus_inactive_value_2 = 0 ;           // Value of inactivated SBUS channels
bool use_sbus = true;                     // If no SBUS connected, use serial

// SBUS channel mappings
const int gimbal_leftdrive = 1;
const int gimbal_rightdrive = 3;
const int gimbal_sound = 4;
const int gimbal_head = 2;
const int rotary_volume = 5;
const int switch_mode = 6;

void resetFunc() {
  SCB_AIRCR = 0x05FA0004;
  delay(2000);  
}

void setup() {
  // Open debugging serial port
  Serial.begin(115200);

  pinMode(board_led, OUTPUT);     
  digitalWrite(board_led, HIGH);
  
  delay(7000);
  log("DroidOS " + dosversion + " starting up ...");

  // Connect to the MP3 player
  startplayer();
  if (!player_active) {
    log("MP3 player initaliztion failed, resetting system.");
    delay(5000);
    resetFunc();
  }
  else {
    log("Waiting for card online.");
    int player_card_start = millis();
    while (!player_card) {
      mp3_update();
      if (millis() - player_card_start > 20000) {
        log("Card online failed, resetting system.");
        delay(5000);
        resetFunc();
      }
    }
  }

  // Count files in Happy & Sad
  int tries = 0;
  while (tries < 5) {
    song_count[0] = mp3player.readFileCountsInFolder(1);
    song_count[1] = mp3player.readFileCountsInFolder(2);
    log("Found " + String(song_count[0]) + " in happy and " + String(song_count[1]) + " in sad.");

    // For some reason the file count proc sometimes returns -1, if it does,
    // try again for 3 times. If it fails, reset the system
    if ((song_count[0] < 1 || song_count[1] < 1) && tries == 4) {
      log("File counts failed, resetting system.");
      delay(2000);
      resetplayer();
      resetFunc();
    } else if (song_count[0] < 1 || song_count[1] < 1) {
      tries++;
    } else {
      break;
    }
  }

  // Play confirmation to let use know it's booting
  log("Droid initaliztion sequence started.");
  play_notification(7);

  // Initialize SBUS
  x4r.begin(false);

  // Wait for SBUS to init
  delay(800);

  // If SBUS initaliztion failed, log, wait and reset
  if ((channels[gimbal_leftdrive] == sbus_inactive_value
    && channels[gimbal_rightdrive] == sbus_inactive_value
    && channels[gimbal_head] == sbus_inactive_value
    && channels[gimbal_sound] == sbus_inactive_value) ||
    (channels[gimbal_leftdrive] == sbus_inactive_value_2
    && channels[gimbal_rightdrive] == sbus_inactive_value_2
    && channels[gimbal_head] == sbus_inactive_value_2
    && channels[gimbal_sound] == sbus_inactive_value_2)) {
      // Check to see if someone is on the serial terminal
      int serialcontrol_start = millis();
      Serial.print("Press any key to use serial control ");

      while(millis() - serialcontrol_start < 3000) {
        if (Serial.available() > 0) {
          Serial.println("");
          log("Using serial control.");

          // Reset all controls to 0
          controls_reset();

          // Allow the droid to be controlled by serial messages
          use_sbus = false;
          break;
        }
        Serial.print(".");

        // Don't flood the console with .
        delay(500);
      }

      // No one is on the terminal and the SBUS channels are empty
      if (use_sbus) {
        Serial.println("");
        log("SBUS initaliztion failed, resetting system.");
        play_notification(4);
        delay(10000);
        resetFunc();
      }
  }

  // Turn status light off
  digitalWrite(board_led, LOW);
  status_led_last = millis();
}

void loop() {
  if (show_activity)
    Serial.print(".");

  if (status_led_state == HIGH && millis() - status_led_last >= status_led_duration) {
    status_led_last = millis();
    status_led_state = LOW;
  }
  else if (status_led_state == LOW && millis() - status_led_last >= status_led_off) {
    status_led_last = millis();
    status_led_state = HIGH;    
  }
  digitalWrite(board_led, status_led_state);
  
  // Write a cloud variable with all the debug info
  update_status();

  if (millis() - mp3_timer > mp3_timer_interval) {
    mp3_update();  
    mp3_timer = millis();
  }
  
  if (millis() - sbus_timer > sbus_timer_interval) {
    sbus_update();
    sbus_timer = millis();
  }
  
  // Only update system variables every 5 sec
  if (millis() - systemloop > 500) {
    systemloop = millis();
  }

  // Check arming status on each loop
  if (armed && channels[switch_mode] < -50)
    disarm();
  else if (!armed && channels[switch_mode] > -50)
    arm();

  // Initiate local reset
  if (channels[gimbal_sound] < -50 && channels[gimbal_head] > 50) {
    delay(reset_timeout);
    if (channels[gimbal_sound] < -50 && channels[gimbal_head] > 50)
      systemreset("Local");
  }

  // Only execute these options when armed
  if (armed) {
    // When system switch pushed to 3rd, position, announce & arm the cloud
    if (channels[switch_mode] > 50 && !is_connected)
      connect();
    else if (channels[switch_mode] < 50 && is_connected)
      disconnect();

    // If not playing and ch4 pushed right, play happy
    if (channels[gimbal_sound] > sound_threshold)
      play_happy();

    // If not playing and ch4 pushed right, play happy
    if (channels[gimbal_sound] < -sound_threshold)
      play_sad();
  }

  // Check the volume, translate RC and change if necessary
  int current_volume = translate_volume();
  if (last_volume != current_volume) {
    last_volume = current_volume;
    mp3player.volume(current_volume);
  }

  if (is_playing && millis() - is_playing_last > is_playing_timeout) {
    log("Force is_playing false.");
    is_playing = false;
  }
}

void mp3_update() {
  if (show_activity)
    Serial.print("m");

  // Log changes to the MP3 player state
  if (player_active && (!player_card || mp3player.available()))
    decode_mp3status(mp3player.readType(), mp3player.read());
}

void arm() {
  log("Droid armed.");
  play_notification(3);
  armed = true;
}

void disarm() {
  log("Droid disarmed.");
  play_notification(6);
  disconnect();
  armed = false;
}

void disconnect() {
  log("Disconnecting communication system.");
  //play_notification(11);

  is_connected = false;
}

void connect() {
  log("Initiating communication system.");
  play_notification(10);

  is_connected = true;
}

int systemreset(String extra) {
  if (extra == "Local") {
    log("Local reset initiated.");
    play_notification(9);
  } else {
    log("Cloud reset initiated.");
    play_notification(8);
  }

  delay(3000);
  resetFunc();
  return 1;
}

void log(String message) {
  if (message.length() > 0) {
    Serial.println(message);
    logs.push(message);
    log_length = logs.count();
  }
}

int clearlog(String extra) {
  logs.clear();
  log_length = logs.count();
  return logs.count();
}

int advancelog(String extra) {
  if (logs.count() > 0)
    readlog = logs.pop();
  else
    readlog = "End of log.";

  log_length = logs.count();
  return logs.count();
}

void startplayer() {
  log("Starting DFPlayer serial.");
  Serial3.begin(9600);

  // Delay to make sure serial connects
  delay(1500);

  log("Connecting DFPlayer serial.");
  if (!mp3player.begin(Serial3))  {
    player_active = false;
    log("DFPlayer Mini failed.");
  }
  else  {
    log("DFPlayer Mini online.");
    player_active = true;
    mp3player.volume(10);
  }
}

void resetplayer() {
  log("Resetting player.");
  player_active = false;
  player_card = false;
  mp3player.reset();
  delay(1000);
  log("Player reset.");
  player_active = true;
}

void update_status() {
  systemstatus = "L:" +
    String(channels[gimbal_leftdrive]) +
    "R:" +
    String(channels[gimbal_rightdrive]) +
    "S:" +
    String(channels[gimbal_sound]) +
    "H:" +
    String(channels[gimbal_head]) +
    "V:" +
    String(channels[rotary_volume]) +
    "M:" +
    String(channels[switch_mode]);

  if ((systemstatus_last != systemstatus)) {
    if (show_changes) {
      systemstatus_time = millis();
      systemstatus_last = systemstatus;
      Serial.println(systemstatus);
    }    
    status_led_last = millis();
    status_led_state = HIGH;
    digitalWrite(board_led, status_led_state);
  }
}

int translate_volume() {
  int normalized = channels[rotary_volume]+80;
  float equalized = normalized/160.0f;
  float spread = equalized*30;
  int newvolume = (int)spread;

  if (newvolume < 0) {
    newvolume = 0;
  }
  else if (newvolume > 30) {
    newvolume = 30;
  }

  return newvolume;
}

void sbus_update() {
  if (show_activity)
    Serial.print("s");

  if (use_sbus) {
    x4r.process();
    channels[gimbal_leftdrive] = x4r.getNormalizedChannel(gimbal_leftdrive);
    channels[gimbal_rightdrive] = x4r.getNormalizedChannel(gimbal_rightdrive);
    channels[gimbal_head] = x4r.getNormalizedChannel(gimbal_head);
    channels[gimbal_sound] = x4r.getNormalizedChannel(gimbal_sound);
    channels[rotary_volume] = x4r.getNormalizedChannel(rotary_volume);
    channels[switch_mode] = x4r.getNormalizedChannel(switch_mode);
  } else {
    if(Serial.available() > 0)
    {
        int ch = Serial.readStringUntil(':').toInt();
        int val = Serial.parseInt();
        manual_control(ch, val);
    }
  }
}

int remote_ctrl(String command) {
  int split = command.indexOf(":");
  int ch = command.substring(0, split).toInt();
  int val = command.substring(split+1).toInt();
  manual_control(ch, val);
  return 1;
}

void manual_control(int ch, int val) {
  systemstatus_time = millis();
  if (ch == 99) {
    resetplayer();
  }
  else if (ch == 98) {
    if (val == 1)
      play_happy();
    else
      play_sad();
  }
  else if (ch == 97) {
    play_notification(val);
  }
  else if (ch == 96) {
    log("Resetting system ...");
    delay(2000);
    resetFunc();
  }
  else if (ch == 95) {
    log("Entering DFU ...");
    delay(2000);
    resetFunc();
  }
  else if (ch > 0) {
    channels[ch] = val;
  }
}

void controls_reset() {
  channels[gimbal_leftdrive] = 0;
  channels[gimbal_rightdrive] = 0;
  channels[gimbal_head] = 0;
  channels[gimbal_sound] = 0;
  channels[rotary_volume] = 0;
  channels[switch_mode] = -80;
}

void play_happy() {
  if (is_playing)
    return;

  is_playing = true;
  is_playing_last = millis();

  if (song_index[0] < song_count[0])
      song_index[0] = song_index[0] + 1;
  else
      song_index[0] = 1;

  log("Playing happy clip: " + String(song_index[0]));
  mp3player.playFolder(1, song_index[0]);
}

void play_sad() {
  if (is_playing)
    return;

  is_playing = true;
  is_playing_last = millis();
  if (song_index[1] < song_count[1])
      song_index[1] = song_index[1] + 1;
  else
      song_index[1] = 1;

  log("Playing sad clip: " + String(song_index[1]));
  mp3player.playFolder(2, song_index[1]);
}

void play_notification(int notification) {
  if (player_active && player_card) {
    log("Playing notification " + String(notification) + ".");
    //mp3player.pause();
    mp3player.playFolder(3, notification);
  }
}

void decode_mp3status(uint8_t type, int value) {
  String mp3error;

  switch (type) {
    case TimeOut:
      log("Time Out!");
      break;
    case WrongStack:
      log("Stack Wrong!");
      break;
    case DFPlayerCardInserted:
      log("Card Inserted!");
      break;
    case DFPlayerCardRemoved:
      log("Card Removed!");
      player_card = false;
      break;
    case DFPlayerCardOnline:
      log("Card Online!");
      player_card = true;
      break;
    case DFPlayerPlayFinished:
      if (value != last_finished) {
        last_finished = value;
        log("Play " + String(value) + " Finished!");
        is_playing = false;
      }
      break;
    case DFPlayerError:
      switch (value) {
        case Busy:
          mp3error = "Card not found";
          break;
        case Sleeping:
          mp3error = "Sleeping";
          break;
        case SerialWrongStack:
          mp3error = "Get Wrong Stack";
          break;
        case CheckSumNotMatch:
          mp3error = "Checksum Not Match";
          break;
        case FileIndexOut:
          mp3error = "File Index Out of Bound";
          break;
        case FileMismatch:
          mp3error = "Cannot Find File";
          break;
        case Advertise:
          mp3error = "In Advertise";
          break;
        default:
          break;
      }

      log("DFPlayerError: " + mp3error);
      break;
    default:
      break;
  }
}
