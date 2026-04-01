#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

#include <string.h>

#include "raylib.h"


#define RAYLIB_NUKLEAR_IMPLEMENTATION
#define RAYLIB_NUKLEAR_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#include "raylib-nuklear.h"

// Gamepad support https://github.com/robloach/nuklear_gamepad
#define NK_GAMEPAD_IMPLEMENTATION
#include "nuklear_gamepad.h"

#define NK_CONSOLE_IMPLEMENTATION
#include "nuklear_console.h"

void UpdateDrawFrame(void);

struct nk_context* ctx;
struct nk_console* console;

#define MAX_TODOS 32
#define TODO_TEXT_LEN 256

typedef enum {
    FILTER_ALL = 0,
    FILTER_ACTIVE,
    FILTER_COMPLETED
} TodoFilter;

typedef struct {
    char    text[TODO_TEXT_LEN];
    nk_bool completed;
} Todo;

static Todo        todos[MAX_TODOS];
static int         todo_count      = 0;
static TodoFilter  current_filter  = FILTER_ALL;
static int         filter_selected = 0; // shared radio group index (0=All,1=Active,2=Completed)
static char        new_todo_buffer[TODO_TEXT_LEN];
static nk_console* todo_widgets[MAX_TODOS];
static nk_console* filter_row;
static nk_console* clear_completed_btn;
static nk_console* textedit_widget;
static struct nk_gamepads gamepads;

/**
 * Style: Dracula
 */
static void demo_set_style(struct nk_context* ctx) {
    struct nk_color table[NK_COLOR_COUNT];
    struct nk_color background = nk_rgba(40, 42, 54, 255);
    struct nk_color currentline = nk_rgba(68, 71, 90, 255);
    struct nk_color foreground = nk_rgba(248, 248, 242, 255);
    struct nk_color comment = nk_rgba(98, 114, 164, 255);
    struct nk_color pink = nk_rgba(255, 121, 198, 255);
    struct nk_color purple = nk_rgba(189, 147, 249, 255);
    table[NK_COLOR_TEXT] = foreground;
    table[NK_COLOR_WINDOW] = background;
    table[NK_COLOR_HEADER] = currentline;
    table[NK_COLOR_BORDER] = currentline;
    table[NK_COLOR_BUTTON] = currentline;
    table[NK_COLOR_BUTTON_HOVER] = comment;
    table[NK_COLOR_BUTTON_ACTIVE] = purple;
    table[NK_COLOR_TOGGLE] = currentline;
    table[NK_COLOR_TOGGLE_HOVER] = comment;
    table[NK_COLOR_TOGGLE_CURSOR] = pink;
    table[NK_COLOR_SELECT] = currentline;
    table[NK_COLOR_SELECT_ACTIVE] = comment;
    table[NK_COLOR_SLIDER] = background;
    table[NK_COLOR_SLIDER_CURSOR] = currentline;
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = comment;
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = comment;
    table[NK_COLOR_PROPERTY] = currentline;
    table[NK_COLOR_EDIT] = currentline;
    table[NK_COLOR_EDIT_CURSOR] = foreground;
    table[NK_COLOR_COMBO] = currentline;
    table[NK_COLOR_CHART] = currentline;
    table[NK_COLOR_CHART_COLOR] = comment;
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = purple;
    table[NK_COLOR_SCROLLBAR] = background;
    table[NK_COLOR_SCROLLBAR_CURSOR] = currentline;
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = comment;
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = purple;
    table[NK_COLOR_TAB_HEADER] = currentline;
    table[NK_COLOR_KNOB] = table[NK_COLOR_SLIDER];
    table[NK_COLOR_KNOB_CURSOR] = table[NK_COLOR_SLIDER_CURSOR];
    table[NK_COLOR_KNOB_CURSOR_HOVER] = table[NK_COLOR_SLIDER_CURSOR_HOVER];
    table[NK_COLOR_KNOB_CURSOR_ACTIVE] = table[NK_COLOR_SLIDER_CURSOR_ACTIVE];
    nk_style_from_table(ctx, table);
}
static void update_visibility(void) {
    for (int i = 0; i < MAX_TODOS; i++) {
        if (i >= todo_count) {
            todo_widgets[i]->visible = nk_false;
            continue;
        }
        switch (current_filter) {
            case FILTER_ALL:
                todo_widgets[i]->visible = nk_true;
                break;
            case FILTER_ACTIVE:
                todo_widgets[i]->visible = !todos[i].completed;
                break;
            case FILTER_COMPLETED:
                todo_widgets[i]->visible = todos[i].completed;
                break;
        }
    }
    nk_bool has_todos = todo_count > 0;
    filter_row->visible        = has_todos;
    clear_completed_btn->visible = has_todos;
}

static void on_todo_changed(nk_console* widget, void* user_data) {
    NK_UNUSED(widget);
    NK_UNUSED(user_data);
    update_visibility();
}

static void on_add_todo(nk_console* btn, void* user_data) {
    (void)btn; (void)user_data;
    if (new_todo_buffer[0] != '\0' && todo_count < MAX_TODOS) {
        strncpy(todos[todo_count].text, new_todo_buffer, TODO_TEXT_LEN - 1);
        todos[todo_count].text[TODO_TEXT_LEN - 1] = '\0';
        todos[todo_count].completed = nk_false;
        todo_count++;
        memset(new_todo_buffer, 0, sizeof(new_todo_buffer));
        update_visibility();
    }
}

static void on_filter_changed(nk_console* widget, void* user_data) {
    NK_UNUSED(widget);
    NK_UNUSED(user_data);
    current_filter = (TodoFilter)filter_selected;
    update_visibility();
}

static void on_clear_completed(nk_console* btn, void* user_data) {
    NK_UNUSED(btn);
    NK_UNUSED(user_data);
    int new_count = 0;
    for (int i = 0; i < todo_count; i++) {
        if (!todos[i].completed) {
            if (new_count != i) {
                todos[new_count] = todos[i];
            }
            new_count++;
        }
    }
    for (int i = new_count; i < MAX_TODOS; i++) {
        memset(todos[i].text, 0, TODO_TEXT_LEN);
        todos[i].completed = nk_false;
    }
    todo_count = new_count;
    update_visibility();
}

static void setup_ui(void) {
    textedit_widget = nk_console_textedit(console, "What needs to be done?", new_todo_buffer, TODO_TEXT_LEN);
    nk_console_add_event(textedit_widget, NK_CONSOLE_EVENT_BACK, on_add_todo);

    // Pre-allocate MAX_TODOS checkbox slots (all hidden until a todo exists)
    for (int i = 0; i < MAX_TODOS; i++) {
        todo_widgets[i] = nk_console_checkbox(console, todos[i].text, &todos[i].completed);
        nk_console_add_event(todo_widgets[i], NK_CONSOLE_EVENT_CHANGED, on_todo_changed);
        todo_widgets[i]->visible = nk_false;
        todo_widgets[i]->alignment = NK_TEXT_RIGHT;
    }

    // Filter / footer row (hidden until todos exist)
    filter_row = nk_console_row_begin(console);
    nk_console_add_event(nk_console_radio(filter_row, "All",       &filter_selected), NK_CONSOLE_EVENT_CHANGED, on_filter_changed);
    nk_console_add_event(nk_console_radio(filter_row, "Active",    &filter_selected), NK_CONSOLE_EVENT_CHANGED, on_filter_changed);
    nk_console_add_event(nk_console_radio(filter_row, "Completed", &filter_selected), NK_CONSOLE_EVENT_CHANGED, on_filter_changed);
    nk_console_row_end(filter_row);
    filter_row->visible = nk_false;

    clear_completed_btn = nk_console_button_onclick(console, "Clear Completed", on_clear_completed);
    clear_completed_btn->visible = nk_false;
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "nuklear_console_todomvc");
    SetWindowMinSize(200, 200);

    // Create the Nuklear Context
    int fontSize = 13 * 2;
    Font font = LoadFontFromNuklear(fontSize);
    GenTextureMipmaps(&font.texture);
    ctx = InitNuklearEx(font, fontSize);
    demo_set_style(ctx);

    nk_gamepad_init(&gamepads, ctx, NULL);
    console = nk_console_init(ctx);
    nk_console_set_gamepads(console, &gamepads);

    // Initialize TodoMVC and build the UI
    setup_ui();

    #if defined(PLATFORM_WEB)
        emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
    #else
        while (!WindowShouldClose()) {
            UpdateDrawFrame();
        }
    #endif

    // De-initialize the Nuklear GUI
    nk_gamepad_free(&gamepads);
    nk_console_free(console);
    UnloadNuklear(ctx);
    UnloadFont(font);

    CloseWindow();
    return 0;
}

void UpdateDrawFrame(void) {
    // Update the Nuklear context, along with input
    UpdateNuklear(ctx);

    nk_gamepad_update(nk_console_get_gamepads(console));

    // Nuklear GUI Code
    struct nk_rect size = nk_rect(0, 0, GetScreenWidth(), GetScreenHeight());
    nk_console_render_window(console, "nuklear_console_todomvc", size, NK_WINDOW_SCROLL_AUTO_HIDE);

    // Render
    BeginDrawing();
        ClearBackground(BLACK);

        // Render the Nuklear GUI
        DrawNuklear(ctx);

    EndDrawing();

    #ifdef PLATFORM_WEB
        if (WindowShouldClose()) {
            emscripten_cancel_main_loop();
        }
    #endif
}
