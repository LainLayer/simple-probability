#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "logka.h"
#include "raylib.h"
#include "raymath.h"

#include "microui.h"
#include "murl.h"

#define QOP_IMPLEMENTATION
#include "qop.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

#define TUTORIAL_TEXT "Press space to add a dice\n" \
                      "     ctrl to roll the dice"
#define TUTORIAL_TEXT_SIZE 48

static bool is_sorting  = false;

static char threshold_buffer[32] = {"3"};
static int  threshold_number     =   3;

static Font font_small; /* 16 */
static Font font_big;   /* 64 */

#define MAX_WIGGLE_TIME 300.0f

static float wiggle_timer = MAX_WIGGLE_TIME + 1.0f;

static Sound dice_sound;
static Sound click_sound;

typedef struct DiceRoll {
    uint32_t amount;
    uint32_t dice_sides;
} DiceRoll;

typedef struct Macro {
    DiceRoll roll;
    char *name;
} Macro;

typedef struct MacroList {
    Macro  *items;
    size_t count;
    size_t capacity;
} MacroList;

static MacroList macro_list = {0};

typedef struct Die {
    uint8_t value;
} Die;

#define MAX_DICE 1024

static Die    dice_buffer[MAX_DICE] = {0};
static size_t dice_count            =  0;

#define DICE_TEXTURE_COUNT 6
static Texture dice_textures[DICE_TEXTURE_COUNT] = {0};

int get_executable_path(char *buffer, unsigned int buffer_size) {
	#if defined(__linux__)
		ssize_t len = readlink("/proc/self/exe", buffer, buffer_size - 1);
		if (len == -1) {
			return 0;
		}
		buffer[len] = '\0';
		return len;
	#elif defined(__APPLE__)
		if (_NSGetExecutablePath(buffer, &buffer_size) == 0) {
			return buffer_size;
		}
	#elif defined(_WIN32)
		return GetModuleFileName(NULL, buffer, buffer_size);
	#endif

	return 0;
}

bool load_texture_from_asset_package(Texture *texture, qop_desc *qop, const char *filename) {
    qop_file *file = qop_find(qop, filename);
	if(file == NULL) {
	    error("QOP failed to find file '%s' while trying to load an image", filename);
	    return false;
	}

	unsigned char *contents = malloc(file->size);

	qop_read(qop, file, contents);

    Image image = LoadImageFromMemory(".png", contents, file->size);

    if(!IsImageValid(image)) {
        error("Tried loading image '%s' from QOP but it was invalid", filename);
        return false;
    }

    *texture = LoadTextureFromImage(image);
    if(!IsTextureValid(*texture)) {
        error("While attempting to upload the data of '%s' as a texture, an error occured. Check the raylib logs.", filename);
        return false;
    }

    UnloadImage(image);
    free(contents);

    return true;
}

bool load_sound_from_asset_package(Sound *sound, qop_desc *qop, const char *filename) {
    qop_file *file = qop_find(qop, filename);
	if(file == NULL) {
	    error("QOP failed to find file '%s' while trying to load a sound", filename);
	    return false;
	}

	unsigned char *contents = malloc(file->size);

	qop_read(qop, file, contents);

	Wave wave = LoadWaveFromMemory(".wav", contents, file->size);

    if(!IsWaveValid(wave)) {
        error("Tried loading wave '%s' from QOP but it was invalid", filename);
        return false;
    }

	*sound = LoadSoundFromWave(wave);

	if(!IsSoundValid(*sound)) {
	   error("Attempted to load sound from wave '%s' but it failed. Check the raylib logs.", filename);
	   return false;
	}

	UnloadWave(wave);
	free(contents);

	return true;
}

bool load_assets(void) {

    char exe_path[PATH_MAX];
    int exe_path_len = get_executable_path(exe_path, sizeof(exe_path));
    if(exe_path_len <= 0) {
        error("Executable path is empty. Something went horribly wrong.");
        return false;
    }

    debug("loading assets from executable path: %s", exe_path);

    qop_desc qop;
	int archive_size = qop_open(exe_path, &qop);
	if(archive_size <= 0) {
	    error("QOP archive is of incorrect size: %d", archive_size);
	    return false;
	}

	int index_len = qop_read_index(&qop, malloc(qop.hashmap_size));
    if(index_len <= 0) {
	    error("QOP index is of incorrect size: %d", index_len);
        return false;
    }

    if(!load_texture_from_asset_package(&dice_textures[0], &qop, "assets/dots_1.png")) return false;
    if(!load_texture_from_asset_package(&dice_textures[1], &qop, "assets/dots_2.png")) return false;
    if(!load_texture_from_asset_package(&dice_textures[2], &qop, "assets/dots_3.png")) return false;
    if(!load_texture_from_asset_package(&dice_textures[3], &qop, "assets/dots_4.png")) return false;
    if(!load_texture_from_asset_package(&dice_textures[4], &qop, "assets/dots_5.png")) return false;
    if(!load_texture_from_asset_package(&dice_textures[5], &qop, "assets/dots_6.png")) return false;

    if(!load_sound_from_asset_package(&dice_sound,  &qop, "assets/dice-1.wav"))  return false;
    if(!load_sound_from_asset_package(&click_sound, &qop, "assets/click_2.wav")) return false;

    free(qop.hashmap);

    qop_close(&qop);

    return true;
}

bool parse_dice_roll(const char *text, DiceRoll *roll) {
    char *text_cursor = (char *)text;

    char   number_buffer[128]   = {0};
    size_t number_buffer_cursor =  0;

    for(char c = *text_cursor; c >= '0' && c <= '9'; c = *(++text_cursor)) {
        if(c == '\0')                   return false;
        if(number_buffer_cursor >= 127) return false;
        if(c == 'd')                    break;
        number_buffer[number_buffer_cursor++] = c;
    }

    if(number_buffer_cursor == 0) return false;

    roll->amount = atoi(number_buffer);
    number_buffer_cursor = 0;
    memset(number_buffer, 0, 128);

    text_cursor += 1; // skip the 'd'

    for(char c = *text_cursor; c >= '0' && c <= '9'; c = *(++text_cursor)) {
        if(number_buffer_cursor >= 127) return false;
        if(c == '\0')                   break;
        number_buffer[number_buffer_cursor++] = c;
    }

    if(number_buffer_cursor == 0) return false;

    roll->dice_sides = atoi(number_buffer);

    return true;
}

bool macro_name_exists(const char *name) {
    for(size_t i = 0; i < macro_list.count; i++)
        if(strcmp(macro_list.items[i].name, name) == 0) return true;

    return false;
}

int compare_dice(const void *dice_1, const void *dice_2) {
    Die *d1 = (Die*)dice_1;
    Die *d2 = (Die*)dice_2;

    return (int)d2->value - (int)d1->value;
}

void sort_dice_if_needed(void) {
    if(is_sorting)
        qsort(dice_buffer, dice_count, sizeof(Die), compare_dice);
}

void roll_dice(void) {
    for(size_t i = 0; i < dice_count; i++)
        dice_buffer[i].value = GetRandomValue(1, 6);

    sort_dice_if_needed();

    if(dice_count > 0) PlaySound(dice_sound);

    wiggle_timer = 0.0f;
}

void add_dice(Die die) {
    if(dice_count >= MAX_DICE) {
        warn("Dice count has reached the maximum amount of %d", MAX_DICE);
        return;
    }

    dice_buffer[dice_count++] = die;

    sort_dice_if_needed();
}

void remove_die() {
    if(dice_count == 0) return;
    dice_count--;
    sort_dice_if_needed();
}

extern Font get_my_epic_font_instead_of_the_default(void) {
    return font_small;
}

int main(void) {
    info("Dice program started");

    InitWindow(1280, 720, "Dice program");

    font_big   = LoadFontEx("assets/LaudatioC.ttf", TUTORIAL_TEXT_SIZE, NULL, 255); // TODO: Pack this with assets
    font_small = LoadFontEx("assets/LaudatioC.ttf", 16, NULL, 255); // TODO: Pack this with assets

    InitAudioDevice();

    SetRandomSeed(100); // TODO: replace with time

    if(!load_assets()) {
        error("Failed to load assets");
        return 1;
    }

    SetSoundVolume(dice_sound, 0.2);

    SetTargetFPS(30);

    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);

    mu_Context mu_context = {0};
    mu_init(&mu_context);
    murl_setup_font(&mu_context);

    while(!WindowShouldClose()) {

        murl_handle_input(&mu_context);

        mu_begin(&mu_context);

        uint32_t dice_total = 0;

        for(size_t i = 0; i < dice_count; i++)
            dice_total += dice_buffer[i].value;

        int panel_width = Clamp(GetScreenWidth() / 8, 160, 220);

        bool typing_text = false;

        if (mu_begin_window_ex(&mu_context, "Hello", mu_rect(0, 0, panel_width, GetScreenHeight()),
            MU_OPT_NOCLOSE | MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_ALWAYS_RESIZE
        )) {

            mu_layout_row(&mu_context, 1, (int[]){-1}, 0);

            mu_label(&mu_context, TextFormat("Dice Sum: %d", dice_total));

            mu_label(&mu_context, "");

            if(mu_button(&mu_context, "Roll")) roll_dice();

            mu_label(&mu_context, "");

            if(mu_checkbox(&mu_context, "Sort dice?", (int*)&is_sorting))
                sort_dice_if_needed();

            mu_label(&mu_context, TextFormat("Threshold: %d", threshold_number));

            if(mu_textbox(&mu_context, threshold_buffer, 32)) typing_text = true;


            int buffer_value = TextToInteger(threshold_buffer);
            if(buffer_value != 0) {
                if(buffer_value > 6) {
                    mu_text(&mu_context, "Please provide number between 1 and 6");
                } else {
                    threshold_number = buffer_value;
                }
            } else {
                mu_text(&mu_context, "Please provide a valid number");
            }

            { // Menu for adding macros
                mu_label(&mu_context, "");
                mu_label(&mu_context, "Define macros:");

                mu_layout_row(&mu_context, 2, (int[]){50, -1}, 0);

                mu_label(&mu_context, "name");
                static char macro_name_buffer[1024] = "my macro";
                if(mu_textbox(&mu_context, macro_name_buffer, 1024)) typing_text = true;

                mu_label(&mu_context, "roll");
                static char macro_roll_buffer[1024] = "12d6";
                if(mu_textbox(&mu_context, macro_roll_buffer, 1024)) typing_text = true;

                DiceRoll roll;
                bool roll_text_valid = parse_dice_roll(macro_roll_buffer, &roll);

                mu_layout_row(&mu_context, 1, (int[]){-1}, 0);
                if(roll_text_valid) {
                    mu_label(&mu_context, TextFormat("amount: %d, sides: %d", roll.amount, roll.dice_sides));
                } else {
                    mu_label(&mu_context, "Invalid macro");
                }

                static char *error_text = "no_error";

                if(mu_button(&mu_context, "make macro")) {
                    if(!roll_text_valid || strlen(macro_name_buffer) < 1) {
                        debug("attempted to create macro with invalid name '%s'", macro_name_buffer);
                        error_text = "please use a valid name";
                        mu_open_popup(&mu_context, "Error");
                    } else if(macro_name_exists(macro_name_buffer)) {
                        debug("attempted to create macro with existing name '%s'", macro_name_buffer);
                        error_text = "Macro name already exists";
                        mu_open_popup(&mu_context, "Error");
                    } else {
                        Macro new_macro = (Macro) {
                            .roll = roll,
                            .name = strdup(macro_name_buffer)
                        };
                        nob_da_append(&macro_list, new_macro);
                    }
                }

                if (mu_begin_popup(&mu_context, "Error")) {
                    int error_text_width = MeasureTextEx(font_small, error_text, font_small.baseSize, 1).x + 10;
                    mu_layout_row(&mu_context, 1, (int[]) { error_text_width }, 0);
                    mu_label(&mu_context, error_text);
                    mu_end_popup(&mu_context);
                }

                if(macro_list.count > 0)
                    mu_label(&mu_context, "macros:");

                mu_layout_row(&mu_context, 2, (int[]) { panel_width * 0.8, -1 }, 0);
                for(size_t i = 0; i < macro_list.count; i++) {
                    Macro it = macro_list.items[i];
                    if(mu_button(&mu_context, TextFormat("%s(%dd%d)", it.name, it.roll.amount, it.roll.dice_sides))) {
                        dice_count = it.roll.amount;
                        roll_dice();
                    }

                    if(mu_button(&mu_context, TextFormat("X#%zu", i))) {
                        if(i + 1 == macro_list.count) {
                            macro_list.count -= 1;
                        } else {
                            free(macro_list.items[i].name);
                            memmove(&macro_list.items[i], &macro_list.items[i+1], (macro_list.count - 1 - i) * sizeof(Macro));
                            macro_list.count -= 1;
                        }
                    }
                }
            }
            mu_end_window(&mu_context);
        }

        mu_end(&mu_context);

        if(!typing_text) {
            if(IsKeyPressed(KEY_SPACE)) {
                add_dice((Die){ .value = GetRandomValue(1, 6) });
                PlaySound(click_sound);
            }

            if(IsKeyPressed(KEY_LEFT_CONTROL))
                roll_dice();

            if(IsKeyPressed(KEY_D)) {
                remove_die();
                PlaySound(click_sound);
            }

            if(IsKeyPressed(KEY_S)) {
                is_sorting = !is_sorting;
                sort_dice_if_needed();
            }
        }

        BeginDrawing();
        ClearBackground((Color){23, 100, 56, 255});

        if(dice_count == 0) {
            Vector2 text_size = MeasureTextEx(font_big, TUTORIAL_TEXT, TUTORIAL_TEXT_SIZE, 1);

            Vector2 corner = (Vector2) {
                .x = (GetScreenWidth() - panel_width) / 2 - (text_size.x / 2) + panel_width,
                .y = GetScreenHeight()                / 2 - (text_size.y / 2)
            };

            DrawTextEx(font_big, TUTORIAL_TEXT, corner, TUTORIAL_TEXT_SIZE, 1.0, (Color){12, 60, 13, 255});
        } else {

            Rectangle dice_rect = { .x = 0.0f, .y = 0.0f, .width = (float)GetScreenWidth(), .height = (float)GetScreenHeight() };

            dice_rect.x     += panel_width;
            dice_rect.width -= panel_width;

            float padding = dice_rect.width / 20;

            dice_rect.x += padding;
            dice_rect.y += padding;
            dice_rect.width  -= padding * 2;
            dice_rect.height -= padding * 2;

            float dice_width = 128;

            float dice_per_row_raw = dice_rect.width / dice_width;
            float dice_per_row     = floorf(dice_per_row_raw);
            if (dice_per_row == 0) dice_per_row = 1;
            float inner_padding    = (dice_per_row_raw - dice_per_row) * dice_width / (float)(dice_per_row - 1);

            float x_cursor = dice_rect.x;
            float y_cursor = dice_rect.y;

            int wiggle = 0;

            if(wiggle_timer < MAX_WIGGLE_TIME)
                wiggle = (int)(sinf((float)GetTime() * 40) * 20 * Lerp(1.0f, 0.0f, wiggle_timer / MAX_WIGGLE_TIME));

            for(size_t i = 0; i < dice_count; i++) {
                Die die = dice_buffer[i];
                DrawTexture(dice_textures[die.value-1], x_cursor + wiggle, y_cursor, WHITE);
                if((i + 1) % (size_t)dice_per_row == 0) {
                    x_cursor = dice_rect.x;
                    y_cursor += dice_width + inner_padding;
                } else {
                    x_cursor += dice_width + inner_padding;
                }
            }
        }

        murl_render(&mu_context);

        EndDrawing();

        if(wiggle_timer < MAX_WIGGLE_TIME) wiggle_timer += GetFrameTime() * 1000;
    }

    return 0;
}
