// aoapps_dither.cpp - the "dither" app animates in shades of white, a switch toggles MSB dithering
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
#include <aoapps_dither.h> // own


/*
DITHER - This is one of the stock applications

DESCRIPTION
- The LEDs are in a dimming cycle (dim up, then dim down, then up again, etc).
- All LEDs dim synchronously and at the same level (so RGBs look white).
- Dithering can be enabled/disabled

BUTTONS
- The X button toggles dim cycling on/off.
- The Y button toggles dithering on/off.

GOAL
- To show the effect of dithering
- View the LEDs with a mobile phone camera in video mode to see flickering starts when dithering is disabled,
  or use LED Light Flicker Meter (https://play.google.com/store/apps/details?id=com.contechity.flicker_meter).
*/


// === Animation helpers ====================================================
// We are lazy: the below functions send multiple telegrams, 
// that should have been spread over multiple aoapps_dither_anim_step()


// For all SAIDs, set the dithering flag of its three channels
static aoresult_t aoapps_dither_anim_setdither(int enadither) {
  uint8_t flags = enadither ? AOOSP_CURCHN_FLAGS_DITHER|AOOSP_CURCHN_CUR_DEFAULT : AOOSP_CURCHN_CUR_DEFAULT;
  // Loop over all nodes to set dithering
  for( uint16_t addr=1; addr<=aomw_topo_numnodes(); addr++ ) {
    aoresult_t result= aomw_topo_node_setcurrents(addr, flags);
    if( result!=aoresult_ok ) return result;
  }
  return aoresult_ok;
}


// For all triplets, r, g, and b will be set to `dimlvl`
static aoresult_t aoapps_dither_anim_setdim(uint16_t dimlvl) {
  aomw_topo_rgb_t rgb= { dimlvl, dimlvl, dimlvl, "grey" };
  // Loop over all triplets to set dimlvl
  for( uint16_t tix=0; tix<aomw_topo_numtriplets(); tix++ ) {
    aoresult_t result= aomw_topo_settriplet(tix, &rgb);
    if( result!=aoresult_ok ) return result;
  }
  return aoresult_ok;
}


// === Animation state machine ==============================================


// Time (in ms) between two animation steps
#define AOAPPS_DITHER_ANIM_MS         25
// Steps in dim level
#define AOAPPS_DITHER_DIMLVL_PERKIBI 32 // if val is x, num steps is approx log(32767)/log(1+x/1024)


// The state of the dither state machine
static uint16_t aoapps_dither_anim_dimlvl;    // 0..32767
static int      aoapps_dither_anim_dir;       // -1=dimdown, +1=dimup
static int      aoapps_dither_anim_enadim;    // 0=disabled, 1=enabled
static int      aoapps_dither_anim_enadither; // 0=disabled, 1=enabled
static uint32_t aoapps_dither_anim_ms;


// Step of the dither state machine
static aoresult_t aoapps_dither_anim() {
  aoresult_t result;
  // Was there a request to toggle `enadither`
  if( aoui32_but_wentdown(AOUI32_BUT_Y) ) {
    aoapps_dither_anim_enadither= !aoapps_dither_anim_enadither;
    // Effectuate new dither state
    result= aoapps_dither_anim_setdither(aoapps_dither_anim_enadither);
    if( result!=aoresult_ok ) return result;
    // Several telegrams were sent, delay dim by one step
    return aoresult_ok;
  }

  // Was there a request to toggle `enadim`
  if( aoui32_but_wentdown(AOUI32_BUT_X) ) {
    aoapps_dither_anim_enadim= !aoapps_dither_anim_enadim;
    // Trigger an update
    aoapps_dither_anim_ms= millis()-AOAPPS_DITHER_ANIM_MS;
  }

  // Is it time for a dim animation step
  if( millis()-aoapps_dither_anim_ms < AOAPPS_DITHER_ANIM_MS ) return aoresult_ok; 
  aoapps_dither_anim_ms = millis();

  // Is dim animation enabled?
  if( ! aoapps_dither_anim_enadim ) return aoresult_ok;
  
  // Compute next dimlvl
  int step= aoapps_dither_anim_dimlvl*AOAPPS_DITHER_DIMLVL_PERKIBI/1024 +1; // +1 ensures step is not 0
  int new_lvl = aoapps_dither_anim_dimlvl + aoapps_dither_anim_dir * step;
  if( new_lvl<0 ) { // clip to min an reverse direction
    aoapps_dither_anim_dimlvl= 0;
    aoapps_dither_anim_dir= +1;
  } else if( new_lvl>AOMW_TOPO_BRIGHTNESS_MAX ) { // clip to max an reverse direction
    aoapps_dither_anim_dimlvl= AOMW_TOPO_BRIGHTNESS_MAX;
    aoapps_dither_anim_dir= -1;
  } else { // use new level
    aoapps_dither_anim_dimlvl= new_lvl;
  }
  
  // Effectuate the new level
  result= aoapps_dither_anim_setdim(aoapps_dither_anim_dimlvl);
  if( result!=aoresult_ok ) return result;
  
  return aoresult_ok;
}


// === Top-level state machine ==============================================


// The application manager entry point (start)
static aoresult_t aoapps_dither_start() {
  aoapps_dither_anim_dimlvl= 0;
  aoapps_dither_anim_dir= +1;
  aoapps_dither_anim_enadim= 1;
  aoapps_dither_anim_enadither= 1;
  aoapps_dither_anim_ms= millis()-AOAPPS_DITHER_ANIM_MS;
  // Effectuate state
  aoresult_t result;
  result= aoapps_dither_anim_setdim(aoapps_dither_anim_dimlvl);
  if( result!=aoresult_ok ) return result;
  result= aoapps_dither_anim_setdither(aoapps_dither_anim_enadither);
  if( result!=aoresult_ok ) return result;
  return aoresult_ok;
}


// The application manager entry point (step)
static aoresult_t aoapps_dither_step() {
  aoresult_t result;
  // actual animation
  result= aoapps_dither_anim();
  if( result!=aoresult_ok ) return result;
  // return success
  return aoresult_ok;
}


// The application manager entry point (stop)
static void aoapps_dither_stop() {
  // nothing to restore
}


// === Registration ==========================================================


/*!
    @brief  Registers the dither app with the app manager.
    @note   This app has a dark to light to dark dimming cycle (in white).
            The dithering feature of the SAID can be en- or disabled
            by pressing the Y button.
    @note   There must be a SAID in the chain (because dithering is a SAID 
            feature). The OSP32 board would be enough.
*/
void aoapps_dither_register() {
  aoapps_mngr_register("dither", "Dithering", "dim 0/1", "dither 0/1", 
    AOAPPS_MNGR_FLAGS_WITHTOPO | AOAPPS_MNGR_FLAGS_WITHREPAIR, 
    aoapps_dither_start, aoapps_dither_step, aoapps_dither_stop, 
    0, 0 /* no config command */ );
}


