#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <unistd.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include "protocols/wlr-layer-shell.h"
#include "protocols/wlr-screencopy.h"

#include <tesseract/capi.h>
#include <leptonica/allheaders.h>

#include "shm.c"
#define OLIVEC_IMPLEMENTATION
#include "olive.c"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// From rust bcs i am not doing json or networking in c
// Maybe ill rewrite all this in rust someday
typedef char* RString;
extern void init_thingy();
extern RString get_item(const char*);
extern float get_item_price(RString);
extern float get_set_price(RString);
extern void free_rstring(RString);

static uint32_t WIDTH = 0;
static uint32_t HEIGHT = 0;

static bool configured = false;
static bool running = true;
static float dt = 0.0;
static uint32_t prev_time = 0;
static const char* tesseract_texts[4] = {0};
static size_t tesseract_text_count = 0;
static bool tesseract_done = true;
static float time_thing = 0;

static struct wl_shm *shm = NULL;
static struct wl_compositor *compositor = NULL;
static struct zwlr_layer_shell_v1 *layer_shell = NULL;
static struct zwlr_screencopy_manager_v1 *screencopy_manager = NULL;

static struct wl_surface *surface = NULL;
static struct wl_buffer* buffer = NULL;
static struct wl_output* output_thing = NULL; // Display or interface?

static Olivec_Canvas oc = {0};
static Olivec_Canvas plat = {0};

static TessBaseAPI* tess_api = NULL;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Rect;

#define TESSERACT_INTERVAL 1.0
//#define DEBUG_MODE

// TODO: Compute these based on screen size and ui scale
// Also maybe support 3 2 and 1 relics
#define RELIC_INFO_W 318
#define RELIC_INFO_H 40
#define RELIC_INFO_Y 242
#define RELIC1_INFO_RECT ((Rect){635,  RELIC_INFO_Y, RELIC_INFO_W, RELIC_INFO_H})
#define RELIC2_INFO_RECT ((Rect){958,  RELIC_INFO_Y, RELIC_INFO_W, RELIC_INFO_H})
#define RELIC3_INFO_RECT ((Rect){1282, RELIC_INFO_Y, RELIC_INFO_W, RELIC_INFO_H})
#define RELIC4_INFO_RECT ((Rect){1609, RELIC_INFO_Y, RELIC_INFO_W, RELIC_INFO_H})

#define RELIC_SCREENCOPY_H 60
#define RELIC_SCREENCOPY_Y 549
#define RELIC1_SCREENCOPY_RECT ((Rect){RELIC1_INFO_RECT.x, RELIC_SCREENCOPY_Y, RELIC_INFO_W, RELIC_SCREENCOPY_H}) // ((Rect){624, 533, 1293, 101})
#define RELIC2_SCREENCOPY_RECT ((Rect){RELIC2_INFO_RECT.x, RELIC_SCREENCOPY_Y, RELIC_INFO_W, RELIC_SCREENCOPY_H})
#define RELIC3_SCREENCOPY_RECT ((Rect){RELIC3_INFO_RECT.x, RELIC_SCREENCOPY_Y, RELIC_INFO_W, RELIC_SCREENCOPY_H})
#define RELIC4_SCREENCOPY_RECT ((Rect){RELIC4_INFO_RECT.x, RELIC_SCREENCOPY_Y, RELIC_INFO_W, RELIC_SCREENCOPY_H})

typedef struct {
    struct wl_buffer* buf;
    void* data;
} ReturnThing;

typedef struct {
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t flags;
    ReturnThing thing;
} ScreencopyThing;

PIX* argb8888_to_grayscale(uint8_t* pixels, int width, int height) {
    PIX *out = pixCreate(width, height, 8);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int i = (y * width + x) * 4;
            
            uint8_t r = pixels[i + 1];
            uint8_t g = pixels[i + 2];
            uint8_t b = pixels[i + 3];

            pixSetPixel(out, x, y, (uint8_t)(0.3 * r + 0.59 * g + 0.11 * b));
        }
    }

    return out;
}

static ReturnThing create_buffer(int width, int height, int stride, int format, int prot, int flags) {
    ReturnThing out = {0};
    int size = width * height * sizeof(uint32_t); // TODO: Could be not u32

    int fd = create_shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
        exit(1);
    }

    out.data = mmap(NULL, size, prot, flags, fd, 0);
    if (out.data == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        exit(1);
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    out.buf = buffer;
    wl_shm_pool_destroy(pool);

    close(fd);

    return out;
}

static void noop() {

}

static void screencopy_frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    (void)frame;
    ScreencopyThing* sc = (ScreencopyThing*)data;
    assert(format == 0 || format == 1); // Only support [ax]rgb8888. Idk if its even possible to get argb or other formats
    sc->format = format;
    sc->width = width;
    sc->height = height;
    sc->stride = stride;
}

static void screencopy_frame_flags(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t flags) {
    (void)frame;
    ((ScreencopyThing*)data)->flags = flags;
}

const char* get_text_from_pix(PIX* img, Rect r) {
    TessBaseAPISetImage2(tess_api, img);
    TessBaseAPISetRectangle(tess_api, r.x, r.y, r.w, r.h);
    TessBaseAPISetVariable(tess_api, "tessedit_char_whitelist", "abcdefghijklmnopqrstuvwxyz");
    return TessBaseAPIGetUTF8Text(tess_api);
}

static void screencopy_frame_ready(void* data, struct zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    zwlr_screencopy_frame_v1_destroy(frame);

    ScreencopyThing* sc = (ScreencopyThing*)data;
    
    // Tesseract wants grayscale ig
    PIX* gray = argb8888_to_grayscale(sc->thing.data, sc->width, sc->height);
    PIX* screen = pixGammaTRC(NULL, gray, 0.8, 0, 255);
    //screen = pixInvert(NULL, screen);

    for (size_t i = 0; i < tesseract_text_count; i++) {
        if (tesseract_texts[i]) TessDeleteText(tesseract_texts[i]);
    }

    tesseract_text_count = 4;
    tesseract_texts[0] = get_text_from_pix(screen, RELIC1_SCREENCOPY_RECT);
    tesseract_texts[1] = get_text_from_pix(screen, RELIC2_SCREENCOPY_RECT);
    tesseract_texts[2] = get_text_from_pix(screen, RELIC3_SCREENCOPY_RECT);
    tesseract_texts[3] = get_text_from_pix(screen, RELIC4_SCREENCOPY_RECT);

    wl_buffer_destroy(sc->thing.buf);
    munmap(sc->thing.data, sc->width * sc->height * sizeof(uint32_t)); // TODO: Could be not u32
    free(sc);
    pixDestroy(&screen);
    pixDestroy(&gray);
    tesseract_done = true;
}

static void buffer_done(void* data, struct zwlr_screencopy_frame_v1* frame) {
    ScreencopyThing* sc = (ScreencopyThing*)data;
    sc->thing = create_buffer(sc->width, sc->height, sc->stride, sc->format, PROT_READ, MAP_PRIVATE);
    assert(sc->thing.buf);
    assert(sc->thing.data);
    zwlr_screencopy_frame_v1_copy(frame, sc->thing.buf);
}

static struct zwlr_screencopy_frame_v1_listener screencopy_frame_listener = {
    .buffer = screencopy_frame_buffer,
    .flags = screencopy_frame_flags,
    .ready = screencopy_frame_ready,
    .failed = noop,
    .damage = noop,
    .linux_dmabuf = noop,
    .buffer_done = buffer_done,
};

static void output_geometry(void* data, struct wl_output* output, int x, int y, int pw, int ph, int sub, const char* make, const char* model, int transform) {
    (void)data;
    (void)output;
    printf("Display: %d %d %d %d %d %s %s %d\n", x, y, pw, ph, sub, make, model, transform);
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = noop,
    .done = noop,
    .scale = noop,
    .name = noop,
    .description = noop
};

// void seat_capabilities(void* data, struct wl_seat* seat, uint32_t c) {
//     (void)data;
//     (void)seat;
//     printf("Can mouse: %b\n", (c & WL_SEAT_CAPABILITY_POINTER) != 0);
//     printf("Can keyboard: %b\n", (c & WL_SEAT_CAPABILITY_KEYBOARD) != 0);
//     printf("Can touch: %b\n", (c & WL_SEAT_CAPABILITY_TOUCH) != 0);
    
//     if (c & WL_SEAT_CAPABILITY_POINTER) {
//         pointer = wl_seat_get_pointer(seat);
//         wl_pointer_add_listener(pointer, &pointer_listener, NULL);
//     }
// }

static const struct wl_seat_listener seat_listener = {
    .name = noop,
    .capabilities = noop,
};

static void layer_surface_configure(void* data, struct zwlr_layer_surface_v1* surface, uint32_t s, uint32_t width, uint32_t height) {
    (void)data;
    (void)surface;
    configured = true;
    WIDTH = width;
    HEIGHT = height;
    printf("Comp wants %dx%d layer\n", width, height);
    zwlr_layer_surface_v1_ack_configure(surface, s);
}

static void layer_surface_closed(void* data, struct zwlr_layer_surface_v1* surface) {
    (void)data;
    (void)surface;
    running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed
};

static void handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, version);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        struct wl_seat *seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        output_thing = wl_registry_bind(registry, name, &wl_output_interface, version);
        wl_output_add_listener(output_thing, &output_listener, NULL);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, version);
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        screencopy_manager = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, version);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

void do_tesseract_thing() {
    tesseract_done = false;
    struct zwlr_screencopy_frame_v1 *frame = zwlr_screencopy_manager_v1_capture_output(screencopy_manager, 0, output_thing);
    ScreencopyThing* sc = malloc(sizeof(ScreencopyThing));
    zwlr_screencopy_frame_v1_add_listener(frame, &screencopy_frame_listener, sc);
}

Olivec_Canvas load_image(const char* filename) {
    int w, h, n;
    uint32_t* data = (uint32_t*)stbi_load(filename, &w, &h, &n, 4);
    if (data == NULL) {
        fprintf(stderr, "ERROR: Failed to open %s\n", filename);
        exit(1);
    }

    // RGBA -> ARGB (i think :staring_cat:)
    // Olivec uses RGBA but shm needs ARGB but whatever bcs olivec doesnt care about the order unless you are using blending (im not)
    // Hyprland doesnt support RGBA apparently for shm or maybe its a problem with the wayland client library
    for (int i = 0; i < w * h; i++) {
        uint32_t p = data[i];
        if ((p >> 24) < 255) {
            p = 0;
        }
        data[i] = OLIVEC_RGBA(p >> 16, p >> 8, p, p >> 24);
    }
    return olivec_canvas(data, w, h, w);
}

void draw() {
    time_thing += dt;
    olivec_fill(oc, 0x00000000);

    if (tesseract_done && time_thing >= TESSERACT_INTERVAL) {
        time_thing = 0.0;
        do_tesseract_thing();
    }

#ifdef DEBUG_MODE
    olivec_rect(oc, RELIC1_INFO_RECT.x, RELIC1_INFO_RECT.y, RELIC1_INFO_RECT.w, RELIC1_INFO_RECT.h, 0xa0ff0000);
    olivec_rect(oc, RELIC2_INFO_RECT.x, RELIC2_INFO_RECT.y, RELIC2_INFO_RECT.w, RELIC2_INFO_RECT.h, 0xa000ff00);
    olivec_rect(oc, RELIC3_INFO_RECT.x, RELIC3_INFO_RECT.y, RELIC3_INFO_RECT.w, RELIC3_INFO_RECT.h, 0xa00000ff);
    olivec_rect(oc, RELIC4_INFO_RECT.x, RELIC4_INFO_RECT.y, RELIC4_INFO_RECT.w, RELIC4_INFO_RECT.h, 0xa0ff00ff);

    olivec_rect(oc, RELIC1_SCREENCOPY_RECT.x, RELIC1_SCREENCOPY_RECT.y, RELIC1_SCREENCOPY_RECT.w, RELIC1_SCREENCOPY_RECT.h, 0xa0ff0000);
    olivec_rect(oc, RELIC2_SCREENCOPY_RECT.x, RELIC2_SCREENCOPY_RECT.y, RELIC2_SCREENCOPY_RECT.w, RELIC2_SCREENCOPY_RECT.h, 0xa000ff00);
    olivec_rect(oc, RELIC3_SCREENCOPY_RECT.x, RELIC3_SCREENCOPY_RECT.y, RELIC3_SCREENCOPY_RECT.w, RELIC3_SCREENCOPY_RECT.h, 0xa00000ff);
    olivec_rect(oc, RELIC4_SCREENCOPY_RECT.x, RELIC4_SCREENCOPY_RECT.y, RELIC4_SCREENCOPY_RECT.w, RELIC4_SCREENCOPY_RECT.h, 0xa0ff00ff);
#endif // DEBUG_MODE

    if (plat.pixels == NULL) {
        plat = load_image("assets/plat.png");
    }

    Rect rects[] = {RELIC1_INFO_RECT, RELIC2_INFO_RECT, RELIC3_INFO_RECT, RELIC4_INFO_RECT};
    for (size_t i = 0; i < tesseract_text_count; i++) {
        if (tesseract_texts[i]) {
            Rect r = rects[i];
            char text[256] = {0};

            RString item = get_item(tesseract_texts[i]);
            
            if (item) {
                float avg_price = get_item_price(item);
                if (avg_price < 0.0) {
                    continue;
                }
                
                olivec_text(oc, item, 0, ((OLIVEC_DEFAULT_FONT_HEIGHT * 3) + 5) * i, olivec_default_font, 3, 0xffffffff);
                
                olivec_sprite_copy(oc, r.x, r.y, r.h, r.h, plat);
                float set_price = get_set_price(item);
                sprintf(text, "%.2f", set_price);
                olivec_text(oc, text, r.x + r.h + 5, r.y - (r.h / 2), olivec_default_font, 3, 0xffffffff);
                
                sprintf(text, "%.2f", avg_price);
                olivec_text(oc, text, r.x + r.h + 5, r.y + (r.h / 2), olivec_default_font, 3, 0xffffffff);

                free_rstring(item);
            }

        }
    }

    // Its dumb you have to reattach the buffer but whatever
    // Why is there no wl_buffer_damage()
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, WIDTH, HEIGHT);
    wl_surface_commit(surface);
}

static const struct wl_callback_listener frame_listener;
void frame_callback(void* data, struct wl_callback* cb, uint32_t time) {
    if (prev_time != 0) {
        dt = (((float)time) - ((float)prev_time)) / 1000.0;
        draw();
    }
    prev_time = time;
    wl_callback_destroy(cb);
    cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, data);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_callback
};

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    init_thingy();

    tess_api = TessBaseAPICreate();
    if (TessBaseAPIInit2(tess_api, NULL, "eng", OEM_LSTM_ONLY) != 0) {
        printf("Could not initialize Tesseract.\n");
        return 1;
    }
    
    struct wl_display *display = wl_display_connect(NULL);
    assert(display);

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    if (wl_display_roundtrip(display) == -1) {
        return 1;
    }

    if (shm == NULL || compositor == NULL || layer_shell == NULL || screencopy_manager == NULL) {
        fprintf(stderr, "no wl_shm, wl_compositor, wlr_layer_shell or wlr_screencopy_manager support\n");
        return 1;
    }

    surface = wl_compositor_create_surface(compositor);
    struct wl_region* region = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(surface, region);
    wl_region_destroy(region);

    struct zwlr_layer_surface_v1* wlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "");
    zwlr_layer_surface_v1_set_size(wlr_layer_surface, 2560, 1440);
    zwlr_layer_surface_v1_set_anchor(wlr_layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_margin(wlr_layer_surface, 0, 0, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(wlr_layer_surface,  -1);
    zwlr_layer_surface_v1_set_exclusive_edge(wlr_layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_add_listener(wlr_layer_surface, &layer_surface_listener, NULL);

    struct wl_callback* cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);

    wl_surface_commit(surface);
    while (wl_display_dispatch(display) != -1 && !configured) {
        // This space intentionally left blank
    }

    ReturnThing thing = create_buffer(WIDTH, HEIGHT, WIDTH * sizeof(uint32_t), WL_SHM_FORMAT_ARGB8888, PROT_READ | PROT_WRITE, MAP_SHARED);
    assert(thing.buf);
    assert(thing.data);
    memset(thing.data, 128, WIDTH * HEIGHT * sizeof(uint32_t));

    buffer = thing.buf;

    oc = olivec_canvas(thing.data, WIDTH, HEIGHT, WIDTH);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);

    while (wl_display_dispatch(display) != -1 && running) {
        // This space intentionally left blank
    }

    zwlr_layer_shell_v1_destroy(layer_shell);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    wl_output_destroy(output_thing);
    TessBaseAPIDelete(tess_api);

    return 0;
}