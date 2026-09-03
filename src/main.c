#include <GLFW/glfw3.h>
#include <stdio.h>
#include "archive.h"
#include "bookmark.h"
#include "config.h"
#include "decoder.h"
#include "osd.h"
#include "renderer.h"

static int current_page;
static ComicRenderer *renderer;
static PageDecoder *decoder;
static OSD *osd;
static CBZArchive *archive;
static int dragging;
static double last_x, last_y;
static const char *comic_file_path;
static AppConfig config;

static void update_title(GLFWwindow *window)
{
    const char *layout = (renderer->layout == LAYOUT_SINGLE) ? "Single" :
    (renderer->layout == LAYOUT_DUAL)   ? "Dual Spread" : "Webtoon";
    const char *direction = (renderer->direction == DIR_RTL) ? "RTL" : "LTR";
    const char *fit = (renderer->fit == FIT_HEIGHT) ? "Fit-Height" : "Fit-Width";

    char title[256];
    const char *shifted = renderer->spread_offset ? ", Shift" : "";
    if (archive->info && archive->info->has_metadata && *archive->info->series) {
        snprintf(title, sizeof(title), "cbzview - %s %s [%d / %d] (%s, %s, %s%s)",
                 archive->info->series, archive->info->number,
                 current_page + 1, archive->total_pages, layout, direction, fit, shifted);
    } else {
        snprintf(title, sizeof(title), "cbzview - [%d / %d] (%s, %s, %s%s)",
                 current_page + 1, archive->total_pages, layout, direction, fit, shifted);
    }
    glfwSetWindowTitle(window, title);
}

static void clamp_page(void)
{
    if (current_page < 0)
        current_page = 0;
    if (current_page >= archive->total_pages)
        current_page = archive->total_pages - 1;
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    int is_single_step = (renderer->layout == LAYOUT_SINGLE || renderer->layout == LAYOUT_WEBTOON ||
                         (current_page == 0 && !renderer->spread_offset));
    int step = is_single_step ? 1 : 2;

    decoder_lock(decoder);
    DecodedSlot *slot = decoder_get_slot_locked(decoder, current_page);
    if (slot && slot->height > 0 && slot->width > slot->height * 1.15f)
        step = 1;
    decoder_unlock(decoder);

    int direction = (renderer->direction == DIR_RTL) ? -1 : 1;

    switch (key) {
        case GLFW_KEY_RIGHT: case GLFW_KEY_L: case GLFW_KEY_SPACE: case GLFW_KEY_PAGE_DOWN:
            current_page += step * direction;
            clamp_page();
            decoder_request_page(decoder, current_page, direction);
            osd_trigger(osd);
            break;

        case GLFW_KEY_LEFT: case GLFW_KEY_H: case GLFW_KEY_BACKSPACE: case GLFW_KEY_PAGE_UP:
            current_page -= step * direction;
            clamp_page();
            decoder_request_page(decoder, current_page, -direction);
            osd_trigger(osd);
            break;

        case GLFW_KEY_HOME:
            current_page = 0;
            decoder_request_page(decoder, current_page, 1);
            osd_trigger(osd);
            break;

        case GLFW_KEY_END:
            current_page = archive->total_pages - 1;
            decoder_request_page(decoder, current_page, -1);
            osd_trigger(osd);
            break;

        case GLFW_KEY_D:
            renderer->layout = (renderer->layout == LAYOUT_SINGLE) ? LAYOUT_DUAL :
            (renderer->layout == LAYOUT_DUAL)   ? LAYOUT_WEBTOON : LAYOUT_SINGLE;
            renderer->pan_x = renderer->pan_y = 0.0f;
            osd_trigger(osd);
            break;

        case GLFW_KEY_M:
            renderer->direction = (renderer->direction == DIR_LTR) ? DIR_RTL : DIR_LTR;
            decoder_request_page(decoder, current_page, (renderer->direction == DIR_RTL) ? -1 : 1);
            osd_trigger(osd);
            break;

        case GLFW_KEY_W: case GLFW_KEY_2:
            renderer->fit = (renderer->fit == FIT_HEIGHT) ? FIT_WIDTH : FIT_HEIGHT;
            osd_trigger(osd);
            break;

        case GLFW_KEY_C:
            renderer->contrast_boost = !renderer->contrast_boost;
            osd_trigger(osd);
            break;

        case GLFW_KEY_S:
            renderer->spread_offset = !renderer->spread_offset;
            osd_trigger(osd);
            break;

        case GLFW_KEY_R:
            renderer->zoom = 1.0f;
            renderer->pan_x = renderer->pan_y = 0.0f;
            break;

        case GLFW_KEY_F: case GLFW_KEY_F11: {
            static int fullscreen, wx, wy, ww, wh;
            if (!fullscreen) {
                glfwGetWindowPos(window, &wx, &wy);
                glfwGetWindowSize(window, &ww, &wh);
                GLFWmonitor *monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode *mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            } else {
                glfwSetWindowMonitor(window, NULL, wx, wy, ww, wh, 0);
            }
            fullscreen = !fullscreen;
            break;
        }

        case GLFW_KEY_Q: case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
    }

    update_title(window);
}

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    (void)xoffset;
    if (renderer->layout == LAYOUT_WEBTOON) {
        renderer->pan_y -= (float)(yoffset * 80.0);
        int win_w, win_h;
        glfwGetFramebufferSize(window, &win_w, &win_h);

        decoder_lock(decoder);
        DecodedSlot *cur = decoder_get_slot_locked(decoder, current_page);
        float cur_h = (cur && cur->width > 0) ? (float)cur->height * ((float)win_w / (float)cur->width) : (float)win_h;
        decoder_unlock(decoder);

        float half_h = cur_h / 2.0f;
        if (renderer->pan_y < -half_h) {
            if (current_page + 1 < archive->total_pages) {
                current_page++;
                renderer->pan_y += cur_h;
            } else {
                renderer->pan_y = -half_h;
            }
        } else if (renderer->pan_y > half_h) {
            if (current_page > 0) {
                current_page--;
                decoder_lock(decoder);
                DecodedSlot *prev = decoder_get_slot_locked(decoder, current_page);
                float prev_h = (prev && prev->width > 0) ? (float)prev->height * ((float)win_w / (float)prev->width) : (float)win_h;
                decoder_unlock(decoder);
                renderer->pan_y -= prev_h;
            } else {
                renderer->pan_y = half_h;
            }
        }

        decoder_request_page(decoder, current_page, 1);
        update_title(window);
        osd_trigger(osd);
        return;
    }

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    int ww, wh;
    glfwGetWindowSize(window, &ww, &wh);

    float mouse_x = (float)mx - ((float)ww / 2.0f);
    float mouse_y = (float)my - ((float)wh / 2.0f);
    float old_zoom = renderer->zoom;
    float new_zoom = old_zoom;

    if (yoffset > 0) {
        new_zoom *= 1.12f;
    } else if (yoffset < 0) {
        new_zoom /= 1.12f;
        if (new_zoom < 0.15f)
            new_zoom = 0.15f;
    }

    if (new_zoom != old_zoom) {
        float ratio = new_zoom / old_zoom;
        renderer->pan_x = mouse_x - (mouse_x - renderer->pan_x) * ratio;
        renderer->pan_y = mouse_y - (mouse_y - renderer->pan_y) * ratio;
        renderer->zoom = new_zoom;
    }
    osd_trigger(osd);
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;

    if (action == GLFW_PRESS) {
        dragging = 1;
        glfwGetCursorPos(window, &last_x, &last_y);
    } else if (action == GLFW_RELEASE) {
        dragging = 0;
    }
}

static void cursor_position_callback(GLFWwindow *window, double x, double y)
{
    (void)window;
    if (dragging) {
        renderer->pan_x += (float)(x - last_x);
        renderer->pan_y += (float)(y - last_y);
        last_x = x;
        last_y = y;
        osd_trigger(osd);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <comic.cbz>\n", argv[0]);
        return 1;
    }
    comic_file_path = argv[1];

    config_load(&config);

    archive = archive_open(comic_file_path);
    if (!archive || archive->total_pages == 0) {
        fprintf(stderr, "Failed to load archive or no supported images found.\n");
        archive_close(archive);
        return 1;
    }

    if (!glfwInit()) {
        archive_close(archive);
        fprintf(stderr, "Failed to initialize GLFW.\n");
        return 1;
    }

    #if defined(GLFW_WAYLAND_APP_ID)
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "cbzview");
    #endif
    #if defined(GLFW_X11_CLASS_NAME)
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "cbzview");
    #endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow *window = glfwCreateWindow(1280, 800, "cbzview", NULL, NULL);
    if (!window) {
        glfwTerminate();
        archive_close(archive);
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);

    decoder = decoder_init(archive);
    renderer = renderer_init();
    osd = osd_init();

    if (!decoder || !renderer || !osd) {
        decoder_cleanup(decoder);
        renderer_cleanup(renderer);
        osd_cleanup(osd);
        glfwDestroyWindow(window);
        glfwTerminate();
        archive_close(archive);
        return 1;
    }

    renderer->layout = config.default_layout;
    renderer->direction = config.default_direction;
    renderer->fit = config.default_fit;
    renderer->contrast_boost = config.contrast_boost;

    /* Auto-configure from ComicInfo.xml metadata if present */
    if (archive->info && archive->info->has_metadata) {
        if (archive->info->is_manga)
            renderer->direction = DIR_RTL;
        printf("[cbzview] Metadata Loaded: %s %s - %s (Manga: %s)\n",
               archive->info->series, archive->info->number, archive->info->title,
               archive->info->is_manga ? "Yes (RTL)" : "No (LTR)");
    }

    if (config.auto_resume) {
        bookmark_load(comic_file_path, &current_page, &renderer->layout,
                      &renderer->direction, &renderer->fit);
    }
    clamp_page();

    decoder_request_page(decoder, current_page, (renderer->direction == DIR_RTL) ? -1 : 1);
    osd_trigger(osd);
    update_title(window);

    /* Event-Driven Render Loop */
    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        renderer_render_frame(renderer, decoder, current_page, width, height);
        osd_draw(osd, archive, current_page, width, height);
        glfwSwapBuffers(window);

        if (osd_is_active(osd) || dragging) {
            glfwWaitEventsTimeout(0.016); /* Animate smoothly when UI elements or drags are active */
        } else {
            glfwWaitEvents();            /* 0% CPU consumption during idle reading */
        }
    }

    if (config.auto_resume) {
        bookmark_save(comic_file_path, current_page, renderer->layout,
                      renderer->direction, renderer->fit);
    }

    decoder_cleanup(decoder);
    renderer_cleanup(renderer);
    osd_cleanup(osd);

    glfwDestroyWindow(window);
    glfwTerminate();
    archive_close(archive);
    return 0;
}
