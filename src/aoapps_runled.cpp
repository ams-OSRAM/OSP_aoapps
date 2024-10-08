// aoapps_runled.cpp - the "runled" app that animates running leds over the entire OSP chain
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
#include <aomw.h>          // aomw_topo_settriplet()
#include <aoui32.h>        // aoui32_but_wentdown()
#include <aoapps_mngr.h>   // aoapps_mngr_register
#include <aoapps_runled.h> // own


/*
RUNLED - This is one of the stock applications

DESCRIPTION
- There is a "virtual cursor" that runs from the begin of the chain to the end and then back
- Chain length and node types are auto detected
- Every 25ms the cursor advances one LED and paints that in the current color
- Every time the cursor hits the begin or end of the chain, it steps color
- Color palette: red, yellow, green, cyan, magenta

BUTTONS
- The X and Y buttons control the dim level (RGB brightness)

GOAL
- To show that various OSP nodes can be mixed and have color/brightness matched
*/


// === Animation =============================================================


// Time (in ms) between two LED updates
#define AOAPPS_RUNLED_ANIM_MS    25


// The colors in the runled loop
static const aomw_topo_rgb_t * const aoapps_runled_anim_rgbs[] = {
  &aomw_topo_red,
  &aomw_topo_yellow,
  &aomw_topo_green,
  &aomw_topo_cyan,
  &aomw_topo_magenta
};
#define AOAPPS_RUNLED_RGBS_SIZE ( sizeof(aoapps_runled_anim_rgbs)/sizeof(aoapps_runled_anim_rgbs[0]) )


// The state of the runled state machine
static int      aoapps_runled_anim_tix;
static int      aoapps_runled_anim_colorix;
static int      aoapps_runled_anim_dir;
static uint32_t aoapps_runled_anim_ms;


// Step of the runled state machine
static aoresult_t aoapps_runled_anim() {
  aoresult_t result;
  
  // Is it time for an animation step
  if( millis()-aoapps_runled_anim_ms < AOAPPS_RUNLED_ANIM_MS ) return aoresult_ok; 
  aoapps_runled_anim_ms = millis();

  // Update: set triplet tix to color cix
  result= aomw_topo_settriplet(aoapps_runled_anim_tix, aoapps_runled_anim_rgbs[aoapps_runled_anim_colorix] );
  if( result!=aoresult_ok ) return result;

  // Go to next triplet
  int new_tix = aoapps_runled_anim_tix + aoapps_runled_anim_dir;
  if( 0<=new_tix && new_tix<aomw_topo_numtriplets() ) {
    aoapps_runled_anim_tix= new_tix;
  } else  { // hit either end
    // reverse direction and step color
    aoapps_runled_anim_dir = -aoapps_runled_anim_dir;
    aoapps_runled_anim_colorix += 1;
    if( aoapps_runled_anim_colorix==AOAPPS_RUNLED_RGBS_SIZE ) {
      aoapps_runled_anim_colorix= 0;
    }
  }
  return aoresult_ok;
}


// === Button ================================================================


#define AOAPPS_RUNLED_BUTTONS_PERKIBI 256 // if macro has value x, num steps is approx log(1024)/log(1+x/1024)
#define AOAPPS_RUNLED_BUTTONS_MS      200 // step interval (in ms) for auto dim


// Handling button presses (to dim down/up)
static uint32_t aoapps_runled_buttons_ms;
static aoresult_t aoapps_runled_buttons_check() {
  if( aoui32_but_wentdown(AOUI32_BUT_X | AOUI32_BUT_Y) ) {
    aoapps_runled_buttons_ms = millis()-AOAPPS_RUNLED_BUTTONS_MS; // spoof time
  }
  if( aoui32_but_isdown(AOUI32_BUT_X | AOUI32_BUT_Y) && millis()-aoapps_runled_buttons_ms> AOAPPS_RUNLED_BUTTONS_MS) {
    aoapps_runled_buttons_ms = millis();
    int dim= aomw_topo_dim_get();
    int step= dim*AOAPPS_RUNLED_BUTTONS_PERKIBI/1024 +1; // +1 ensures step is not 0
    if( aoui32_but_isdown(AOUI32_BUT_X) ) dim-=step; else dim+=step;
    aomw_topo_dim_set(dim); // function clips (no need to do that here)
    // Serial.printf("dim %d\n", aomw_topo_dim_get());
  }
  return aoresult_ok;
}


// === Top-level state machine ===============================================


// save the topo global dim level
static int aoapps_runled_dimdft; 


// The application manager entry point (start)
aoresult_t aoapps_runled_start() {
  aoapps_runled_anim_tix= 0;
  aoapps_runled_anim_colorix= 0;
  aoapps_runled_anim_dir= +1;
  aoapps_runled_anim_ms= millis();
  aoapps_runled_buttons_ms= millis();
  aoapps_runled_dimdft= aomw_topo_dim_get();
  return aoresult_ok;
}


// The application manager entry point (step)
aoresult_t aoapps_runled_step() {
  aoresult_t result;
  // check ui32 buttons
  result= aoapps_runled_buttons_check();
  if( result!=aoresult_ok ) return result;
  // actual animation
  result = aoapps_runled_anim();
  if( result!=aoresult_ok ) return result;
  // return success
  return aoresult_ok;
}


// The application manager entry point (stop)
void aoapps_runled_stop() {
  // restore original dim level
  aomw_topo_dim_set(aoapps_runled_dimdft);
}


// === Registration ==========================================================


/*!
    @brief  Registers the runled app with the app manager.
    @note   This app triplet-by-triplet fills the strip with a color,
            then switches direction and color, and fills in reverse. 
            Then repeats.
    @note   This runs on any demo board with LEDs.
            The OSP32 board would be enough.
*/
void aoapps_runled_register() {
  aoapps_mngr_register("runled", "Running LEDs", "dim -", "dim +", 
    AOAPPS_MNGR_FLAGS_WITHTOPO | AOAPPS_MNGR_FLAGS_WITHREPAIR, 
    aoapps_runled_start, aoapps_runled_step, aoapps_runled_stop, 
    0, 0 /* no config command */ );
}


