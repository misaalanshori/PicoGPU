#include "shared_state.h"
#include <stdio.h>
#include <stdlib.h>

// Flappy Bird global allocations
bool game_active = false;
bool game_over = false;
float bird_y = 120.0f;
float bird_velocity = 0.0f;
float pipe_x = 640.0f;
float pipe_gap_y = 140.0f;
int score = 0;
int high_score = 0;

void update_game_physics(void) {
    if (game_active) {
        // Apply gravity & velocity
        bird_velocity += GRAVITY;
        if (bird_velocity > 8.0f) bird_velocity = 8.0f; // Terminal velocity
        bird_y += bird_velocity;
        
        // Move pipes
        pipe_x -= PIPE_SPEED;
        if (pipe_x < -PIPE_WIDTH) {
            pipe_x = GAME_WIDTH;
            pipe_gap_y = 100.0f + (rand() % 80); // random gap center
            score++;
            current_sfx = SFX_POINT; // play point chime
            sfx_frame = 0;
            
            if (score > high_score) {
                high_score = score;
            }
            
            char str_score[64];
            snprintf(str_score, sizeof(str_score), "Score: %d | High: %d", score, high_score);
            if (lbl_score) lv_label_set_text(lbl_score, str_score);
        }
        
        // Collision checks
        bool hit = false;
        if (bird_y < 0.0f || bird_y > (float)GAME_HEIGHT - BIRD_SIZE) {
            hit = true;
        }
        
        float bird_top = bird_y;
        float bird_bottom = bird_y + BIRD_SIZE;
        
        // Check if bird is horizontally within pipe bounds
        if (BIRD_X + BIRD_SIZE > pipe_x && BIRD_X < pipe_x + PIPE_WIDTH) {
            float gap_top = pipe_gap_y - GAP_SIZE/2.0f;
            float gap_bottom = pipe_gap_y + GAP_SIZE/2.0f;
            
            if (bird_top < gap_top || bird_bottom > gap_bottom) {
                hit = true;
            }
        }
        
        if (hit) {
            game_active = false;
            game_over = true;
            current_sfx = SFX_CRASH;
            sfx_frame = 0;
            
            if (lbl_status) {
                lv_label_set_text(lbl_status, "GAME OVER\nClick to restart!");
                lv_obj_remove_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);
            }
            printf("[game] Collided! Game active = false, Game over = true.\r\n");
        }
        
        // Update widget positions
        if (bird_obj) {
            lv_obj_set_pos(bird_obj, BIRD_X, (int)bird_y);
        }
        
        if (pipe_top && pipe_bottom) {
            // Top pipe height
            int top_h = (int)(pipe_gap_y - GAP_SIZE/2.0f);
            // Bottom pipe height
            int bottom_h = GAME_HEIGHT - (int)(pipe_gap_y + GAP_SIZE/2.0f);
            
            lv_obj_set_size(pipe_top, PIPE_WIDTH, top_h);
            lv_obj_set_pos(pipe_top, (int)pipe_x, 0);
            
            lv_obj_set_size(pipe_bottom, PIPE_WIDTH, bottom_h);
            lv_obj_set_pos(pipe_bottom, (int)pipe_x, GAME_HEIGHT - bottom_h);
        }
    }
}

void flap_bird(void) {
    if (!game_active) {
        // Start/restart game
        bird_y = 120.0f;
        bird_velocity = 0.0f;
        pipe_x = 640.0f;
        pipe_gap_y = 100.0f + (rand() % 80); // random gap center
        score = 0;
        game_active = true;
        game_over = false;
        if (lbl_status) lv_obj_add_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);
        
        printf("[game] Start/Restart! Game active = true, variables reset.\r\n");

        char str_score[64];
        snprintf(str_score, sizeof(str_score), "Score: %d | High: %d", score, high_score);
        if (lbl_score) lv_label_set_text(lbl_score, str_score);
    }
    bird_velocity = FLAP_IMPULSE;
    current_sfx = SFX_FLAP;
    sfx_frame = 0;
    printf("[game] Flapped! Velocity = %.1f\r\n", bird_velocity);
}
