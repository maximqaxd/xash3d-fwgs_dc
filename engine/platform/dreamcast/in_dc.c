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
    static int last_X = 0, last_Y = 0, last_X2 = 0, last_Y2 = 0;
	maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *cont;
    mouse_state_t *mouse;
    kbd_state_t	*kbd;

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
			
            // Handle button presses
            for (i = 0; i < DC_MAX_KEYS; i++) 
            {
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
			uint8_t shiftkeys = kbd->last_modifiers.raw ^ last_mods.raw;
			
			if (shiftkeys & KBD_MOD_CTRL)
			{
				Key_Event(K_CTRL , ((kbd->last_modifiers.raw & KBD_MOD_CTRL) != 0));
			}
			
			if (shiftkeys & KBD_MOD_SHIFT)
			{
				Key_Event(K_SHIFT , ((kbd->last_modifiers.raw & KBD_MOD_SHIFT) != 0));
			}
			
			if (shiftkeys & KBD_MOD_ALT)
			{
				Key_Event(K_ALT , ((kbd->last_modifiers.raw & KBD_MOD_ALT) != 0));
			}
			
			if (shiftkeys & (KBD_MOD_S1 | KBD_MOD_S2))
			{
				Key_Event(K_WIN , ((kbd->last_modifiers.raw & (KBD_MOD_S1 | KBD_MOD_S2)) != 0));
			}
			
			for(i = 0; i < sizeof(dc_kbd_map); ++i) 
			{
				/* Get the state of key i */
				key_state_t i_state = kbd->key_states[i];

				/* If the state of i changed */
				if(i_state.is_down ^ i_state.was_down)
				{
					if (i == KBD_KEY_PAD_NUMLOCK && i_state.is_down)
					{
						numlock_en ^= 1;
					}
					else if (i == KBD_KEY_CAPSLOCK && i_state.is_down)
					{
						capslock_en ^= 1;
					}
					
					uint8_t key = dc_kbd_map[i];

					if(key) 
					{
						Key_Event( key , i_state.is_down );
						
						if (numlock_en && i >= KBD_KEY_PAD_1 && i <= KBD_KEY_PAD_PERIOD)
						{
							key = dc_kbd_map_numlock[i-KBD_KEY_PAD_1];
						}
						
						if (text_in_en && i_state.is_down && (key >= 32 && key < 127))
						{
							if( (kbd->last_modifiers.raw & KBD_MOD_SHIFT))
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
			last_mods = kbd->last_modifiers;
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