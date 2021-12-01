#ifndef SDL_INPUT_H_
#define SDL_INPUT_H_

#include <stdio.h>

#define MAX_PAD_BUTTONS 16

/*Configuration of a real SDL joystick*/
typedef struct SDL_INPUT_RealJSConfig_t {
	/*value that will cause sending stick event to atari */
	int deadzone;
	/*difference between enabled side disabling and disabled side enabling*/
	float tolerance;
	/*true if joystick should be used as atari stick*/
	int use_as_stick;
	/*true if joystick hat should be used as atari stick*/
	int use_hat_as_stick;
	/*true if joystick should be used in menus*/
	int use_in_menus;
	/*true if joystick hat should be used in menus*/
	int use_hat_in_menus;
	/*button number used to select things in menus*/
	int in_menus_select_button;
	/*button number used to return to upper menu*/
	int in_menus_back_button;
	/*frequency of autofire in frames on/off, eg. 3 means that 3 frames fire will be on, and 3 frames off*/
	int autofire_freq;
	/*functions assigned to specified buttons*/
	int button_functions[MAX_PAD_BUTTONS];
	/*functions assigned to specified buttons (called when FN_SP button is pressed)*/
	int button_sp_functions[MAX_PAD_BUTTONS];
	
} SDL_INPUT_RealJSConfig_t;

int SDL_INPUT_ReadConfig(char *option, char *parameters);
void SDL_INPUT_WriteConfig(FILE *fp);

int SDL_INPUT_Initialise(int *argc, char *argv[]);
void SDL_INPUT_Exit(void);
/* Restarts input after e.g. exiting the console monitor. */
void SDL_INPUT_Restart(void);

void SDL_INPUT_Mouse(void);

void SDL_INPUT_Update(void);

/*Get pointer to a real joystick configuration (for UI)*/
SDL_INPUT_RealJSConfig_t* SDL_INPUT_GetRealJSConfig(int joyIndex);

#endif /* SDL_INPUT_H_ */
