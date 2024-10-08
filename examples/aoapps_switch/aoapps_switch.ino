// aoapps_switch.ino - illustrates how to switch between registered apps
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
#include <aoui32.h>        // aoui32_oled_splash()
#include <aoapps.h>        // aoapps_mngr_start()


/*
DESCRIPTION
This demo implements an application with two (dummy) apps.
By pressing but A, the user can switch between the apps.
The name of the active app is shown on the OLED.
The green and red signaling LED show heart beat and error.
App1 shows how to process button presses.
App2 goes into error after a while (to show that the green heart beat 
LED stops and the red error LED switches on).

HARDWARE
The demo runs on the OSP32 board (uses the A, X, and Y buttons).
In Arduino select board "ESP32S3 Dev Module".

BEHAVIOR
When demo starts, OLED shows that "Application 1" is running and that 
X and Y cause a print. When pressing X or Y, we see that on Serial.
While running, the green/ok LED blinks ("heartbeat").
We switch to the next app by pressing A. The OLED shows "Application 2".
The X and Y have no function. Green/ok led blinks heartbeat. After a 
while Application 2 goes into error. Heartbeat stops, red led is on
permanently, and the OLED shows an error message (from aoresult_other).
If the flag AOAPPS_MNGR_FLAGS_NEXTONERR is passed to application 2,
the app manager will switch automatically to the next app (Application 1) 
when the error occurs (and some wait).

OUTPUT
Welcome to aoapps_switch
ui32: init
apps: init

app1: start
app1: step
app1: step
app1: step
app1: step
app1: step
app1: but X
app1: step
app1: but Y
app1: step
app1: step
app1: step
app1: stop

app2: start
app2: step
app2: step
app2: step
app2: step
app2: step
app2: step
app2: step (pretend error)
apps: ERROR in app 'app2': other
app2: stop

app1: start
app1: step
app1: step
app1: step
*/


// === dummy app1 ===========================================================


static uint32_t last;

static aoresult_t app1_start() {
  Serial.printf("app1: start\n");
  last= millis();
  return aoresult_ok;
}

static aoresult_t app1_step() {
  if( aoui32_but_wentdown(AOUI32_BUT_X) ) Serial.printf("app1: but X\n"); 
  if( aoui32_but_wentdown(AOUI32_BUT_Y) ) Serial.printf("app1: but Y\n"); 
  if( millis()-last>2000 ) { Serial.printf("app1: step\n"); last=millis(); }
  return aoresult_ok;
}

static void app1_stop() {
  Serial.printf("app1: stop\n\n");
}

void app1_register() {
  aoapps_mngr_register("app1", "Application 1", "print X", "print Y", AOAPPS_MNGR_FLAGS_NONE, app1_start, app1_step, app1_stop, 0, 0);
}


// === dummy app2 ===========================================================


static int      count;

static aoresult_t app2_start() {
  Serial.printf("app2: start\n");
  last= millis();
  count=0;
  return aoresult_ok;
}

static aoresult_t app2_step() {
  if( count>5 ) { Serial.printf("app2: step (pretend error)\n"); return aoresult_other; }
  if( millis()-last>2000 ) { Serial.printf("app2: step\n"); last=millis(); count++; }
  return aoresult_ok;
}

static void app2_stop() {
  Serial.printf("app2: stop\n\n");
}

void app2_register() {
  // Option: add flag AOAPPS_MNGR_FLAGS_NEXTONERR
  aoapps_mngr_register("app2", "Application 2", "--", "--", AOAPPS_MNGR_FLAGS_NONE , app2_start, app2_step, app2_stop, 0, 0); 
}


// === application ==========================================================


// Pick apps that we want in this application (either from the aoapps library, or the local ones)
void apps_register() {
  app1_register();
  app2_register();
}


void setup() {
  // Identify over Serial
  Serial.begin(115200);
  do delay(250); while( ! Serial );
  Serial.printf("\n\n\nWelcome to aoapps_switch\n");

  // Initialize all libraries
  aoui32_init();
  aoapps_init();
  apps_register();
  Serial.printf("\n");

  // Start the first app
  aoapps_mngr_start();
}


void loop() {
  // Check physical buttons
  aoui32_but_scan();

  // Switch to next app when A was pressed
  if( aoui32_but_wentdown(AOUI32_BUT_A) ) aoapps_mngr_switchnext();

  // Let current app progress
  aoapps_mngr_step();
}



