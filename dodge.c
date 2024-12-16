
#include <stdlib.h>
#include <string.h>

#include <stdlib.h>
#include <string.h>

// include NESLIB header
#include "neslib.h"

// include CC65 NES Header (PPU)
#include <nes.h>

// link the pattern table into CHR ROM
//#link "chr_generic.s"

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x03,			// screen color

  0x11,0x30,0x27,0x0,	// background palette 0
  0x1c,0x20,0x2c,0x0,	// background palette 1
  0x00,0x10,0x20,0x0,	// background palette 2
  0x06,0x16,0x26,0x0,   // background palette 3

  0x16,0x35,0x24,0x0,	// sprite palette 0
  0x00,0x37,0x25,0x0,	// sprite palette 1
  0x0d,0x2d,0x3a,0x0,	// sprite palette 2
  0x0d,0x27,0x2a	// sprite palette 3
};

// setup PPU and tables
void setup_graphics() {
  // clear sprites
  oam_clear();
  // set palette colors
  pal_all(PALETTE);
  // enable rendering
  ppu_on_all();
  // set nmi vram update
  vrambuf_flush();
  set_vram_update(updbuf);
}

char* get_digit(byte digit) {
  switch (digit) {
    case 0:
     return "0";
    case 1:
     return "1";
    case 2:
     return "2";
    case 3:
     return "3";
    case 4:
     return "4";
    case 5:
     return "5";
    case 6:
     return "6";
    case 7:
     return "7";
    case 8:
     return "8";
    case 9:
     return "9";
    default:
      return "?";
  }
}

// draw bcd coded number (word)
void draw_number(word addr, word number) {
  vrambuf_put(addr, get_digit( (number&0xf000)>>12 ), 1);
  vrambuf_put(addr+1, get_digit( (number&0x0f00)>>8 ), 1);
  vrambuf_put(addr+2, get_digit( (number&0x00f0)>>4 ), 1);
  vrambuf_put(addr+3, get_digit( (number&0x000f)>>0 ), 1);
}

#define MOVE_SPEED 8

// bcd coded shared score
word score = 0x96;
// player variables
byte playertwo = false;
byte player1_x = 0;
sbyte player1_vx = 0;
byte player1_y = 0;
sbyte player1_vy = 0;
byte player1_lives = 0;
// invulnerability cooldown
byte player1_inv = 0;
byte player2_x = 0;
sbyte player2_vx = 0;
byte player2_y = 0;
sbyte player2_vy = 0;
byte player2_lives = 0;
// invulnerability cooldown
byte player2_inv = 0;

void main(void) {
  // next oam_id
  byte oam_id = 0;
  // controllers
  byte pad0 = 0;
  byte pad1 = 0;
  byte trg0 = 0;
  byte trg1 = 0;
  // variables
  byte state = 0;
  byte counter = 0;
  byte i;
  // set up graphics
  setup_graphics();
  // game loop
  while(1) {
    // get controller state
    trg0 = pad_trigger(0);
    trg1 = pad_trigger(1);
    pad0 = pad_state(0);
    pad1 = pad_state(1);
    // flush vram buffer
    vrambuf_flush();
    // reset oam_id
    oam_id = 0;
    // game states
    switch (state) {
      // main screen
      case 0: {
        // controller
        if (trg0&PAD_SELECT) playertwo = !playertwo;
        if (trg0&PAD_START) {
          // transition animation
          state = 1;
          counter = 0;
          // prepare for game
          player1_lives = 3;
          player2_lives = 3;
          player1_vx = 0;
          player1_vy = 0;
          player2_vx = 0;
          player2_vy = 0;
          player1_x = 128;
          player1_y = 128;
          player2_x = 128;
          player2_y = 128;
        }
        // draw screen
        vrambuf_put(NTADR_A(2,2), "THE BUBBLEDODGER GAME!", 22);
        vrambuf_put(NTADR_A(2,26), "\x10 2024 SPEARS ENTERTAINMENT", 27);
      	// player selector
        vrambuf_put(NTADR_A(5, 12), "1 PLAYER", 8);
        vrambuf_put(NTADR_A(5, 15), "2 PLAYER", 8);
        // display current players selected
        if (!playertwo) {
          vrambuf_put(NTADR_A(3, 15), " ", 1);
          vrambuf_put(NTADR_A(3, 12), "\x3e", 1);
        }
        else {
          vrambuf_put(NTADR_A(3, 12), " ", 1);
          vrambuf_put(NTADR_A(3, 15), "\x3e", 1);
        }
      } break;
      // transition
      case 1: {
        if (counter>0) vrambuf_put(NTADR_A(1,counter-1), "                              ", 30);
        if (counter<29) vrambuf_put(NTADR_A(1,counter), "________________________________", 30);
        counter++;
        if (counter == 29) state = 2;
      } break;
      // game
      case 2: {
      	// controllers
        // player 1
        if (pad0&PAD_RIGHT) player1_vx = MOVE_SPEED;
        if (pad0&PAD_LEFT) player1_vx = -MOVE_SPEED;
        if (pad0&PAD_UP) player1_vy = -MOVE_SPEED;
        if (pad0&PAD_DOWN) player1_vy = MOVE_SPEED;
        // player 2
        if (pad1&PAD_RIGHT) player2_vx = MOVE_SPEED;
        if (pad1&PAD_LEFT) player2_vx = -MOVE_SPEED;
        if (pad1&PAD_UP) player2_vy = -MOVE_SPEED;
        if (pad1&PAD_DOWN) player2_vy = MOVE_SPEED;
        
        // movement
        // player 1
        player1_x += player1_vx;
        player1_y += player1_vy;
        if (player1_vx!=0) player1_vx += (player1_vx>0) ? -1 : 1;
        if (player1_vy!=0) player1_vy += (player1_vy>0) ? -1 : 1;
        // player 2
        player2_x += player2_vx;
        player2_y += player2_vy;
        if (player2_vx!=0) player2_vx += (player2_vx>0) ? -1 : 1;
        if (player2_vy!=0) player2_vy += (player2_vy>0) ? -1 : 1;
        // render
        // players
        if (player1_lives > 0) oam_id = oam_spr(player1_x, player1_y, 0xaf, 0x00, oam_id);
        if (playertwo && player2_lives > 0) oam_id = oam_spr(player2_x, player2_y, 0xae, 0x00, oam_id);
      	// lives
        for (i=0; i<player1_lives; i++) {
          vrambuf_put(NTADR_A(2+i,2), "\x15", 1);
        }
        if (playertwo) for (i=0; i<player2_lives; i++) {
          vrambuf_put(NTADR_A(29-i,2), "\x15", 1);
        }
        // score
        draw_number(NTADR_A(14,2), score);
      } break;
      // error catch
      default: state = 0;
    }
    // hide unused sprites
    oam_hide_rest(oam_id);
    // wait for next frame
    ppu_wait_nmi();
  }
}
