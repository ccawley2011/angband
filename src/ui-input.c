/*
 * File: ui-input.c
 * Purpose: Some high-level UI functions, inkey()
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cmds.h"
#include "target.h"
#include "files.h"
#include "game-event.h"
#include "pathfind.h"
#include "randname.h"

static bool inkey_xtra;

/*
 * Flush all pending input.
 *
 * Actually, remember the flush, using the "inkey_xtra" flag, and in the
 * next call to "inkey()", perform the actual flushing, for efficiency,
 * and correctness of the "inkey()" function.
 */
void flush(void)
{
	/* Do it later */
	inkey_xtra = TRUE;
}


/*
 * Helper function called only from "inkey()"
 */
static ui_event inkey_aux(int scan_cutoff)
{
	int w = 0;	

	ui_event ke;
	
	/* Wait for a keypress */
	if (scan_cutoff == SCAN_OFF)
	{
		(void)(Term_inkey(&ke, TRUE, TRUE));
	}
	else
	{
		w = 0;

		/* Wait only as long as macro activation would wait*/
		while (Term_inkey(&ke, FALSE, TRUE) != 0)
		{
			/* Increase "wait" */
			w++;

			/* Excessive delay */
			if (w >= scan_cutoff)
			{
				ui_event empty = EVENT_EMPTY;
				return empty;
			}

			/* Delay */
			Term_xtra(TERM_XTRA_DELAY, 10);
		}
	}

	return (ke);
}



/*
 * Mega-Hack -- special "inkey_next" pointer.  XXX XXX XXX
 *
 * This special pointer allows a sequence of keys to be "inserted" into
 * the stream of keys returned by "inkey()".  This key sequence cannot be
 * bypassed by the Borg.  We use it to implement keymaps.
 */
struct keypress *inkey_next = NULL;

/**
 * See if more propmts will be skipped while in a keymap.
 */
static bool keymap_auto_more;


/*
 * Get a keypress from the user.
 *
 * This function recognizes a few "global parameters".  These are variables
 * which, if set to TRUE before calling this function, will have an effect
 * on this function, and which are always reset to FALSE by this function
 * before this function returns.  Thus they function just like normal
 * parameters, except that most calls to this function can ignore them.
 *
 * If "inkey_xtra" is TRUE, then all pending keypresses will be flushed.
 * This is set by flush(), which doesn't actually flush anything itself
 * but uses that flag to trigger delayed flushing.
 *
 * If "inkey_scan" is TRUE, then we will immediately return "zero" if no
 * keypress is available, instead of waiting for a keypress.
 *
 * If "inkey_flag" is TRUE, then we are waiting for a command in the main
 * map interface, and we shouldn't show a cursor.
 *
 * If we are waiting for a keypress, and no keypress is ready, then we will
 * refresh (once) the window which was active when this function was called.
 *
 * Note that "back-quote" is automatically converted into "escape" for
 * convenience on machines with no "escape" key.
 *
 * If "angband_term[0]" is not active, we will make it active during this
 * function, so that the various "main-xxx.c" files can assume that input
 * is only requested (via "Term_inkey()") when "angband_term[0]" is active.
 *
 * Mega-Hack -- This function is used as the entry point for clearing the
 * "signal_count" variable, and of the "character_saved" variable.
 *
 * Mega-Hack -- Note the use of "inkey_hack" to allow the "Borg" to steal
 * control of the keyboard from the user.
 */
ui_event inkey_ex(void)
{
	bool cursor_state;
	ui_event kk;
	ui_event ke = EVENT_EMPTY;

	bool done = FALSE;

	term *old = Term;

	/* Delayed flush */
	if (inkey_xtra) {
		Term_flush();
		inkey_next = NULL;
		inkey_xtra = FALSE;
	}

	/* Hack -- Use the "inkey_next" pointer */
	while (inkey_next && inkey_next->code)
	{
		/* Get next character, and advance */
		ke.key = *inkey_next++;

		/* Cancel the various "global parameters" */
		inkey_flag = FALSE;
		inkey_scan = 0;

		/* peek at the key, and see if we want to skip more prompts */
		if (ke.key.code == '(') {
			keymap_auto_more = TRUE;
			/* since we are not returning this char, make sure the next key below works well */
			if (!inkey_next || !inkey_next->code) {
				ke.type = EVT_NONE;
				break;
			}
			continue;
		} else
		if (ke.key.code == ')') {
			keymap_auto_more = FALSE;
			/* since we are not returning this char, make sure the next key below works well */
			if (!inkey_next || !inkey_next->code) {
				ke.type = EVT_NONE;
				break;
			}
			continue;
		}

		/* Accept result */
		return (ke);
	}

	/* make sure that the flag to skip more prompts is off */
	keymap_auto_more = FALSE;

	/* Forget pointer */
	inkey_next = NULL;

	/* Get the cursor state */
	(void)Term_get_cursor(&cursor_state);

	/* Show the cursor if waiting, except sometimes in "command" mode */
	if (!inkey_scan && (!inkey_flag || character_icky || (OPT(show_target) && target_sighted())))
		(void)Term_set_cursor(TRUE);


	/* Hack -- Activate main screen */
	Term_activate(term_screen);


	/* Get a key */
	while (ke.type == EVT_NONE)
	{
		/* Hack -- Handle "inkey_scan == SCAN_INSTANT */
		if (inkey_scan == SCAN_INSTANT &&
				(0 != Term_inkey(&kk, FALSE, FALSE)))
			break;


		/* Hack -- Flush output once when no key ready */
		if (!done && (0 != Term_inkey(&kk, FALSE, FALSE)))
		{
			/* Hack -- activate proper term */
			Term_activate(old);

			/* Flush output */
			Term_fresh();

			/* Hack -- activate main screen */
			Term_activate(term_screen);

			/* Mega-Hack -- reset saved flag */
			character_saved = FALSE;

			/* Mega-Hack -- reset signal counter */
			signal_count = 0;

			/* Only once */
			done = TRUE;
		}


		/* Get a key (see above) */
		ke = inkey_aux(inkey_scan);

		if(inkey_scan && ke.type == EVT_NONE)
			/* The keypress timed out. We need to stop here. */
			break;

		/* Treat back-quote as escape */
		if (ke.key.code == '`')
			ke.key.code = ESCAPE;
	}


	/* Hack -- restore the term */
	Term_activate(old);


	/* Restore the cursor */
	Term_set_cursor(cursor_state);


	/* Cancel the various "global parameters" */
	inkey_flag = FALSE;
	inkey_scan = 0;

	/* Return the keypress */
	return (ke);
}


/*
 * Get a keypress or mouse click from the user.
 */
void anykey(void)
{
	ui_event ke = EVENT_EMPTY;
  
	/* Only accept a keypress or mouse click */
	while (ke.type != EVT_MOUSE && ke.type != EVT_KBRD)
		ke = inkey_ex();
}

/*
 * Get a "keypress" from the user.
 */
struct keypress inkey(void)
{
	ui_event ke = EVENT_EMPTY;

	/* Only accept a keypress */
	/*while (ke.type != EVT_ESCAPE && ke.type != EVT_KBRD)
		ke = inkey_ex();*/

	/* Paranoia */ /*
	if (ke.type == EVT_ESCAPE) {
		ke.type = EVT_KBRD;
		ke.key.code = ESCAPE;
		ke.key.mods = 0;
  }
  */
	while (ke.type != EVT_ESCAPE && ke.type != EVT_KBRD
			&& ke.type != EVT_MOUSE  && ke.type != EVT_BUTTON)
		ke = inkey_ex();

	/* make the event a keypress */
	if (ke.type == EVT_ESCAPE) {
		ke.type = EVT_KBRD;
		ke.key.code = ESCAPE;
		ke.key.mods = 0;
	} else
	if (ke.type == EVT_MOUSE) {
		if (ke.mouse.button == 1) {
			ke.type = EVT_KBRD;
			ke.key.code = '\n';
			ke.key.mods = 0;
		} else {
			ke.type = EVT_KBRD;
			ke.key.code = ESCAPE;
			ke.key.mods = 0;
		}
	} else
	if (ke.type == EVT_BUTTON) {
		ke.type = EVT_KBRD;
	}

	return ke.key;
}

/*
 * Get a "keypress" or a "mousepress" from the user.
 * on return the event must be either a key press or a mouse press
 */
ui_event inkey_m(void)
{
	ui_event ke = EVENT_EMPTY;

	/* Only accept a keypress */
	while (ke.type != EVT_ESCAPE && ke.type != EVT_KBRD
			&& ke.type != EVT_MOUSE  && ke.type != EVT_BUTTON)
		ke = inkey_ex();
	if (ke.type == EVT_ESCAPE) {
		ke.type = EVT_KBRD;
		ke.key.code = ESCAPE;
		ke.key.mods = 0;
	} else
	if (ke.type == EVT_BUTTON) {
		ke.type = EVT_KBRD;
	}

  return ke;
}



/*
 * Flush the screen, make a noise
 */
void bell(const char *reason)
{
	/* Mega-Hack -- Flush the output */
	Term_fresh();

	/* Hack -- memorize the reason if possible */
	if (character_generated && reason)
	{
		message_add(reason, MSG_BELL);

		/* Window stuff */
		player->redraw |= (PR_MESSAGE);
		redraw_stuff(player);
	}

	/* Flush the input (later!) */
	flush();
}



/*
 * Hack -- Make a (relevant?) sound
 */
void sound(int val)
{
	/* No sound */
	if (!OPT(use_sound) || !sound_hook) return;

	sound_hook(val);
}



/*
 * Hack -- flush
 */
static void msg_flush(int x)
{
	byte a = TERM_L_BLUE;

	/* Pause for response */
	Term_putstr(x, 0, -1, a, "-more-");

	if ((!OPT(auto_more)) && !keymap_auto_more)
		anykey();

	/* Clear the line */
	Term_erase(0, 0, 255);
}


static int message_column = 0;


/*
 * Output a message to the top line of the screen.
 *
 * Break long messages into multiple pieces (40-72 chars).
 *
 * Allow multiple short messages to "share" the top line.
 *
 * Prompt the user to make sure he has a chance to read them.
 *
 * These messages are memorized for later reference (see above).
 *
 * We could do a "Term_fresh()" to provide "flicker" if needed.
 *
 * The global "msg_flag" variable can be cleared to tell us to "erase" any
 * "pending" messages still on the screen, instead of using "msg_flush()".
 * This should only be done when the user is known to have read the message.
 *
 * We must be very careful about using the "msg("%s", )" functions without
 * explicitly calling the special "msg("%s", NULL)" function, since this may
 * result in the loss of information if the screen is cleared, or if anything
 * is displayed on the top line.
 *
 * Hack -- Note that "msg("%s", NULL)" will clear the top line even if no
 * messages are pending.
 */
static void msg_print_aux(u16b type, const char *msg)
{
	int n;
	char *t;
	char buf[1024];
	byte color;
	int w, h;

	if (!Term)
		return;

	/* Obtain the size */
	(void)Term_get_size(&w, &h);

	/* Hack -- Reset */
	if (!msg_flag) message_column = 0;

	/* Message Length */
	n = (msg ? strlen(msg) : 0);

	/* Hack -- flush when requested or needed */
	if (message_column && (!msg || ((message_column + n) > (w - 8))))
	{
		/* Flush */
		msg_flush(message_column);

		/* Forget it */
		msg_flag = FALSE;

		/* Reset */
		message_column = 0;
	}


	/* No message */
	if (!msg) return;

	/* Paranoia */
	if (n > 1000) return;


	/* Memorize the message (if legal) */
	if (character_generated && !(player->is_dead))
		message_add(msg, type);

	/* Window stuff */
	player->redraw |= (PR_MESSAGE);

	/* Copy it */
	my_strcpy(buf, msg, sizeof(buf));

	/* Analyze the buffer */
	t = buf;

	/* Get the color of the message */
	color = message_type_color(type);

	/* Split message */
	while (n > w - 1)
	{
		char oops;

		int check, split;

		/* Default split */
		split = w - 8;

		/* Find the rightmost split point */
		for (check = (w / 2); check < w - 8; check++)
			if (t[check] == ' ') split = check;

		/* Save the split character */
		oops = t[split];

		/* Split the message */
		t[split] = '\0';

		/* Display part of the message */
		Term_putstr(0, 0, split, color, t);

		/* Flush it */
		msg_flush(split + 1);

		/* Restore the split character */
		t[split] = oops;

		/* Insert a space */
		t[--split] = ' ';

		/* Prepare to recurse on the rest of "buf" */
		t += split; n -= split;
	}

	/* Display the tail of the message */
	Term_putstr(message_column, 0, n, color, t);

	/* Remember the message */
	msg_flag = TRUE;

	/* Remember the position */
	message_column += n + 1;

	/* Send refresh event */
	event_signal(EVENT_MESSAGE);
}

/*
 * Display a formatted message, using "vstrnfmt()" and "msg("%s", )".
 */
void msg(const char *fmt, ...)
{
	va_list vp;

	char buf[1024];

	/* Begin the Varargs Stuff */
	va_start(vp, fmt);

	/* Format the args, save the length */
	(void)vstrnfmt(buf, sizeof(buf), fmt, vp);

	/* End the Varargs Stuff */
	va_end(vp);

	/* Display */
	msg_print_aux(MSG_GENERIC, buf);
}

void msgt(unsigned int type, const char *fmt, ...)
{
	va_list vp;
	char buf[1024];
	va_start(vp, fmt);
	vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);
	sound(type);
	msg_print_aux(type, buf);
}

/*
 * Print the queued messages.
 */
void message_flush(void)
{
	/* Hack -- Reset */
	if (!msg_flag) message_column = 0;

	/* Flush when needed */
	if (message_column)
	{
		/* Print pending messages */
		if (Term)
			msg_flush(message_column);

		/* Forget it */
		msg_flag = FALSE;

		/* Reset */
		message_column = 0;
	}
}




/*
 * Clear part of the screen
 */
void clear_from(int row)
{
	int y;

	/* Erase requested rows */
	for (y = row; y < Term->hgt; y++)
	{
		/* Erase part of the screen */
		Term_erase(0, y, 255);
	}
}

/*
 * The default "keypress handling function" for askfor_aux, this takes the
 * given keypress, input buffer, length, etc, and does the appropriate action
 * for each keypress, such as moving the cursor left or inserting a character.
 *
 * It should return TRUE when editing of the buffer is "complete" (e.g. on
 * the press of RETURN).
 */
bool askfor_aux_keypress(char *buf, size_t buflen, size_t *curs, size_t *len, struct keypress keypress, bool firsttime)
{
	switch (keypress.code)
	{
		case ESCAPE:
		{
			*curs = 0;
			return TRUE;
		}
		
		case KC_ENTER:
		{
			*curs = *len;
			return TRUE;
		}
		
		case ARROW_LEFT:
		{
			if (firsttime) *curs = 0;
			if (*curs > 0) (*curs)--;
			break;
		}
		
		case ARROW_RIGHT:
		{
			if (firsttime) *curs = *len - 1;
			if (*curs < *len) (*curs)++;
			break;
		}
		
               case KC_BACKSPACE:
               case KC_DELETE:
		{
			/* If this is the first time round, backspace means "delete all" */
			if (firsttime)
			{
				buf[0] = '\0';
				*curs = 0;
				*len = 0;

				break;
			}

			/* Refuse to backspace into oblivion */
			if((keypress.code == KC_BACKSPACE && *curs == 0) ||
			   (keypress.code == KC_DELETE && *curs >= *len))
				break;

			/* Move the string from k to nul along to the left by 1 */
			if(keypress.code == KC_BACKSPACE)
				memmove(&buf[*curs - 1], &buf[*curs],
					*len - *curs);
			else
				memmove(&buf[*curs], &buf[*curs+1],
					*len - *curs -1);

			/* Decrement */
			if(keypress.code == KC_BACKSPACE)
				(*curs)--;
			(*len)--;

			/* Terminate */
			buf[*len] = '\0';

			break;
		}
		
		default:
		{
			bool atnull = (buf[*curs] == 0);

			if (!isprint(keypress.code))
			{
				bell("Illegal edit key!");
				break;
			}

			/* Clear the buffer if this is the first time round */
			if (firsttime)
			{
				buf[0] = '\0';
				*curs = 0;
				*len = 0;
				atnull = 1;
			}

			if (atnull)
			{
				/* Make sure we have enough room for a new character */
				if ((*curs + 1) >= buflen) break;
			}
			else
			{
				/* Make sure we have enough room to add a new character */
				if ((*len + 1) >= buflen) break;

				/* Move the rest of the buffer along to make room */
				memmove(&buf[*curs+1], &buf[*curs], *len - *curs);
			}

			/* Insert the character */
			buf[(*curs)++] = (char)keypress.code;
			(*len)++;

			/* Terminate */
			buf[*len] = '\0';

			break;
		}
	}

	/* By default, we aren't done. */
	return FALSE;
}


/*
 * Get some input at the cursor location.
 *
 * The buffer is assumed to have been initialized to a default string.
 * Note that this string is often "empty" (see below).
 *
 * The default buffer is displayed in yellow until cleared, which happens
 * on the first keypress, unless that keypress is Return.
 *
 * Normal chars clear the default and append the char.
 * Backspace clears the default or deletes the final char.
 * Return accepts the current buffer contents and returns TRUE.
 * Escape clears the buffer and the window and returns FALSE.
 *
 * Note that 'len' refers to the size of the buffer.  The maximum length
 * of the input is 'len-1'.
 *
 * 'keypress_h' is a pointer to a function to handle keypresses, altering
 * the input buffer, cursor position and suchlike as required.  See
 * 'askfor_aux_keypress' (the default handler if you supply NULL for
 * 'keypress_h') for an example.
 */
bool askfor_aux(char *buf, size_t len, bool (*keypress_h)(char *, size_t, size_t *, size_t *, struct keypress, bool))
{
	int y, x;

	size_t k = 0;		/* Cursor position */
	size_t nul = 0;		/* Position of the null byte in the string */

	struct keypress ch = { 0 };

	bool done = FALSE;
	bool firsttime = TRUE;

	if (keypress_h == NULL)
	{
		keypress_h = askfor_aux_keypress;
	}

	/* Locate the cursor */
	Term_locate(&x, &y);


	/* Paranoia */
	if ((x < 0) || (x >= 80)) x = 0;


	/* Restrict the length */
	if (x + len > 80) len = 80 - x;

	/* Truncate the default entry */
	buf[len-1] = '\0';

	/* Get the position of the null byte */
	nul = strlen(buf);

	/* Display the default answer */
	Term_erase(x, y, (int)len);
	Term_putstr(x, y, -1, TERM_YELLOW, buf);

	/* Process input */
	while (!done)
	{
		/* Place cursor */
		Term_gotoxy(x + k, y);

		/* Get a key */
		ch = inkey();

		/* Let the keypress handler deal with the keypress */
		done = keypress_h(buf, len, &k, &nul, ch, firsttime);

		/* Update the entry */
		Term_erase(x, y, (int)len);
		Term_putstr(x, y, -1, TERM_WHITE, buf);

		/* Not the first time round anymore */
		firsttime = FALSE;
	}

	/* Done */
	return (ch.code != ESCAPE);
}




/*
 * A "keypress" handling function for askfor_aux, that handles the special
 * case of '*' for a new random "name" and passes any other "keypress"
 * through to the default "editing" handler.
 */
static bool get_name_keypress(char *buf, size_t buflen, size_t *curs, size_t *len, struct keypress keypress, bool firsttime)
{
	bool result;

	switch (keypress.code)
	{
		case '*':
		{
			*len = randname_make(RANDNAME_TOLKIEN, 4, 8, buf, buflen, name_sections);
			my_strcap(buf);
			*curs = 0;
			result = FALSE;
			break;
		}

		default:
		{
			result = askfor_aux_keypress(buf, buflen, curs, len, keypress, firsttime);
			break;
		}
	}

	return result;
}


/*
 * Gets a name for the character, reacting to name changes.
 *
 * If sf is TRUE, we change the savefile name depending on the character name.
 *
 * What a horrible name for a global function.  XXX XXX XXX
 */
bool get_name(char *buf, size_t buflen)
{
	bool res;

	/* Paranoia XXX XXX XXX */
	message_flush();

	/* Display prompt */
	prt("Enter a name for your character (* for a random name): ", 0, 0);

	/* Save the player name */
	my_strcpy(buf, op_ptr->full_name, buflen);

	/* Ask the user for a string */
	res = askfor_aux(buf, buflen, get_name_keypress);

	/* Clear prompt */
	prt("", 0, 0);

	/* Revert to the old name if the player doesn't pick a new one. */
	if (!res)
	{
		my_strcpy(buf, op_ptr->full_name, buflen);
	}

	return res;
}



/*
 * Prompt for a string from the user.
 *
 * The "prompt" should take the form "Prompt: ".
 *
 * See "askfor_aux" for some notes about "buf" and "len", and about
 * the return value of this function.
 */
bool get_string(const char *prompt, char *buf, size_t len)
{
	bool res;

	/* Paranoia XXX XXX XXX */
	message_flush();

	/* Display prompt */
	prt(prompt, 0, 0);

	/* Ask the user for a string */
	res = askfor_aux(buf, len, NULL);

	/* Clear prompt */
	prt("", 0, 0);

	/* Result */
	return (res);
}



/*
 * Request a "quantity" from the user
 */
s16b get_quantity(const char *prompt, int max)
{
	int amt = 1;

	/* Prompt if needed */
	if (max != 1)
	{
		char tmp[80];
		char buf[80];

		/* Build a prompt if needed */
		if (!prompt)
		{
			/* Build a prompt */
			strnfmt(tmp, sizeof(tmp), "Quantity (0-%d, *=all): ", max);

			/* Use that prompt */
			prompt = tmp;
		}

		/* Build the default */
		strnfmt(buf, sizeof(buf), "%d", amt);

		/* Ask for a quantity */
		if (!get_string(prompt, buf, 7)) return (0);

		/* Extract a number */
		amt = atoi(buf);

		/* A star or letter means "all" */
		if ((buf[0] == '*') || isalpha((unsigned char)buf[0])) amt = max;
	}

	/* Enforce the maximum */
	if (amt > max) amt = max;

	/* Enforce the minimum */
	if (amt < 0) amt = 0;

	/* Return the result */
	return (amt);
}


/*
 * Verify something with the user
 *
 * The "prompt" should take the form "Query? "
 *
 * Note that "[y/n]" is appended to the prompt.
 */
bool get_check(const char *prompt)
{
	//struct keypress ke;
	ui_event ke;

	char buf[80];

	/* Paranoia XXX XXX XXX */
	message_flush();

	/* Hack -- Build a "useful" prompt */
	strnfmt(buf, 78, "%.70s[y/n] ", prompt);

	/* Prompt for it */
	prt(buf, 0, 0);
	ke = inkey_m();

	/* Erase the prompt */
	prt("", 0, 0);

	/* Normal negation */
	if (ke.type == EVT_MOUSE) {
		if ((ke.mouse.button != 1) && (ke.mouse.y != 0)) return (FALSE);
	} else
	if ((ke.key.code != 'Y') && (ke.key.code != 'y')) return (FALSE);

	/* Success */
	return (TRUE);
}

/* TODO: refactor get_check() in terms of get_char() */
/*
 * Ask the user to respond with a character. Options is a constant string,
 * e.g. "yns"; len is the length of the constant string, and fallback should
 * be the default answer if the user hits escape or an invalid key.
 *
 * Example: get_char("Study? ", "yns", 3, 'n')
 *     This prompts "Study? [yns]" and defaults to 'n'.
 *
 */
char get_char(const char *prompt, const char *options, size_t len, char fallback)
{
	struct keypress key;
	char buf[80];

	/* Paranoia XXX XXX XXX */
	message_flush();

	/* Hack -- Build a "useful" prompt */
	strnfmt(buf, 78, "%.70s[%s] ", prompt, options);

	/* Prompt for it */
	prt(buf, 0, 0);

	/* Get an acceptable answer */
	key = inkey();

	/* Lowercase answer if necessary */
	if (key.code >= 'A' && key.code <= 'Z') key.code += 32;

	/* See if key is in our options string */
	if (!strchr(options, (char)key.code))
		key.code = fallback;

	/* Erase the prompt */
	prt("", 0, 0);

	/* Success */
	return key.code;
}


/**
 * Text-native way of getting a filename.
 */
static bool get_file_text(const char *suggested_name, char *path, size_t len)
{
	char buf[160];

	/* Get filename */
	my_strcpy(buf, suggested_name, sizeof buf);
	if (!get_string("File name: ", buf, sizeof buf)) return FALSE;

	/* Make sure it's actually a filename */
	if (buf[0] == '\0' || buf[0] == ' ') return FALSE;

	/* Build the path */
	path_build(path, len, ANGBAND_DIR_USER, buf);

	/* Check if it already exists */
	if (file_exists(path) && !get_check("Replace existing file? "))
		return FALSE;

	/* Tell the user where it's saved to. */
	msg("Saving as %s.", path);

	return TRUE;
}




/**
 * Get a pathname to save a file to, given the suggested name.  Returns the
 * result in "path".
 */
bool (*get_file)(const char *suggested_name, char *path, size_t len) = get_file_text;




/*
 * Prompts for a keypress
 *
 * The "prompt" should take the form "Command: "
 *
 * Returns TRUE unless the character is "Escape"
 */
bool get_com(const char *prompt, struct keypress *command)
{
	ui_event ke;
	bool result;

	result = get_com_ex(prompt, &ke);
	*command = ke.key;

	return result;
}


bool get_com_ex(const char *prompt, ui_event *command)
{
	ui_event ke;

	/* Paranoia XXX XXX XXX */
	message_flush();

	/* Display a prompt */
	prt(prompt, 0, 0);

	/* Get a key */
	ke = inkey_m();

	/* Clear the prompt */
	prt("", 0, 0);

	/* Save the command */
	*command = ke;

	/* Done */
	if ((ke.type == EVT_KBRD && ke.key.code != ESCAPE) || (ke.type == EVT_MOUSE))
	  return TRUE;
	return FALSE;
}


/*
 * Pause for user response
 *
 * This function is stupid.  XXX XXX XXX
 */
void pause_line(struct term *term)
{
	prt("", term->hgt - 1, 0);
	put_str("[Press any key to continue]", term->hgt - 1, 23);
	(void)anykey();
	prt("", term->hgt - 1, 0);
}

static int dir_transitions[10][10] =
{
	/* 0-> */ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
	/* 1-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 2-> */ { 0, 0, 2, 0, 1, 0, 3, 0, 5, 0 },
	/* 3-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 4-> */ { 0, 0, 1, 0, 4, 0, 5, 0, 7, 0 },
	/* 5-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 6-> */ { 0, 0, 3, 0, 5, 0, 6, 0, 9, 0 },
	/* 7-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 8-> */ { 0, 0, 5, 0, 7, 0, 9, 0, 8, 0 },
	/* 9-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

/*
 * Request a "movement" direction (1,2,3,4,6,7,8,9) from the user.
 *
 * Return TRUE if a direction was chosen, otherwise return FALSE.
 *
 * This function should be used for all "repeatable" commands, such as
 * run, walk, open, close, bash, disarm, spike, tunnel, etc, as well
 * as all commands which must reference a grid adjacent to the player,
 * and which may not reference the grid under the player.
 *
 * Directions "5" and "0" are illegal and will not be accepted.
 *
 * This function tracks and uses the "global direction", and uses
 * that as the "desired direction", if it is set.
 */
bool get_rep_dir(int *dp, bool allow_5)
{
	int dir = 0;

	ui_event ke;

	/* Initialize */
	(*dp) = 0;

	/* Get a direction */
	while (!dir)
	{
		/* Paranoia XXX XXX XXX */
		message_flush();

		/* Get first keypress - the first test is to avoid displaying the
		 prompt for direction if there's already a keypress queued up
		 and waiting - this just avoids a flickering prompt if there is
		 a "lazy" movement delay. */
		inkey_scan = SCAN_INSTANT;
		ke = inkey_ex();
		inkey_scan = SCAN_OFF;

		if (ke.type == EVT_NONE ||
			(ke.type == EVT_KBRD && target_dir(ke.key) == 0))
		{
			prt("Direction or <click> (Escape to cancel)? ", 0, 0);
			ke = inkey_ex();
		}

		/* Check mouse coordinates */
		if (ke.type == EVT_MOUSE) {
			if (ke.mouse.button == 1) {
				int y = KEY_GRID_Y(ke);
				int x = KEY_GRID_X(ke);
				struct loc from = loc(player->px, player->py);
				struct loc to = loc(x, y);

				dir = pathfind_direction_to(from, to);
			} else
				if (ke.mouse.button == 2) {
					/* Clear the prompt */
					prt("", 0, 0);

					return (FALSE);
				}
		}

		/* Get other keypresses until a direction is chosen. */
		else if (ke.type == EVT_KBRD)
		{
			int keypresses_handled = 0;

			while (ke.type == EVT_KBRD && ke.key.code != 0)
			{
				int this_dir;

				if (ke.key.code == ESCAPE)
				{
					/* Clear the prompt */
					prt("", 0, 0);

					return (FALSE);
				}

				/* XXX Ideally show and move the cursor here to indicate
				 the currently "Pending" direction. XXX */
				this_dir = target_dir_allow(ke.key, allow_5);

				if (this_dir)
					dir = dir_transitions[dir][this_dir];

				if (lazymove_delay == 0 || ++keypresses_handled > 1)
					break;

				inkey_scan = lazymove_delay;
				ke = inkey_ex();
			}

			/* 5 is equivalent to "escape" */
			if (dir == 5 && !allow_5)
			{
				/* Clear the prompt */
				prt("", 0, 0);

				return (FALSE);
			}
		}

		/* Oops */
		if (!dir) bell("Illegal repeatable direction!");
	}

	/* Clear the prompt */
	prt("", 0, 0);

	/* Save direction */
	(*dp) = dir;

	/* Success */
	return (TRUE);
}

/*
 * Get an "aiming direction" (1,2,3,4,6,7,8,9 or 5) from the user.
 *
 * Return TRUE if a direction was chosen, otherwise return FALSE.
 *
 * The direction "5" is special, and means "use current target".
 *
 * This function tracks and uses the "global direction", and uses
 * that as the "desired direction", if it is set.
 *
 * Note that "Force Target", if set, will pre-empt user interaction,
 * if there is a usable target already set.
 */
bool get_aim_dir(int *dp)
{
	/* Global direction */
	int dir = 0;

	ui_event ke;

	const char *p;

	/* Initialize */
	(*dp) = 0;

	/* Hack -- auto-target if requested */
	if (OPT(use_old_target) && target_okay() && !dir) dir = 5;

	/* Ask until satisfied */
	while (!dir)
	{
		/* Choose a prompt */
		if (!target_okay())
			p = "Direction ('*' or <click> to target, \"'\" for closest, Escape to cancel)? ";
		else
			p = "Direction ('5' for target, '*' or <click> to re-target, Escape to cancel)? ";

		/* Get a command (or Cancel) */
		if (!get_com_ex(p, &ke)) break;

		if (ke.type == EVT_MOUSE) {
			if (ke.mouse.button == 1) {
				if (target_set_interactive(TARGET_KILL, KEY_GRID_X(ke), KEY_GRID_Y(ke)))
					dir = 5;
			} else
				if (ke.mouse.button == 2) {
					break;
				}
		} else
			if (ke.type == EVT_KBRD) {
				if (ke.key.code == '*')
				{
					/* Set new target, use target if legal */
					if (target_set_interactive(TARGET_KILL, -1, -1))
						dir = 5;
				}
				else if (ke.key.code == '\'')
				{
					/* Set to closest target */
					if (target_set_closest(TARGET_KILL))
						dir = 5;
				}
				else if (ke.key.code == 't' || ke.key.code == '5' ||
						 ke.key.code == '0' || ke.key.code == '.')
				{
					if (target_okay())
						dir = 5;
				}
				else
				{
					/* Possible direction */
					int keypresses_handled = 0;

					while (ke.key.code != 0)
					{
						int this_dir;

						/* XXX Ideally show and move the cursor here to indicate
						 the currently "Pending" direction. XXX */
						this_dir = target_dir(ke.key);

						if (this_dir)
							dir = dir_transitions[dir][this_dir];
						else
							break;

						if (lazymove_delay == 0 || ++keypresses_handled > 1)
							break;

						/* See if there's a second keypress within the defined
						 period of time. */
						inkey_scan = lazymove_delay;
						ke = inkey_ex();
					}
				}
			}

		/* Error */
		if (!dir) bell("Illegal aim direction!");
	}

	/* No direction */
	if (!dir) return (FALSE);
	
	/* Save direction */
	(*dp) = dir;
	
	/* A "valid" direction was entered */
	return (TRUE);
}
