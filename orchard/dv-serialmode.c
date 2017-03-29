#include "ch.h"
#include "hal.h"

#include "orchard.h"
#include "orchard-shell.h"
#include "orchard-events.h"
#include "oled.h"
#include "gfx.h"

#include "dv-serialmode.h"

#define SENTINAL_LEN 5
static uint8_t lockon_pos = 0;
static uint8_t lockoff_pos = 0;
uint8_t locker_mode = 0;
static uint8_t sentinal_pos = 0;
static uint8_t firmware_pos = 0;
static char lockon[] = "#LCK\n";
static char lockoff[] = "#RUN\n";
static char sentinal[] = "#SYN\n";
static char firmware[] = "#VER\n";

#define MAX_COLS 18
#define MAX_ROWS 5
// the extra max_rows is to accommodate newlines on each line which don't print but take space
#define TEXT_LEN ((MAX_COLS * MAX_ROWS) + SENTINAL_LEN + MAX_ROWS + 1) 
static char text_buffer[TEXT_LEN];
static int16_t write_ptr = 0; 

extern uint32_t serial_needs_update;
systime_t last_update_time = 0;

uint8_t isprint_local(char c) {
  return ((c >= ' ' && c <= '~') ? 1 : 0);
}

int find_printable_window(void) {
  int chars_searched = 0;
  int num_lines = 0;
  int cur_ptr = write_ptr;
  int chars_since_newline = 0;
  int chars_since_hard_newline = 0;

  // starting from the last written character, search backwards...
  while( chars_searched < TEXT_LEN ) {
    if( text_buffer[cur_ptr] == '\n' ) {
      //      chprintf(stream, "n");
      num_lines++;
      // MAX_ROWS+1 if you want to show the current incoming line too
      // MAX_ROWS+2 if you only want to show fully formed lines
      if( num_lines == (MAX_ROWS) ) { // forward back over the final newline we found
	cur_ptr++; cur_ptr %= TEXT_LEN;
      	break;
      }
      chars_since_newline = 0;
      chars_since_hard_newline = 0;
      chars_searched++;

      cur_ptr--;
      if( cur_ptr < 0 )
	cur_ptr = TEXT_LEN - 1;
      continue;
    }

    if( chars_since_newline >= (MAX_COLS) ) {
      //      chprintf(stream, "c");
      num_lines++; // don't count lines that are exactly the width of the screen + a newline
      // MAX_ROWS+1 if you want to show the current incoming line too
      // MAX_ROWS+2 if you only want to show fully formed lines
      if( (
	   (num_lines == (MAX_ROWS-1)) &&
	   (chars_since_hard_newline <= (MAX_COLS * (MAX_ROWS-1) -1 ) )
	   ) ||
	  (num_lines == MAX_ROWS) ) {
	// cur_ptr++; cur_ptr %= TEXT_LEN; // comment out because we didn't end on a newline, so don't eat
	break;
      }
      chars_since_newline = 0;
    }
    cur_ptr--;
    if( cur_ptr < 0 )
      cur_ptr = TEXT_LEN - 1;
    chars_searched++;
    chars_since_newline++;
    chars_since_hard_newline++;
  }

  // this happens when there are no new lines at all, and the buffer just wraps around
  if( cur_ptr == write_ptr ) {
    //    chprintf(stream, "x");
    cur_ptr = write_ptr + 1;
    cur_ptr %= TEXT_LEN;
  }
  //  chprintf(stream, "\n\r");
  
  return cur_ptr;
}

void updateSerialScreen(void) {
  coord_t width;
  coord_t font_height;
  font_t font;
  int cur_char;
  int i;
  int chars_processed;
  int render_line = 0;
  char str_to_render[MAX_COLS + 1];

  orchardGfxStart();
  font = gdispOpenFont("fixed_7x14");
  width = gdispGetWidth();
  //  height = gdispGetHeight();

  font_height = gdispGetFontMetric(font, fontHeight);

  gdispClear(Black);
  chars_processed = 0;
  cur_char = find_printable_window();
  while( (chars_processed < TEXT_LEN) && (render_line <= MAX_ROWS) ) {
    // copy text starting at last newline into local render buffer
    i = 0;
    while( i < MAX_COLS ) {
      if( cur_char == write_ptr )
	break;

      if( text_buffer[cur_char] == '\n'  ) {
	str_to_render[i] = '\0';  // don't use a space for newline so we can go to edge of screen
	cur_char++; i++; chars_processed++;
	cur_char %= TEXT_LEN;
	break;
      }
      if( isprint_local(text_buffer[cur_char]) )
	str_to_render[i] = text_buffer[cur_char];
      else
	str_to_render[i] = '.';
      cur_char++; i++; chars_processed++;
      cur_char %= TEXT_LEN;
    }
    str_to_render[i] = '\0';
    //    chprintf(stream, "%s\n\r", str_to_render);
    gdispDrawStringBox(0, render_line * font_height, width, gdispGetFontMetric(font, fontHeight),
		       (const char *) str_to_render, font, White, justifyLeft);

#if 0
    if( (i == MAX_COLS) && (text_buffer[cur_char] == '\n') ) {
      // in this case, we printed a full line, with a newline just at the end. eat the newline, don't print it.
      cur_char++; chars_processed++;
      cur_char %= TEXT_LEN; // i gets reset on the next iter
    }
#endif
    render_line++;

    if( cur_char == write_ptr )
      break;
  }
  gdispFlush();
  gdispCloseFont(font);
  orchardGfxEnd();
}

void dvInit(void) {
  int i;
  last_update_time = chVTGetSystemTime();
  for( i = 0; i < TEXT_LEN; i++ ) {
    text_buffer[i] = '\n'; // init with CR's
  }
  text_buffer[TEXT_LEN-1] = '\n'; // simulate final newline
  write_ptr = 0;
}

uint32_t dv_search_sentinal(char c) {
  if( c == sentinal[sentinal_pos] && (sentinal_pos == (SENTINAL_LEN - 1)) ) {
    sentinal_pos = 0;
    return 1;
  }
  if( c == sentinal[sentinal_pos] ) {
    sentinal_pos++;
    return 0;
  }
  
  sentinal_pos = 0;
  return 0;
}

uint32_t dv_search_lockoff(char c) {
  if( c == lockoff[lockoff_pos] && (lockoff_pos == (SENTINAL_LEN - 1)) ) {
    lockoff_pos = 0;
    return 1;
  }
  if( c == lockoff[lockoff_pos] ) {
    lockoff_pos++;
    return 0;
  }
  
  lockoff_pos = 0;
  return 0;
}

uint32_t dv_search_lockon(char c) {
  if( c == lockon[lockon_pos] && (lockon_pos == (SENTINAL_LEN - 1)) ) {
    lockon_pos = 0;
    return 1;
  }
  if( c == lockon[lockon_pos] ) {
    lockon_pos++;
    return 0;
  }
  
  lockon_pos = 0;
  return 0;
}

uint32_t dv_search_firmware(char c) {
  if( c == firmware[firmware_pos] && (firmware_pos == (SENTINAL_LEN - 1)) ) {
    firmware_pos = 0;
    return 1;
  }
  if( c == firmware[firmware_pos] ) {
    firmware_pos++;
    return 0;
  }
  
  firmware_pos = 0;
  return 0;
}

void dvDoSerial(void) {
  char c;
  int8_t prev_ptr;
  char vers[11];

  while(TRUE) {
    prev_ptr = write_ptr - 1;
    if( prev_ptr == -1 )
      prev_ptr = TEXT_LEN - 1;
    
    if(chSequentialStreamRead((BaseSequentialStream *) stream, (uint8_t *)&c, 1) == 0)
      return;  // we keep on running until the buffer is empty

    if( c == '\r' )
      c = '\n';

    if(dv_search_sentinal(c)) {
      // render the buffer to screen, clear the buffer, and quit if the sentinal sequence is found      
      // eat the #SYN sentinel
      write_ptr -= (SENTINAL_LEN - 1);
      if( write_ptr < 0 )
	write_ptr += TEXT_LEN;
      // display the screen contents
      last_update_time = chVTGetSystemTime();
      updateSerialScreen();
      
      dvInit();
      return; 
    }
    if(dv_search_lockon(c)) {
      locker_mode = 1;
      dvInit();
      return;
    }
    if(dv_search_lockoff(c)) {
      locker_mode = 0;
      dvInit();
      return;
    }
    if(dv_search_firmware(c)) {
      chsnprintf(vers, sizeof(vers), "%s", gitversion );
      oledPauseBanner(vers);
      chThdSleepMilliseconds(4500);
      dvInit();
      last_update_time = chVTGetSystemTime();
      updateSerialScreen();
      return;
    }
    
    // if CRLF, eat multiple CRLF
    if( c == '\n' ) {
      if( text_buffer[prev_ptr] == '\n' ) {
	return;
      }
      if( !locker_mode ) {
	last_update_time = chVTGetSystemTime();
	serial_needs_update = 1; // update on CR
      }
    }
    
    text_buffer[write_ptr] = c;

    write_ptr++;
    write_ptr %= TEXT_LEN;
    text_buffer[write_ptr] = ' '; // rule: current spot we're pointing to for write cannot be a newline
  }
}
