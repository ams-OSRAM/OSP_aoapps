// aoapps_mngr.cpp - apps manager (records the entry functions associated with all registered apps)
/*****************************************************************************
 * Copyright 2024 by ams OSRAM AG                                            *
 * All rights are reserved.                                                  *
 *                                                                           *
 * IMPORTANT - PLEASE READ CAREFULLY BEFORE COPYING, INSTALLING OR USING     *
 * THE SOFTWARE.                                                             *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       *
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         *
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS         *
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  *
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,     *
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT          *
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     *
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY     *
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT       *
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     *
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.      *
 *****************************************************************************/
#include <Arduino.h>      // Serial.printf
#include <aocmd.h>        // aocmd_cint_register()
#include <aoosp.h>        // aoosp_send_clrerror()
#include <aomw.h>         // aomw_topo_build_start()
#include <aoui32.h>       // aoui32_oled_splash()
#include <aoapps_mngr.h>  // own


// === voidapp ===============================================================
// The void app, doing nothing, allowing unhindered USB commands
// It is always registered (in init), but not part of switchnext()


static aoresult_t aoapps_mngr_voidapp_start() { 
  return aoresult_ok; // do nothing
}


static aoresult_t aoapps_mngr_voidapp_step () { 
  return aoresult_ok; // do nothing 
}


static void aoapps_mngr_voidapp_stop () { 
 // do nothing
}


void aoapps_mngr_voidapp_register() {
  aoapps_mngr_register("voidapp", "USB command", "--", "--", 
    0, /* no dither, no repair */
    aoapps_mngr_voidapp_start, aoapps_mngr_voidapp_step, aoapps_mngr_voidapp_stop, 
    0, 0 /* no config command */ );
}


// === app registration ======================================================


typedef struct aoapps_mngr_app_s {
  const char *        name;  // the (short) name of the app (identifier)
  const char *        oled;  // the (long) name of the app for on the OLED (spaces and caps allowed)
  const char *        xlbl;  // the label (function) for the x-button
  const char *        ylbl;  // the label (function) for the y-button
  int                 flags; // indicate if topo and or repair should be run by mngr
  aoapps_mngr_start_t start; // reset app state machine to the starting state
  aoapps_mngr_step_t  step;  // step the app state machine
  aoapps_mngr_stop_t  stop;  // shutdown the app state machine (eg signaling LEDs)
  aoapps_mngr_cmd_t   cmd;   // plugin for 'apps config' if an app has configuration needs
  const char *        help;  // help text for configuration
 } aoapps_mngr_app_t;


// All app descriptors
static int aoapps_mngr_count;
static aoapps_mngr_app_t aoapps_mngr_apps[AOAPPS_MNGR_REGISTRATION_SLOTS] = {};


/*!
    @brief  Registers and app with the app manager. 
            The app manager starts and stops apps, updates the OLED, reports 
            errors, and has a plug-in for the command interpreter allowing 
            apps to publish a configuration handler.
    @param  name
            The (short) name of the app. Used as an "id" in the command 
            "apps" to show which app is running or to start another app.
            Machine readable; must be short string of alphanumeric characters.
    @param  oled
            The (longer) name of the app.
            It will be shown on the OLED in the big upper box.
            Human readable; may have spaces, dashes, slashes, etc.
    @param  xlbl
            The text describing the function of the X button.
            It will be shown on the OLED in the lower left box
    @param  ylbl
            The text describing the function of the Y button.
            It will be shown on the OLED in the lower right box
    @param  flags
            Enables extra features the app manager can provide to an app.
            A mask consisting of OR-ing AOAPPS_MNGR_FLAGS_XXX.
            AOAPPS_MNGR_FLAGS_WITHTOPO    
              build topo map before starting the app
            AOAPPS_MNGR_FLAGS_WITHREPAIR  
              periodically repairs the chain by broadcasting clrerror and 
              goactive telegrams
            AOAPPS_MNGR_FLAGS_NEXTONERR
              when the app goes into error, the app manager will switch to 
              the next app (after a 10 seconds)
    @param  start
            The start() function will be called once by the app manager before 
            the app starts. This is intended for initialization of the app's
            state machine. If start() reports an error the app manager will 
            show the error to the user (OLED, signaling LEDs), and stay idle. 
    @param  step
            The step() function will be called continuously (after start()).
            The app (the app's state machine) runs its animation on this.
            If step() reports an error the app manager will show the error 
            to the user (OLED, signaling LEDs), and stay idle. 
    @param  stop
            The stop() function will be called by the app manager when the 
            user selects another app to run. It offers an app the option to
            cleanup (e.g. clear signaling LEDs connected to an I/O-expander).
            If stop() has an error, it is no use reporting it, the app 
            manager will start a new app anyhow. 
    @param  cmd
            A plugin for the command interpreter. The cmd() function will be 
            called if the user has given a command that starts with 
            "apps configure name ..." (where name matches the name during
            registration), this handler should parse and act on the "..." part.
    @param  help
            A help string for the cmd() command handler. It will be shown
            when the user has given the command "apps configure name",
            (where name matches the name during registration).
    @note   Might assert when too many apps are registered or when an 
            app registers with e.g. an illegal name.
    @note   It is optional to have a command handler. Either `cmd` and `help`
            are both 0 (no plugin) or both have a real value. `flags` can be
            AOAPPS_MNGR_FLAGS_NONE (which is 0). All other registration 
            parameters are mandatory (can not be 0).
*/
void aoapps_mngr_register(const char * name, const char * oled, const char * xlbl, const char * ylbl, int flags, aoapps_mngr_start_t start, aoapps_mngr_step_t step, aoapps_mngr_stop_t stop, aoapps_mngr_cmd_t cmd, const char * help) {
  AORESULT_ASSERT( aoapps_mngr_count<AOAPPS_MNGR_REGISTRATION_SLOTS );
  AORESULT_ASSERT( name!=0 && oled!=0 && xlbl!=0 && ylbl!=0 && start!=0 && step!=0 && stop!=0 );
  AORESULT_ASSERT( (cmd==0) == (help==0) );
  AORESULT_ASSERT( 0==(flags & ~AOAPPS_MNGR_FLAGS_ALL) );
  for( const char *app_name_char=name; *app_name_char!=0; app_name_char++ ) 
    AORESULT_ASSERT( isalnum(*app_name_char) ); // illegal char in app name
  
  int slot = aoapps_mngr_count;
  aoapps_mngr_count++;
  
  aoapps_mngr_apps[slot].name = name;
  aoapps_mngr_apps[slot].oled = oled;
  aoapps_mngr_apps[slot].xlbl = xlbl;
  aoapps_mngr_apps[slot].ylbl = ylbl;
  aoapps_mngr_apps[slot].flags= flags;
  aoapps_mngr_apps[slot].start= start;
  aoapps_mngr_apps[slot].step = step;
  aoapps_mngr_apps[slot].stop = stop;
  aoapps_mngr_apps[slot].cmd  = cmd;
  aoapps_mngr_apps[slot].help = help;
}


// === managing ==============================================================


// Forward declarations when the manager is flagged to run topo build
static aoresult_t aoapps_mngr_startwithtopo();
static aoresult_t aoapps_mngr_stepwithtopo();


// Flash frequency of the green signaling LED ("heartbeat" of the app)
#define AOAPPS_MNGR_HEARTBEAT_MS 500
// Time (in ms) between two repair steps
#define AOAPPS_MNGR_REPAIR_MS  250
// Timeout (in ms) for an error (to go to next app
#define AOAPPS_MNGR_ERROR_MS  10000


// Global state of application
static int        aoapps_mngr_appix;      // index of current app - in aoapps_mngr_apps[]
static int        aoapps_mngr_moderun;    // is the app running (1) or stopped (0)
static aoresult_t aoapps_mngr_result;     // last error reported by app
static uint32_t   aoapps_mngr_lastgrn;    // last time heartbeat (on green signaling LED) was updated
static uint32_t   aoapps_mngr_lastrepair; // last time a repair was done
static uint32_t   aoapps_mngr_lasterror;  // last time an error was detected


/*!
    @brief  Initialize the app manager.
            See `aoapps_mngr_register()`.
    @note   One app, the "voidapp", is always registered as first (appix==0).
            This app does nothing. It is intended to be activated
            when the user wants to take control of the OSP chain via the
            command interpreter.
    @note   No app is "running" after init.
            First register some apps, then call aoapps_mngr_start().
*/            
void aoapps_mngr_init() {
  aoapps_mngr_count= 0;
  aoapps_mngr_appix= 0;
  aoapps_mngr_moderun= 0;
  aoapps_mngr_result= aoresult_ok;
  // aoui32_led_off(AOUI32_LED_GRN|AOUI32_LED_RED);
  aoapps_mngr_voidapp_register();
  aoapps_mngr_lastgrn= millis();
  aoapps_mngr_lastrepair= millis();
  aoapps_mngr_lasterror= millis();
}


// Show app status to user: red error (and OLED) or green heartbeat
static void aoapps_mngr_showstatus() {
  // Check for error
  if( aoapps_mngr_result!=aoresult_ok ) {
    aoapps_mngr_lasterror= millis();
    // Error: GRN off and RED on
    aoui32_led_off(AOUI32_LED_GRN);
    aoui32_led_on (AOUI32_LED_RED); 
    // Also on Serial
    Serial.printf("apps: ERROR in app '%s': %s\n", aoapps_mngr_apps[aoapps_mngr_appix].name, aoresult_to_str(aoapps_mngr_result) );
    // Also show on OLED
    aoui32_oled_msg( aoresult_to_str(aoapps_mngr_result,1) );
    return;
  }

  // No error: use green signaling LED for heartbeat
  if( millis()-aoapps_mngr_lastgrn > AOAPPS_MNGR_HEARTBEAT_MS ) {
    aoui32_led_toggle(AOUI32_LED_GRN);
    aoapps_mngr_lastgrn= millis();
  }
}


// Just in case there was and error (under voltage) broadcast clrerror and goactive
static aoresult_t aoapps_mngr_repair() {
  aoresult_t result;
  // Is it time for a repair step?
  if( millis()-aoapps_mngr_lastrepair > AOAPPS_MNGR_REPAIR_MS ) {
    result= aoosp_send_clrerror(0x000);
    if( result!=aoresult_ok ) return result;
    result=aoosp_send_goactive(0x000);
    if( result!=aoresult_ok ) return result;
    aoapps_mngr_lastrepair = millis();
  }
  return aoresult_ok;
}


/*!
    @brief  Starts app with index `appix`.
            That is, set "current" app to `appix` and "run" it.
    @param  appix
            The application index of the app to start.
            0 <= appix < aoapps_mngr_app_count()
    @note   An app is said to "run" once its `start()` has been called.
            Optionally `step()` may have been called zero or more times after
            that. But once `stop()` is called the app is no longer said to 
            "run" but rather to be "stop"[ed]. 
    @note   Exactly one app is always "current" (and can "run" or "stop").
            All other apps are "stop" sometimes referred to as "idle".
    @note   The application indices are handed out in registration order.
            Check `aoapps_mngr_app_name()` when in doubt. The "voidapp" is 
            always registered and has index 0.
    @note   It is an error when the current app is "run" (must be "stop").
    @note   This function is typically called once, in setup(). After setup()
            apps are made current via `aoapps_mngr_switch()` or
            `aoapps_mngr_switchnext()`.
*/            
void aoapps_mngr_start(int appix) {
  AORESULT_ASSERT( aoapps_mngr_count>0 );
  // Current mode should be NOT running
  AORESULT_ASSERT( ! aoapps_mngr_moderun );
  // Make appix the current app (if valid)
  AORESULT_ASSERT( 0<=appix && appix<aoapps_mngr_count );
  aoapps_mngr_appix= appix;
  // Update OLED with app name and button labels
  aoui32_oled_state(aoapps_mngr_apps[aoapps_mngr_appix].oled, aoapps_mngr_apps[aoapps_mngr_appix].xlbl, aoapps_mngr_apps[aoapps_mngr_appix].ylbl);
  // Print app name to serial
  //Serial.printf("apps: start '%s'\n", aoapps_mngr_apps[aoapps_mngr_appix].name );
  // Record new run mode
  aoapps_mngr_moderun=1;
  // Initialize signaling LEDs
  aoui32_led_off(AOUI32_LED_GRN|AOUI32_LED_RED);
  // Show first heartbeat
  aoui32_led_on(AOUI32_LED_GRN);
  aoapps_mngr_lastgrn= millis();
  // Call start() function of the app
  if( aoapps_mngr_apps[aoapps_mngr_appix].flags & AOAPPS_MNGR_FLAGS_WITHTOPO ) {
    aoapps_mngr_result= aoapps_mngr_startwithtopo();
  } else {
    aoapps_mngr_result= aoapps_mngr_apps[aoapps_mngr_appix].start();
  }
  // Show app status to user
  aoapps_mngr_showstatus();
}


/*!
    @brief  Steps the current app.
    @note   It is an error when the current app is "stop" (must be "run").
    @note   If any start() or step() before this call reported an error, 
            this step() is ignored.
    @note   If flag AOAPPS_MNGR_FLAGS_WITHTOPO is passed in registration, 
            the first series of step()'s build the topo map.
    @note   If flag AOAPPS_MNGR_FLAGS_WITHREPAIR is passed in registration 
            then some step()'s send repair telegrams (clrerror, goactive).
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
    @note   This function is typically called in loop().
*/            
void aoapps_mngr_step() {
  // Current mode should be running
  AORESULT_ASSERT( aoapps_mngr_moderun );
  // If there was an error in a previous step, do not step again
  if( aoapps_mngr_result!=aoresult_ok ) {
    if( aoapps_mngr_apps[aoapps_mngr_appix].flags & AOAPPS_MNGR_FLAGS_NEXTONERR )
      if( millis()-aoapps_mngr_lasterror>AOAPPS_MNGR_ERROR_MS ) {
        Serial.printf("apps: this app switches to next after error\n");
        aoapps_mngr_switchnext();
      }
    return;
  }
  // Call step() function of the underlying app.
  if( aoapps_mngr_apps[aoapps_mngr_appix].flags & AOAPPS_MNGR_FLAGS_WITHTOPO ) {
    aoapps_mngr_result= aoapps_mngr_stepwithtopo();
  } else {
    aoapps_mngr_result= aoapps_mngr_apps[aoapps_mngr_appix].step();
  }
  // Call repair
  if( aoapps_mngr_result==aoresult_ok ) 
    if( aoapps_mngr_apps[aoapps_mngr_appix].flags & AOAPPS_MNGR_FLAGS_WITHREPAIR ) {
      aoapps_mngr_result= aoapps_mngr_repair();
  }
  // Show app status to user
  aoapps_mngr_showstatus();
}


/*!
    @brief  Stops the current app.
    @note   It is an error when the current app is "stop" (must be "run").
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
    @note   This function is typically not called, rather 
            `aoapps_mngr_switch()` or `aoapps_mngr_switchnext()` is called
            which handles calling `stop()`.
*/            
void aoapps_mngr_stop() {
  // Current mode should be running
  AORESULT_ASSERT( aoapps_mngr_moderun );
  // Call stop() function of the underlying app.
  aoapps_mngr_apps[aoapps_mngr_appix].stop();
  // Record new run mode
  aoapps_mngr_moderun=0;
}


/*!
    @brief  Stops the current app and starts app with index `appix`.
    @param  appix
            The application index of the app to start.
            0 <= appix < aoapps_mngr_app_count()
    @note   It is an error when the current app is "stop" (must be "run").
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
    @note   aoapps_mngr_switch(0) will select the voidapp; the function
            aoapps_mngr_switchnext() skips the voidapp.
    @note   See `aoapps_mngr_switchnext()`.
*/            
void aoapps_mngr_switch(int appix) {
  aoapps_mngr_stop();
  aoapps_mngr_start(appix);
}


/*!
    @brief  Stops the current app and starts the next one.
    @note   The next one in the sense of appix (registration order).
    @note   Wraps around to appix 1 (so skips voidapp).
    @note   It is an error when the current app is "stop" (must be "run").
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
*/            
void aoapps_mngr_switchnext() {
  aoapps_mngr_switch( aoapps_mngr_appix % (aoapps_mngr_count-1) + 1);
}


// === observers =============================================================


/*!
    @brief  Returns index of current app.
    @return index of current app, 0 <= appix < aoapps_mngr_app_count()
    @note   Current app can be "stop" or "run", see `aoapps_mngr_app_running()`.
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
*/            
int aoapps_mngr_app_appix() {
  return aoapps_mngr_appix;
}


/*!
    @brief  Returns if current app is "run".
    @return 1 if "run", 0 if "stop".
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
*/            
int aoapps_mngr_app_running() {
  return aoapps_mngr_moderun;
}


/*!
    @brief  Returns the number of registered apps.
    @return number of registered apps.
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
*/            
int aoapps_mngr_app_count() {
  return aoapps_mngr_count;
}


/*!
    @brief  Gets the (short) name ("id") of the app with index `appix`.
    @param  appix
            The application index of the app.
            0 <= appix < aoapps_mngr_app_count()
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
*/            
const char * aoapps_mngr_app_name(int appix) {
  return aoapps_mngr_apps[appix].name;
}


/*!
    @brief  Gets the (longer) name (OLED) of the app with index `appix`.
    @param  appix
            The application index of the app.
            0 <= appix < aoapps_mngr_app_count()
    @note   See `aoapps_mngr_start()` for start/stop/current/appix terminology.
*/            
const char * aoapps_mngr_app_oled(int appix) {
  return aoapps_mngr_apps[appix].oled;
}


// === "with topo" statemachine ==============================================
// Most apps want to run after a topo build, so the below functions wrap the
// apps' start/step/stop state machine to include a topo build.


// State of the app (this manager runs topo build)
typedef enum aoapps_mngr_state_e {
  AOAPPS_MNGR_STATE_TOPOBUILD,  // Topo build (resetinit, scan, config nodes)
  AOAPPS_MNGR_STATE_APPANIM,    // Animation steps implemented by app 
  AOAPPS_MNGR_STATE_ERROR,      // Terminal state when an error is detected (that error is recorded in aoapps_runled_error)
} aoapps_mngr_state_t;


static aoapps_mngr_state_t aoapps_mngr_state;  // current state
static aoresult_t          aoapps_mngr_error;  // last error reported by app


static aoresult_t aoapps_mngr_startwithtopo() {
  aoapps_mngr_error= aoresult_ok;
  aoapps_mngr_state= AOAPPS_MNGR_STATE_TOPOBUILD;
  aomw_topo_build_start();
  return aoapps_mngr_error;
}


static aoresult_t aoapps_mngr_stepwithtopo() {
  switch( aoapps_mngr_state ) {

    case AOAPPS_MNGR_STATE_TOPOBUILD:
      if( !aomw_topo_build_done() ) {
        aoapps_mngr_error= aomw_topo_build_step();
        if( aoapps_mngr_error!=aoresult_ok ) aoapps_mngr_state= AOAPPS_MNGR_STATE_ERROR;
        return aoresult_ok; // loop topo build
      }
      Serial.printf("%s: starting on %d RGBs\n", aoapps_mngr_apps[aoapps_mngr_appix].name, aomw_topo_numtriplets() );
      aoapps_mngr_error= aoapps_mngr_apps[aoapps_mngr_appix].start(); // call start of app
      if( aoapps_mngr_error!=aoresult_ok ) aoapps_mngr_state= AOAPPS_MNGR_STATE_ERROR;
      aoapps_mngr_state= AOAPPS_MNGR_STATE_APPANIM;
    break;

    case AOAPPS_MNGR_STATE_APPANIM:
      aoapps_mngr_error= aoapps_mngr_apps[aoapps_mngr_appix].step();
      if( aoapps_mngr_error!=aoresult_ok ) aoapps_mngr_state= AOAPPS_MNGR_STATE_ERROR;
    break;

    case AOAPPS_MNGR_STATE_ERROR:
      // ERROR is a final state
    break;

  }
  return aoapps_mngr_error;
}


// === command handler =======================================================


// Some apps have registered a configuration handler with help.
static void aoapps_mngr_cmd_config( int argc, char * argv[] ) {
  // List which apps are configurable
  if( argc==2 ) { 
    int count=0;
    for( int appix=0; appix<aoapps_mngr_app_count(); appix++ ) {
      if( aoapps_mngr_apps[appix].help ) {
        if( count==0 ) Serial.printf("Configurable apps\n");
        Serial.printf("%s (%s)\n", aoapps_mngr_apps[appix].name, aoapps_mngr_apps[appix].oled );
        count++;
      }
    }
    if( count==0 ) Serial.printf("No registered app is configurable\n");
    return; 
  }
  
  // List configuration help for specific app
  if( argc==3 ) {
    int count=0; 
    for( int appix=0; appix<aoapps_mngr_app_count(); appix++ ) {
      if( aocmd_cint_isprefix(aoapps_mngr_app_name(appix),argv[2]) ) {
        if( aoapps_mngr_apps[appix].cmd==0 ) { Serial.printf("ERROR: app '%s' is not configurable\n",argv[2] ); return; }
        AORESULT_ASSERT( aoapps_mngr_apps[appix].help );
        Serial.printf("%s", aoapps_mngr_apps[appix].help );
        count++;
      }
    }
    if( count==0 ) Serial.printf("No registered app matches '%s'\n", argv[2]);
    return;
  }
  
  // Call configuration handler for specific app
  if( argc>3 ) {
    int count=0; 
    for( int appix=0; appix<aoapps_mngr_app_count(); appix++ ) {
      if( aocmd_cint_isprefix(aoapps_mngr_app_name(appix),argv[2]) ) {
        if( aoapps_mngr_apps[appix].cmd==0 ) { Serial.printf("ERROR: app '%s' is not configurable\n",argv[2] ); return; }
        aoapps_mngr_apps[appix].cmd(argc,argv);
        count++;
      }
    }
    if( count==0 ) Serial.printf("No registered app matches '%s'\n", argv[2]);
    return;
  }
}


// Lists one app (with status)
static void aoapps_mngr_cmd_listone(int appix) {
  const char* name= aoapps_mngr_app_name(appix);
  int cur= aoapps_mngr_app_appix();
  int run= aoapps_mngr_app_running();
  const char * mode;
  if( appix!=cur ) mode= "stop";
  else if( run ) mode= "run"; 
  else mode= "idle";
  char flags[4]="tre";
  if( aoapps_mngr_apps[appix].flags & AOAPPS_MNGR_FLAGS_WITHTOPO   ) flags[0]='T';
  if( aoapps_mngr_apps[appix].flags & AOAPPS_MNGR_FLAGS_WITHREPAIR ) flags[1]='R';
  if( aoapps_mngr_apps[appix].flags & AOAPPS_MNGR_FLAGS_NEXTONERR  ) flags[2]='E';
  const char* oled= aoapps_mngr_app_oled(appix);
  Serial.printf("%d %-10s %-4s %-5s %s\n",appix,name,mode,flags,oled);
}


// Lists all apps (with status)
static void aoapps_mngr_cmd_listall(int verbose) {
  if( verbose ) Serial.printf("# %-10s %-4s %5s %s\n","name","mode","flags","display name");
  for( int appix=0; appix<aoapps_mngr_app_count(); appix++ ) 
    aoapps_mngr_cmd_listone(appix);
  if( verbose ) Serial.printf("\nflags: T=withtopo R=withrepair, E=nextonerr\n");
}


// The handler for the "apps" command
static void aoapps_mngr_cmd( int argc, char * argv[] ) {
  if( argc==1 ) {
    aoapps_mngr_cmd_listone(aoapps_mngr_app_appix()); 
    return;
  } else if( aocmd_cint_isprefix("list",argv[1]) ) {
    if( argc!=2 ) { Serial.printf("ERROR: too many args\n" ); return; }
    aoapps_mngr_cmd_listall(argv[0][0]!='@');
    return;
  } else if( aocmd_cint_isprefix("switch",argv[1]) ) {
    if( argc!=3 ) { Serial.printf("ERROR: <app> missing\n" ); return; }
    // <app> is number?
    int appix;
    bool ok= aocmd_cint_parse_dec(argv[2],&appix) ;
    if( ok ) {
      if( appix<0 || appix>=aoapps_mngr_app_count() ) { Serial.printf("ERROR: %d out of bounds\n",appix ); return; }
      aoapps_mngr_switch(appix);
      if( argv[0][0]!='@' ) aoapps_mngr_cmd_listone(appix);
      return;
    }
    // <app> is string?
    appix= -1;
    for( int ix=0; ix<aoapps_mngr_app_count(); ix++ ) {
      if( aocmd_cint_isprefix(aoapps_mngr_app_name(ix),argv[2]) ) {
        appix= ix; break; 
      }
    }
    if( appix==-1 ) { Serial.printf("ERROR: no app with name starting with '%s'\n",argv[2] ); return; }
    aoapps_mngr_switch(appix);
    if( argv[0][0]!='@' ) aoapps_mngr_cmd_listone(appix);
    return;
  } else if( aocmd_cint_isprefix("config",argv[1]) ) {
    aoapps_mngr_cmd_config(argc,argv);
  } else {
    Serial.printf("ERROR: unknown arguments for 'apps'\n" ); return;
  }
}


// The long help text for the "apps" command.
static const char aoapps_mngr_cmd_longhelp[] = 
  "SYNTAX: apps [list]\n"
  "- without argument, shows current app\n"
  "- with argument lists all registered apps\n"
  "SYNTAX: apps switch <app>\n"
  "- stops current app and starts <app>\n"
  "- <app> is either a name or an id (see list)\n"
  "- <app> 0 is the 'voidapp' (doing nothing): no interference with commands\n"
  "SYNTAX: apps config [...]\n"
  "- without arguments, shows which apps offer configuration\n"
  "- with app name shows help for configuration of that app\n"
  "- with app name and arguments configures that app (see its help)\n"
  "NOTES:\n"
  "- supports @-prefix to suppress output\n"
;


/*!
    @brief  Registers the "apps" command with the command interpreter.
    @return Number of remaining registration slots (or -1 if registration failed).
*/
int aoapps_mngr_cmd_register() {
  return aocmd_cint_register(aoapps_mngr_cmd, "apps", "manage and configure active app", aoapps_mngr_cmd_longhelp);
}

