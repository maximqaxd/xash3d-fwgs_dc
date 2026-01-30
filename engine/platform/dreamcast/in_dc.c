/*
in_dc.c - DC input component
Copyright (C) 2024 maximqad
some code are borrowed from Xash3D PSP Port Copyright (C) 2021 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#if !XASH_DEDICATED
#if XASH_INPUT == INPUT_KOS
#include <dc/maple/controller.h>
#include <dc/maple/keyboard.h>
#include <dc/maple/mouse.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "sound.h"
#include "vid_common.h"

#define DC_MWHELL_UP	(1 << 8)
#define DC_MWHELL_DOWN	(1 << 9)

#define DC_CONT_LT		(1 << 16)
#define DC_CONT_RT		(1 << 17)

#define DC_MAX_KEYS		sizeof(dc_keymap) / sizeof(struct dc_keymap_s)
#define DC_MAX_MSKEY	sizeof(dc_mousemap) / sizeof(struct dc_mousemap_s)

static struct dc_keymap_s {
    int srckey;     
    int dstkey;      
} dc_keymap[] = 
{
	{ CONT_START,		K_START_BUTTON },
	{ CONT_B,			K_B_BUTTON },
	{ CONT_A,			K_A_BUTTON },
	{ CONT_DPAD_DOWN,	K_DPAD_DOWN },
	{ CONT_DPAD_UP,		K_DPAD_UP },
	{ CONT_DPAD_LEFT,	K_DPAD_LEFT },
	{ CONT_DPAD_RIGHT,	K_DPAD_RIGHT },
	{ CONT_Y,			K_Y_BUTTON },
	{ CONT_X,			K_X_BUTTON},
	{ CONT_C,			K_R1_BUTTON },
	{ CONT_Z,			K_L1_BUTTON },
	{ CONT_D,			K_BACK_BUTTON },
	{ CONT_DPAD2_LEFT,	K_LSTICK },
	{ CONT_DPAD2_RIGHT,	K_RSTICK },
	{ DC_CONT_LT,		K_JOY1 },
	{ DC_CONT_RT,		K_JOY2 },
};


static struct dc_mousemap_s {
    int srckey;
    int dstkey;
} dc_mousemap[] = {
	{ MOUSE_LEFTBUTTON, 0 },
	{ MOUSE_RIGHTBUTTON, 1 },
	{ MOUSE_SIDEBUTTON, 2 },
	{ 1 << 4, 3 },
	{ 1 << 5, 4 },
};

const static uint8_t dc_kbd_map[] =
{
	  0,   0,   0,   0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 
	'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2', 
	'3', '4', '5', '6', '7', '8', '9', '0', K_ENTER, K_ESCAPE, K_BACKSPACE, K_TAB, K_SPACE, '-', '=', '[', 
	']','\\',   0, ';', '\'', '`', ',', '.', '/', K_CAPSLOCK, K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, 
	K_F7, K_F8, K_F9, K_F10, K_F11, K_F12, 0, K_SCROLLLOCK, 0, K_INS, K_HOME, K_PGUP, K_DEL, K_END, K_PGDN, K_RIGHTARROW, 
	K_LEFTARROW, K_DOWNARROW, K_UPARROW, K_KP_NUMLOCK, K_KP_SLASH, K_KP_MUL, K_KP_MINUS, K_KP_PLUS, K_KP_ENTER, K_KP_END, K_KP_DOWNARROW, K_KP_PGDN, K_KP_LEFTARROW, K_KP_5, K_KP_RIGHTARROW, K_KP_HOME, 
	K_KP_UPARROW, K_KP_PGUP, K_KP_INS, K_KP_DEL, 0 /* S3 */
};

const static uint8_t dc_kbd_map_shift[] =
{
	'!', '@', '#', '$', '%', '^', '&', '*', 
	'(', ')',  0 ,  0 ,  0 ,  0 ,  0 , '_', 
	'+', '{', '}', '|',  0 , ':', '"', '~', 
	'<', '>', '?'
};

const static uint8_t dc_kbd_map_numlock[] =
{
	'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.'
};

static qboolean text_in_en = 1;
static qboolean numlock_en = 0;
static qboolean capslock_en = 0;

// Combo button system
typedef struct combo_button_s
{
	uint32_t modifier_mask;  // Button that must be held (e.g., L trigger)
	uint32_t button_mask;    // Button that triggers the combo (e.g., B button)
	char *command;           // Command to execute when combo is detected (allocated)
	qboolean is_active_press; // Track if combo is currently held (for +command/-command pairs)
	struct combo_button_s *next; // Linked list
} combo_button_t;

static combo_button_t *combo_buttons = NULL; // Dynamic list

// Button name to mask mapping
typedef struct button_name_s
{
	const char *name;
	uint32_t mask;
} button_name_t;

static const button_name_t button_names[] =
{
	{ "ltrigger", DC_CONT_LT },
	{ "ltrig", DC_CONT_LT },
	{ "lt", DC_CONT_LT },
	{ "rtrigger", DC_CONT_RT },
	{ "rtrig", DC_CONT_RT },
	{ "rt", DC_CONT_RT },
	{ "a", CONT_A },
	{ "b", CONT_B },
	{ "x", CONT_X },
	{ "y", CONT_Y },
	{ "c", CONT_C },
	{ "z", CONT_Z },
	{ "d", CONT_D },
	{ "start", CONT_START },
	{ "dpad_up", CONT_DPAD_UP },
	{ "dpad_down", CONT_DPAD_DOWN },
	{ "dpad_left", CONT_DPAD_LEFT },
	{ "dpad_right", CONT_DPAD_RIGHT },
	{ NULL, 0 }
};

// Parse button name to mask
static uint32_t Combo_ParseButton( const char *name )
{
	int i;
	
	if ( !name || !*name )
		return 0;
	
	for ( i = 0; button_names[i].name; i++ )
	{
		if ( !Q_stricmp( name, button_names[i].name ))
			return button_names[i].mask;
	}
	
	return 0;
}

// Parse combo string like "ltrigger+b" or "lt+x"
static qboolean Combo_ParseCombo( const char *combo_str, uint32_t *modifier, uint32_t *button )
{
	char combo_copy[64];
	char *mod_str, *btn_str, *plus;
	
	if ( !combo_str || !*combo_str )
		return false;
	
	Q_strncpy( combo_copy, combo_str, sizeof( combo_copy ));
	combo_copy[sizeof(combo_copy)-1] = 0;
	
	// Find the '+' separator
	plus = Q_strchr( combo_copy, '+' );
	if ( !plus )
		return false;
	
	*plus = '\0';
	mod_str = combo_copy;
	btn_str = plus + 1;
	
	// Trim whitespace
	while ( *mod_str == ' ' || *mod_str == '\t' ) mod_str++;
	while ( *btn_str == ' ' || *btn_str == '\t' ) btn_str++;
	
	*modifier = Combo_ParseButton( mod_str );
	*button = Combo_ParseButton( btn_str );
	
	return ( *modifier != 0 && *button != 0 );
}

// Add or update a combo binding
static void Combo_AddBinding( const char *combo_str, const char *command )
{
	combo_button_t *combo, *prev;
	uint32_t modifier, button;
	
	if ( !Combo_ParseCombo( combo_str, &modifier, &button ))
	{
		Con_Printf( S_ERROR "Invalid combo format: %s (expected: modifier+button, e.g., ltrigger+b)\n", combo_str );
		return;
	}
	
	// Check if combo already exists
	for ( prev = NULL, combo = combo_buttons; combo; prev = combo, combo = combo->next )
	{
		if ( combo->modifier_mask == modifier && combo->button_mask == button )
		{
			// Update existing combo
			if ( combo->command )
				Mem_Free( combo->command );
			combo->command = copystring( command );
			return;
		}
	}
	
	// Create new combo
	if ( !host.mempool )
	{
		Con_Printf( S_ERROR "Combo_AddBinding: host.mempool is NULL!\n" );
		return;
	}
	
	combo = Mem_Malloc( host.mempool, sizeof( combo_button_t ));
	if ( !combo )
	{
		Con_Printf( S_ERROR "Combo_AddBinding: Mem_Malloc failed!\n" );
		return;
	}
	
	combo->modifier_mask = modifier;
	combo->button_mask = button;
	combo->command = copystring( command );
	combo->is_active_press = false;
	combo->next = combo_buttons;
	combo_buttons = combo;
}

// Remove a combo binding
static void Combo_RemoveBinding( const char *combo_str )
{
	combo_button_t *combo, *prev;
	uint32_t modifier, button;
	
	if ( !Combo_ParseCombo( combo_str, &modifier, &button ))
	{
		Con_Printf( S_ERROR "Invalid combo format: %s\n", combo_str );
		return;
	}
	
	for ( prev = NULL, combo = combo_buttons; combo; prev = combo, combo = combo->next )
	{
		if ( combo->modifier_mask == modifier && combo->button_mask == button )
		{
			if ( prev )
				prev->next = combo->next;
			else
				combo_buttons = combo->next;
			
			if ( combo->command )
				Mem_Free( combo->command );
			Mem_Free( combo );
			Con_Printf( "Removed combo binding: %s\n", combo_str );
			return;
		}
	}
	
	Con_Printf( S_WARN "Combo binding not found: %s\n", combo_str );
}

// Clear all combo bindings
static void Combo_ClearAll( void )
{
	combo_button_t *combo, *next;
	
	for ( combo = combo_buttons; combo; combo = next )
	{
		next = combo->next;
		if ( combo->command )
			Mem_Free( combo->command );
		Mem_Free( combo );
	}
	
	combo_buttons = NULL;
	Con_Printf( "Cleared all combo bindings\n" );
}

// Command handler for combobind
static void Combo_Bind_f( void )
{
	int argc = Cmd_Argc();
	
	if ( argc < 2 )
	{
		Con_Printf( S_USAGE "combobind <combo> [command] : bind a command to a button combo\n" );
		Con_Printf( "  Example: combobind \"ltrigger+b\" \"client_buy_open\"\n" );
		Con_Printf( "  Format: modifier+button (e.g., ltrigger+b, lt+x)\n" );
		Con_Printf( "  Modifiers: ltrigger, ltrig, lt, rtrigger, rtrig, rt\n" );
		Con_Printf( "  Buttons: a, b, x, y, c, z, d, start, dpad_up, dpad_down, dpad_left, dpad_right\n" );
		return;
	}
	
	if ( argc == 2 )
	{
		// List binding if it exists
		uint32_t modifier, button;
		combo_button_t *combo;
		
		if ( Combo_ParseCombo( Cmd_Argv( 1 ), &modifier, &button ))
		{
			for ( combo = combo_buttons; combo; combo = combo->next )
			{
				if ( combo->modifier_mask == modifier && combo->button_mask == button )
				{
					Con_Printf( "\"%s\" = \"%s\"\n", Cmd_Argv( 1 ), combo->command );
					return;
				}
			}
		}
		
		Con_Printf( "\"%s\" is not bound\n", Cmd_Argv( 1 ));
		return;
	}
	
	// Add or update binding
	if ( !Q_stricmp( Cmd_Argv( 2 ), "" ))
	{
		// Remove binding
		Combo_RemoveBinding( Cmd_Argv( 1 ));
	}
	else
	{
		// Add/update binding
		char cmd[1024];
		int i;
		
		cmd[0] = 0;
		for ( i = 2; i < argc; i++ )
		{
			if ( i > 2 ) Q_strncat( cmd, " ", sizeof( cmd ));
			Q_strncat( cmd, Cmd_Argv( i ), sizeof( cmd ));
		}
		Q_strncat( cmd, "\n", sizeof( cmd ));
		
		Combo_AddBinding( Cmd_Argv( 1 ), cmd );
	}
}

// Command handler for combounbind
static void Combo_Unbind_f( void )
{
	if ( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "combounbind <combo> : remove a combo binding\n" );
		return;
	}
	
	Combo_RemoveBinding( Cmd_Argv( 1 ));
}

// Command handler for combounbindall
static void Combo_Unbindall_f( void )
{
	Combo_ClearAll();
}

// Write combo bindings to config file
void GAME_EXPORT Combo_WriteBindings( dc_file_t *f )
{
	combo_button_t *combo;
	string combo_str, newCommand;
	const button_name_t *mod_name, *btn_name;
	int i, j;
	
	if ( !f ) return;
	
	for ( combo = combo_buttons; combo; combo = combo->next )
	{
		// Find modifier name
		mod_name = NULL;
		for ( i = 0; button_names[i].name; i++ )
		{
			if ( button_names[i].mask == combo->modifier_mask )
			{
				mod_name = &button_names[i];
				break;
			}
		}
		
		// Find button name
		btn_name = NULL;
		for ( j = 0; button_names[j].name; j++ )
		{
			if ( button_names[j].mask == combo->button_mask )
			{
				btn_name = &button_names[j];
				break;
			}
		}
		
		if ( mod_name && btn_name && combo->command )
		{
			Q_snprintf( combo_str, sizeof( combo_str ), "%s+%s", mod_name->name, btn_name->name );
			
			// Remove trailing newline from command for config
			Q_strncpy( newCommand, combo->command, sizeof( newCommand ));
			if ( newCommand[Q_strlen(newCommand)-1] == '\n' )
				newCommand[Q_strlen(newCommand)-1] = '\0';
			
			Cmd_Escape( newCommand, newCommand, sizeof( newCommand ));
			FS_Printf( f, "combobind \"%s\" \"%s\"\n", combo_str, newCommand );
		}
	}
}

// Initialize combo system
void GAME_EXPORT Combo_Init( void )
{
	static qboolean initialized = false;
	
	if ( initialized )
		return;
	
	initialized = true;
	
	Cmd_AddRestrictedCommand( "combobind", Combo_Bind_f, "bind a command to a button combo (e.g., combobind \"ltrigger+b\" \"buy\")" );
	Cmd_AddRestrictedCommand( "combounbind", Combo_Unbind_f, "remove a combo binding" );
	Cmd_AddRestrictedCommand( "combounbindall", Combo_Unbindall_f, "remove all combo bindings" );
	
	// Hardcoded default combo bindings for Dreamcast
	Combo_AddBinding( "ltrigger+b", "buy\n" );
	Combo_AddBinding( "ltrigger+x", "autobuy\n" );
	Combo_AddBinding( "ltrigger+y", "chooseteam\n" );
	Combo_AddBinding( "ltrigger+a", "+attack2\n" );
}

/*
=============
Platform_GetMousePos

=============
*/
void GAME_EXPORT Platform_GetMousePos(int *x, int *y) 
{
	// not used
}

/*
=============
Platform_SetMousePos

============
*/
void GAME_EXPORT Platform_SetMousePos(int x, int y) 
{
	// not used
}

/*
========================
Platform_MouseMove

========================
*/
void Platform_MouseMove( float *x, float *y )
{
	maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_MOUSE);
    mouse_state_t *mouse;
    
    if (!dev || !(mouse = (mouse_state_t *)maple_dev_status(dev)))
	{
		*x = *y = 0.0;
		return;
	}
	
	*x = (float) mouse->dx;
	*y = (float) mouse->dy;
}

/*
=============
Platform_GetClipobardText

=============
*/
int Platform_GetClipboardText( char *buffer, size_t size )
{
	// stub
	return 0;
}

/*
=============
Platform_SetClipobardText

=============
*/
void Platform_SetClipboardText( const char *buffer )
{
	// stub
}

/*
=============
Platform_Vibrate

=============
*/
void Platform_Vibrate( float time, char flags )
{
	// stub
}

/*
=============
Platform_EnableTextInput

=============
*/
void Platform_EnableTextInput(qboolean enable) 
{
	text_in_en = enable;
}
    

/*
=============
Platform_JoyInit

=============
*/
int Platform_JoyInit( int numjoy )
{
	// Initialize combo system early so commands are available for config.cfg
	Combo_Init();
    return 1; // Return success
}

/*
========================
SDLash_InitCursors

========================
*/
void SDLash_InitCursors( void )
{
    // stub
}

/*
========================
SDLash_FreeCursors

========================
*/
void SDLash_FreeCursors( void )
{
    // stub
}

/*
========================
Platform_SetCursorType

========================
*/
void Platform_SetCursorType( VGUI_DefaultCursor type )
{
    // stub
}

/*
========================
Platform_GetKeyModifiers

========================
*/
key_modifier_t Platform_GetKeyModifiers( void )
{
	return KeyModifier_None;
}

/*
=============
Platform_RunEvents

Processes input events from the keyboard, mouse, and joystick.
=============
*/
void Platform_RunEvents(void)
{
    int i;
    static uint32_t last_buttons = 0;
    static uint32_t last_msbtn;
    static kbd_mods_t last_mods = {0};
	//keep our own "previous frame" state to generate clean edge-triggered events.
    static uint8_t last_kbd_down[sizeof(dc_kbd_map)] = { 0 };
    static int last_X = 0, last_Y = 0, last_X2 = 0, last_Y2 = 0;
    static qboolean combo_initialized = false;
	maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *cont;
    mouse_state_t *mouse;
    kbd_state_t	*kbd;
    
    // Combo system should already be initialized in Platform_JoyInit
    // This is just a safety check
    if ( !combo_initialized )
    {
        Combo_Init();
        combo_initialized = true;
    }

    // Check if the joystick device is available
    if (dev) 
    {
        cont = (cont_state_t *) maple_dev_status(dev);

        if (cont) 
        {
			uint32_t buttons = cont->buttons;
			
			if (cont->ltrig > 0xB0)
			{
				buttons |= DC_CONT_LT;
			}
			
			if (cont->rtrig > 0xB0)
			{
				buttons |= DC_CONT_RT;
			}
			
			// Process combo command execution (edge-triggered for press/release)
			{
				combo_button_t *combo;
				for ( combo = combo_buttons; combo; combo = combo->next )
				{
					qboolean modifier_now = (buttons & combo->modifier_mask) != 0;
					qboolean button_now = (buttons & combo->button_mask) != 0;
					qboolean modifier_before = (last_buttons & combo->modifier_mask) != 0;
					qboolean button_before = (last_buttons & combo->button_mask) != 0;
					
					// Check if combo is currently held (both buttons pressed)
					qboolean combo_held_now = modifier_now && button_now;
					qboolean combo_held_before = modifier_before && button_before;
					
					if ( combo_held_now && !combo->is_active_press )
					{
						// Combo just pressed (edge trigger)
						// Check if either modifier or button was just pressed
						qboolean modifier_just_pressed = !modifier_before && modifier_now;
						qboolean button_just_pressed = !button_before && button_now;
						
						if ( modifier_just_pressed || button_just_pressed )
						{
							// Combo detected! Execute command
							if ( combo->command )
							{
								// Check if this is a +command style (holdable command)
								if ( combo->command[0] == '+' )
								{
									// For +command, send the +command on press
									// The engine expects: +command keynum, but we'll send just +command
									// since we don't have a keynum in this context
									Cbuf_AddText( combo->command );
								}
								else
								{
									// Regular command, send once
									Cbuf_AddText( combo->command );
								}
							}
							combo->is_active_press = true;
						}
					}
					else if ( !combo_held_now && combo->is_active_press )
					{
						// Combo just released (edge trigger)
						// For +command style, send -command on release
						if ( combo->command && combo->command[0] == '+' )
						{
							// Extract command name (skip the '+') and strip trailing newline
							char release_cmd[1024];
							char cmd_name[1024];
							size_t cmd_len;
							
							Q_strncpy( cmd_name, combo->command + 1, sizeof( cmd_name ));
							// Strip trailing newline if present
							cmd_len = Q_strlen( cmd_name );
							if ( cmd_len > 0 && cmd_name[cmd_len - 1] == '\n' )
								cmd_name[cmd_len - 1] = '\0';
							
							Q_snprintf( release_cmd, sizeof( release_cmd ), "-%s\n", cmd_name );
							Cbuf_AddText( release_cmd );
						}
						combo->is_active_press = false;
					}
				}
			}
			
            // Handle button presses (skip if part of active combo)
            for (i = 0; i < DC_MAX_KEYS; i++) 
            {
				// Check if this button is part of an active combo
				// Skip both the modifier and the button if combo is currently active (held)
				qboolean skip_button = false;
				{
					combo_button_t *combo;
					for ( combo = combo_buttons; combo; combo = combo->next )
					{
						// Check if combo is currently active (both buttons pressed)
						qboolean modifier_pressed = (buttons & combo->modifier_mask) != 0;
						qboolean button_pressed = (buttons & combo->button_mask) != 0;
						
						// If both are pressed (combo is active), skip processing of both buttons
						// This prevents individual button binds from firing while combo is held
						if ( modifier_pressed && button_pressed )
						{
							// Skip if this is the button OR the modifier of an active combo
							if ( dc_keymap[i].srckey == combo->button_mask || 
							     dc_keymap[i].srckey == combo->modifier_mask )
							{
								skip_button = true;
								break;
							}
						}
					}
				}
				
				if (skip_button)
					continue;
				
                if (buttons & dc_keymap[i].srckey)
                 {
                    if (!(last_buttons & dc_keymap[i].srckey)) 
                    {
                        Key_Event(dc_keymap[i].dstkey, true); 
                    }
                } 
                else
                {
                    if (last_buttons & dc_keymap[i].srckey) 
                    {
                        Key_Event(dc_keymap[i].dstkey, false); 
                    }
                }
            }
            
            last_buttons = buttons;

			int curr_X = cont->joyx; 
			int curr_Y = cont->joyy; 
            float sensitivity = 256.0f; // HACK HACK FIX ME 

			if (last_X != curr_X) 
			{
				Joy_AxisMotionEvent(0, curr_X * sensitivity); 
			}

			if (last_Y != curr_Y) 
			{
				Joy_AxisMotionEvent(1, curr_Y * sensitivity); 
			}

			last_X = curr_X;
			last_Y = curr_Y;
			
			curr_X = cont->joy2x; 
			curr_Y = cont->joy2y; 
			
			if (last_X2 != curr_X) 
			{
				Joy_AxisMotionEvent(2, curr_X * sensitivity); 
			}

			if (last_Y2 != curr_Y) 
			{
				Joy_AxisMotionEvent(3, curr_Y * sensitivity); 
			}

			last_X2 = curr_X;
			last_Y2 = curr_Y;
		}
	}
	
	dev = maple_enum_type(0, MAPLE_FUNC_MOUSE);
	
	if (dev)
	{
		mouse = (mouse_state_t *)maple_dev_status(dev);
		
		if (mouse != NULL)
		{
			uint32_t msbtn = mouse->buttons;
			
			if (mouse->dz > 0)
			{
				msbtn |= DC_MWHELL_DOWN;
				
				if (!(last_msbtn & DC_MWHELL_DOWN))
				{
					IN_MWheelEvent(1);
				}
			}
			else if (mouse->dz < 0)
			{
				msbtn |= DC_MWHELL_UP;
				
				if (!(last_msbtn & DC_MWHELL_UP))
				{
					IN_MWheelEvent(-1);
				}
			}
			
			for (i = 0; i < DC_MAX_MSKEY; i++) 
			{
				if (msbtn & dc_mousemap[i].srckey)
				{
					if (!(last_msbtn & dc_mousemap[i].srckey)) 
					{
						IN_MouseEvent(dc_mousemap[i].dstkey, true); 
					}
				} 
                else if (last_msbtn & dc_mousemap[i].srckey) 
				{
					IN_MouseEvent(dc_mousemap[i].dstkey, false); 
				}
            }
            last_msbtn = msbtn;
		}
	}
	
	dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
	
	if(dev)
	{
		kbd = (kbd_state_t *) maple_dev_status(dev);
		
		if (kbd)
		{
			const uint8_t mods = kbd->cond.modifiers.raw;
			uint8_t shiftkeys = mods ^ last_mods.raw;
			
			if (shiftkeys & KBD_MOD_CTRL)
			{
				Key_Event(K_CTRL , ((mods & KBD_MOD_CTRL) != 0));
			}
			
			if (shiftkeys & KBD_MOD_SHIFT)
			{
				Key_Event(K_SHIFT , ((mods & KBD_MOD_SHIFT) != 0));
			}
			
			if (shiftkeys & KBD_MOD_ALT)
			{
				Key_Event(K_ALT , ((mods & KBD_MOD_ALT) != 0));
			}
			
			if (shiftkeys & (KBD_MOD_S1 | KBD_MOD_S2))
			{
				Key_Event(K_WIN , ((mods & (KBD_MOD_S1 | KBD_MOD_S2)) != 0));
			}
			
			for(i = 0; i < sizeof(dc_kbd_map); ++i) 
			{
				const key_state_t i_state = kbd->key_states[i];
				const uint8_t was_down = last_kbd_down[i] ? 1 : 0;
				const uint8_t is_down = i_state.is_down ? 1 : 0;

				// If the state changed since our last frame, emit edge event.
				if( is_down != was_down )
				{
					last_kbd_down[i] = is_down;

					if (i == KBD_KEY_PAD_NUMLOCK && is_down)
					{
						numlock_en ^= 1;
					}
					else if (i == KBD_KEY_CAPSLOCK && is_down)
					{
						capslock_en ^= 1;
					}
					
					uint8_t key = dc_kbd_map[i];

					if(key) 
					{
						Key_Event( key , is_down );
						
						if (numlock_en && i >= KBD_KEY_PAD_1 && i <= KBD_KEY_PAD_PERIOD)
						{
							key = dc_kbd_map_numlock[i-KBD_KEY_PAD_1];
						}
						
						// Text input: generate characters only on key press (no per-frame spam).
						if (text_in_en && is_down && (key >= 32 && key < 127))
						{
							if( (mods & KBD_MOD_SHIFT))
							{
								if (i >= KBD_KEY_1 && i <= KBD_KEY_SLASH )
								{
									key = dc_kbd_map_shift[i-KBD_KEY_1];
								}
								else
								{
									key = Key_ToUpper(key);
								}
							}
							else if (capslock_en)
							{
								key = Key_ToUpper(key);
							}
							
							if (key)
							{
								CL_CharEvent( key );
							}
						}
					}
				}
			}
			last_mods.raw = mods;
		}
	}
}
/*
========================
Platform_PreCreateMove

this should disable mouse look on client when m_ignore enabled
TODO: kill mouse in win32 clients too
========================
*/
void Platform_PreCreateMove(void)
{
	// not used
	/*if( m_ignore.value )
	{
		Platform_GetMousePos( NULL, NULL );
	}*/
}

#endif // XASH_INPUT == INPUT_KOS
#endif // !XASH_DEDICATED