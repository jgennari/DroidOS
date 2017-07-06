// DroidOS v1.03 by Joey Gennari
// This is the operating system that powers our R2 unit. It includes an MP3
// player from DFPlayer and an SBUS receiver to initiate responses based on
// the RC controller. This sketch is designed for a Particle Electron to take
// full use of the cloud connectivity as well as 3 serial buses.

SYSTEM_MODE(SEMI_AUTOMATIC);             // Managed cloud connection via switch

#include <DFRobotDFPlayerMini.h>         // MP3 Player Library
#include <SBUS.h>                        // SBUS Library
#include <Serial4/Serial4.h>             // Serial4 Library for Electron

DFRobotDFPlayerMini mp3player;           // Declare MP3 player_active
SBUS x4r(Serial4);                       // Declare RC recv, assign to S4
Timer mp3_timer(5, mp3_update);         // Create a 1ms timer, assign proc
Timer sbus_timer(5, sbus_update);       // Create a 1ms timer, assign proc

// System and cloud variables
String dosversion = "v1.16";             // DroidOS version
bool armed = false;                      // Is the Droid armed?
String lastlog;                          // Last debug message for cloud
String systemstatus;                     // System status for cloud
String systemstatus_last;                // Last system status displayed
bool show_changes = true;                // Show RC changes in console
const int reset_timeout = 5000;          // Start reset after value, in millis
const int sound_threshold = 50;          // Percent before activating sound
bool is_connected = false;               // Ccloud connection status

// Keep track of the status of the MP3 player, including the last song played
// and the state of the player to avoid errors
bool player_active = false;              // Is the MP3 player initialized
bool player_card = false;                // Is the card mounted yet
int song_index[] = { 0, 0 };             // The index of the happy/sad songs
bool is_playing = false;                 // Debounce playing efforts
int is_playing_last;                     // Time (in millis) since last playing
int is_playing_timeout = 6000;           // Debounce timeout
int last_volume;                         // Last volume

// SBUS channel positions, refreshed in the sbus_update proc, 1-based index
int channels[17];                         // 16ch and their -100 to 100 value
int sbus_inactive_value = -100;           // Value of inactivated SBUS channels
bool use_sbus = true;                     // If no SBUS connected, use serial

// SBUS channel mappings
const int gimbal_leftdrive = 1;
const int gimbal_rightdrive = 2;
const int gimbal_sound = 3;
const int gimbal_head = 4;
const int rotary_volume = 5;
const int switch_mode = 6;

void setup() {
  // Open debugging serial port
  Serial.begin(115200);

  delay(7000);
  log("DroidOS " + dosversion + " starting up ...");

  // Publish cloud variables and functions
  Particle.variable("systemstatus", systemstatus);
  Particle.variable("lastlog", lastlog);
  Particle.variable("dosversion", dosversion);
  Particle.function("droid_reset", droid_reset);

  // Connect to the MP3 player
  startplayer();
  if (!player_active) {
    log("MP3 player initaliztion failed, resetting system.");
    delay(5000);
    System.reset();
  }
  else {
    log("Waiting for card online.");
    int player_card_start = millis();
    while (!player_card) {
      if (millis() - player_card_start > 5000) {
        log("Card online failed, resetting system.");
        delay(5000);
        System.reset();
      }
    }
  }

  // Play confirmation to let use know it's booting
  log("Droid initaliztion sequence started.");
  play_notification(7);

  // Initialize SBUS
  x4r.begin(false);

  // Delay to make sure everything has settled
  delay(500);

  // Check SBUS channels
  sbus_update();

  // If SBUS initaliztion failed, log, wait and reset
  if (channels[gimbal_leftdrive] == sbus_inactive_value
    && channels[gimbal_rightdrive] == sbus_inactive_value
    && channels[gimbal_head] == sbus_inactive_value
    && channels[gimbal_sound] == sbus_inactive_value) {
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
        delay(4000);
        System.reset();
      }

      sbus_timer.start();
  }
}

void loop() {
  // Write a cloud variable with all the debug info
  update_status();

  // Check arming status on each loop
  if (armed && channels[switch_mode] < -50)
    disarm();
  else if (!armed && channels[switch_mode] > -50)
    arm();

  // Initiate local reset
  if (channels[gimbal_sound] < -50 && channels[gimbal_head] > 50) {
    delay(reset_timeout);
    if (channels[gimbal_sound] < -50 && channels[gimbal_head] > 50)
      droid_reset("Local");
  }

  // Only execute these options when armed
  if (armed) {
    // When system switch pushed to 3rd, position, announce & arm the cloud
    if (channels[switch_mode] > 50 && !is_connected)
      connect();
    else if (channels[switch_mode] < 50 && is_connected)
      disconnect();

    // If not playing and ch4 pushed right, play happy
    if (!is_playing && channels[gimbal_sound] > sound_threshold)
      play_happy();

    // If not playing and ch4 pushed right, play happy
    if (!is_playing && channels[gimbal_sound] < -sound_threshold)
      play_sad();

    // Check the volume, translate RC and change if necessary
    int current_volume = translate_volume();
    if (last_volume != current_volume) {
      last_volume = current_volume;
      mp3player.volume(current_volume);
    }
  }

  if (is_playing && millis() - is_playing_last > is_playing_timeout) {
    log("Force is_playing false.");
    is_playing = false;
  }
}

void mp3_update() {
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
  Cellular.off();
  is_connected = false;
}

void connect() {
  log("Initiating communication system.");
  play_notification(10);
  Particle.connect();
  is_connected = true;
}

int droid_reset(String extra) {
  if (extra == "Local") {
    log("Local reset initiated.");
    play_notification(9);
  } else {
    log("Cloud reset initiated.");
    play_notification(8);
  }

  delay(3000);
  System.reset();
  return 1;
}

void log(String message) {
  if (message.length() > 0) {
    Serial.println(message);
    lastlog = message;
  }
}

void startplayer() {
  log("Starting DFPlayer serial.");
  Serial1.begin(9600);

  // Delay to make sure serial connects
  delay(1500);

  log("Connecting DFPlayer serial.");
  if (!mp3player.begin(Serial1))  {
    player_active = false;
    log("DFPlayer Mini failed.");
  }
  else  {
    log("DFPlayer Mini online.");
    mp3_timer.start();
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
    String(channels[gimbal_head]) +
    "H:" +
    String(channels[gimbal_sound]) +
    "V:" +
    String(channels[rotary_volume]) +
    "M:" +
    String(channels[switch_mode]);

  if (show_changes && (systemstatus_last != systemstatus)) {
    systemstatus_last = systemstatus;
    Serial.println(systemstatus);
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
          System.reset();
        }
        else if (ch > 0) {
          channels[ch] = val;
        }
    }
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
  is_playing = true;
  is_playing_last = millis();

  if (song_index[0] <= 20)
      song_index[0] = song_index[0] + 1;
  else
      song_index[0] = 1;

  log("Playing happy clip: " + String(song_index[0]));
  mp3player.playFolder(1, song_index[0]);
}

void play_sad() {
  is_playing = true;
  is_playing_last = millis();
  if (song_index[1] <= 20)
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
      //log("Time Out!");
      break;
    case WrongStack:
      //log("Stack Wrong!");
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
      log("Play " + String(value) + " Finished!");
      is_playing = false;
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
