// aoapps_mngr.h - apps manager (records the entry functions associated with all registered apps)
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
#ifndef _AOAPPS_MNGR_H_
#define _AOAPPS_MNGR_H_


#include <aoresult.h>     // aoresult_t


// Total number of registration slots for apps.
#define AOAPPS_MNGR_REGISTRATION_SLOTS 8


// The handler signatures for an app
typedef aoresult_t (*aoapps_mngr_start_t)(void); // Function starting the (state machine of) the app.
typedef aoresult_t (*aoapps_mngr_step_t )(void); // Function progressing the (state machine of) the app.
typedef void       (*aoapps_mngr_stop_t )(void); // Function stopping the app (shuts down hardware that is no longer needed, may result in errors, but is ignored anyhow).

// An app may implement a command handler plugin for configuration. It is much like C's main, it has argc and argv.
typedef void       (*aoapps_mngr_cmd_t)( int argc, char * argv[] );

// Extra features can be enabled in the app manager
#define AOAPPS_MNGR_FLAGS_NONE        0x00
#define AOAPPS_MNGR_FLAGS_WITHTOPO    0x01
#define AOAPPS_MNGR_FLAGS_WITHREPAIR  0x02
#define AOAPPS_MNGR_FLAGS_NEXTONERR   0x04
#define AOAPPS_MNGR_FLAGS_ALL         (AOAPPS_MNGR_FLAGS_WITHTOPO | AOAPPS_MNGR_FLAGS_WITHREPAIR | AOAPPS_MNGR_FLAGS_NEXTONERR ) 

// To register an app pass its (identifier and oled) name, help text for the two buttons, feature flags, pointers to its three handlers, command handler and command help. Asserts when no more free slots.
void aoapps_mngr_register(const char * name, const char * oled, const char * xlbl, const char * ylbl, int flags, aoapps_mngr_start_t start, aoapps_mngr_step_t step, aoapps_mngr_stop_t stop, aoapps_mngr_cmd_t cmd, const char * help);  
// Initializes the apps manager (selects app 0, but does not run it)
void aoapps_mngr_init();


// Starts the app in slot appix to be the current (no app must be running before)
void aoapps_mngr_start(int appix=1); // 0 is the voidapp
// Steps the current app (an app must be running)
void aoapps_mngr_step();
// Stops the current app (an app must be running)
void aoapps_mngr_stop();


// Switches the current app (must be running) to the app at appix; 0 <= appix < aoapps_mngr_app_count()
void aoapps_mngr_switch(int appix);
// Switches the current app (must be running) to the next app
void aoapps_mngr_switchnext();


// Returns index of current app
int aoapps_mngr_app_appix();
// Returns if current app is running
int aoapps_mngr_app_running();
// Returns number of registered apps
int aoapps_mngr_app_count();
// Gets the short name (identifier) of the app at appix; 0 <= appix < aoapps_mngr_app_count()
const char * aoapps_mngr_app_name(int appix);
// Gets the long name (for oled) of the app at appix; 0 <= appix < aoapps_mngr_app_count()
const char * aoapps_mngr_app_oled(int appix);


// Registers the "app" command with the command interpreter.
int aoapps_mngr_cmd_register();


#endif
