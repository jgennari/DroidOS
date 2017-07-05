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
Timer sbus_timer(1, update_sbus);        // Create a 1ms timer, assign proc

// System and cloud variables
String dosversion = "v1.03";             // DroidOS version
bool armed = false;                      // Is the Droid armed?
String lastlog;                          // Last debug message for cloud
String systemstatus;                     // System status for cloud
int systemstatus_change;                 // Time (in millis) since change
const int sound_threshold = 50;          // Percent before activating sound
bool show_changes = true;                // Show RC changes in console

// Remote reset initiated by head & sound gimbals after timeout
const int reset_timeout = 5000;          // Start reset after value, in millis
bool is_resetting = false;               // Is the Droid resetting?
int reset_start;                         // Time (in millis), since reset

// Keep track of these next to values to filter out unnecessary status updates
// The MP3 player will keep looping the last status
uint8_t laststatus;                      // Last status from MP3 player
int lastvalue;                           // Last value from MP3 player

// Keep track of the status of the MP3 player, including the last song played
// and the state of the player to avoid errors
bool player_active = false;              // Is the MP3 player initialized
int song_index[] = { 0, 0 };             // The index of the happy/sad songs
bool is_playing = false;                 // Is the player playing music
int is_playing_song = false;             // If playing, which music

// SBUS channel positions, refreshed in the update_sbus proc, 1-based index
int channels[17];                         // 16ch and their -100 to 100 value
int sbus_inactive_value = -100;           // Value of inactivated SBUS channels

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

  // Publish cloud variables and functions
  Particle.variable("systemstatus", systemstatus);
  Particle.variable("lastlog", lastlog);
  Particle.variable("dosversion", dosversion);
  Particle.function("droid_reset", droid_reset);

  // Connect to the MP3 player
  startplayer();
  if (player_active) {
    log("MP3 player initaliztion failed, resetting system.");
    delay(5000);
    System.reset();
  }

  // Play confirmation to let use know it's booting
  log("Droid initaliztion sequence started.");
  play_notification(7);
  delay(3500);

  // Initialize SBUS and start timer
  x4r.begin(false);
  sbus_timer.start();

  // Delay to make sure everything has settled
  delay(500);

  // If SBUS initaliztion failed, log, wait and reset
  if (channels[gimbal_leftdrive] == sbus_inactive_value
    && channels[gimbal_rightdrive] == sbus_inactive_value
    && channels[gimbal_head] == sbus_inactive_value
    && channels[gimbal_sound] == sbus_inactive_value) {
      log("SBUS initaliztion failed, resetting system.");
      play_notification(4);
      delay(4000);
      System.reset();
  }
}

void loop() {
  // Check arming status on each loop
  if (armed && channels[switch_mode] < -50)
    disarm();
  else if (!armed && channels[switch_mode] > -50)
    arm();

  // Initiate remote reset
  if (channels[gimbal_sound] < -50 && channels[gimbal_head] > 50) {
    if (!is_resetting) {
      is_resetting = true;
      reset_start = millis();
    } else if (is_resetting && millis() - reset_start > reset_timeout) {
      droid_reset("Local");
    }
  } else {
    is_resetting = false;
    reset_start = 0;
  }

  // Only execute these options when armed
  if (armed) {
    // When system switch pushed to 3rd, position, announce & arm the cloud
    if (channels[switch_mode] > 50 && !Particle.connected())
      connect();
    else if (channels[switch_mode] < 50 && Particle.connected())
      disconnect();

    // If not playing and ch4 pushed right, play happy
    if (!is_playing && channels[gimbal_sound] > sound_threshold)
      play_happy();

    // If not playing and ch4 pushed right, play happy
    if (!is_playing && channels[gimbal_sound] < -sound_threshold)
      play_sad();

    // Check the volume, translate RC and change if necessary
    if (mp3player.readVolume() != translate_volume())
      mp3player.volume(translate_volume());
  }

  // Log changes to the MP3 player state
  decode_mp3status(mp3player.readType(), mp3player.read());

  // Write a cloud variable with all the debug info
  update_status();

  delay(200);
}

void arm() {
  log("Droid armed.");
  play_notification(3);
  delay(2500);
  armed = true;

}

void disarm() {
  log("Droid disarmed.");
  play_notification(6);
  disconnect();
  delay(2500);
  armed = false;
}

void disconnect() {
  log("Disconnecting communication system.");
  play_notification(11);
  Cellular.off();
}

void connect() {
  log("Initiating communication system.");
  play_notification(10);
  Particle.connect();
}

int droid_reset(String extra) {
  if (extra == "Local") {
    log("Local reset initiated.");
    play_notification(9);
  } else {
      log("Cloud reset initiated.");
      play_notification(9);
  }

  delay(3000);
  System.reset();
  return 1;
}

void log(String message) {
  Serial.println(message);
  lastlog = message;
}

void startplayer() {
  log("Starting DFPlayer serial:");
  Serial1.begin(9600);

  log("Connecting DFPlayer serial:");
  if (!mp3player.begin(Serial1))  {
      log("Unable to begin:");
      log("  1.Please recheck the connection!");
      log("  2.Please insert the SD card!");
  }
  else  {
      log("DFPlayer Mini online.");
      player_active = true;
      mp3player.volume(10);
  }
}

void update_status() {
  String oldstatus = systemstatus;
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
    String(channels[switch_mode]) +
    "P:";

  if (player_active)
    systemstatus = systemstatus + "Y";
  else
    systemstatus = systemstatus + "N";

  if (show_changes && (oldstatus != systemstatus)) {
    if (millis() - systemstatus_change > 100 || systemstatus_change == 0) {
      systemstatus_change = millis();
      Serial.println(systemstatus);
    }
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

void update_sbus() {
  x4r.process();

  channels[gimbal_leftdrive] = x4r.getNormalizedChannel(gimbal_leftdrive);
  channels[gimbal_rightdrive] = x4r.getNormalizedChannel(gimbal_rightdrive);
  channels[gimbal_head] = x4r.getNormalizedChannel(gimbal_head);
  channels[gimbal_sound] = x4r.getNormalizedChannel(gimbal_sound);

  channels[rotary_volume] = x4r.getNormalizedChannel(rotary_volume);
  channels[switch_mode] = x4r.getNormalizedChannel(switch_mode);
}

void play_happy() {
  is_playing = true;
  if (song_index[0] <= 20)
      song_index[0] = song_index[0] + 1;
  else
      song_index[0] = 1;

  log("Playing happy clip " + song_index[0]);
  mp3player.playFolder(1, song_index[0]);
}

void play_sad() {
  is_playing = true;
  if (song_index[1] <= 20)
      song_index[1] = song_index[1] + 1;
  else
      song_index[1] = 1;

  log("Playing sad clip " + song_index[1]);
  mp3player.playFolder(2, song_index[1]);
}

void play_notification(int notification) {
  mp3player.pause();
  mp3player.playFolder(3, notification);
}

void decode_mp3status(uint8_t type, int value) {
  if (type == DFPlayerPlayFinished && value == is_playing_song)
    is_playing = false;

  if (type != laststatus && value != lastvalue)
  {
    laststatus = type;
    lastvalue = value;
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
        break;
      case DFPlayerCardOnline:
        log("Card Online!");
        break;
      case DFPlayerPlayFinished:
        log(value + " Play Finished!");
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
            mp3error = "Check Sum Not Match";
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
        mp3player.reset();
        break;
      default:
        break;
    }
  }
}
