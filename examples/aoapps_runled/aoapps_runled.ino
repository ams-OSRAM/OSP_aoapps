// aoapps_runled.ino - - illustrates how to run a single app
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
#include <aospi.h>         // aospi_init()
#include <aoosp.h>         // aoosp_init()
#include <aomw.h>          // aomw_init()
#include <aoapps_runled.h> // aoapps_runled_step()

/*
DESCRIPTION
This demo shows how to run an app.
It uses the runled app, which has been made to expose its internals;
the start, step and stop function.

HARDWARE
The demo runs on the OSP32 board, with the SAIDbasic board connected.
In Arduino select board "ESP32S3 Dev Module".

BEHAVIOR
See aoapps_runled.cpp for how the running led behaves; but informally
it a a color bar going left to righ and back in changing colors.

OUTPUT
Welcome to aoapps_runled.ino

spi: init
osp: init
mw: init
topo ok
*/


void setup() {
  Serial.begin(115200);
  do delay(250); while( ! Serial );
  Serial.printf("\n\nWelcome to aoapps_runled.ino\n\n");

  // Initialize all libraries
  aospi_init(); 
  aoosp_init();
  aomw_init();
  aoresult_t result = aomw_topo_build(); // runled uses topo
  Serial.printf("topo %s\n", aoresult_to_str(result) );

  // Start the app
  aoapps_runled_start();
}


void loop() {
  // Animate the app
  aoapps_runled_step();
}

