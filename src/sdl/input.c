/*
 * sdl/input.c - SDL library specific port code - input device support
 *
 * Copyright (c) 2001-2002 Jacek Poplawski
 * Copyright (C) 2001-2014 Atari800 development team (see DOC/CREDITS)
 *
 * also edited by adept_zap to add better gamepad support (2021)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifdef __linux__
#define LPTJOY	1
#endif

#ifdef LPTJOY
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/lp.h>
#endif /* LPTJOY */

#include <SDL.h>

#include "config.h"
#include "sdl/input.h"
#include "akey.h"
#include "atari.h"
#include "binload.h"
#include "colours.h"
#include "filter_ntsc.h"
#include "../input.h"
#include "log.h"
#include "platform.h"
#include "pokey.h"
#include "sdl/video.h"
#include "ui.h"
#include "util.h"
#include "videomode.h"
#include "screen.h"
#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD
#include "ui_basic.h"
#endif

static int grab_mouse = FALSE;
static int swap_joysticks = FALSE;

/* joystick emulation
   keys are loaded from config file
   Here the defaults if there is no keymap in the config file... */

/* a runtime switch for the kbd_joy_X_enabled vars is in the UI */
int PLATFORM_kbd_joy_0_enabled = TRUE;	/* enabled by default, doesn't hurt */
int PLATFORM_kbd_joy_1_enabled = FALSE;	/* disabled, would steal normal keys */

static int KBD_TRIG_0 = SDLK_RCTRL;
static int KBD_STICK_0_LEFT = SDLK_KP4;
static int KBD_STICK_0_RIGHT = SDLK_KP6;
static int KBD_STICK_0_DOWN = SDLK_KP5;
static int KBD_STICK_0_UP = SDLK_KP8;
static int KBD_TRIG_1 = SDLK_LCTRL;
static int KBD_STICK_1_LEFT = SDLK_a;
static int KBD_STICK_1_RIGHT = SDLK_d;
static int KBD_STICK_1_DOWN = SDLK_s;
static int KBD_STICK_1_UP = SDLK_w;

/* Maping for the START, RESET, OPTION, SELECT and EXIT keys */
static int KBD_UI = SDLK_F1;
static int KBD_OPTION = SDLK_F2;
static int KBD_SELECT = SDLK_F3;
static int KBD_START = SDLK_F4;
static int KBD_RESET = SDLK_F5;
static int KBD_HELP = SDLK_F6;
static int KBD_BREAK = SDLK_F7;
static int KBD_MON = SDLK_F8;
static int KBD_EXIT = SDLK_F9;
static int KBD_SSHOT = SDLK_F10;
static int KBD_TURBO = SDLK_F12;

/* real joysticks */

static int fd_joystick0 = -1;
static int fd_joystick1 = -1;

/*atari joystick state struct*/
typedef struct atari_joystick_state {
	unsigned int port;
	unsigned int trig;
} atari_joystick_state;


/*---------------------------------------------------------------------------------------------------------------------
 *GamePad support
 *---------------------------------------------------------------------------------------------------------------------*/

/* gamepad functions*/
/*0 - none*/
/*1 - 255 - internal functions*/
/*256 - 512 - press and release functions*/
/*512 - 767 - press only functions*/
/*768 - 1023 - press only functions but with key code reading*/
#define FNPAD_NONE					0
#define FNPAD_SP_HOLD				1
#define FNPAD_FIRE_HOLD				2
#define FNPAD_FIRE_TOGGLE			3
#define FNPAD_AUTOFIRE_HOLD			4
#define FNPAD_AUTOFIRE_TOGGLE		5
#define FNPAD_START_HOLD			6
#define FNPAD_SELECT_HOLD			7
#define FNPAD_OPTION_HOLD			8
#define FNPAD_TURBO_HOLD			256
#define FNPAD_TURBO_TOGGLE			512
#define FNPAD_EXIT					513
#define FNPAD_UI					514
#define FNPAD_SAVESTATE				515
#define FNPAD_LOADSTATE				516
#define FNPAD_WARMSTART				517
#define FNPAD_COLDSTART				518
#define FNPAD_BREAK					519
#define FNPAD_SCREENSHOT			520
#define FNPAD_SCREENSHOT_INTERLACE	521
#define FNPAD_KEYB					522
#define FNPAD_HELP					523
#define FNPAD_ESCAPE				524
#define FNPAD_KEY_SPACEBAR			525

#define FNPAD_CODE_					768

typedef struct gamepads_sdl_state_t {
	/*-1,0,1 for x axis*/
	int x;
	/*-1,0,1 for y axis*/
	int y;
	/*same as x only for hat*/
	int hx;
	/*same as y only for hat*/
	int hy;
	/*every bit represents button state (16 bits)*/
	unsigned int buttons;
	/*TRUE if special button is pressed*/
	int special;
} gamepads_sdl_state_t;

typedef struct gamepads_fire_state_t {
	/*phase of autofire, if it is less than autofire frequency then fire will be pressed, if greater then not*/
	int autofire_phase;
	/*tells if autofire from toggle is on (not from autofire hold button)*/
	int autofire_toggle_on;
	/*tells if autofire is on;*/
	int autofire_actual_on;
	/*tells if autofire in last update was on*/
	int autofire_last_on;
	/*tells if fire toggle is on (not from fire hold button)*/
	int fire_toggle_on;
	/*result of both autofire and fire buttons adn states*/
	int fire;
} gamepads_fire_state_t;

/*maximum number of supported gamepads*/
#define MAX_GAMEPADS			4
/*maximum number of supported buttons per gamepad*/
#define MAX_GAMEPAD_BUTTONS		16
/*default deadzone size for gamepads*/
#define DEFAULT_DEADZONE 15000
/*gamepads detected by SDL system*/
static SDL_Joystick *sdl_gamepads[MAX_GAMEPADS] = { NULL, NULL, NULL, NULL };
/*tells if config was reseting*/
static int gamepad_config_reset = FALSE;
/*gamepads configuration (also used in ui.c)*/
static SDL_INPUT_RealJSConfig_t gamepad_configuration[MAX_GAMEPADS];
/*mask helping to detect if special function was invoked (button pressed with special button at the same time)*/
static unsigned int gamepad_special_mask[MAX_GAMEPADS];
/*mask helping to detect if atari key buttons is pressed/released*/
static unsigned int gamepad_atari_key_mask[MAX_GAMEPADS];
/*tells if masks are valid*/
static int gamepad_masks_valid;
/*finite state of the atari joystick for gamepads*/
static atari_joystick_state gamepads_atari_joystick_state[MAX_GAMEPADS];
/*pad state during previous read*/
static gamepads_sdl_state_t gamepads_sdl_last_state[MAX_GAMEPADS];
/*pad state during actual read*/
static gamepads_sdl_state_t gamepads_sdl_actual_state[MAX_GAMEPADS];
/*start/select/option state from gamepads*/
static int gamepads_consol_state;
/*number of gamepads found by sdl system*/
static int gamepads_found = 0;
/*number of buttons found on each gamepad*/
static int gamepads_max_buttons[MAX_GAMEPADS];
/*state of all things connected to fire buttons*/
static gamepads_fire_state_t gamepads_fire_state[MAX_GAMEPADS];

/******************************************************************************/
/*StringToPadFunction*/
/*changes config string to config enum value.*/
/******************************************************************************/
static int StringToPadFunction(char *padFunctionString)
{
	if (strcmp(padFunctionString, "FNPAD_NONE") == 0) return FNPAD_NONE;
	else if (strcmp(padFunctionString, "FNPAD_SP_HOLD") == 0) return FNPAD_SP_HOLD;
	else if (strcmp(padFunctionString, "FNPAD_FIRE_HOLD") == 0) return FNPAD_FIRE_HOLD;
	else if (strcmp(padFunctionString, "FNPAD_FIRE_TOGGLE") == 0) return FNPAD_FIRE_TOGGLE;
	else if (strcmp(padFunctionString, "FNPAD_AUTOFIRE_HOLD") == 0) return FNPAD_AUTOFIRE_HOLD;
	else if (strcmp(padFunctionString, "FNPAD_AUTOFIRE_TOGGLE") == 0) return FNPAD_AUTOFIRE_TOGGLE;
	else if (strcmp(padFunctionString, "FNPAD_START_HOLD") == 0) return FNPAD_START_HOLD;
	else if (strcmp(padFunctionString, "FNPAD_SELECT_HOLD") == 0) return FNPAD_SELECT_HOLD;
	else if (strcmp(padFunctionString, "FNPAD_OPTION_HOLD") == 0) return FNPAD_OPTION_HOLD;
	else if (strcmp(padFunctionString, "FNPAD_TURBO_HOLD") == 0) return FNPAD_TURBO_HOLD;
	else if (strcmp(padFunctionString, "FNPAD_TURBO_TOGGLE") == 0) return FNPAD_TURBO_TOGGLE;
	else if (strcmp(padFunctionString, "FNPAD_EXIT") == 0) return FNPAD_EXIT;
	else if (strcmp(padFunctionString, "FNPAD_UI") == 0) return FNPAD_UI;
	else if (strcmp(padFunctionString, "FNPAD_SAVESTATE") == 0) return FNPAD_SAVESTATE;
	else if (strcmp(padFunctionString, "FNPAD_LOADSTATE") == 0) return FNPAD_LOADSTATE;
	else if (strcmp(padFunctionString, "FNPAD_WARMSTART") == 0) return FNPAD_WARMSTART;
	else if (strcmp(padFunctionString, "FNPAD_COLDSTART") == 0) return FNPAD_COLDSTART;
	else if (strcmp(padFunctionString, "FNPAD_BREAK") == 0) return FNPAD_BREAK;
	else if (strcmp(padFunctionString, "FNPAD_SCREENSHOT") == 0) return FNPAD_SCREENSHOT;
	else if (strcmp(padFunctionString, "FNPAD_SCREENSHOT_INTERLACE") == 0) return FNPAD_SCREENSHOT_INTERLACE;
	else if (strcmp(padFunctionString, "FNPAD_KEYB") == 0) return FNPAD_KEYB;
	else if (strcmp(padFunctionString, "FNPAD_HELP") == 0) return FNPAD_HELP;
	else if (strcmp(padFunctionString, "FNPAD_ESCAPE") == 0) return FNPAD_ESCAPE;
	else if (strcmp(padFunctionString, "FNPAD_KEY_SPACEBAR") == 0) return FNPAD_KEY_SPACEBAR;
	else if (strncmp(padFunctionString, "FNPAD_CODE_", 11) == 0) {
		int code = Util_sscanhex(&(padFunctionString[11]));
		if (code >= 0x00 && code <= 0xff) {
			return FNPAD_CODE_ + code;
		}
	}
	return FNPAD_NONE;
}

/******************************************************************************/
/*PadFunctionToString*/
/*changes config enum value to config string.*/
/******************************************************************************/
static void PadFunctionToString(char *outString, int padFunction)
{
	switch (padFunction)
	{
		case FNPAD_NONE: sprintf(outString, "FNPAD_NONE"); break;
		case FNPAD_SP_HOLD: sprintf(outString, "FNPAD_SP_HOLD"); break;
		case FNPAD_FIRE_HOLD: sprintf(outString, "FNPAD_FIRE_HOLD"); break;
		case FNPAD_FIRE_TOGGLE: sprintf(outString, "FNPAD_FIRE_TOGGLE"); break;
		case FNPAD_AUTOFIRE_HOLD: sprintf(outString, "FNPAD_AUTOFIRE_HOLD"); break;
		case FNPAD_AUTOFIRE_TOGGLE: sprintf(outString, "FNPAD_AUTOFIRE_TOGGLE"); break;
		case FNPAD_START_HOLD: sprintf(outString, "FNPAD_START_HOLD"); break;
		case FNPAD_SELECT_HOLD: sprintf(outString, "FNPAD_SELECT_HOLD"); break;
		case FNPAD_OPTION_HOLD: sprintf(outString, "FNPAD_OPTION_HOLD"); break;
		case FNPAD_TURBO_HOLD: sprintf(outString, "FNPAD_TURBO_HOLD"); break;
		case FNPAD_TURBO_TOGGLE: sprintf(outString, "FNPAD_TURBO_TOGGLE"); break;
		case FNPAD_EXIT: sprintf(outString, "FNPAD_EXIT"); break;
		case FNPAD_UI: sprintf(outString, "FNPAD_UI"); break;
		case FNPAD_SAVESTATE: sprintf(outString, "FNPAD_SAVESTATE"); break;
		case FNPAD_LOADSTATE: sprintf(outString, "FNPAD_LOADSTATE"); break;
		case FNPAD_WARMSTART: sprintf(outString, "FNPAD_WARMSTART"); break;
		case FNPAD_COLDSTART: sprintf(outString, "FNPAD_COLDSTART"); break;
		case FNPAD_BREAK: sprintf(outString, "FNPAD_BREAK"); break;
		case FNPAD_SCREENSHOT: sprintf(outString, "FNPAD_SCREENSHOT"); break;
		case FNPAD_SCREENSHOT_INTERLACE: sprintf(outString, "FNPAD_SCREENSHOT_INTERLACE"); break;
		case FNPAD_KEYB: sprintf(outString, "FNPAD_KEYB"); break;
		case FNPAD_HELP: sprintf(outString, "FNPAD_HELP"); break;
		case FNPAD_ESCAPE: sprintf(outString, "FNPAD_ESCAPE"); break;
		case FNPAD_KEY_SPACEBAR: sprintf(outString, "FNPAD_KEY_SPACEBAR"); break;
		default:
			if (padFunction >= FNPAD_CODE_ && padFunction <= FNPAD_CODE_ + 0xff) {
				sprintf(outString, "FNPAD_CODE_%02X", padFunction - FNPAD_CODE_);
			}
			else {
				sprintf(outString, "FNPAD_NONE");
			}
	}
}

/******************************************************************************/
/*GamePads_DetermineMasks*/
/*determines mask helpers*/
/******************************************************************************/
static void GamePads_DetermineMasks(int gamepadNumber)
{
	int i;
	/*determining special mask*/
	gamepad_special_mask[gamepadNumber] = 0;
	for (i = 0; i < MAX_PAD_BUTTONS; i++) {
		if (gamepad_configuration[gamepadNumber].button_functions[i] == FNPAD_SP_HOLD) {
			gamepad_special_mask[gamepadNumber] |= 1 << i;
		}
	}
	gamepad_special_mask[gamepadNumber] |= gamepad_special_mask[gamepadNumber] << 16;
	/*determining other masks*/
	gamepad_atari_key_mask[gamepadNumber] = 0;
	for (i = 0; i < MAX_PAD_BUTTONS; i++) {
		if (gamepad_configuration[gamepadNumber].button_functions[i] >= 256) {
			gamepad_atari_key_mask[gamepadNumber] |= 1 << i;
		}
		if (gamepad_configuration[gamepadNumber].button_sp_functions[i] >= 256) {
			gamepad_atari_key_mask[gamepadNumber] |= 1 << (i + 16);
		}
	}
}

/******************************************************************************/
/*GamePads_Reset_Config*/
/*Reset configurations for the gamepads.*/
/******************************************************************************/
static void GamePads_Reset_Config(void)
{
    int i;
	int j;
    for (i = 0; i < MAX_GAMEPADS; i++) {
		gamepad_configuration[i].radial = 1;
		gamepad_configuration[i].deadzone = 15000;
		gamepad_configuration[i].tolerance = 0.1;
		gamepad_configuration[i].radial_tolerance = 0.1;
		gamepad_configuration[i].use_as_stick = TRUE;
		gamepad_configuration[i].use_hat_as_stick = TRUE;
		gamepad_configuration[i].use_in_menus = FALSE;
		gamepad_configuration[i].use_hat_in_menus = FALSE;
		gamepad_configuration[i].in_menus_select_button = 0;
		gamepad_configuration[i].in_menus_back_button = 1;
		gamepad_configuration[i].autofire_freq = 2;
		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++) {
			gamepad_configuration[i].button_functions[j] = FNPAD_FIRE_HOLD;
			gamepad_configuration[i].button_sp_functions[j] = FNPAD_FIRE_HOLD;
		}
		GamePads_DetermineMasks(i);
    }
	gamepad_masks_valid = TRUE;
}

/******************************************************************************/
/*GamePads_Write_Config*/
/*Write configurations for the gamepads.*/
/******************************************************************************/
static void GamePads_Write_Config(FILE* fp)
{
    int i;
	int j;
	char outString[50];

	if (!gamepad_config_reset) {
		GamePads_Reset_Config();
		gamepad_config_reset = TRUE;
	}
    for (i = 0; i < MAX_GAMEPADS; i++) {
        fprintf(fp, "SDL_PAD_%d_JOY_RADIAL=%d\n", i, gamepad_configuration[i].radial);
        fprintf(fp, "SDL_PAD_%d_JOY_DEADZONE=%d\n", i, gamepad_configuration[i].deadzone);
        fprintf(fp, "SDL_PAD_%d_JOY_TOLERANCE=%f\n", i, gamepad_configuration[i].tolerance);
        fprintf(fp, "SDL_PAD_%d_JOY_RADIAL_TOLERANCE=%f\n", i, gamepad_configuration[i].radial_tolerance);
        fprintf(fp, "SDL_PAD_%d_JOY_USE_AS_STICK=%d\n", i, gamepad_configuration[i].use_as_stick);
        fprintf(fp, "SDL_PAD_%d_HAT_USE_AS_STICK=%d\n", i, gamepad_configuration[i].use_hat_as_stick);
        fprintf(fp, "SDL_PAD_%d_JOY_USE_IN_MENUS=%d\n", i, gamepad_configuration[i].use_in_menus);
        fprintf(fp, "SDL_PAD_%d_HAT_USE_IN_MENUS=%d\n", i, gamepad_configuration[i].use_hat_in_menus);
        fprintf(fp, "SDL_PAD_%d_IN_MENUS_SELECT_BUTTON=%d\n", i, gamepad_configuration[i].in_menus_select_button);
        fprintf(fp, "SDL_PAD_%d_IN_MENUS_BACK_BUTTON=%d\n", i, gamepad_configuration[i].in_menus_back_button);
        fprintf(fp, "SDL_PAD_%d_AUTOFIRE_FREQ=%d\n", i, gamepad_configuration[i].autofire_freq);
		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++) {
			PadFunctionToString(outString, gamepad_configuration[i].button_functions[j]);
			fprintf(fp, "SDL_PAD_%d_BUTTON_%d_FUNC=%s\n", i, j, outString);
		}
		for (j = 0; j < MAX_GAMEPAD_BUTTONS; j++) {
			PadFunctionToString(outString, gamepad_configuration[i].button_sp_functions[j]);
			fprintf(fp, "SDL_PAD_%d_BUTTON_%d_SP_FUNC=%s\n", i, j, outString);
		}
    }
}

/******************************************************************************/
/*GamePads_Read_Config*/
/*Read configurations for the gamepads.*/
/******************************************************************************/
static void GamePads_Read_Config(char *option, char *parameters)
{
	char str[3];
	double number;
	char *cnt;
	int len;
	int joy_number,button_number;
	char *option0;
	char *option1;

	if (!gamepad_config_reset) {
		GamePads_Reset_Config();
		gamepad_config_reset = TRUE;
	}

	if (parameters == NULL)
		return;

	gamepad_masks_valid = FALSE;
	/*looking for pad number*/
	cnt = strchr(option, '_');
	if (cnt == NULL) return;
	len = cnt - option;
	if (len > 2 || len == 0) return;
	strncpy(str, option, len);
	str[len] = '\0';
	joy_number = Util_sscandec(str);
	if (joy_number >= MAX_GAMEPADS) {
		return;
	}
	option = &option[len];
	if (strcmp(option, "_JOY_RADIAL") == 0) {
		gamepad_configuration[joy_number].radial = Util_sscanbool(parameters);
		return;
	}
	else if (strcmp(option, "_JOY_DEADZONE") == 0) {
		gamepad_configuration[joy_number].deadzone = Util_sscandec(parameters);
		return;
	}
	else if (strcmp(option, "_JOY_TOLERANCE") == 0) {
		Util_sscandouble(parameters, &number);
		gamepad_configuration[joy_number].tolerance = number;
		return;
	}
	else if (strcmp(option, "_JOY_RADIAL_TOLERANCE") == 0) {
		Util_sscandouble(parameters, &number);
		gamepad_configuration[joy_number].radial_tolerance = number;
		return;
	}
	else if (strcmp(option, "_JOY_USE_AS_STICK") == 0) {
		gamepad_configuration[joy_number].use_as_stick = Util_sscanbool(parameters);
		return;
	}
	else if (strcmp(option, "_HAT_USE_AS_STICK") == 0) {
		gamepad_configuration[joy_number].use_hat_as_stick = Util_sscanbool(parameters);
		return;
	}
	else if (strcmp(option, "_JOY_USE_IN_MENUS") == 0) {
		gamepad_configuration[joy_number].use_in_menus = Util_sscanbool(parameters);
		return;
	}
	else if (strcmp(option, "_HAT_USE_IN_MENUS") == 0) {
		gamepad_configuration[joy_number].use_hat_in_menus = Util_sscanbool(parameters);
		return;
	}
	else if (strcmp(option, "_IN_MENUS_SELECT_BUTTON") == 0) {
		gamepad_configuration[joy_number].in_menus_select_button = Util_sscandec(parameters);
		return;
	}
	else if (strcmp(option, "_AUTOFIRE_FREQ") == 0) {
		gamepad_configuration[joy_number].autofire_freq = Util_sscandec(parameters);
		if (gamepad_configuration[joy_number].autofire_freq <= 0) {
			gamepad_configuration[joy_number].autofire_freq = 1;
		}
		return;
	}
	else if (strncmp(option, "_BUTTON_", 8) == 0) {
		option0 = option + 8;
		cnt = strchr(option0, '_');
		if (cnt == NULL) return;
		len = cnt - option0;
		if (len > 2 || len == 0) return;
		strncpy(str, option0, len);
		str[len] = '\0';
		button_number = Util_sscandec(str);
		if (button_number >= MAX_GAMEPAD_BUTTONS) {
			return;
		}
		option1 = &option0[len];
		if (strcmp(option1, "_FUNC") == 0) {
			gamepad_configuration[joy_number].button_functions[button_number] = StringToPadFunction(parameters);
			return;
		}
		if (strcmp(option1, "_SP_FUNC") == 0) {
			gamepad_configuration[joy_number].button_sp_functions[button_number] = StringToPadFunction(parameters);
			return;
		}
		return;
	}
}

/******************************************************************************/
/*GamePads_Init*/
/*Initialize gamepads reading.*/
/*first,second - -1 means that this joy should be disabled.*/
/******************************************************************************/
static void GamePads_Init(int first, int second)
{
	int i;

	if (!gamepad_config_reset) {
		GamePads_Reset_Config();
		gamepad_config_reset = TRUE;
	}
	/*reseting states*/
	for(i = 0; i < MAX_GAMEPADS; i++) {
		gamepads_atari_joystick_state[i].port = INPUT_STICK_CENTRE;
		gamepads_atari_joystick_state[i].trig = TRUE;
		gamepads_sdl_last_state[i].x = 0;
		gamepads_sdl_last_state[i].y = 0;
		gamepads_sdl_last_state[i].hx = 0;
		gamepads_sdl_last_state[i].hy = 0;
		gamepads_sdl_last_state[i].buttons = 0;
		gamepads_fire_state[i].autofire_phase = 0;
		gamepads_fire_state[i].autofire_toggle_on = 0;
		gamepads_fire_state[i].autofire_actual_on = 0;
		gamepads_fire_state[i].autofire_last_on = 0;
		gamepads_fire_state[i].fire_toggle_on = 0;
		gamepads_fire_state[i].fire = 0;
	}
	gamepads_consol_state = INPUT_CONSOL_NONE;
	/*looking for gamepads*/
	gamepads_found = 0;
	for(i = 0; i < SDL_NumJoysticks() && gamepads_found < MAX_GAMEPADS; i++) {
		sdl_gamepads[gamepads_found] = SDL_JoystickOpen(i);
		if (sdl_gamepads[gamepads_found] == NULL) {
			Log_print("Joystick %i not found", i);
		}
		else {
			Log_print("Joystick %i found", i);
			gamepads_max_buttons[gamepads_found] = SDL_JoystickNumButtons(sdl_gamepads[gamepads_found]);
			if (gamepads_max_buttons[gamepads_found] > MAX_GAMEPAD_BUTTONS)
				gamepads_max_buttons[gamepads_found] = MAX_GAMEPAD_BUTTONS;
/* #ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD*/
			/* if (joystick_nbuttons[joysticks_found] > OSK_MAX_BUTTONS)*/
				/* joystick_nbuttons[joysticks_found] = OSK_MAX_BUTTONS;*/
/* #endif*/
			gamepads_found++;
		}
	}
}

/******************************************************************************/
/*SDL_INPUT_GetRealJSConfig*/
/*returns pointer to gamepad configuration*/
/*this is used by ui*/
/******************************************************************************/
SDL_INPUT_RealJSConfig_t* SDL_INPUT_GetRealJSConfig(int joyIndex)
{
    return &gamepad_configuration[joyIndex];
}

/******************************************************************************/
/*GamePads_GetFunctionForButton*/
/*gets pad function for given button from configuration*/
/******************************************************************************/
static int GamePads_GetFunctionForButton(int gamepadNumber, int buttonNumber)
{
	if (buttonNumber >= 16) {
		return gamepad_configuration[gamepadNumber].button_sp_functions[buttonNumber - 16];
	}
	else {
		return gamepad_configuration[gamepadNumber].button_functions[buttonNumber];
	}
}

/******************************************************************************/
/*GamePads_Update*/
/*Updates gamepads states. (gamepads_sdl_actual_state)*/
/******************************************************************************/
static void GamePads_UpdatePad(int gamepadNumber)
{
	int i,x,y,xminlimit,xmaxlimit,yminlimit,ymaxlimit,autofire,fire,mask,trigFunc,handled,changed,pressed,fire_from_autofire,fire_from_fire;
	Uint8 hat;
	unsigned int trig;
	unsigned int last_trig;
	int last_crook, last_angle, actual_angle, max_angle, min_angle, recalc;
	double angle_fine, dist, xn, yn;
	
	/*updating x and y axis*/
	x = SDL_JoystickGetAxis(sdl_gamepads[gamepadNumber], 0);
	y = SDL_JoystickGetAxis(sdl_gamepads[gamepadNumber], 1);

	if (gamepad_configuration[gamepadNumber].radial) {
		/*calculating actual joy position for radial|*/
		/*checking trigonometric coordinates for joy*/
		angle_fine = atan2(y, x);
		xn = ((double)x) / gamepad_configuration[gamepadNumber].deadzone;
		yn = ((double)y) / gamepad_configuration[gamepadNumber].deadzone;
		dist = xn * xn + yn * yn;
		/*checking if joy is out of actually triggered zone*/
		recalc = FALSE;
		last_crook = gamepads_sdl_last_state[gamepadNumber].x != 0 || gamepads_sdl_last_state[gamepadNumber].y != 0;
		if (last_crook) {
			if (dist < (1.0 - gamepad_configuration[gamepadNumber].tolerance) * (1.0 - gamepad_configuration[gamepadNumber].tolerance)) {
				/*recalc because joy was out of center and now is in center*/
				recalc = TRUE;
			}
			else {
				if (gamepads_sdl_last_state[gamepadNumber].x == -1) {
					last_angle = 4 - gamepads_sdl_last_state[gamepadNumber].y;
				}
				else if (gamepads_sdl_last_state[gamepadNumber].x == 0) {
					last_angle = gamepads_sdl_last_state[gamepadNumber].y * 2;
				}
				else {
					last_angle = gamepads_sdl_last_state[gamepadNumber].y;
				}
				if (last_angle == 5) {
					last_angle = -3;
				}
				min_angle = last_angle * M_PI / 4 - M_PI / 8 * (1 + gamepad_configuration[gamepadNumber].radial_tolerance / 2);
				max_angle = last_angle * M_PI / 4 + M_PI / 8 * (1 + gamepad_configuration[gamepadNumber].radial_tolerance / 2);
				if (last_angle == 4) {
					max_angle -= M_PI * 2;
					if (angle_fine > max_angle && angle_fine < min_angle) {
						/*recalc because joy was off center in specific region and now is in another*/
						recalc = TRUE;
					}
				}
				else {
					if (angle_fine > max_angle || angle_fine < min_angle) {
						/*recalc because joy was off center in specific region and now is in another*/
						recalc = TRUE;
					}
				}
			}
		}
		else {
			if (dist > (1.0 + gamepad_configuration[gamepadNumber].tolerance) * (1.0 + gamepad_configuration[gamepadNumber].tolerance)) {
				/*recalc because joy was in center and now is off center*/
				recalc = TRUE;
			}
		}
		recalc = TRUE;
		if (recalc) {
			/*recalculating zone that joy is in*/
			if (dist < 1.0) {
				gamepads_sdl_actual_state[gamepadNumber].x = 0;
				gamepads_sdl_actual_state[gamepadNumber].y = 0;
			}
			else {
				actual_angle = round(angle_fine / (M_PI / 4));
				switch (actual_angle) {
					case -4:
					case 4:
						gamepads_sdl_actual_state[gamepadNumber].x = -1;
						gamepads_sdl_actual_state[gamepadNumber].y = 0;
						break;
					case -3:
						gamepads_sdl_actual_state[gamepadNumber].x = -1;
						gamepads_sdl_actual_state[gamepadNumber].y = -1;
						break;
					case -2:
						gamepads_sdl_actual_state[gamepadNumber].x = 0;
						gamepads_sdl_actual_state[gamepadNumber].y = -1;
						break;
					case -1:
						gamepads_sdl_actual_state[gamepadNumber].x = 1;
						gamepads_sdl_actual_state[gamepadNumber].y = -1;
						break;
					case 0:
						gamepads_sdl_actual_state[gamepadNumber].x = 1;
						gamepads_sdl_actual_state[gamepadNumber].y = 0;
						break;
					case 1:
						gamepads_sdl_actual_state[gamepadNumber].x = 1;
						gamepads_sdl_actual_state[gamepadNumber].y = 1;
						break;
					case 2:
						gamepads_sdl_actual_state[gamepadNumber].x = 0;
						gamepads_sdl_actual_state[gamepadNumber].y = 1;
						break;
					case 3:
						gamepads_sdl_actual_state[gamepadNumber].x = -1;
						gamepads_sdl_actual_state[gamepadNumber].y = 1;
						break;
				}
			}
		}
	}
	else {
		/*calculating actual joy position for square*/
		xminlimit = -gamepad_configuration[gamepadNumber].deadzone * (1.0f + (gamepads_sdl_last_state[gamepadNumber].x == -1 ? -0.5f : 0.5f) * gamepad_configuration[gamepadNumber].tolerance);
		xmaxlimit = gamepad_configuration[gamepadNumber].deadzone * (1.0f + (gamepads_sdl_last_state[gamepadNumber].x == 1 ? -0.5f : 0.5f) * gamepad_configuration[gamepadNumber].tolerance);
		yminlimit = -gamepad_configuration[gamepadNumber].deadzone * (1.0f + (gamepads_sdl_last_state[gamepadNumber].y == -1 ? -0.5f : 0.5f) * gamepad_configuration[gamepadNumber].tolerance);
		ymaxlimit = gamepad_configuration[gamepadNumber].deadzone * (1.0f + (gamepads_sdl_last_state[gamepadNumber].y == 1 ? -0.5f : 0.5f) * gamepad_configuration[gamepadNumber].tolerance);
		gamepads_sdl_actual_state[gamepadNumber].x = x > xmaxlimit ? 1 : (x < xminlimit ? -1 : 0);
		gamepads_sdl_actual_state[gamepadNumber].y = y > ymaxlimit ? 1 : (y < yminlimit ? -1 : 0);
	}
	/*updating x and y for hats*/
	hat = SDL_JoystickGetHat(sdl_gamepads[gamepadNumber], 0);
	gamepads_sdl_actual_state[gamepadNumber].hx =  ((hat & SDL_HAT_LEFT) == SDL_HAT_LEFT) ? -1 : 0;
	gamepads_sdl_actual_state[gamepadNumber].hx += ((hat & SDL_HAT_RIGHT) == SDL_HAT_RIGHT) ? 1 : 0;
	gamepads_sdl_actual_state[gamepadNumber].hy =  ((hat & SDL_HAT_DOWN) == SDL_HAT_DOWN) ? 1 : 0;
	gamepads_sdl_actual_state[gamepadNumber].hy += ((hat & SDL_HAT_UP) == SDL_HAT_UP) ? -1 : 0;
	
	/*updating buttons*/
	trig = 0;
	for (i = 0; i < gamepads_max_buttons[gamepadNumber]; i++) {
		if (SDL_JoystickGetButton(sdl_gamepads[gamepadNumber], i)) {
			trig |= 1 << i;
		}
	}
	gamepads_sdl_actual_state[gamepadNumber].special = (gamepad_special_mask[gamepadNumber] & trig) != 0;
	if (gamepads_sdl_actual_state[gamepadNumber].special) {
		trig &= ~gamepad_special_mask[gamepadNumber];
		trig = trig << 16;
	}
	gamepads_sdl_actual_state[gamepadNumber].buttons = trig;
	last_trig = gamepads_sdl_last_state[gamepadNumber].buttons;
	if (!UI_is_active) {
		autofire = FALSE;
		fire = FALSE;
		for(i = 0; i < 32; i++) {
			mask = 1 << i;
			trigFunc = GamePads_GetFunctionForButton(gamepadNumber, i);
			handled = TRUE;
			changed = (trig & mask) != (last_trig & mask);
			pressed = (trig & mask) != 0;
			switch (trigFunc) {
				case FNPAD_START_HOLD:
					if (pressed)
						gamepads_consol_state &= ~INPUT_CONSOL_START;
					break;
				case FNPAD_SELECT_HOLD:
					if (pressed)
						gamepads_consol_state &= ~INPUT_CONSOL_SELECT;
					break;
				case FNPAD_OPTION_HOLD:
					if (pressed)
						gamepads_consol_state &= ~INPUT_CONSOL_OPTION;
					break;
				case FNPAD_AUTOFIRE_TOGGLE:
					if (changed && pressed) {
						gamepads_fire_state[gamepadNumber].autofire_toggle_on = !gamepads_fire_state[gamepadNumber].autofire_toggle_on;
					}
					break;
				case FNPAD_AUTOFIRE_HOLD:
					if (pressed) {
						autofire = TRUE;
					}
					break;
				case FNPAD_FIRE_TOGGLE:
					if (changed && pressed) {
						gamepads_fire_state[gamepadNumber].fire_toggle_on = !gamepads_fire_state[gamepadNumber].fire_toggle_on;
					}
					break;
				case FNPAD_FIRE_HOLD:
					if (pressed) {
						fire = TRUE;
					}
					break;
				default:
					handled = FALSE;
			}
			
			/*updating last buttons to match actual buttons*/
			if (handled && changed)
			{
				if (pressed) {
					gamepads_sdl_last_state[gamepadNumber].buttons |= mask;
				}
				else {
					gamepads_sdl_last_state[gamepadNumber].buttons &= ~mask;
				}
			}
		}
		/*autofire and fire handling*/
		gamepads_fire_state[gamepadNumber].autofire_actual_on = gamepads_fire_state[gamepadNumber].autofire_toggle_on != autofire;
		if (!gamepads_fire_state[gamepadNumber].autofire_last_on && gamepads_fire_state[gamepadNumber].autofire_actual_on) {
			gamepads_fire_state[gamepadNumber].autofire_phase = 0;
		}
		fire_from_autofire = gamepads_fire_state[gamepadNumber].autofire_actual_on && (gamepads_fire_state[gamepadNumber].autofire_phase < gamepad_configuration[gamepadNumber].autofire_freq);
		gamepads_fire_state[gamepadNumber].autofire_last_on = gamepads_fire_state[gamepadNumber].autofire_actual_on;
		gamepads_fire_state[gamepadNumber].autofire_phase += 1;
		if (gamepads_fire_state[gamepadNumber].autofire_phase >= (gamepad_configuration[gamepadNumber].autofire_freq * 2)) {
			gamepads_fire_state[gamepadNumber].autofire_phase = 0;
		}
		fire_from_fire = gamepads_fire_state[gamepadNumber].fire_toggle_on != fire;
		gamepads_fire_state[gamepadNumber].fire = fire_from_autofire != fire_from_fire;
	}
}

static void GamePads_Update(void)
{
	int i;
	if (! gamepads_found)
		return;

	if (!gamepad_masks_valid) {
		for(i = 0; i < MAX_GAMEPADS; i++) {
			GamePads_DetermineMasks(i);
		}
		gamepad_masks_valid = TRUE;
	}
	SDL_JoystickUpdate();
	gamepads_consol_state = INPUT_CONSOL_NONE;
	for(i = 0; i < gamepads_found; i++) {
		GamePads_UpdatePad(i);
	}
}

/******************************************************************************/
/*GamePads_AtariKeys*/
/*Simulates atari keys/functions by pads.*/
/*returns atari key/function.*/
/******************************************************************************/
/*function converts button function to atari key*/
static int GamePads_AtariKeysPressRelease(int buttonFunc, int pressed)
{
	switch (buttonFunc) {
		case FNPAD_TURBO_HOLD: return pressed ? AKEY_TURBO_START : AKEY_TURBO_STOP;
	}
	return AKEY_NONE;
}

/*function converts button function to atari key*/
static int GamePads_AtariKeysPress(int buttonFunc)
{
	switch (buttonFunc) {
		case FNPAD_TURBO_TOGGLE: return AKEY_TURBO;
		case FNPAD_EXIT: return AKEY_EXIT;
		case FNPAD_UI: return UI_is_active ? AKEY_ESCAPE : AKEY_UI;
		case FNPAD_SAVESTATE: return AKEY_NONE;
		case FNPAD_LOADSTATE: return AKEY_NONE;
		case FNPAD_WARMSTART: return AKEY_WARMSTART;
		case FNPAD_COLDSTART: return AKEY_COLDSTART;
		case FNPAD_BREAK: return AKEY_BREAK;
		case FNPAD_SCREENSHOT: return AKEY_SCREENSHOT;
		case FNPAD_SCREENSHOT_INTERLACE: return AKEY_SCREENSHOT_INTERLACE;
#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD
		case FNPAD_KEYB: return AKEY_KEYB;
#endif /* USE_UI_BASIC_ONSCREEN_KEYBOARD */
		case FNPAD_HELP: return AKEY_SPACE;
		case FNPAD_ESCAPE: return AKEY_SPACE;
		case FNPAD_KEY_SPACEBAR: return AKEY_SPACE;
	}
	if (buttonFunc >= FNPAD_CODE_ && buttonFunc <= FNPAD_CODE_ + 0xff) {
		return buttonFunc - FNPAD_CODE_;
	}
	return AKEY_NONE;
			/* result = StateSav_SaveAtariState(state_filename, "wb", TRUE);*/
		/* if (!result)*/
			/* CantSave(state_filename);*/
		/* if (!StateSav_ReadAtariState(state_filename, "rb"))*/
			/* CantLoad(state_filename);*/
}

static int GamePads_AtariKeysFromJoy(int gamepadNumber)
{
	if (UI_is_active && gamepad_configuration[gamepadNumber].use_in_menus) {
		gamepads_sdl_last_state[gamepadNumber].x = gamepads_sdl_actual_state[gamepadNumber].x;
		if (gamepads_sdl_actual_state[gamepadNumber].x == -1) {
			return AKEY_LEFT;
		}
		if (gamepads_sdl_actual_state[gamepadNumber].x == 1) {
			return AKEY_RIGHT;
		}
		gamepads_sdl_last_state[gamepadNumber].y = gamepads_sdl_actual_state[gamepadNumber].y;
		if (gamepads_sdl_actual_state[gamepadNumber].y == -1) {
			return AKEY_UP;
		}
		if (gamepads_sdl_actual_state[gamepadNumber].y == 1) {
			return AKEY_DOWN;
		}
	}
	else {
		gamepads_sdl_last_state[gamepadNumber].x = gamepads_sdl_actual_state[gamepadNumber].x;
		gamepads_sdl_last_state[gamepadNumber].y = gamepads_sdl_actual_state[gamepadNumber].y;
	}
	return AKEY_NONE;
}

static int GamePads_AtariKeysFromHat(int gamepadNumber)
{
	if (UI_is_active && gamepad_configuration[gamepadNumber].use_hat_in_menus) {
		gamepads_sdl_last_state[gamepadNumber].hx = gamepads_sdl_actual_state[gamepadNumber].hx;
		if (gamepads_sdl_actual_state[gamepadNumber].hx == -1) {
			return AKEY_LEFT;
		}
		if (gamepads_sdl_actual_state[gamepadNumber].hx == 1) {
			return AKEY_RIGHT;
		}
		gamepads_sdl_last_state[gamepadNumber].hy = gamepads_sdl_actual_state[gamepadNumber].hy;
		if (gamepads_sdl_actual_state[gamepadNumber].hy == -1) {
			return AKEY_UP;
		}
		if (gamepads_sdl_actual_state[gamepadNumber].hy == 1) {
			return AKEY_DOWN;
		}
	}
	else {
		gamepads_sdl_last_state[gamepadNumber].hx = gamepads_sdl_actual_state[gamepadNumber].hx;
		gamepads_sdl_last_state[gamepadNumber].hy = gamepads_sdl_actual_state[gamepadNumber].hy;
	}
	return AKEY_NONE;
}

static int GamePads_AtariKeysByPad(int gamepadNumber)
{
	unsigned int last_buttons,buttons,mask;
	int atkey,i,ui_button,pressed;
	atkey = GamePads_AtariKeysFromJoy(gamepadNumber);
	if (atkey != AKEY_NONE) {
		return atkey;
	}
	atkey = GamePads_AtariKeysFromHat(gamepadNumber);
	if (atkey != AKEY_NONE) {
		return atkey;
	}
	
	last_buttons = gamepads_sdl_last_state[gamepadNumber].buttons;
	buttons = gamepads_sdl_actual_state[gamepadNumber].buttons;
	mask = 0;
	for(i = 0; i < 32; i++) {
		mask = 1 << i;
		ui_button = FALSE;
		if ((last_buttons & mask) != (buttons & mask)) {
			pressed  = (buttons & mask) != 0;
			if (UI_is_active && (gamepad_configuration[gamepadNumber].use_in_menus || gamepad_configuration[gamepadNumber].use_hat_in_menus)) {
				if (i == gamepad_configuration[gamepadNumber].in_menus_back_button) {
					ui_button = TRUE;
					if (pressed) {
						atkey = AKEY_ESCAPE;
					}
				}
				if (i == gamepad_configuration[gamepadNumber].in_menus_select_button) {
					ui_button = TRUE;
					if (pressed) {
						atkey = AKEY_RETURN;
					}
				}
			}
			if (atkey == AKEY_NONE && !ui_button && (gamepad_atari_key_mask[gamepadNumber] & mask)) {
				int button_func = GamePads_GetFunctionForButton(gamepadNumber, i);
				if (button_func >= 512 && button_func < 1024) {
					/*press only functions*/
					if (pressed) {
						atkey = GamePads_AtariKeysPress(button_func);
					}
				}
				else {
					/*press/release functions*/
					atkey = GamePads_AtariKeysPressRelease(button_func, pressed);
				}
			}
			/*updating last buttons to match actual buttons*/
			if (pressed) {
				gamepads_sdl_last_state[gamepadNumber].buttons |= mask;
			}
			else {
				gamepads_sdl_last_state[gamepadNumber].buttons &= ~mask;
			}
			/*returning atari key if found*/
			if (atkey != AKEY_NONE) {
				return atkey;
			}
		}
	}
	return AKEY_NONE;
}

static int GamePads_AtariKeys(void)
{
	int atkey,i;
	atkey = AKEY_NONE;
	for(i = 0; i < gamepads_found; i++) {
		atkey = GamePads_AtariKeysByPad(i);
		if (atkey != AKEY_NONE) {
			return atkey;
		}
	}
	return AKEY_NONE;
}

/******************************************************************************/
/*GamePads_AtariJoyFromGamePadJoy*/
/*Simulates atari joystick by pads joy.*/
/*returns atari stick state.*/
/******************************************************************************/
static int GamePads_AtariJoyFromGamePadJoy(int joyNumber)
{
	int ret;
	ret = INPUT_STICK_CENTRE;
	if (gamepads_sdl_actual_state[joyNumber].x == -1) ret &= INPUT_STICK_LEFT;
	if (gamepads_sdl_actual_state[joyNumber].x == 1) ret &= INPUT_STICK_RIGHT;
	if (gamepads_sdl_actual_state[joyNumber].y == -1) ret &= INPUT_STICK_FORWARD;
	if (gamepads_sdl_actual_state[joyNumber].y == 1) ret &= INPUT_STICK_BACK;
	return ret;	
}

/******************************************************************************/
/*GamePads_AtariJoyFromGamePadHat*/
/*Simulates atari joystick by pads hat.*/
/*returns atari stick state.*/
/******************************************************************************/
static int GamePads_AtariJoyFromGamePadHat(int joyNumber)
{
	int ret;
	ret = INPUT_STICK_CENTRE;
	if (gamepads_sdl_actual_state[joyNumber].hx == -1) ret &= INPUT_STICK_LEFT;
	if (gamepads_sdl_actual_state[joyNumber].hx == 1) ret &= INPUT_STICK_RIGHT;
	if (gamepads_sdl_actual_state[joyNumber].hy == -1) ret &= INPUT_STICK_FORWARD;
	if (gamepads_sdl_actual_state[joyNumber].hy == 1) ret &= INPUT_STICK_BACK;
	return ret;	
}

/******************************************************************************/
/*GamePads_AtariJoy*/
/*Simulates atari joystick by pads.*/
/*returns atari stick state.*/
/******************************************************************************/
static int GamePads_AtariJoy(int joyNumber)
{
	int atari_stick;
	atari_stick = INPUT_STICK_CENTRE;
	if (gamepad_configuration[joyNumber].use_as_stick) {
		atari_stick &= GamePads_AtariJoyFromGamePadJoy(joyNumber);
	}
	if (gamepad_configuration[joyNumber].use_hat_as_stick) {
		atari_stick &= GamePads_AtariJoyFromGamePadHat(joyNumber);
	}
	return atari_stick;
}

/******************************************************************************/
/*GamePads_AtariKeys*/
/*Simulates atari trigger by pads.*/
/*returns 0 if trigger is pressed.*/
/******************************************************************************/
static int GamePads_AtariTrigger(int joyNumber)
{
	return !gamepads_fire_state[joyNumber].fire;
}

/*old function*/
/* static void update_SDL_joysticks(void)*/
/* {*/
	/* int joy;*/

	/* if (! gamepads_found)*/
		/* return;*/

	/* SDL_JoystickUpdate();*/

	/* for(joy = 0; joy < gamepads_found; joy++) {*/
		/* int i;*/

		/* sdl_js_state[joy].port = INPUT_STICK_CENTRE;*/
		
		/* if (real_js_configs[joy].use_as_stick) {*/
			/* sdl_js_state[joy].port &= get_SDL_joystick_state(joystick[joy], joy);*/
		/* }*/
		/* if (real_js_configs[joy].use_hat_as_stick) {*/
			/* sdl_js_state[joy].port &= get_SDL_joystick_hat_state(joystick[joy]);*/
		/* }*/

		/* sdl_js_state[joy].trig = 0;*/
		/* for (i = 0; i < joystick_nbuttons[joy]; i++) {*/
			/* if (SDL_JoystickGetButton(joystick[joy], i)) {*/
				/* sdl_js_state[joy].trig |= 1 << i;*/
			/* }*/
		/* }*/
	/* }*/
/* }*/

/*---------------------------------------------------------------------------------------------------------------------*/
/*Keyboard support*/
/*---------------------------------------------------------------------------------------------------------------------*/

/*consol keys from keyboard*/
static int input_key_consol_from_keys;

/* keyboard */
static Uint8 *kbhits;

#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD
static int SDL_controller_kb(void);
static int SDL_consol_keys(void);
int OSK_enabled = 1;
#define OSK_MAX_BUTTONS 6
#define OSK_BUTTON_0 0
#define OSK_BUTTON_1 1
#define OSK_BUTTON_2 2
#define OSK_BUTTON_3 3
#define OSK_BUTTON_4 4
#define OSK_BUTTON_5 5
#define OSK_BUTTON_TRIGGER OSK_BUTTON_0
#define OSK_BUTTON_LEAVE   OSK_BUTTON_1      /* button to exit emulator UI or keyboard emulation screen */
#define OSK_BUTTON_UI      OSK_BUTTON_4      /* button to enter emulator UI */
#define OSK_BUTTON_KEYB    OSK_BUTTON_5      /* button to enter keyboard emulation screen */
#define OSK_BUTTON_START   OSK_BUTTON_LEAVE
#define OSK_BUTTON_SELECT  OSK_BUTTON_2
#define OSK_BUTTON_OPTION  OSK_BUTTON_3
#endif /* #ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD */


/* For better handling of the PLATFORM_Configure-recognition...
   Takes a keySym as integer-string and fills the value
   into the retval referentially.
   Authors: B.Schreiber, A.Martinez
   fixed and cleaned up by joy */
static int SDLKeyBind(int *retval, char *sdlKeySymIntStr)
{
	int ksym;

	if (retval == NULL || sdlKeySymIntStr == NULL) {
		return FALSE;
	}

	/* make an int out of the keySymIntStr... */
	ksym = Util_sscandec(sdlKeySymIntStr);

	if (ksym > SDLK_FIRST && ksym < SDLK_LAST) {
		*retval = ksym;
		return TRUE;
	}
	else {
		return FALSE;
	}
}

/* For getting sdl key map out of the config...
   Authors: B.Schreiber, A.Martinez
   cleaned up by joy */
static int Keyboard_ReadConfig(char *option, char *parameters)
{
	static int was_config_initialized = FALSE;
    
	if (was_config_initialized == FALSE) {
		was_config_initialized=TRUE;
	}
    
	if (strcmp(option, "SDL_JOY_0_ENABLED") == 0) {
		PLATFORM_kbd_joy_0_enabled = (parameters != NULL && parameters[0] != '0');
		return TRUE;
	}
	else if (strcmp(option, "SDL_JOY_1_ENABLED") == 0) {
		PLATFORM_kbd_joy_1_enabled = (parameters != NULL && parameters[0] != '0');
		return TRUE;
	}
	else if (strcmp(option, "SDL_JOY_0_LEFT") == 0)
		return SDLKeyBind(&KBD_STICK_0_LEFT, parameters);
	else if (strcmp(option, "SDL_JOY_0_RIGHT") == 0)
		return SDLKeyBind(&KBD_STICK_0_RIGHT, parameters);
	else if (strcmp(option, "SDL_JOY_0_DOWN") == 0)
		return SDLKeyBind(&KBD_STICK_0_DOWN, parameters);
	else if (strcmp(option, "SDL_JOY_0_UP") == 0)
		return SDLKeyBind(&KBD_STICK_0_UP, parameters);
	else if (strcmp(option, "SDL_JOY_0_TRIGGER") == 0)
		return SDLKeyBind(&KBD_TRIG_0, parameters);
	else if (strcmp(option, "SDL_JOY_1_LEFT") == 0)
		return SDLKeyBind(&KBD_STICK_1_LEFT, parameters);
	else if (strcmp(option, "SDL_JOY_1_RIGHT") == 0)
		return SDLKeyBind(&KBD_STICK_1_RIGHT, parameters);
	else if (strcmp(option, "SDL_JOY_1_DOWN") == 0)
		return SDLKeyBind(&KBD_STICK_1_DOWN, parameters);
	else if (strcmp(option, "SDL_JOY_1_UP") == 0)
		return SDLKeyBind(&KBD_STICK_1_UP, parameters);
	else if (strcmp(option, "SDL_JOY_1_TRIGGER") == 0)
		return SDLKeyBind(&KBD_TRIG_1, parameters);
	/* else if (strcmp(option, "SDL_JOY_0_USE_HAT") == 0)*/
		/* return set_real_js_use_hat(0,parameters);*/
	/* else if (strcmp(option, "SDL_JOY_1_USE_HAT") == 0)*/
		/* return set_real_js_use_hat(1,parameters);*/
	/* else if (strcmp(option, "SDL_JOY_2_USE_HAT") == 0)*/
		/* return set_real_js_use_hat(2,parameters);*/
	/* else if (strcmp(option, "SDL_JOY_3_USE_HAT") == 0)*/
		/* return set_real_js_use_hat(3,parameters);*/
	else if (strcmp(option, "SDL_UI_KEY") == 0)
		return SDLKeyBind(&KBD_UI, parameters);
	else if (strcmp(option, "SDL_OPTION_KEY") == 0)
		return SDLKeyBind(&KBD_OPTION, parameters);
	else if (strcmp(option, "SDL_SELECT_KEY") == 0)
		return SDLKeyBind(&KBD_SELECT, parameters);
	else if (strcmp(option, "SDL_START_KEY") == 0)
		return SDLKeyBind(&KBD_START, parameters);
	else if (strcmp(option, "SDL_RESET_KEY") == 0)
		return SDLKeyBind(&KBD_RESET, parameters);
	else if (strcmp(option, "SDL_HELP_KEY") == 0)
		return SDLKeyBind(&KBD_HELP, parameters);
	else if (strcmp(option, "SDL_BREAK_KEY") == 0)
		return SDLKeyBind(&KBD_BREAK, parameters);
	else if (strcmp(option, "SDL_MON_KEY") == 0)
		return SDLKeyBind(&KBD_MON, parameters);
	else if (strcmp(option, "SDL_EXIT_KEY") == 0)
		return SDLKeyBind(&KBD_EXIT, parameters);
	else if (strcmp(option, "SDL_SSHOT_KEY") == 0)
		return SDLKeyBind(&KBD_SSHOT, parameters);
	else if (strcmp(option, "SDL_TURBO_KEY") == 0)
		return SDLKeyBind(&KBD_TURBO, parameters);
	else
		return FALSE;
}

/* Save the keybindings and the keybindapp options to the config file...
   Authors: B.Schreiber, A.Martinez
   cleaned up by joy */
static void Keyboard_WriteConfig(FILE *fp)
{
	fprintf(fp, "SDL_JOY_0_ENABLED=%d\n", PLATFORM_kbd_joy_0_enabled);
	fprintf(fp, "SDL_JOY_0_LEFT=%d\n", KBD_STICK_0_LEFT);
	fprintf(fp, "SDL_JOY_0_RIGHT=%d\n", KBD_STICK_0_RIGHT);
	fprintf(fp, "SDL_JOY_0_UP=%d\n", KBD_STICK_0_UP);
	fprintf(fp, "SDL_JOY_0_DOWN=%d\n", KBD_STICK_0_DOWN);
	fprintf(fp, "SDL_JOY_0_TRIGGER=%d\n", KBD_TRIG_0);

	fprintf(fp, "SDL_JOY_1_ENABLED=%d\n", PLATFORM_kbd_joy_1_enabled);
	fprintf(fp, "SDL_JOY_1_LEFT=%d\n", KBD_STICK_1_LEFT);
	fprintf(fp, "SDL_JOY_1_RIGHT=%d\n", KBD_STICK_1_RIGHT);
	fprintf(fp, "SDL_JOY_1_UP=%d\n", KBD_STICK_1_UP);
	fprintf(fp, "SDL_JOY_1_DOWN=%d\n", KBD_STICK_1_DOWN);
	fprintf(fp, "SDL_JOY_1_TRIGGER=%d\n", KBD_TRIG_1);

	fprintf(fp, "SDL_UI_KEY=%d\n", KBD_UI);
	fprintf(fp, "SDL_OPTION_KEY=%d\n", KBD_OPTION);
	fprintf(fp, "SDL_SELECT_KEY=%d\n", KBD_SELECT);
	fprintf(fp, "SDL_START_KEY=%d\n", KBD_START);
	fprintf(fp, "SDL_RESET_KEY=%d\n", KBD_RESET);
	fprintf(fp, "SDL_HELP_KEY=%d\n", KBD_HELP);
	fprintf(fp, "SDL_BREAK_KEY=%d\n", KBD_BREAK);
	fprintf(fp, "SDL_MON_KEY=%d\n", KBD_MON);
	fprintf(fp, "SDL_EXIT_KEY=%d\n", KBD_EXIT);
	fprintf(fp, "SDL_SSHOT_KEY=%d\n", KBD_SSHOT);
	fprintf(fp, "SDL_TURBO_KEY=%d\n", KBD_TURBO);
}

void PLATFORM_SetJoystickKey(int joystick, int direction, int value)
{
	if (joystick == 0) {
		switch(direction) {
			case 0: KBD_STICK_0_LEFT = value; break;
			case 1: KBD_STICK_0_UP = value; break;
			case 2: KBD_STICK_0_RIGHT = value; break;
			case 3: KBD_STICK_0_DOWN = value; break;
			case 4: KBD_TRIG_0 = value; break;
		}
	}
	else {
		switch(direction) {
			case 0: KBD_STICK_1_LEFT = value; break;
			case 1: KBD_STICK_1_UP = value; break;
			case 2: KBD_STICK_1_RIGHT = value; break;
			case 3: KBD_STICK_1_DOWN = value; break;
			case 4: KBD_TRIG_1 = value; break;
		}
	}
}

void PLATFORM_GetJoystickKeyName(int joystick, int direction, char *buffer, int bufsize)
{
	char *key = "";
	switch(direction) {
		case 0: key = SDL_GetKeyName((SDLKey)(joystick == 0 ? KBD_STICK_0_LEFT : KBD_STICK_1_LEFT));
			break;
		case 1: key = SDL_GetKeyName((SDLKey)(joystick == 0 ? KBD_STICK_0_UP : KBD_STICK_1_UP));
			break;
		case 2: key = SDL_GetKeyName((SDLKey)(joystick == 0 ? KBD_STICK_0_RIGHT : KBD_STICK_1_RIGHT));
			break;
		case 3: key = SDL_GetKeyName((SDLKey)(joystick == 0 ? KBD_STICK_0_DOWN : KBD_STICK_1_DOWN));
			break;
		case 4: key = SDL_GetKeyName((SDLKey)(joystick == 0 ? KBD_TRIG_0 : KBD_TRIG_1));
			break;
	}
	snprintf(buffer, bufsize, "%11s", key);
}

static void SwapJoysticks(void)
{
	swap_joysticks = 1 - swap_joysticks;
}

int PLATFORM_GetRawKey(void)
{
	while(TRUE)
	{
		SDL_Event event;
		if (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_KEYDOWN:
				return event.key.keysym.sym;
			}
		}
	}
}

static int lastkey = SDLK_UNKNOWN, key_pressed = 0, key_control = 0;
static int lastuni = 0;

#if HAVE_WINDOWS_H
/* On Windows 7 rapidly changing the window size invokes a bug in SDL
   which causes the window to be resized to size 0,0. This hack delays
   the resize requests a bit to ensure that no two resizes happen within
   a span of 0.5 second. See also
   http://www.atariage.com/forums/topic/179912-atari800-220-released/page__view__findpost__p__2258092 */
enum { USER_EVENT_RESIZE_DELAY };
static Uint32 ResizeDelayCallback(Uint32 interval, void *param)
{
	SDL_Event event;
	event.user.type = SDL_USEREVENT;
	event.user.code = USER_EVENT_RESIZE_DELAY;
	event.user.data1 = NULL;
	event.user.data2 = NULL;
	SDL_PushEvent(&event);
	return 0;
}
#endif /* HAVE_WINDOWS_H */

#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD
static unsigned char *atari_screen_backup;
#endif

/******************************************************************************/
/*Keyboard_AtariKeys*/
/*Simulates atari keys/functions by keyboard.*/
/*returns atari key/function.*/
/******************************************************************************/
static int Keyboard_AtariKeys(void)
{
	int shiftctrl = 0;
	SDL_Event event;
	int event_found = 0; /* Was there at least one event? */

#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD
	if (!atari_screen_backup)
		atari_screen_backup = malloc(Screen_HEIGHT * Screen_WIDTH);
#endif

#if HAVE_WINDOWS_H
	/* Used to delay resize events on Windows 7, see above. */
	enum { RESIZE_INTERVAL = 500 };
	static int resize_delayed = FALSE;
	static int resize_needed = FALSE;
	static int resize_w, resize_h;
#endif /* HAVE_WINDOWS_H */

	/* Very ugly fix for SDL CAPSLOCK brokenness.  This will let the user
	 * press CAPSLOCK and get a brief keypress on the Atari but it is not
	 * possible to emulate holding down CAPSLOCK for longer periods with
	 * the broken SDL*/
	if (lastkey == SDLK_CAPSLOCK) {
		lastkey = SDLK_UNKNOWN;
		key_pressed = 0;
 		lastuni = 0;
	}

	while (SDL_PollEvent(&event)) {
		event_found = 1;
		switch (event.type) {
		case SDL_KEYDOWN:
			lastkey = event.key.keysym.sym;
 			lastuni = event.key.keysym.unicode;
			key_pressed = 1;
			break;
		case SDL_KEYUP:
			lastkey = event.key.keysym.sym;
 			lastuni = 0; /* event.key.keysym.unicode is not defined for KEYUP */
			key_pressed = 0;
			/* ugly hack to fix broken SDL CAPSLOCK*/
			/* Because SDL is only sending Keydown and keyup for every change
			 * of state of the CAPSLOCK status, rather than the actual key.*/
			if(lastkey == SDLK_CAPSLOCK) {
				key_pressed = 1;
			}
			break;
		case SDL_VIDEORESIZE:
#if HAVE_WINDOWS_H
			/* Delay resize events on Windows 7, see above. */
			if (resize_delayed) {
				resize_w = event.resize.w;
				resize_h = event.resize.h;
				resize_needed = TRUE;
			} else {
				VIDEOMODE_SetWindowSize(event.resize.w, event.resize.h);
				resize_delayed = TRUE;
				if (SDL_AddTimer(RESIZE_INTERVAL, &ResizeDelayCallback, NULL) == NULL) {
					Log_print("Error: SDL_AddTimer failed: %s", SDL_GetError());
					Log_flushlog();
					exit(-1);
				}
			}
#else
			VIDEOMODE_SetWindowSize(event.resize.w, event.resize.h);
#endif /* HAVE_WINDOWS_H */
			break;
		case SDL_VIDEOEXPOSE:
			/* When window is "uncovered", and we are in the emulator's menu,
			   we need to refresh display manually. */
			PLATFORM_DisplayScreen();
			break;
		case SDL_QUIT:
			return AKEY_EXIT;
			break;
#if HAVE_WINDOWS_H
		case SDL_USEREVENT:
			/* Process delayed video resize on Windows 7, see above. */
			if (event.user.code == USER_EVENT_RESIZE_DELAY) {
				if (resize_needed) {
					SDL_Event events[1];
					resize_needed = FALSE;
					/* If there's a resize event in the queue,
					   wait for it and don't resize now. */
					if (SDL_PeepEvents(events, 1, SDL_PEEKEVENT, SDL_EVENTMASK(SDL_VIDEORESIZE)) != 0)
						resize_delayed = FALSE;
					else {
						VIDEOMODE_SetWindowSize(resize_w, resize_h);
						if (SDL_AddTimer(RESIZE_INTERVAL, &ResizeDelayCallback, NULL) == NULL) {
							Log_print("Error: SDL_AddTimer failed: %s", SDL_GetError());
							Log_flushlog();
							exit(-1);
						}
					}
				} else
					resize_delayed = FALSE;
			}
			break;
#endif /* HAVE_WINDOWS_H */
		}
	}

	if (!event_found && !key_pressed) {
#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD
		SDL_consol_keys();
		return SDL_controller_kb();
#else
		return AKEY_NONE;
#endif
	}

	UI_alt_function = -1;
	if (kbhits[SDLK_LALT]) {
		if (key_pressed) {
			switch (lastkey) {
			case SDLK_f:
				key_pressed = 0;
				VIDEOMODE_ToggleWindowed();
				break;
			case SDLK_x:
				if (INPUT_key_shift) {
					key_pressed = 0;
#if defined(XEP80_EMULATION) || defined(PBI_PROTO80) || defined(AF80) || defined(BIT3)
					VIDEOMODE_Toggle80Column();
#endif
				}
				break;
			case SDLK_g:
				key_pressed = 0;
				VIDEOMODE_ToggleHorizontalArea();
				break;
			case SDLK_j:
				key_pressed = 0;
				SwapJoysticks();
				break;
			case SDLK_r:
				UI_alt_function = UI_MENU_RUN;
				break;
			case SDLK_y:
				UI_alt_function = UI_MENU_SYSTEM;
				break;
			case SDLK_o:
				UI_alt_function = UI_MENU_SOUND;
				break;
			case SDLK_w:
				UI_alt_function = UI_MENU_SOUND_RECORDING;
				break;
			case SDLK_v:
				UI_alt_function = UI_MENU_VIDEO_RECORDING;
				break;
			case SDLK_a:
				UI_alt_function = UI_MENU_ABOUT;
				break;
			case SDLK_s:
				UI_alt_function = UI_MENU_SAVESTATE;
				break;
			case SDLK_d:
				UI_alt_function = UI_MENU_DISK;
				break;
			case SDLK_l:
				UI_alt_function = UI_MENU_LOADSTATE;
				break;
			case SDLK_c:
				UI_alt_function = UI_MENU_CARTRIDGE;
				break;
			case SDLK_t:
				UI_alt_function = UI_MENU_CASSETTE;
				break;
			case SDLK_BACKSLASH:
				return AKEY_PBI_BB_MENU;
			case SDLK_m:
				grab_mouse = !grab_mouse;
				SDL_WM_GrabInput(grab_mouse ? SDL_GRAB_ON : SDL_GRAB_OFF);
				key_pressed = 0;
				break;
			case SDLK_1:
				if (kbhits[SDLK_LSHIFT]) {
					if (Colours_setup->hue > COLOURS_HUE_MIN)
						Colours_setup->hue -= 0.02;
				} else {
					if (Colours_setup->hue < COLOURS_HUE_MAX)
						Colours_setup->hue += 0.02;
				}
				Colours_Update();
				return AKEY_NONE;
			case SDLK_2:
				if (kbhits[SDLK_LSHIFT]) {
					if (Colours_setup->saturation > COLOURS_SATURATION_MIN)
						Colours_setup->saturation -= 0.02;
				} else {
					if (Colours_setup->saturation < COLOURS_SATURATION_MAX)
						Colours_setup->saturation += 0.02;
				}
				Colours_Update();
				return AKEY_NONE;
			case SDLK_3:
				if (kbhits[SDLK_LSHIFT]) {
					if (Colours_setup->contrast > COLOURS_CONTRAST_MIN)
						Colours_setup->contrast -= 0.04;
				} else {
					if (Colours_setup->contrast < COLOURS_CONTRAST_MAX)
					Colours_setup->contrast += 0.04;
				}
				Colours_Update();
				return AKEY_NONE;
			case SDLK_4:
				if (kbhits[SDLK_LSHIFT]) {
					if (Colours_setup->brightness > COLOURS_BRIGHTNESS_MIN)
						Colours_setup->brightness -= 0.04;
				} else {
					if (Colours_setup->brightness < COLOURS_BRIGHTNESS_MAX)
						Colours_setup->brightness += 0.04;
				}
				Colours_Update();
				return AKEY_NONE;
			case SDLK_5:
				if (kbhits[SDLK_LSHIFT]) {
					if (Colours_setup->gamma > COLOURS_GAMMA_MIN)
						Colours_setup->gamma -= 0.02;
				} else {
					if (Colours_setup->gamma < COLOURS_GAMMA_MAX)
						Colours_setup->gamma += 0.02;
				}
				Colours_Update();
				return AKEY_NONE;
			case SDLK_6:
				if (kbhits[SDLK_LSHIFT]) {
					if (Colours_setup->color_delay > COLOURS_DELAY_MIN)
						Colours_setup->color_delay -= 0.4;
				} else {
					if (Colours_setup->color_delay < COLOURS_DELAY_MAX)
						Colours_setup->color_delay += 0.4;
				}
				Colours_Update();
				return AKEY_NONE;
			case SDLK_LEFTBRACKET:
				if (kbhits[SDLK_LSHIFT])
					SDL_VIDEO_SetScanlinesPercentage(SDL_VIDEO_scanlines_percentage - 1);
				else
					SDL_VIDEO_SetScanlinesPercentage(SDL_VIDEO_scanlines_percentage + 1);
				return AKEY_NONE;
			default:
				if(FILTER_NTSC_emu != NULL){
					switch(lastkey){
					case SDLK_7:
						if (kbhits[SDLK_LSHIFT]) {
							if (FILTER_NTSC_setup.sharpness > FILTER_NTSC_SHARPNESS_MIN)
								FILTER_NTSC_setup.sharpness -= 0.02;
						} else {
							if (FILTER_NTSC_setup.sharpness < FILTER_NTSC_SHARPNESS_MAX)
								FILTER_NTSC_setup.sharpness += 0.02;
						}
						FILTER_NTSC_Update(FILTER_NTSC_emu);
						return AKEY_NONE;
					case SDLK_8:
						if (kbhits[SDLK_LSHIFT]) {
							if (FILTER_NTSC_setup.resolution > FILTER_NTSC_RESOLUTION_MIN)
								FILTER_NTSC_setup.resolution -= 0.02;
						} else {
							if (FILTER_NTSC_setup.resolution < FILTER_NTSC_RESOLUTION_MAX)
								FILTER_NTSC_setup.resolution += 0.02;
						}
						FILTER_NTSC_Update(FILTER_NTSC_emu);
						return AKEY_NONE;
					case SDLK_9:
						if (kbhits[SDLK_LSHIFT]) {
							if (FILTER_NTSC_setup.artifacts > FILTER_NTSC_ARTIFACTS_MIN)
								FILTER_NTSC_setup.artifacts -= 0.02;
						} else {
							if (FILTER_NTSC_setup.artifacts < FILTER_NTSC_ARTIFACTS_MAX)
								FILTER_NTSC_setup.artifacts += 0.02;
						}
						FILTER_NTSC_Update(FILTER_NTSC_emu);
						return AKEY_NONE;
					case SDLK_0:
						if (kbhits[SDLK_LSHIFT]) {
							if (FILTER_NTSC_setup.fringing > FILTER_NTSC_FRINGING_MIN)
								FILTER_NTSC_setup.fringing -= 0.02;
						} else {
							if (FILTER_NTSC_setup.fringing < FILTER_NTSC_FRINGING_MAX)
								FILTER_NTSC_setup.fringing += 0.02;
						}
						FILTER_NTSC_Update(FILTER_NTSC_emu);
						return AKEY_NONE;
					case SDLK_MINUS:
						if (kbhits[SDLK_LSHIFT]) {
							if (FILTER_NTSC_setup.bleed > FILTER_NTSC_BLEED_MIN)
								FILTER_NTSC_setup.bleed -= 0.02;
						} else {
							if (FILTER_NTSC_setup.bleed < FILTER_NTSC_BLEED_MAX)
								FILTER_NTSC_setup.bleed += 0.02;
						}
						FILTER_NTSC_Update(FILTER_NTSC_emu);
						return AKEY_NONE;
					case SDLK_EQUALS:
						if (kbhits[SDLK_LSHIFT]) {
							if (FILTER_NTSC_setup.burst_phase > FILTER_NTSC_BURST_PHASE_MIN)
								FILTER_NTSC_setup.burst_phase -= 0.02;
						} else {
							if (FILTER_NTSC_setup.burst_phase < FILTER_NTSC_BURST_PHASE_MAX)
								FILTER_NTSC_setup.burst_phase += 0.02;
						}
						FILTER_NTSC_Update(FILTER_NTSC_emu);
						return AKEY_NONE;
					case SDLK_RIGHTBRACKET:
						key_pressed = 0;
						FILTER_NTSC_NextPreset();
						FILTER_NTSC_Update(FILTER_NTSC_emu);
						break;
					}
				}
			break;
			}
		}
	}

	/* SHIFT STATE */
	if ((kbhits[SDLK_LSHIFT]) || (kbhits[SDLK_RSHIFT]))
		INPUT_key_shift = 1;
	else
		INPUT_key_shift = 0;

	/* CONTROL STATE */
	if ((kbhits[SDLK_LCTRL]) || (kbhits[SDLK_RCTRL]))
		key_control = 1;
	else
		key_control = 0;

	/*
	if (event.type == 2 || event.type == 3) {
		Log_print("E:%x S:%x C:%x K:%x U:%x M:%x",event.type,INPUT_key_shift,key_control,lastkey,event.key.keysym.unicode,event.key.keysym.mod);
	}
	*/

	BINLOAD_pause_loading = FALSE;

	/* OPTION / SELECT / START keys */
	input_key_consol_from_keys = INPUT_CONSOL_NONE;
	if (kbhits[KBD_OPTION])
		input_key_consol_from_keys &= ~INPUT_CONSOL_OPTION;
	if (kbhits[KBD_SELECT])
		input_key_consol_from_keys &= ~INPUT_CONSOL_SELECT;
	if (kbhits[KBD_START])
		input_key_consol_from_keys &= ~INPUT_CONSOL_START;

	if (key_pressed == 0)
		return AKEY_NONE;

	/* Handle movement and special keys. */

	/* Since KBD_RESET & KBD_EXIT are variables, they can't be cases */
	/* in a switch statement.  So handle them here with if-blocks */
	if (lastkey == KBD_RESET) {
		key_pressed = 0;
		return INPUT_key_shift ? AKEY_COLDSTART : AKEY_WARMSTART;
	}
	if (lastkey == KBD_EXIT) {
		return AKEY_EXIT;
	}
	if (lastkey == KBD_UI) {
		key_pressed = 0;
		return AKEY_UI;
	}
	if (lastkey == KBD_MON) {
		UI_alt_function = UI_MENU_MONITOR;
	}
	if (lastkey == KBD_HELP) {
		return AKEY_HELP ^ shiftctrl;
	}
	if (lastkey == KBD_BREAK) {
		if (BINLOAD_wait_active) {
			BINLOAD_pause_loading = TRUE;
			return AKEY_NONE;
		}
		else
			return AKEY_BREAK;
	}
	if (lastkey == KBD_SSHOT) {
		key_pressed = 0;
		return INPUT_key_shift ? AKEY_SCREENSHOT_INTERLACE : AKEY_SCREENSHOT;
	}
	if (lastkey == KBD_TURBO) {
		key_pressed = 0;
		return AKEY_TURBO;
	}
	if (UI_alt_function != -1) {
		key_pressed = 0;
		return AKEY_UI;
	}

	/* keyboard joysticks: don't pass the keypresses to emulation
	 * as some games pause on a keypress (River Raid, Bruce Lee)
	 */
	if (!UI_is_active && PLATFORM_kbd_joy_0_enabled) {
		if (lastkey == KBD_STICK_0_LEFT || lastkey == KBD_STICK_0_RIGHT ||
			lastkey == KBD_STICK_0_UP || lastkey == KBD_STICK_0_DOWN || lastkey == KBD_TRIG_0) {
			key_pressed = 0;
			return AKEY_NONE;
		}
	}

	if (!UI_is_active && PLATFORM_kbd_joy_1_enabled) {
		if (lastkey == KBD_STICK_1_LEFT || lastkey == KBD_STICK_1_RIGHT ||
			lastkey == KBD_STICK_1_UP || lastkey == KBD_STICK_1_DOWN || lastkey == KBD_TRIG_1) {
			key_pressed = 0;
			return AKEY_NONE;
		}
	}

	if (INPUT_key_shift)
		shiftctrl ^= AKEY_SHFT;

	if (Atari800_machine_type == Atari800_MACHINE_5200 && !UI_is_active) {
		if (lastkey == SDLK_F4)
			return AKEY_5200_START ^ shiftctrl;
		switch (lastuni) {
		case 'p':
			return AKEY_5200_PAUSE ^ shiftctrl;
		case 'r':
			return AKEY_5200_RESET ^ shiftctrl;
		case '0':
			return AKEY_5200_0 ^ shiftctrl;
		case '1':
			return AKEY_5200_1 ^ shiftctrl;
		case '2':
			return AKEY_5200_2 ^ shiftctrl;
		case '3':
			return AKEY_5200_3 ^ shiftctrl;
		case '4':
			return AKEY_5200_4 ^ shiftctrl;
		case '5':
			return AKEY_5200_5 ^ shiftctrl;
		case '6':
			return AKEY_5200_6 ^ shiftctrl;
		case '7':
			return AKEY_5200_7 ^ shiftctrl;
		case '8':
			return AKEY_5200_8 ^ shiftctrl;
		case '9':
			return AKEY_5200_9 ^ shiftctrl;
		case '#':
		case '=':
			return AKEY_5200_HASH ^ shiftctrl;
		case '*':
			return AKEY_5200_ASTERISK ^ shiftctrl;
		}
		return AKEY_NONE;
	}

	if (key_control)
		shiftctrl ^= AKEY_CTRL;

	switch (lastkey) {
	case SDLK_BACKQUOTE: /* fallthrough */
		/* These are the "Windows" keys, but they don't work on Windows*/
	case SDLK_LSUPER:
		return AKEY_ATARI ^ shiftctrl;
	case SDLK_RSUPER:
		if (INPUT_key_shift)
			return AKEY_CAPSLOCK;
		else
			return AKEY_CAPSTOGGLE;
	case SDLK_END:
	case SDLK_PAGEDOWN:
		return AKEY_F2 | AKEY_SHFT;
	case SDLK_PAGEUP:
		return AKEY_F1 | AKEY_SHFT;
	case SDLK_HOME:
		return key_control ? AKEY_LESS|shiftctrl : AKEY_CLEAR;
	case SDLK_PAUSE:
	case SDLK_CAPSLOCK:
		if (INPUT_key_shift)
			return AKEY_CAPSLOCK|shiftctrl;
		else
			return AKEY_CAPSTOGGLE|shiftctrl;
	case SDLK_SPACE:
		return AKEY_SPACE ^ shiftctrl;
	case SDLK_BACKSPACE:
		return AKEY_BACKSPACE|shiftctrl;
	case SDLK_RETURN:
		return AKEY_RETURN ^ shiftctrl;
	case SDLK_LEFT:
		return (!UI_is_active && Atari800_f_keys ? AKEY_F3 : (INPUT_key_shift ? AKEY_PLUS : AKEY_LEFT)) ^ shiftctrl;
	case SDLK_RIGHT:
		return (!UI_is_active && Atari800_f_keys ? AKEY_F4 : (INPUT_key_shift ? AKEY_ASTERISK : AKEY_RIGHT)) ^ shiftctrl;
	case SDLK_UP:
		return (!UI_is_active && Atari800_f_keys ? AKEY_F1 : (INPUT_key_shift ? AKEY_MINUS : AKEY_UP)) ^ shiftctrl;
	case SDLK_DOWN:
		return (!UI_is_active && Atari800_f_keys ? AKEY_F2 : (INPUT_key_shift ? AKEY_EQUAL : AKEY_DOWN)) ^ shiftctrl;
	case SDLK_ESCAPE:
		/* Windows takes ctrl+esc and ctrl+shift+esc */
		return AKEY_ESCAPE ^ shiftctrl;
	case SDLK_TAB:
#if HAVE_WINDOWS_H
		/* On Windows, when an SDL window has focus and LAlt+Tab is pressed,
		   a window-switching menu appears, but the LAlt+Tab key sequence is
		   still forwarded to the SDL window. In the effect the user cannot
		   switch with LAlt+Tab without the emulator registering unwanted key
		   presses. On other operating systems (e.g. GNU/Linux/KDE) everything
		   is OK, the key sequence is not registered by the emulator. This
		   hack fixes the behaviour on Windows. */
		if (kbhits[SDLK_LALT]) {
			key_pressed = 0;
			/* 1. In fullscreen software (non-OpenGL) mode, user presses LAlt, then presses Tab.
			      Atari800 window gets minimised and the window-switching menu appears.
			   2. User switches back to Atari800 without releasing LAlt.
			   3. User releases LAlt. Atari800 gets switched back to fullscreen.
			   In the above situation, the emulator would register pressing of LAlt but
			   would not register releasing of the key. It would think that LAlt is still
			   pressed. The hack below fixes the issue by causing SDL to assume LAlt is
			   not pressed. */
#if HAVE_OPENGL
			if (!VIDEOMODE_windowed && !SDL_VIDEO_opengl)
#else
			if (!VIDEOMODE_windowed)
#endif /* HAVE_OPENGL */
				kbhits[SDLK_LALT] = 0;
			return AKEY_NONE;
		}
#endif /* HAVE_WINDOWS_H */
		return AKEY_TAB ^ shiftctrl;
	case SDLK_DELETE:
		if (INPUT_key_shift)
			return AKEY_DELETE_LINE|shiftctrl;
		else
			return AKEY_DELETE_CHAR;
	case SDLK_INSERT:
		if (INPUT_key_shift)
			return AKEY_INSERT_LINE|shiftctrl;
		else
			return AKEY_INSERT_CHAR;
	}
	if (INPUT_cx85) switch (lastkey) {
	case SDLK_KP1:
		return AKEY_CX85_1;
	case SDLK_KP2:
		return AKEY_CX85_2;
	case SDLK_KP3:
		return AKEY_CX85_3;
	case SDLK_KP4:
		return AKEY_CX85_4;
	case SDLK_KP5:
		return AKEY_CX85_5;
	case SDLK_KP6:
		return AKEY_CX85_6;
	case SDLK_KP7:
		return AKEY_CX85_7;
	case SDLK_KP8:
		return AKEY_CX85_8;
	case SDLK_KP9:
		return AKEY_CX85_9;
	case SDLK_KP0:
		return AKEY_CX85_0;
	case SDLK_KP_PERIOD:
		return AKEY_CX85_PERIOD;
	case SDLK_KP_MINUS:
		return AKEY_CX85_MINUS;
	case SDLK_KP_ENTER:
		return AKEY_CX85_PLUS_ENTER;
	case SDLK_KP_DIVIDE:
		return (key_control ? AKEY_CX85_ESCAPE : AKEY_CX85_NO);
	case SDLK_KP_MULTIPLY:
		return AKEY_CX85_DELETE;
	case SDLK_KP_PLUS:
		return AKEY_CX85_YES;
	}

	/* Handle CTRL-0 to CTRL-9 and other control characters */
	if (key_control) {
		switch(lastuni) {
		case '.':
			return AKEY_FULLSTOP|shiftctrl;
		case ',':
			return AKEY_COMMA|shiftctrl;
		case ';':
			return AKEY_SEMICOLON|shiftctrl;
		}
		switch (lastkey) {
		case SDLK_PERIOD:
			return AKEY_FULLSTOP|shiftctrl;
		case SDLK_COMMA:
			return AKEY_COMMA|shiftctrl;
		case SDLK_SEMICOLON:
			return AKEY_SEMICOLON|shiftctrl;
		case SDLK_SLASH:
			return AKEY_SLASH|shiftctrl;
		case SDLK_BACKSLASH:
			/* work-around for Windows */
			return AKEY_ESCAPE|shiftctrl;
		case SDLK_0:
			return AKEY_CTRL_0|shiftctrl;
		case SDLK_1:
			return AKEY_CTRL_1|shiftctrl;
		case SDLK_2:
			return AKEY_CTRL_2|shiftctrl;
		case SDLK_3:
			return AKEY_CTRL_3|shiftctrl;
		case SDLK_4:
			return AKEY_CTRL_4|shiftctrl;
		case SDLK_5:
			return AKEY_CTRL_5|shiftctrl;
		case SDLK_6:
			return AKEY_CTRL_6|shiftctrl;
		case SDLK_7:
			return AKEY_CTRL_7|shiftctrl;
		case SDLK_8:
			return AKEY_CTRL_8|shiftctrl;
		case SDLK_9:
			return AKEY_CTRL_9|shiftctrl;
		}
	}

	/* Host Caps Lock will make lastuni switch case, so prevent this*/
    if(lastuni>='A' && lastuni <= 'Z' && !INPUT_key_shift) lastuni += 0x20;
    if(lastuni>='a' && lastuni <= 'z' && INPUT_key_shift) lastuni -= 0x20;
	/* Uses only UNICODE translation, no shift states (this was added to
	 * support non-US keyboard layouts)*/
	/* input.c takes care of removing invalid shift+control keys */
	switch (lastuni) {
	case 1:
		return AKEY_CTRL_a|shiftctrl;
	case 2:
		return AKEY_CTRL_b|shiftctrl;
	case 3:
		return AKEY_CTRL_c|shiftctrl;
	case 4:
		return AKEY_CTRL_d|shiftctrl;
	case 5:
		return AKEY_CTRL_e|shiftctrl;
	case 6:
		return AKEY_CTRL_f|shiftctrl;
	case 7:
		return AKEY_CTRL_g|shiftctrl;
	case 8:
		return AKEY_CTRL_h|shiftctrl;
	case 9:
		return AKEY_CTRL_i|shiftctrl;
	case 10:
		return AKEY_CTRL_j|shiftctrl;
	case 11:
		return AKEY_CTRL_k|shiftctrl;
	case 12:
		return AKEY_CTRL_l|shiftctrl;
	case 13:
		return AKEY_CTRL_m|shiftctrl;
	case 14:
		return AKEY_CTRL_n|shiftctrl;
	case 15:
		return AKEY_CTRL_o|shiftctrl;
	case 16:
		return AKEY_CTRL_p|shiftctrl;
	case 17:
		return AKEY_CTRL_q|shiftctrl;
	case 18:
		return AKEY_CTRL_r|shiftctrl;
	case 19:
		return AKEY_CTRL_s|shiftctrl;
	case 20:
		return AKEY_CTRL_t|shiftctrl;
	case 21:
		return AKEY_CTRL_u|shiftctrl;
	case 22:
		return AKEY_CTRL_v|shiftctrl;
	case 23:
		return AKEY_CTRL_w|shiftctrl;
	case 24:
		return AKEY_CTRL_x|shiftctrl;
	case 25:
		return AKEY_CTRL_y|shiftctrl;
	case 26:
		return AKEY_CTRL_z|shiftctrl;
	case 'A':
		return AKEY_A;
	case 'B':
		return AKEY_B;
	case 'C':
		return AKEY_C;
	case 'D':
		return AKEY_D;
	case 'E':
		return AKEY_E;
	case 'F':
		return AKEY_F;
	case 'G':
		return AKEY_G;
	case 'H':
		return AKEY_H;
	case 'I':
		return AKEY_I;
	case 'J':
		return AKEY_J;
	case 'K':
		return AKEY_K;
	case 'L':
		return AKEY_L;
	case 'M':
		return AKEY_M;
	case 'N':
		return AKEY_N;
	case 'O':
		return AKEY_O;
	case 'P':
		return AKEY_P;
	case 'Q':
		return AKEY_Q;
	case 'R':
		return AKEY_R;
	case 'S':
		return AKEY_S;
	case 'T':
		return AKEY_T;
	case 'U':
		return AKEY_U;
	case 'V':
		return AKEY_V;
	case 'W':
		return AKEY_W;
	case 'X':
		return AKEY_X;
	case 'Y':
		return AKEY_Y;
	case 'Z':
		return AKEY_Z;
	case ':':
		return AKEY_COLON;
	case '!':
		return AKEY_EXCLAMATION;
	case '@':
		return AKEY_AT;
	case '#':
		return AKEY_HASH;
	case '$':
		return AKEY_DOLLAR;
	case '%':
		return AKEY_PERCENT;
	case '^':
		return AKEY_CARET;
	case '&':
		return AKEY_AMPERSAND;
	case '*':
		return AKEY_ASTERISK;
	case '(':
		return AKEY_PARENLEFT;
	case ')':
		return AKEY_PARENRIGHT;
	case '+':
		return AKEY_PLUS;
	case '_':
		return AKEY_UNDERSCORE;
	case '"':
		return AKEY_DBLQUOTE;
	case '?':
		return AKEY_QUESTION;
	case '<':
		return AKEY_LESS;
	case '>':
		return AKEY_GREATER;
	case 'a':
		return AKEY_a;
	case 'b':
		return AKEY_b;
	case 'c':
		return AKEY_c;
	case 'd':
		return AKEY_d;
	case 'e':
		return AKEY_e;
	case 'f':
		return AKEY_f;
	case 'g':
		return AKEY_g;
	case 'h':
		return AKEY_h;
	case 'i':
		return AKEY_i;
	case 'j':
		return AKEY_j;
	case 'k':
		return AKEY_k;
	case 'l':
		return AKEY_l;
	case 'm':
		return AKEY_m;
	case 'n':
		return AKEY_n;
	case 'o':
		return AKEY_o;
	case 'p':
		return AKEY_p;
	case 'q':
		return AKEY_q;
	case 'r':
		return AKEY_r;
	case 's':
		return AKEY_s;
	case 't':
		return AKEY_t;
	case 'u':
		return AKEY_u;
	case 'v':
		return AKEY_v;
	case 'w':
		return AKEY_w;
	case 'x':
		return AKEY_x;
	case 'y':
		return AKEY_y;
	case 'z':
		return AKEY_z;
	case ';':
		return AKEY_SEMICOLON;
	case '0':
		return AKEY_0;
	case '1':
		return AKEY_1;
	case '2':
		return AKEY_2;
	case '3':
		return AKEY_3;
	case '4':
		return AKEY_4;
	case '5':
		return AKEY_5;
	case '6':
		return AKEY_6;
	case '7':
		return AKEY_7;
	case '8':
		return AKEY_8;
	case '9':
		return AKEY_9;
	case ',':
		return AKEY_COMMA;
	case '.':
		return AKEY_FULLSTOP;
	case '=':
		return AKEY_EQUAL;
	case '-':
		return AKEY_MINUS;
	case '\'':
		return AKEY_QUOTE;
	case '/':
		return AKEY_SLASH;
	case '\\':
		return AKEY_BACKSLASH;
	case '[':
		return AKEY_BRACKETLEFT;
	case ']':
		return AKEY_BRACKETRIGHT;
	case '|':
		return AKEY_BAR;
	}

	return AKEY_NONE;
}

/*---------------------------------------------------------------------------------------------------------------------*/
/*Mouse support*/
/*---------------------------------------------------------------------------------------------------------------------*/

void SDL_INPUT_Mouse(void)
{
	Uint8 buttons;

	if(INPUT_direct_mouse) {
		int potx, poty;

		buttons = SDL_GetMouseState(&potx, &poty);
		if(potx < 0) potx = 0;
		if(poty < 0) poty = 0;
		potx = (double)potx * (228.0 / (double)SDL_VIDEO_width);
		poty = (double)poty * (228.0 / (double)SDL_VIDEO_height);
		if(potx > 227) potx = 227;
		if(poty > 227) poty = 227;
		POKEY_POT_input[INPUT_mouse_port << 1] = 227 - potx;
		POKEY_POT_input[(INPUT_mouse_port << 1) + 1] = 227 - poty;
	} else {
		buttons = SDL_GetRelativeMouseState(&INPUT_mouse_delta_x, &INPUT_mouse_delta_y);
	}

	INPUT_mouse_buttons =
		((buttons & SDL_BUTTON(1)) ? 1 : 0) | /* Left button */
		((buttons & SDL_BUTTON(3)) ? 2 : 0) | /* Right button */
		((buttons & SDL_BUTTON(2)) ? 4 : 0); /* Middle button */
}

/*---------------------------------------------------------------------------------------------------------------------*/
/*Global functions*/
/*---------------------------------------------------------------------------------------------------------------------*/

/******************************************************************************/
/*PLATFORM_Keyboard*/
/*Called externally to acquire atari key presses and atari functions*/
/*returns atari key/function*/
/******************************************************************************/
int PLATFORM_Keyboard(void)
{
	int key_code;
	
	GamePads_Update();
	/*getting functions from keyboard presses*/
	key_code = Keyboard_AtariKeys();
	/*if none then getting functions from game pads*/
	if (key_code == AKEY_NONE)
		key_code = GamePads_AtariKeys();
	/*merging consol key states*/
	INPUT_key_consol = input_key_consol_from_keys;
	INPUT_key_consol &= gamepads_consol_state;
	return key_code;
}

/******************************************************************************/
/*SDL_INPUT_ReadConfig*/
/*Called externally to load config, one call for every line of configuration file*/
/******************************************************************************/
int SDL_INPUT_ReadConfig(char *option, char *parameters)
{
	int ret;
	ret = Keyboard_ReadConfig(option, parameters);
	if (ret) {
		return TRUE;
	}
	if (strncmp(option, "SDL_PAD_", 8) == 0) {
		GamePads_Read_Config(&(option[8]), parameters);
		return TRUE;
	}
	return FALSE;
}

/******************************************************************************/
/*SDL_INPUT_WriteConfig*/
/*Called externally to write config*/
/******************************************************************************/
void SDL_INPUT_WriteConfig(FILE *fp)
{
	Keyboard_WriteConfig(fp);
	GamePads_Write_Config(fp);
}

/******************************************************************************/
/*SDL_INPUT_Initialise*/
/*Called to initialize whole input system*/
/*argc, argv - arguments passed from command line*/
/*returns TRUE if initialization completed successfully*/
/******************************************************************************/

int SDL_INPUT_Initialise(int *argc, char *argv[])
{
	/* TODO check for errors! */
#ifdef LPTJOY
	char *lpt_joy0 = NULL;
	char *lpt_joy1 = NULL;
#endif /* LPTJOY */
	int i;
	int j;
	int no_joystick = FALSE;
	int help_only = FALSE;

	input_key_consol_from_keys = INPUT_CONSOL_NONE;

	for (i = j = 1; i < *argc; i++) {
#ifdef LPTJOY
		int i_a = (i + 1 < *argc);		/* is argument available? */
#endif /* LPTJOY */
		int a_m = FALSE;			/* error, argument missing! */
		if (strcmp(argv[i], "-nojoystick") == 0) {
			no_joystick = TRUE;
			Log_print("no joystick");
		}
		else if (strcmp(argv[i], "-grabmouse") == 0) {
			grab_mouse = TRUE;
		}
/*TODO: add support for better aguments for pad and pad configuration ui*/
                /* else if (strcmp(argv[i], "-joy0hat") == 0) {*/
                        /* real_js_configs[0].use_hat = TRUE;*/
                /* }*/
                /* else if (strcmp(argv[i], "-joy1hat") == 0) {*/
                        /* real_js_configs[1].use_hat = TRUE;*/
                /* }*/
                /* else if (strcmp(argv[i], "-joy2hat") == 0) {*/
                        /* real_js_configs[2].use_hat = TRUE;*/
                /* }*/
                /* else if (strcmp(argv[i], "-joy3hat") == 0) {*/
                        /* real_js_configs[3].use_hat = TRUE;*/
                /* }*/
#ifdef LPTJOY
		else if (strcmp(argv[i], "-joy0") == 0) {
			if (i_a) {
				lpt_joy0 = argv[++i];
			}
			else a_m = TRUE;
		}
		else if (!strcmp(argv[i], "-joy1")) {
			if (i_a) {
				lpt_joy1 = argv[++i];
			}
			else a_m = TRUE;
		}
#endif /* LPTJOY */
		else if (strcmp(argv[i], "-kbdjoy0") == 0) {
			PLATFORM_kbd_joy_0_enabled = TRUE;
		}
		else if (!strcmp(argv[i], "-kbdjoy1")) {
			PLATFORM_kbd_joy_1_enabled = TRUE;
		}
		else if (strcmp(argv[i], "-no-kbdjoy0") == 0) {
			PLATFORM_kbd_joy_0_enabled = FALSE;
		}
		else if (!strcmp(argv[i], "-no-kbdjoy1")) {
			PLATFORM_kbd_joy_1_enabled = FALSE;
		}
		else {
			if (strcmp(argv[i], "-help") == 0) {
				help_only = TRUE;
				Log_print("\t-nojoystick      Disable joystick");
				/* Log_print("\t-joy0hat         Use hat of joystick 0");*/
				/* Log_print("\t-joy1hat         Use hat of joystick 1");*/
				/* Log_print("\t-joy2hat         Use hat of joystick 2");*/
				/* Log_print("\t-joy3hat         Use hat of joystick 3");*/
#ifdef LPTJOY
				Log_print("\t-joy0 <pathname> Select LPTjoy0 device");
				Log_print("\t-joy1 <pathname> Select LPTjoy1 device");
#endif /* LPTJOY */
				Log_print("\t-kbdjoy0         enable joystick 0 keyboard emulation");
				Log_print("\t-kbdjoy1         enable joystick 1 keyboard emulation");
				Log_print("\t-no-kbdjoy0      disable joystick 0 keyboard emulation");
				Log_print("\t-no-kbdjoy1      disable joystick 1 keyboard emulation");

				Log_print("\t-grabmouse       Prevent mouse pointer from leaving window");
			}
			argv[j++] = argv[i];
		}

		if (a_m) {
			Log_print("Missing argument for '%s'", argv[i]);
			return FALSE;
		}
	}
	*argc = j;

	if (help_only)
		return TRUE;

	if (!no_joystick) {
#ifdef LPTJOY
		if (lpt_joy0 != NULL) {				/* LPT1 joystick */
			fd_joystick0 = open(lpt_joy0, O_RDONLY);
			if (fd_joystick0 == -1)
				perror(lpt_joy0);
		}
		if (lpt_joy1 != NULL) {				/* LPT2 joystick */
			fd_joystick1 = open(lpt_joy1, O_RDONLY);
			if (fd_joystick1 == -1)
				perror(lpt_joy1);
		}
#endif /* LPTJOY */
		/*initializing gamepads*/
		GamePads_Init(fd_joystick0 == -1, fd_joystick1 == -1);
	}

	if (INPUT_cx85) { /* disable keyboard joystick if using CX85 numpad */
		PLATFORM_kbd_joy_0_enabled = 0;
	}
	if(grab_mouse)
		SDL_WM_GrabInput(SDL_GRAB_ON);

	kbhits = SDL_GetKeyState(NULL);

	if (kbhits == NULL) {
		Log_print("SDL_GetKeyState() failed");
		Log_flushlog();
		return FALSE;
	}

	return TRUE;
}

void SDL_INPUT_Exit(void)
{
	SDL_WM_GrabInput(SDL_GRAB_OFF);
}

void SDL_INPUT_Restart(void)
{
	lastkey = SDLK_UNKNOWN;
	key_pressed = key_control = lastuni = 0;
	if(grab_mouse) SDL_WM_GrabInput(SDL_GRAB_ON);
}

static int get_LPT_joystick_state(int fd)
{
#ifdef LPTJOY
	int status;

	ioctl(fd, LPGETSTATUS, &status);
	status ^= 0x78;

	if (status & 0x40) {			/* right */
		if (status & 0x10) {		/* up */
			return INPUT_STICK_UR;
		}
		else if (status & 0x20) {	/* down */
			return INPUT_STICK_LR;
		}
		else {
			return INPUT_STICK_RIGHT;
		}
	}
	else if (status & 0x80) {		/* left */
		if (status & 0x10) {		/* up */
			return INPUT_STICK_UL;
		}
		else if (status & 0x20) {	/* down */
			return INPUT_STICK_LL;
		}
		else {
			return INPUT_STICK_LEFT;
		}
	}
	else {
		if (status & 0x10) {		/* up */
			return INPUT_STICK_FORWARD;
		}
		else if (status & 0x20) {	/* down */
			return INPUT_STICK_BACK;
		}
		else {
			return INPUT_STICK_CENTRE;
		}
	}
#else
	return 0;
#endif /* LPTJOY */
}

static void get_platform_PORT(Uint8 *s0, Uint8 *s1, Uint8 *s2, Uint8 *s3)
{
	int stick0, stick1;
	stick0 = stick1 = INPUT_STICK_CENTRE;

	if (PLATFORM_kbd_joy_0_enabled) {
		if (kbhits[KBD_STICK_0_LEFT])
			stick0 &= INPUT_STICK_LEFT;
		if (kbhits[KBD_STICK_0_RIGHT])
			stick0 &= INPUT_STICK_RIGHT;
		if (kbhits[KBD_STICK_0_UP])
			stick0 &= INPUT_STICK_FORWARD;
		if (kbhits[KBD_STICK_0_DOWN])
			stick0 &= INPUT_STICK_BACK;
	}
	if (PLATFORM_kbd_joy_1_enabled) {
		if (kbhits[KBD_STICK_1_LEFT])
			stick1 &= INPUT_STICK_LEFT;
		if (kbhits[KBD_STICK_1_RIGHT])
			stick1 &= INPUT_STICK_RIGHT;
		if (kbhits[KBD_STICK_1_UP])
			stick1 &= INPUT_STICK_FORWARD;
		if (kbhits[KBD_STICK_1_DOWN])
			stick1 &= INPUT_STICK_BACK;
	}

	if (swap_joysticks) {
		*s1 = stick0;
		*s0 = stick1;
	}
	else {
		*s0 = stick0;
		*s1 = stick1;
	}

	if (fd_joystick0 != -1)
		*s0 &= get_LPT_joystick_state(fd_joystick0);
	else if (sdl_gamepads[0] != NULL)
		/**s0 &= sdl_js_state[0].port;*/
		*s0 &= GamePads_AtariJoy(0);

	if (fd_joystick1 != -1)
		*s1 &= get_LPT_joystick_state(fd_joystick1);
	else if (sdl_gamepads[1] != NULL)
		/**s1 &= sdl_js_state[1].port;*/
		*s1 &= GamePads_AtariJoy(1);
	
	/* *s2 = sdl_js_state[2].port;*/
	/* *s3 = sdl_js_state[3].port;*/
	*s2 = GamePads_AtariJoy(2);
	*s3 = GamePads_AtariJoy(3);
}

static void get_platform_TRIG(Uint8 *t0, Uint8 *t1, Uint8 *t2, Uint8 *t3)
{
	int trig0, trig1;
	trig0 = trig1 = 1;

	if (PLATFORM_kbd_joy_0_enabled) {
		trig0 = !kbhits[KBD_TRIG_0];
	}

	if (PLATFORM_kbd_joy_1_enabled) {
		trig1 = !kbhits[KBD_TRIG_1];
	}

	if (swap_joysticks) {
		*t1 = trig0;
		*t0 = trig1;
	}
	else {
		*t0 = trig0;
		*t1 = trig1;
	}

	if (fd_joystick0 != -1) {
#ifdef LPTJOY
		int status;
		ioctl(fd_joystick0, LPGETSTATUS, &status);
		*t0 &= ((status & 8) > 0);
#endif /* LPTJOY */
	}
	/* else if (joystick[0] != NULL) {*/
		/* trig0 = 1;*/
/* #ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD*/
		/* if (OSK_enabled) {*/
			/* if (sdl_js_state[0].trig & (1 << OSK_BUTTON_TRIGGER))*/
				/* trig0 = 0;*/
		/* }*/
		/* else*/
/* #endif*/
			/* if (sdl_js_state[0].trig)*/
				/* trig0 = 0;*/
		/* *t0 &= trig0;*/
	/* }*/
	
	*t0 &= GamePads_AtariTrigger(0);

	if (fd_joystick1 != -1) {
#ifdef LPTJOY
		int status;
		ioctl(fd_joystick1, LPGETSTATUS, &status);
		*t1 &= ((status & 8) > 0);
#endif /* LPTJOY */
	}
	/* else if (joystick[1] != NULL) {*/
		/* trig1 = 1;*/
		/* if (sdl_js_state[1].trig)*/
			/* trig1 = 0;*/
		/* *t1 &= trig1;*/
	/* }*/
	*t1 &= GamePads_AtariTrigger(1);

	/* *t2 = sdl_js_state[2].trig ? 0 : 1;*/
	/* *t3 = sdl_js_state[3].trig ? 0 : 1;*/
	*t2 = GamePads_AtariTrigger(2);
	*t3 = GamePads_AtariTrigger(3);
}

int PLATFORM_PORT(int num)
{
#ifndef DONT_DISPLAY
	UBYTE a, b, c, d;
	/*update_SDL_joysticks();*/
	get_platform_PORT(&a, &b, &c, &d);
	if (num == 0) {
		return (b << 4) | (a & 0x0f);
	}
	else if (num == 1) {
		return (d << 4) | (c & 0x0f);
	}
#endif
	return 0xff;
}

int PLATFORM_TRIG(int num)
{
#ifndef DONT_DISPLAY
	UBYTE a, b, c, d;
	get_platform_TRIG(&a, &b, &c, &d);
	switch (num) {
	case 0:
		return a;
	case 1:
		return b;
	case 2:
		return c;
	case 3:
		return d;
	default:
		break;
	}
#endif
	return 1;
}

#ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD

#define REPEAT_DELAY 100  /* in ms */
#define REPEAT_INI_DELAY (5 * REPEAT_DELAY)

int UI_BASIC_in_kbui;

static int ui_leave_in_progress;   /* was 'b_ui_leave' */

/*
 * do some basic keyboard emulation using the joystick controller
 */
static int SDL_controller_kb1(void)
{
	static int prev_up = FALSE, prev_down = FALSE, prev_trigger = FALSE,
		prev_keyb = FALSE, prev_left = FALSE, prev_right = FALSE,
		prev_leave = FALSE, prev_ui = FALSE;
	static int repdelay_timeout = REPEAT_DELAY;
	struct js_state *state = &sdl_js_state[0];

	if (! joysticks_found) return(AKEY_NONE);  /* no controller present */

	/*update_SDL_joysticks();*/

	if (!UI_is_active && (state->trig & (1 << OSK_BUTTON_UI))) {
		return(AKEY_UI);
	}
	if (!UI_is_active && (state->trig & (1 << OSK_BUTTON_KEYB))) {
		return(AKEY_KEYB);
	}
	/* provide keyboard emulation to enter file name */
	if (UI_is_active && !UI_BASIC_in_kbui && (state->trig & (1 << OSK_BUTTON_KEYB))) {
		int keycode;
		/*update_SDL_joysticks();*/
		UI_BASIC_in_kbui = TRUE;
		memcpy(atari_screen_backup, Screen_atari, Screen_HEIGHT * Screen_WIDTH);
		keycode = UI_BASIC_OnScreenKeyboard(NULL, -1);
		memcpy(Screen_atari, atari_screen_backup, Screen_HEIGHT * Screen_WIDTH);
		Screen_EntireDirty();
		PLATFORM_DisplayScreen();
		UI_BASIC_in_kbui = FALSE;
		return keycode;
#if 0 /* @@@ 26-Mar-2013, chris: check this */
		if (inject_key != AKEY_NONE) {
			keycode = inject_key;
			inject_key = AKEY_NONE;
			return(keycode);
		}
		else {
			return(AKEY_NONE);
		}
#endif
	}

	if (UI_is_active || UI_BASIC_in_kbui) {
		if (!(state->port & 1)) {
			prev_down = FALSE;
			if (! prev_up) {
				repdelay_timeout = SDL_GetTicks() + REPEAT_INI_DELAY;
				prev_up = 1;
				return(AKEY_UP);
			}
			else {
				if (SDL_GetTicks() > repdelay_timeout) {
					repdelay_timeout = SDL_GetTicks() + REPEAT_DELAY;
					return(AKEY_UP);
				}
			}
		}
		else {
			prev_up = FALSE;
		}

		if (!(state->port & 2)) {
			prev_up = FALSE;
			if (! prev_down) {
				repdelay_timeout = SDL_GetTicks() + REPEAT_INI_DELAY;
				prev_down = TRUE;
				return(AKEY_DOWN);
			}
			else {
				if (SDL_GetTicks() > repdelay_timeout) {
					repdelay_timeout = SDL_GetTicks() + REPEAT_DELAY;
					return(AKEY_DOWN);
				}
			}
		}
		else {
			prev_down = FALSE;
		}

		if (!(state->port & 4)) {
			prev_right = FALSE;
			if (! prev_left) {
				repdelay_timeout = SDL_GetTicks() + REPEAT_INI_DELAY;
				prev_left = TRUE;
				return(AKEY_LEFT);
			}
			else {
				if (SDL_GetTicks() > repdelay_timeout) {
					repdelay_timeout = SDL_GetTicks() + REPEAT_DELAY;
					return(AKEY_LEFT);
				}
			}
		}
		else {
			prev_left = FALSE;
		}

		if (!(state->port & 8)) {
			prev_left = FALSE;
			if (! prev_right) {
				repdelay_timeout = SDL_GetTicks() + REPEAT_INI_DELAY;
				prev_right = TRUE;
				return(AKEY_RIGHT);
			}
			else {
				if (SDL_GetTicks() > repdelay_timeout) {
					repdelay_timeout = SDL_GetTicks() + REPEAT_DELAY;
					return(AKEY_RIGHT);
				}
			}
		}
		else {
			prev_right = FALSE;
		}


		if ((state->trig & (1 << OSK_BUTTON_TRIGGER))) {
			if (! prev_trigger) {
				prev_trigger = TRUE;
				return(AKEY_RETURN);
			}
		}
		else {
			prev_trigger = FALSE;
		}

		if ((state->trig & (1 << OSK_BUTTON_LEAVE))) {
			if (! prev_leave) {
				prev_leave = TRUE;
				ui_leave_in_progress = TRUE;   /* OSK_BUTTON_LEAVE must be released again */
				return(AKEY_ESCAPE);
			}
		}
		else {
			prev_leave = FALSE;
		}

		if ((state->trig & (1 << OSK_BUTTON_UI))) {
			if (! prev_ui && UI_BASIC_in_kbui) {
				prev_ui = TRUE;
				return(AKEY_ESCAPE);
			}
		}
		else {
			prev_ui = FALSE;
		}

		if ((state->trig & (1 << OSK_BUTTON_KEYB))) {
			if (! prev_keyb) {
				prev_keyb = TRUE;
				return(AKEY_ESCAPE);
			}
		}
		else {
			prev_keyb = FALSE;
		}
	}
	return(AKEY_NONE);
}

static int SDL_controller_kb(void)
{
	int key = SDL_controller_kb1();
#ifdef DEBUG
	if (key != AKEY_NONE) printf("SDL_controller_kb: key = 0x%x\n", key);
#endif
	return key;
}

static int SDL_consol_keys(void)
{
	struct js_state *state = &sdl_js_state[0];

	INPUT_key_consol = INPUT_CONSOL_NONE;

#if OSK_BUTTON_START != OSK_BUTTON_LEAVE
#error FIXME: make button assignments configurable
#endif
	if (Atari800_machine_type != Atari800_MACHINE_5200) {
		if (! (UI_is_active || UI_BASIC_in_kbui)) {
			if ((state->trig & (1 << OSK_BUTTON_START))) {
				if (! ui_leave_in_progress)
					INPUT_key_consol &= ~INPUT_CONSOL_START;
				else
					INPUT_key_consol |= INPUT_CONSOL_START;
			}
			else {
				ui_leave_in_progress = FALSE;
				INPUT_key_consol |= INPUT_CONSOL_START;
			}

			if ((state->trig & (1 << OSK_BUTTON_SELECT)))
				INPUT_key_consol &= ~INPUT_CONSOL_SELECT;
			else
				INPUT_key_consol |= INPUT_CONSOL_SELECT;

			if ((state->trig & (1 << OSK_BUTTON_OPTION)))
				INPUT_key_consol &= ~INPUT_CONSOL_OPTION;
			else
				INPUT_key_consol |= INPUT_CONSOL_OPTION;
		}
	}
	else {
		/* @@@ 5200: TODO @@@ */
	}
	return(AKEY_NONE);
}

#endif /* #ifdef USE_UI_BASIC_ONSCREEN_KEYBOARD */
