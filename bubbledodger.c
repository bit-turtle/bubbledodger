#include <stdlib.h>
#include <string.h>

#include <stdlib.h>
#include <string.h>

// include NESLIB header
#include "neslib.h"

// include CC65 NES Header (PPU)
#include <nes.h>

// link the pattern table into CHR ROM
//#link "chr_bubbledodger.s"

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// APU sound library
#include "apu.h"
//#link "apu.c"

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x03,			// screen color

  0x11,0x30,0x27,0x00,	// background palette 0
  0x1C,0x20,0x2C,0x00,	// background palette 1
  0x00,0x10,0x20,0x00,	// background palette 2
  0x06,0x16,0x26,0x00,   // background palette 3

  0x16,0x35,0x24,0x00,	// sprite palette 0
  0x11,0x30,0x27,0x00,	// sprite palette 1
  0x00,0x2D,0x3D,0x00,	// sprite palette 2
  0x0D,0x27,0x2A	// sprite palette 3
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

byte rand_onscreen() {  
  return rand()%(0xff-0x30)+0x10;
}

bool hitbox(
  byte x1, byte y1,
  byte w1, byte h1,
  byte x2, byte y2, 
  byte w2, byte h2
) {
  if (
    x1 < x2 + w2 &&
    x1 + w1 > x2 &&
    y1 < y2 + h2 &&
    y1 + h1 > y2
  ) return true;
  else return false;
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

// max speed
// frames till speed slows to 0 (1 is minimum value)
#define MOVE_SPEED 1
// speed multiples by this
#define MOVE_SCALE 6
// delay between hearts lost
#define DAMAGE_DELAY 120
// time until gameover screen
#define GAMEOVER_TIME 120

// game variables
byte playertwo = false;
// game timer (incremented every 256 frames)
byte time = 0;
// bcd coded shared score
word score = 0x0000;
// player variables
// frames per stage of gameover animation
#define GAMEOVER_ANIM 8
// blink rate of damage animation
#define DAMAGE_BLINK 3
byte player1_x = 0;
sbyte player1_vx = 0;
byte player1_y = 0;
sbyte player1_vy = 0;
byte player1_lives = 0;
// invulnerability cooldown / gameover animation counter
byte player1_timer = 0;
// player 2 variables
byte player2_x = 0;
sbyte player2_vx = 0;
byte player2_y = 0;
sbyte player2_vy = 0;
byte player2_lives = 0;
// invulnerability cooldown / gameover animation counter
byte player2_timer = 0;

// coins
#define MAX_COINS 4
byte coins = 0;
byte coin_x[MAX_COINS];
byte coin_y[MAX_COINS];

// spikes
#define MAX_SPIKES 32
// next spike
byte next_spike_x = 0;
byte next_spike_y = 0;
// time till next spike is created
#define NEXT_SPIKE_TIME 240
// how much less time every 256 frames
#define NEXT_SPIKE_SCALE 1
// speed
// inital speed
#define SPIKE_SPEED 1
// speed increase by 1 by (256 frames / scale)
#define SPIKE_SCALE 2
byte next_spike_time = 0;
// current spikes
byte spikes = 0;
byte spike_x[MAX_SPIKES];
byte spike_y[MAX_SPIKES];
sbyte spike_vx[MAX_SPIKES];
sbyte spike_vy[MAX_SPIKES];

sbyte rand_speed() {
  return rand()%(SPIKE_SPEED+time/SPIKE_SCALE)-(SPIKE_SPEED+time/SPIKE_SCALE)/2;
};

// particles
#define MAX_PARTICLES 16
byte particles = 0;
byte particle_x[MAX_PARTICLES];
byte particle_y[MAX_PARTICLES];
// particle charater rom index
byte particle_c[MAX_PARTICLES];
// particle sprite pallete
byte particle_p[MAX_PARTICLES];
// particle deletion timer
byte particle_t[MAX_PARTICLES];

void new_particle(byte x, byte y, byte c, byte p, byte t) {
  // index is next particle unless full, then index is 0
  byte index = (particles < MAX_PARTICLES) ? particles : 0;
  particle_x[particles] = x;
  particle_y[particles] = y;
  particle_c[particles] = c;
  particle_p[particles] = p;
  particle_t[particles] = t;
  // increment next particle index if not full
  if (particles < MAX_PARTICLES) particles += 1;
}

// audio
void setup_audio() {
  APU_ENABLE(0x0f);
}
void powerup_sound() {
  APU_PULSE_DECAY(0, 687, 128, 6, 4);
  APU_PULSE_SWEEP(0, 4, 2, 1);
}

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
  byte i, c;
  byte cache;
  // set up graphics
  setup_graphics();
  // set up audio
  setup_audio();
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
          coins = 0;
          spikes = 0;
          particles = 0;
          time = 0;
          score = 0;
          // prepare first spike
          next_spike_x = rand_onscreen();
          next_spike_y = rand_onscreen();
          next_spike_time = NEXT_SPIKE_TIME;
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
        if (counter>0) vrambuf_put(NTADR_A(2,counter-1), "                            ", 28);
        if (counter<29) vrambuf_put(NTADR_A(2,counter), "____________________________", 28);
        counter++;
        if (counter == 30) {
          state = 2;
          counter = 0;
          // show first spike warning
          new_particle(next_spike_x, next_spike_y, 0xa7, 0x00, next_spike_time);
        }
      } break;
      // game
      case 2: {
      	// controllers
        // player 1
        if (pad0&PAD_RIGHT)
          player1_vx = MOVE_SPEED;
        if (pad0&PAD_LEFT)
          player1_vx = -MOVE_SPEED;
        if (pad0&PAD_UP)
          player1_vy = -MOVE_SPEED;
        if (pad0&PAD_DOWN)
          player1_vy = MOVE_SPEED;
        // player 2
        if (pad1&PAD_RIGHT)
          player2_vx = MOVE_SPEED;
        if (pad1&PAD_LEFT)
          player2_vx = -MOVE_SPEED;
        if (pad1&PAD_UP)
          player2_vy = -MOVE_SPEED;
        if (pad1&PAD_DOWN)
          player2_vy = MOVE_SPEED;
        
        // movement
        // player 1
        if (player1_lives > 0) {
          player1_x += player1_vx * MOVE_SCALE;
          player1_y += player1_vy * MOVE_SCALE;
        }
        if (player1_vx!=0)
          player1_vx += (player1_vx>0) ? -1 : 1;
        if (player1_vy!=0)
          player1_vy += (player1_vy>0) ? -1 : 1;
        // player 2
        if (player2_lives > 0) {
          player2_x += player2_vx * MOVE_SCALE;
          player2_y += player2_vy * MOVE_SCALE;
        }
        if (player2_vx!=0)
          player2_vx += (player2_vx>0) ? -1 : 1;
        if (player2_vy!=0)
          player2_vy += (player2_vy>0) ? -1 : 1;
        
        // process
        // counter
        counter += 1;
        // time score if still alive
        if (
          counter == 0xff &&
          (
            player1_lives > 0 ||
            playertwo && player2_lives > 0
          )
        ) time += 1;
        // player timers
        if (player1_timer > 0) player1_timer -= 1;
        if (player2_timer > 0) player2_timer -= 1;
        // time score
        if (counter%64 == 0) score = bcd_add(score, 0x1);
        // collect coins
        for (i=0; i<coins; i++) {
          if (
            player1_lives > 0 && hitbox(
              player1_x, player1_y, 8, 8,
              coin_x[i], coin_y[i], 8, 8
            ) ||
            playertwo && player2_lives > 0 && hitbox(
              player2_x, player2_y, 8, 8,
              coin_x[i], coin_y[i], 8, 8)
          ) {
            // sound effect
            powerup_sound();
            // increment score (with bcd)
            score = bcd_add(score, 0x15);
            new_particle(coin_x[i], coin_y[i], 0xa8, 0x00, 16);
            // remove coin
            for (c=i+1; c<coins; c++) {
              coin_x[c-1] = coin_x[c];
              coin_y[c-1] = coin_y[c];
            }
            coins -= 1;
            i -= 1;
          }
        }
        // place coins
        if (coins < MAX_COINS) {
          coin_x[coins] = rand_onscreen();
          coin_y[coins] = rand_onscreen();
          coins += 1;
        }
        // spikes
        for (i=0; i<spikes; i++) {
          // update spike positions
          spike_x[i] += spike_vx[i];
          spike_y[i] += spike_vy[i];
          // spike damage
          // player 1
          if (
            player1_lives > 0 && player1_timer == 0 &&
            hitbox(
              player1_x, player1_y, 8, 8,
              spike_x[i], spike_y[i], 8, 8
            )
          ) {
            player1_lives -= 1;
            player1_timer = (player1_lives > 0) ? DAMAGE_DELAY : GAMEOVER_ANIM*4;
            counter = 0;
          }
          // player 2
          if (
            playertwo && player2_lives > 0 && player2_timer == 0 &&
            hitbox(
              player2_x, player2_y, 8, 8,
              spike_x[i], spike_y[i], 8, 8
            )
          ) {
            player2_lives -= 1;
            player2_timer = (player2_lives > 0) ? DAMAGE_DELAY : GAMEOVER_ANIM*4;
            counter = 0;
          }
        }
        // create spikes
        if (next_spike_time == 0 && spikes < MAX_SPIKES) {
          // place spike
          spike_x[spikes] = next_spike_x;
          spike_y[spikes] = next_spike_y;
          spike_vx[spikes] = rand_speed();
          spike_vy[spikes] = rand_speed();
          spikes += 1;
          // prepare next spike
          next_spike_x = rand_onscreen();
          next_spike_y = rand_onscreen();
          new_particle(next_spike_x, next_spike_y, 0xa7, 0x00, NEXT_SPIKE_TIME-time*NEXT_SPIKE_SCALE);
          next_spike_time = NEXT_SPIKE_TIME-time*NEXT_SPIKE_SCALE;
        }
        else
          next_spike_time -= 1;
        
        // render
        // sprites
        // coins
        for (i=0; i<coins; i++)
          oam_id = oam_spr(coin_x[i], coin_y[i], 0xad, 0x01, oam_id);
        // spikes 4 frames per texture animation
        cache = 0xa9+counter/4%4;
        for (i=0; i<spikes; i++)
          oam_id = oam_spr(spike_x[i], spike_y[i], cache, 0x02, oam_id);
        // players
        if (playertwo) {
          if (player1_lives > 0 && player1_timer/DAMAGE_BLINK%2 == 0)
            oam_id = oam_spr(player1_x, player1_y, 0xaf, 0x00, oam_id);
          if (player2_lives > 0 && player2_timer/DAMAGE_BLINK%2 == 0)
            oam_id = oam_spr(player2_x, player2_y, 0xae, 0x00, oam_id);
        }
        else if (player1_lives > 0 && player1_timer/DAMAGE_BLINK%2 == 0)
          oam_id = oam_spr(player1_x, player1_y, 0xb0, 0x00, oam_id);
        // gameover players
        if (player1_lives == 0 && player1_timer > 0)
          oam_id = oam_spr(player1_x, player1_y, 0xb1+player1_timer/GAMEOVER_ANIM, 0x00, oam_id);
        if (playertwo && player2_lives == 0 && player2_timer > 0)
          oam_id = oam_spr(player2_x, player2_y, 0xb1+player2_timer/GAMEOVER_ANIM, 0x00, oam_id);
        // tiles
        // lives
        // player 1
        vrambuf_put(NTADR_A(2,2), "   ", 3);
        for (i=0; i<player1_lives; i++)
          vrambuf_put(NTADR_A(2+i,2), "\x15", 1);
        if (player1_lives == 0)
          vrambuf_put(NTADR_A(2,2), "GAMEOVER", 8);
        // player 2
        vrambuf_put(NTADR_A(26, 2), "   ", 3);
        if (playertwo) for (i=0; i<player2_lives; i++)
          vrambuf_put(NTADR_A(29-i,2), "\x15", 1);
        if (playertwo && player2_lives == 0)
          vrambuf_put(NTADR_A(22,2), "GAMEOVER", 8);
        // score
        draw_number(NTADR_A(14,2), score);
        
        // gameover
        if ( counter == GAMEOVER_TIME &&
            (
              !playertwo && player1_lives == 0 ||
              playertwo && player1_lives == 0 && player2_lives == 0
            )
          ) {
          // go to gameover screen
          state = 3;
          // clear vram
          vrambuf_put(NTADR_A(2,2), "                             ", 29);
          // remove particles
          particles = 0;
        }
      } break;
      // gameover screen
      case 3: {
        vrambuf_put(NTADR_A(10,10), "GAME OVER", 9);
        vrambuf_put(NTADR_A(6, 12), "FINAL SCORE: ", 13);
        draw_number(NTADR_A(19, 12), score);
        if (score > 0x9000)
          vrambuf_put(NTADR_A(5, 15), "AMAZING!!!!", 11);
        else if (score > 0x5000)
          vrambuf_put(NTADR_A(5, 15), "WOW!!!", 6);
        else if (score > 0x2000)
          vrambuf_put(NTADR_A(5,15), "GREAT SCORE!", 12);
        else if (score > 0x1000)
          vrambuf_put(NTADR_A(5, 15), "NOT BAD!", 8);
        else if (score > 0x0500)
          vrambuf_put(NTADR_A(5,15), "CAN YOU DO BETTER?", 18);
        else
          vrambuf_put(NTADR_A(5,15), "TRY DODGING...", 14);
        vrambuf_put(NTADR_A(3, 18), "PRESS START TO CONTINUE", 23);
        if (trg0&PAD_START) {
          // transition
          state = 4;
          counter = 0;
        }
      } break;
      // transition
      case 4: {
        if (counter>0) vrambuf_put(NTADR_A(2,29-counter+1), "                            ", 28);
        if (counter<29) vrambuf_put(NTADR_A(2,29-counter), "____________________________", 28);
        counter++;
        if (counter == 30) {
          state = 0;
          counter = 0;
        }
      } break;
      // error catch
      default: state = 0;
    }
    // particles
    // remove particles once timer over;
    for (i=0; i<particles; i++) {
      // update particle timer
      if (particle_t[i] > 0) particle_t[i] -= 1;
      if (particle_t[i] == 0) {
        // delete particle
        for (c=i+1; c<particles; c++) {
          particle_x[c-1] = particle_x[c];
          particle_y[c-1] = particle_y[c];
          particle_c[c-1] = particle_c[c];
          particle_p[c-1] = particle_p[c];
          particle_t[c-1] = particle_t[c];
        }
        particles -= 1;
        i -= 1;
      }
    }
    // render particles
    for (i=0; i<particles; i++)
      oam_id = oam_spr(particle_x[i], particle_y[i], particle_c[i], particle_p[i], oam_id);
    // hide unused sprites
    oam_hide_rest(oam_id);
    // wait for next frame
    ppu_wait_nmi();
  }
}

