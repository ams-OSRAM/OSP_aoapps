// aoapps_swflag.cpp - the "swflag" app shows a flag, which can be changed by pressing switches
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
#include <Arduino.h>       // Serial.printf
#include <aoresult.h>      // AORESULT_ASSERT, aoresult_t
#include <aoosp.h>         // aoosp_send_clrerror()
#include <aocmd.h>         // aocmd_cint_isprefix()
#include <aoui32.h>        // aoui32_but_wentdown()
#include <aomw.h>          // aomw_topo_build_start()
#include <aoapps_mngr.h>   // aoapps_mngr_register
#include <aoapps_swflag.h> // own


/*
SWFLAG - This is one of the stock applications

DESCRIPTION
- Shows one (static) flag at a time, eg the Dutch national flag red/white/blue spread over the OSP chain
- Tries to find a SAID with an I2C bridge with an I/O-expander (with four buttons and four indicator LEDs)
- If there is no I/O-expander, shows four static flags switching on a time basis
- If there are multiple I/O-expanders the first one is taken
- When an I/O-expander is found the four buttons select which flag to show
- The indicator LEDs indicate which button/flag was selected

BUTTONS
- The X and Y buttons control the dim level (RGB brightness)

NOTES
- When the app quits, the indicator LED switches off
- This app adds a command to configure which four flags will be shown

GOAL
- To show a "sensor" (button) being accessible from the root MCU (the ESP)
*/


// === Animation state machine ===============================================


// We have a list of flag painters (indices)
#define AOAPPS_SWFLAG_ANIM_NUMFLAGS 4 // dictated by number of buttons in IOX
static int aomw_swflag_anim_pix[AOAPPS_SWFLAG_ANIM_NUMFLAGS] = { 
  AOMW_FLAG_PIX_DUTCH, AOMW_FLAG_PIX_MALI, AOMW_FLAG_PIX_EUROPE, AOMW_FLAG_PIX_ITALY 
};


// Time between flags (when not aoapps_swflag_anim_ioxpresent)
#define AOAPPS_SWFLAG_ANIM_MS 2000


static int      aoapps_swflag_anim_ioxpresent; // the I/O-expander is present (if not, flags auto change every UMBUT_STEP_MS)
static int      aoapps_swflag_anim_flagix;     // index of flag being shown
static uint32_t aoapps_swflag_anim_lastms;     // last time stamp (in ms) a flag was shown (for auto change)


// Step of the swflag state machine
static aoresult_t aoapps_swflag_anim() {
  aoresult_t result;
  
  // New flag to display? Record that in flagix
  int flagix= aoapps_swflag_anim_flagix;
  if( aoapps_swflag_anim_ioxpresent ) {
    // IOX present: switch flags when button is pressed
    result= aomw_iox_but_scan();
    if( result!=aoresult_ok ) return result;
    if( aomw_iox_but_wentdown(AOMW_IOX_BUT0) ) flagix=0;
    if( aomw_iox_but_wentdown(AOMW_IOX_BUT1) ) flagix=1;
    if( aomw_iox_but_wentdown(AOMW_IOX_BUT2) ) flagix=2;
    if( aomw_iox_but_wentdown(AOMW_IOX_BUT3) ) flagix=3;
  } else {
    // IOX absent: switch flags every AOAPPS_SWFLAG_ANIM_MS
    if( millis()-aoapps_swflag_anim_lastms > AOAPPS_SWFLAG_ANIM_MS ) {
      aoapps_swflag_anim_lastms= millis();
      flagix= (aoapps_swflag_anim_flagix+1) % AOAPPS_SWFLAG_ANIM_NUMFLAGS;
    }
  }

  // Different flag selected? Paint it
  if( aoapps_swflag_anim_flagix!=flagix ) {
    aoapps_swflag_anim_flagix = flagix;
    // Paint the flag 
    result= aomw_flag_painter(aomw_swflag_anim_pix[aoapps_swflag_anim_flagix])();
    if( result!=aoresult_ok ) return result;
    // Highlight the associated indicator LED
    if( aoapps_swflag_anim_ioxpresent ) {
      result= aomw_iox_led_set( AOMW_IOX_LED(aoapps_swflag_anim_flagix) ); 
      if( result!=aoresult_ok ) return result;
    }
  }

  return aoresult_ok;
}


// === UI32 Button ===========================================================


#define AOAPPS_SWFLAG_BUTTONS_PERKIBI 256 // if macro has value x, num steps is approx log(1024)/log(1+x/1024)
#define AOAPPS_SWFLAG_BUTTONS_MS      200 // step interval (in ms) for auto dim


// Handling button presses (to dim down/up)
static uint32_t aoapps_swflag_buttons_ms;
static aoresult_t aoapps_swflag_buttons_check() {
  if( aoui32_but_wentdown(AOUI32_BUT_X | AOUI32_BUT_Y) ) {
    aoapps_swflag_buttons_ms = millis()-AOAPPS_SWFLAG_BUTTONS_MS; // spoof time
  }
  if( aoui32_but_isdown(AOUI32_BUT_X | AOUI32_BUT_Y) && millis()-aoapps_swflag_buttons_ms> AOAPPS_SWFLAG_BUTTONS_MS) {
    aoapps_swflag_buttons_ms = millis();
    int dim= aomw_topo_dim_get();
    int step= dim*AOAPPS_SWFLAG_BUTTONS_PERKIBI/1024 +1; // +1 ensures step is not 0
    if( aoui32_but_isdown(AOUI32_BUT_X) ) dim-=step; else dim+=step;
    aomw_topo_dim_set(dim); // function clips (no need to do that here)
    // Serial.printf("dim %d\n", aomw_topo_dim_get());
    // Repaint the flag 
    aoresult_t result= aomw_flag_painter(aomw_swflag_anim_pix[aoapps_swflag_anim_flagix])();
    if( result!=aoresult_ok ) return result;
  }
  return aoresult_ok;
}


// === Configuration handler =================================================
// This application actually has a configuration option: which flags to show


// Lookup the named `flag` in the list of flags aomw_flag_name().
// If found, return its index (0..), otherwise return -1.
static int aoapps_swflag_cmd_find( const char * flag ) {
  for( int pix=0; pix<aomw_flag_count(); pix++ ) {
    if( aocmd_cint_isprefix(aomw_flag_name(pix),flag) ) 
      return pix;
  }
  return -1;
}


// Show on Serial which are the four flags
static void aoapps_swflag_cmd_show( ) {
  for( int flagix=0; flagix<AOAPPS_SWFLAG_ANIM_NUMFLAGS; flagix++ ) {
    Serial.printf("SW%d %s\n", flagix, aomw_flag_name(aomw_swflag_anim_pix[flagix]) );
  }
}


// The handler for the "apps config swflag" command
static void aoapps_swflag_cmd_main( int argc, char * argv[] ) {
  AORESULT_ASSERT( argc>3 );
  if( aocmd_cint_isprefix("list",argv[3]) ) {
    if( argc!=4 ) { Serial.printf("ERROR: 'swflag' has too many args\n" ); return; }
    for( int pix=0; pix<aomw_flag_count(); pix++ ) {
      Serial.printf(" %s\n", aomw_flag_name(pix) );
    }
    return;
  } else if( aocmd_cint_isprefix("get",argv[3]) ) {
    if( argc!=4 ) { Serial.printf("ERROR: 'swflag' has too many args\n" ); return; }
    aoapps_swflag_cmd_show();
    return;
  } else if( aocmd_cint_isprefix("set",argv[3]) ) {
    if( argc!=8 ) { Serial.printf("ERROR: 'swflag' expects <flag1> <flag2> <flag3> <flag4>\n" ); return; }
    AORESULT_ASSERT( AOAPPS_SWFLAG_ANIM_NUMFLAGS==4 );
    // check if all entered flags exist, if so, copy them over
    for( int flagix=0; flagix<4; flagix++ ) 
      if( aoapps_swflag_cmd_find(argv[4+flagix])==-1 ) { Serial.printf("ERROR: 'swflag' expects flag name, not '%s'\n", argv[4+flagix] ); return; }
    for( int flagix=0; flagix<4; flagix++ ) 
      aomw_swflag_anim_pix[flagix]= aoapps_swflag_cmd_find(argv[4+flagix]);
    if( argv[0][0]!='@' ) aoapps_swflag_cmd_show();
    return;
  } else {
    Serial.printf("ERROR: 'swflag' has unknown argument (%s)\n",argv[3] ); return;
  }
}


// The long help text for the "apps config swflag" command.
static const char aoapps_swflag_cmd_help[] = 
  "SYNTAX: apps config swflag list\n"
  "- shows available flags\n"
  "SYNTAX: apps config swflag get\n"
  "- shows configured flags\n"
  "SYNTAX: apps config swflag set <flag1> <flag2> <flag3> <flag4>\n"
  "- configures four flags (from list)\n"
;


// Registration does not exist, part of aoapps_swflag_register()
// aoapps_swflag_cmd_register() 


// === Top-level state machine ===============================================


// save the topo global dim level
static int aoapps_swflag_dimdft; 


// The application manager entry point (start)
static aoresult_t aoapps_swflag_start() {
  aoresult_t result;
  
  // Is there an IOX in the OSP chain?
  uint16_t addr;
  result= aomw_topo_i2cfind( AOMW_IOX_DADDR7, &addr );
  if( result!=aoresult_ok && result!=aoresult_dev_noi2cdev ) return result;
  aoapps_swflag_anim_ioxpresent= result==aoresult_ok;
  // Init IOX
  if( aoapps_swflag_anim_ioxpresent ) {
    Serial.printf("swflag: using I/O-expander %02x on SAID %03x \n",AOMW_IOX_DADDR7,addr);
    result= aomw_iox_init( addr ); 
    if( result!=aoresult_ok ) return result;
  } else {
    Serial.printf("swflag: no I/O-expander found, cycling flags\n");
  }
  
  // Select first flag
  aoapps_swflag_anim_flagix= 0;
  // Paint the selected flag 
  result= aomw_flag_painter(aomw_swflag_anim_pix[aoapps_swflag_anim_flagix])(); 
  if( result!=aoresult_ok ) return result;
  // Highlight the associated indicator LED
  if( aoapps_swflag_anim_ioxpresent ) {
    result= aomw_iox_led_set( AOMW_IOX_LED(aoapps_swflag_anim_flagix) ); 
    if( result!=aoresult_ok ) return result;
  }

  // Record time stamp of painting
  aoapps_swflag_anim_lastms= millis();
  // Record initial dim level
  aoapps_swflag_dimdft= aomw_topo_dim_get();
  
  return aoresult_ok;
}


// The application manager entry point (step)
static aoresult_t aoapps_swflag_step() {
  aoresult_t result;
  // check ui32 buttons
  result= aoapps_swflag_buttons_check();
  if( result!=aoresult_ok ) return result;
  // actual animation
  result= aoapps_swflag_anim();
  if( result!=aoresult_ok ) return result;
  // return success
  return aoresult_ok;
}


// The application manager entry point (stop)
static void aoapps_swflag_stop() {
  // Shut down indicator LEDs
  aomw_iox_led_set( AOMW_IOX_LEDNONE );
  // restore original dim level
  aomw_topo_dim_set(aoapps_swflag_dimdft);
}


// === Registration ==========================================================


/*!
    @brief  Registers the swflag app with the app manager.
    @note   This app shows one of four flags on the OSP chain.
            By pressing one of the 4 buttons attached to an I/O-expander
            select which flags is shown.
    @note   The app needs an I/O-expander with 4 buttons and 4 LEDs
            attached to a SAID. It tries to find this, if not it just 
            cycles the flags.
    @note   A typical board to use is the SAIDbasic demo board.
*/
void aoapps_swflag_register() {
  aoapps_mngr_register("swflag", "Switch flag", "dim -", "dim +", 
    AOAPPS_MNGR_FLAGS_WITHTOPO | AOAPPS_MNGR_FLAGS_WITHREPAIR,  
    aoapps_swflag_start, aoapps_swflag_step, aoapps_swflag_stop, 
    aoapps_swflag_cmd_main, aoapps_swflag_cmd_help );
}


// === Extra =================================================================


/*!
    @brief  Resets the hardware (I/O-expander) controlled by the swflag app.
    @return aoresult_ok iff successful
    @note   The swflag app switches on the indicator LEDs connected to
            the I/O-expander. A reboot of the executable restarts the app
            manager, which restarts the first app. In case this is not the
            swflag app, the indicator LEDs stay on. 
            This function is supposed to be called in setup() of executables
            that contain the swflag app to prevent the indicator LEDs from
            staying on after a reboot.
*/
aoresult_t aoapps_swflag_resethw() {
  aoresult_t result;
  
  // Init chain and find I2C bridges
  result= aomw_topo_build();
  if( result!=aoresult_ok ) return result;

  // Is there an IOX in the OSP chain?
  uint16_t addr;
  result= aomw_topo_i2cfind( AOMW_IOX_DADDR7, &addr );
  if( result!=aoresult_ok ) return result;

  // Init IOX
  result= aomw_iox_init( addr ); 
  if( result!=aoresult_ok ) return result;
  
  return aoresult_ok;
}

