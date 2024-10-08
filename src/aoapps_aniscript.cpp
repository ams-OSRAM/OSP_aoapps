// aoapps_aniscript.cpp - the "aniscript" app animates the OSP chain from a script in EEPROM
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
#include <aoui32.h>        // aoui32_but_wentdown()
#include <aomw.h>          // aomw_topo_build_start()
#include <aoapps_mngr.h>   // aoapps_mngr_register
#include <aoapps_aniscript.h> // own


/*
ANISCRIPT - This is one of the stock applications

DESCRIPTION
- Plays a light show as defined by an animation script
- Tries to find a SAID with an I2C bridge with an EEPROM
- If there is an external EEPROM stick (I2C address 0x51) it favors that over an internal EEPROM (address 0x50)
- If there are multiple (of the same kind, external or internal) the first one is taken
- If no EEPROM is found, uses the heartbeat script included in the firmware
- If an EEPROM is found, loads the script from the EEPROM and plays that
- The internal EEPROM (on the SAIDbasic board) contains the rainbow script
- External EEPROMs are flashed with bouncing-block and color-mix

NOTES
- Ensure the I2C EEPROM stick faces "chip up" otherwise there is a short circuit (see PCB labels)
- Safest is to only swap an EEPROM when USB power is removed
- On your own risk when swapping life
- The script loading takes place when starting the app, so either (1) power cycle, (2) reset, (3) A button
- Note, the tool OSP_aotop/tree/main/examples/eepromflasher allows flashing EEPROMs with the various animation scripts

BUTTONS
- The X and Y buttons control the FPS level (frames-per-second animation speed).

GOAL
- Show that the root MCU can access I2C devices (EEPROM) e.g. for calibration values
*/


// === helpers ===============================================================


// Maximum number of instructions in an animation script (we get them from 256 bytes EEPROM)
#define AOAPPS_ANISCRIPT_MAXNUMINST 128 
// List of instructions ("the script")
static uint16_t aoapps_aniscript_insts[AOAPPS_ANISCRIPT_MAXNUMINST]; 


// This function implements the EEPROM searching scheme as explained 
// to the user: first a stick, most upstream one, then a built-in, also
// most upstream one.
// Either returns 
// - aoresult_xxx          a real (OSP transmission, or I2C transaction) error
// - aoresult_dev_noi2cdev when no device was found
// - aoresult_ok           when a device was found (node *addr, device *daddr7)
static aoresult_t aoapps_aniscript_find(uint16_t * addr, uint8_t * daddr7) {
  aoresult_t result;
  *addr= 0xFFFF;
  *daddr7= 0xFF;
  
  // Is there an "I2C EEPROM stick" in the OSP chain?
  result= aomw_topo_i2cfind( AOMW_EEPROM_DADDR7_STICK, addr );
  if( result==aoresult_ok ) { *daddr7= AOMW_EEPROM_DADDR7_STICK; return aoresult_ok; } 
  if( result!=aoresult_dev_noi2cdev ) return result; // real error
  
  // Is there a SAIDbasic board (with an EEPROM) in the OSP chain?
  result= aomw_topo_i2cfind( AOMW_EEPROM_DADDR7_SAIDBASIC, addr );
  if( result==aoresult_ok ) { *daddr7= AOMW_EEPROM_DADDR7_SAIDBASIC; return aoresult_ok; } 
  if( result!=aoresult_dev_noi2cdev ) return result; // real error

  // We will not look elsewhere (eg OSP32 EEPROM); signal we didn't find an EEPROM
  return aoresult_dev_noi2cdev;
}


// Tries to find an EEPROM, next loads the script (or uses a stock one) 
// and installs at at the player.
static aoresult_t aoapps_aniscript_load() {
  uint16_t addr;
  uint8_t daddr7;
  aoresult_t result = aoapps_aniscript_find(&addr,&daddr7);
  if( result!=aoresult_ok && result!=aoresult_dev_noi2cdev ) return result; 
  
  if( result==aoresult_dev_noi2cdev ) {
    // No EEPROM found, use built-in script
    Serial.printf("aniscript: no EEPROM, playing 'heartbeat'\n");
    memcpy( aoapps_aniscript_insts, aomw_tscript_heartbeat(), aomw_tscript_heartbeat_bytes() );
  } else {
    // Hack: using array of size n of uint16_t as array of size 2n of uint8_t.
    // The compiler might pad, so we try to check that here.
    // Endianess is ignored since we read and write with same processor (see eepromflasher)
    AORESULT_ASSERT( sizeof(uint8_t[4]) == sizeof(uint16_t[2]) );
    result= aomw_eeprom_read(addr, daddr7, 0, (uint8_t*)aoapps_aniscript_insts, AOAPPS_ANISCRIPT_MAXNUMINST*2 );
    if( result!=aoresult_ok ) return result; 
    Serial.printf("aniscript: playing from EEPROM %02x on SAID %03x \n", daddr7,addr);
  }

  // Install the script
  aomw_tscript_install( aoapps_aniscript_insts, aomw_topo_numtriplets() );

  return aoresult_ok;
}


// === Animation state machine ===============================================


// Time (in ms) between two LED updates
static int aoapps_aniscript_anim_frame_ms;
// The state of the aniscript state machine
static uint32_t aoapps_aniscript_anim_ms;


// Step of the aniscript state machine
static aoresult_t aoapps_aniscript_anim() {
  aoresult_t result;
  
  // Is it time for an animation step
  if( millis()-aoapps_aniscript_anim_ms < aoapps_aniscript_anim_frame_ms ) return aoresult_ok; 
  aoapps_aniscript_anim_ms = millis();

  result= aomw_tscript_playframe(); 
  if( result!=aoresult_ok ) return result;
  
  return aoresult_ok;
}


// === UI32 Button ===========================================================


#define AOAPPS_ANISCRIPT_BUTTONS_PERKIBI 100 // if macro has value x, num steps is approx log(1024)/log(1+x/1024)
#define AOAPPS_ANISCRIPT_BUTTONS_MS      200 // step interval (in ms) for auto dim


// Handling button presses (to dim down/up)
static uint32_t aoapps_aniscript_buttons_ms;
static aoresult_t aoapps_aniscript_buttons_check() {
  if( aoui32_but_wentdown(AOUI32_BUT_X | AOUI32_BUT_Y) ) {
    aoapps_aniscript_buttons_ms = millis()-AOAPPS_ANISCRIPT_BUTTONS_MS; // spoof time
  }
  if( aoui32_but_isdown(AOUI32_BUT_X | AOUI32_BUT_Y) && millis()-aoapps_aniscript_buttons_ms> AOAPPS_ANISCRIPT_BUTTONS_MS) {
    aoapps_aniscript_buttons_ms = millis();
    int step= aoapps_aniscript_anim_frame_ms*AOAPPS_ANISCRIPT_BUTTONS_PERKIBI/1024 +1; // +1 ensures step is not 0
    if( aoui32_but_isdown(AOUI32_BUT_Y) ) {
      aoapps_aniscript_anim_frame_ms-= step; 
      if( aoapps_aniscript_anim_frame_ms < 1 ) aoapps_aniscript_anim_frame_ms= 1;
    } else {
      aoapps_aniscript_anim_frame_ms+= step;
      if( aoapps_aniscript_anim_frame_ms > 2000 ) aoapps_aniscript_anim_frame_ms= 2000;
    }
    //Serial.printf("aniscript: frame %d ms\n", aoapps_aniscript_anim_frame_ms );
  }
  return aoresult_ok;
}


// === Top-level state machine ===============================================


// The application manager entry point (start)
static aoresult_t aoapps_aniscript_start() {
  aoresult_t result;
  
  // Find and load the most appropriate EEPROM in the OSP chain
  result= aoapps_aniscript_load();
  if( result!=aoresult_ok ) return result;
  
  // Record time stamp of painting
  aoapps_aniscript_anim_frame_ms= 100;
  aoapps_aniscript_anim_ms= millis();
  
  return aoresult_ok;
}


// The application manager entry point (step)
static aoresult_t aoapps_aniscript_step() {
  aoresult_t result;
  // check ui32 buttons
  result= aoapps_aniscript_buttons_check();
  if( result!=aoresult_ok ) return result;
  // actual animation
  result= aoapps_aniscript_anim();
  if( result!=aoresult_ok ) return result;
  // return success
  return aoresult_ok;
}


// The application manager entry point (stop)
static void aoapps_aniscript_stop() {
  // Nothing to restore
}


// === Registration ==========================================================


/*!
    @brief  Registers the aniscript app with the app manager.
    @note   This app plays a light shows as defined by an animation script.
    @note   The script is read from an EEPROM, so there should be an EEPROM 
            (attached to a SAID with an I2C bridge) in the OSP chain 
            (especially an EEPROM on a insertable I2C stick). If not,
            this app plays a stock script (heartbeat) from ROM.
    @note   A typical board to use is the SAIDbasic demo board.
*/
void aoapps_aniscript_register() {
  aoapps_mngr_register("aniscript", "Animation script", "FPS -", "FPS +", 
    AOAPPS_MNGR_FLAGS_WITHTOPO | AOAPPS_MNGR_FLAGS_WITHREPAIR, 
    aoapps_aniscript_start, aoapps_aniscript_step, aoapps_aniscript_stop, 
    0, 0 /* no config command */ );
}


