#include <SDL3/SDL.h>

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

  long long millis(void) {
      struct timeval tv;

      mingw_gettimeofday(&tv,NULL);
      return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
  }

#include <stdbool.h>

  typedef float      f32;
  typedef double     f64;
  typedef int32_t    i32;
  typedef int64_t    i64;
  typedef uint32_t   u32;
  typedef uint64_t   u64;
  typedef size_t   usize;
  typedef ssize_t  isize;

#define SCREEN_WIDTH 2160
#define SCREEN_HEIGHT 1440
#define GRID_SIZE 1
#define WINDOW_SCALE 1

  typedef struct v2  { f64 x, y; } v2;
  typedef struct v2i { i64 x, y; } v2i;

  static inline v2i v2_to_v2i (v2 v) {
    return (v2i){(i64)v.x, (i64)v.y};
  }

  static inline v2 scale_v2 (v2 v, f64 sf) {
    return (v2) {v.x * sf, v.y * sf};
  }

  static inline v2 translate_v2 (v2 v, v2 u) {
    return (v2) {v.x + u.x, v.y + u.y};
  }

  struct curve { // Quadratic Bezier
    v2 from;
    v2 to;
    v2 control;
  };

  struct shape {
    struct curve *curves;
    usize n;
  };

  struct object {
    struct shape *shape;
    f64 scale;
    u32 colour;
    struct v2 pos;

    struct {
      v2 min;
      v2 max;
    } bbox;
  };

  static void precalculate_bbox(struct object* o) {
    o->bbox.min = (v2) {INFINITY, INFINITY};
    o->bbox.max = (v2) {-INFINITY, -INFINITY};

    for (usize i = 0; i <= o->shape->n; i++) {
      const struct curve c = o->shape->curves[i];
      const v2 to      = translate_v2(scale_v2(c.to     , o->scale), o->pos),
               from    = translate_v2(scale_v2(c.from   , o->scale), o->pos),
               control = translate_v2(scale_v2(c.control, o->scale), o->pos);

      for (f64 t = 0; t <= 1; t += 0.01) {
        const v2 p = translate_v2(translate_v2(control, scale_v2(translate_v2(from, scale_v2(control, -1)),(1-t)*(1-t))), scale_v2(translate_v2(to, scale_v2(control, -1)),t*t));
        if (p.x > o->bbox.max.x)
          o->bbox.max.x = p.x;
        if (p.x < o->bbox.min.x)
          o->bbox.min.x = p.x;
        if (p.y > o->bbox.max.y)
          o->bbox.max.y = p.y;
        if (p.y < o->bbox.min.y)
          o->bbox.min.y = p.y;
      }
    }
  }

  struct {
    SDL_Window *window;
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    u32 *pixels;
    bool quit;

    struct {
      f64 scale;
      v2 pos;
      v2 pointer;
    } camera;

    struct {
      struct object *objects;
      usize n;
      usize max;
    } objects;
  } state;

  static inline v2 from_screen(v2i pos) {
    return (v2) {state.camera.pos.x + (pos.x - SCREEN_WIDTH  / 2) / state.camera.scale, 
                 state.camera.pos.y + (pos.y - SCREEN_HEIGHT / 2) / state.camera.scale};
  }

  static inline v2i to_screen (v2 pos) {
    return v2_to_v2i((v2){( pos.x - state.camera.pos.x) * state.camera.scale + SCREEN_WIDTH  / 2,
                          ( pos.y - state.camera.pos.y) * state.camera.scale + SCREEN_HEIGHT / 2});
  }

  static inline void update_pointer() {
    f32 x, y;
    SDL_GetMouseState(&x, &y);
    state.camera.pointer = from_screen((v2i){(i32)x, (i32)SCREEN_HEIGHT - y});
  }

  static inline bool point_in_screen(v2i v) {
    if (v.y < SCREEN_HEIGHT && v.x < SCREEN_WIDTH && v.y >= 0 && v.x >= 0)
      return true;
    return false;
  }

  static inline void draw_pixel(v2i v, u32 colour) {
    if (point_in_screen(v))
      state.pixels[v.y * SCREEN_WIDTH + v.x] = colour;
  } 

  // DDA (update to Bresenham for time save?)
  static inline void draw_line(v2i a, v2i b, u32 colour) {
    i32 dx = b.x - a.x, 
        dy = b.y - a.y;
    u32 steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    f32 xstep = (f32)dx / (f32)steps,
        ystep = (f32)dy / (f32)steps;
    f32 x = a.x, y = a.y;
    for (u32 i=0; i < steps; i++) {
      x += xstep;
      y += ystep;
      draw_pixel(v2_to_v2i((v2) {x, y}), colour);
    }
  }
  static inline void draw_curve(struct curve c, f64 scale, v2 translate, u32 colour) {
    const v2 to      = translate_v2(scale_v2(c.to     , scale), translate),
             from    = translate_v2(scale_v2(c.from   , scale), translate),
             control = translate_v2(scale_v2(c.control, scale), translate);
    for (f64 t = 0; t <= 1; t += .5 / state.camera.scale) {
      const v2 p = translate_v2(translate_v2(control, scale_v2(translate_v2(from, scale_v2(control, -1)),(1-t)*(1-t))), scale_v2(translate_v2(to, scale_v2(control, -1)),t*t));
      draw_pixel(to_screen(p), colour);
    }
  }

  static inline void draw_shape(struct shape s, f64 scale, v2 translate, u32 colour) {
    for (usize i = 0; i < s.n; i++) 
      draw_curve(s.curves[i], scale, translate, colour);
  }

  static inline bool object_in_screen(struct object o) {
    if (to_screen(translate_v2(o.pos, o.bbox.min)).x >= SCREEN_WIDTH || to_screen(translate_v2(o.pos, o.bbox.min)).y >= SCREEN_HEIGHT) 
      return false;
    if (to_screen(translate_v2(o.pos, o.bbox.max)).x  < 0            || to_screen(translate_v2(o.pos, o.bbox.max)).y  < 0)
      return false;
    return true;
  }

  static inline bool point_in_object(struct object o, v2 p) {
    if (p.x > translate_v2(o.pos, o.bbox.min).x && p.x < translate_v2(o.pos, o.bbox.max).x && p.y > translate_v2(o.pos, o.bbox.min).y && p.y < translate_v2(o.pos, o.bbox.max).y)
      return true;
    return false;
  }

  static inline void draw_object(struct object o) {
    if (object_in_screen(o))
      draw_shape(*o.shape, o.scale, o.pos, o.colour);
  }

  static void instance_object(struct object o) {
    if (state.objects.n == state.objects.max) {
      struct object *t = malloc(sizeof(struct object) * state.objects.max * 2);
      memcpy(t, state.objects.objects, sizeof(struct object) * state.objects.max);
      free(state.objects.objects);
      state.objects.objects = t;
      state.objects.max = state.objects.max * 2;
    }
    
    state.objects.objects[state.objects.n] = o;
    state.objects.n++;
  }

  static void destroy_object(usize n) {
    if (n >= state.objects.n)
      return;

    struct object *t = malloc(sizeof(struct object) * state.objects.max);
    memcpy(t, state.objects.objects, sizeof(struct object) * n);
    memcpy(&t[n], &state.objects.objects[n + 1], sizeof(struct object) * (state.objects.max - n - 1));
    free(state.objects.objects);
    state.objects.objects = t; 
    state.objects.n--;
  }

  static void render() {
    // Draw Grid 
    f64 grid_size = GRID_SIZE * (state.camera.scale);
    
    for (f64 x = fmod(state.camera.pos.x * -state.camera.scale + SCREEN_WIDTH / 2, grid_size); x < SCREEN_WIDTH; x += grid_size) {
       draw_line((v2i){(i32)x, 0}, (v2i){(i32)x, SCREEN_HEIGHT-1}, 0xFF757070);
    }
    for (f64 y = fmod(state.camera.pos.y * -state.camera.scale + SCREEN_HEIGHT / 2, grid_size); y < SCREEN_HEIGHT; y += grid_size) {
      draw_line((v2i){0, (i32)y}, (v2i){SCREEN_WIDTH-1, (i32)y}, 0xFF757070);
    }
    
    // Draw Pointer
    draw_line((v2i){to_screen(state.camera.pointer).x - 8, to_screen(state.camera.pointer).y    },
              (v2i){to_screen(state.camera.pointer).x + 8, to_screen(state.camera.pointer).y    }, 0xFF00FFFF);
    draw_line((v2i){to_screen(state.camera.pointer).x    , to_screen(state.camera.pointer).y - 8},
              (v2i){to_screen(state.camera.pointer).x    , to_screen(state.camera.pointer).y + 8}, 0xFF00FFFF);

    for (usize i = 0; i < state.objects.n; i++)
      draw_object(state.objects.objects[i]);
}

// https://wiki.libsdl.org/SDL3/Tutorials/FrontPage
static void init_SDL() {
  SDL_Init(SDL_INIT_VIDEO);
      
  state.window = SDL_CreateWindow("logic", SCREEN_WIDTH * WINDOW_SCALE, SCREEN_HEIGHT * WINDOW_SCALE, 0);
  if (state.window == NULL) {
    fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
    SDL_Quit();
    exit(1);
  }

  state.renderer = SDL_CreateRenderer(state.window, NULL);
  if (state.renderer == NULL) {
    fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
    SDL_DestroyWindow(state.window);
    SDL_Quit();
    exit(1);
  }

  state.texture = SDL_CreateTexture(state.renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

  if (state.texture == NULL) {
    fprintf(stderr, "SDL_CreateTexture Error: %s\n", SDL_GetError());
    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    SDL_Quit();
    exit(1);
  } 
}

static void fill_pixels(u32 colour) {
  for (usize i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
    state.pixels[i] = colour;
  }
}

i32 main(i32 argc, char* argv[]) {
  memset(&state, 0, sizeof(state));
  
  state.camera.scale = SCREEN_WIDTH / 32;

  init_SDL();
  SDL_HideCursor();
    
  state.pixels = (u32 *) malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(u32));
  
  state.objects.objects = malloc(sizeof(struct object) * 8);
  state.objects.max = 8;
  
  struct curve test_curves[6]   = {
                                    (struct curve){(v2){ 1,  0}, (v2){ 0,  1}, (v2){ 1,  1}}, 
                                    (struct curve){(v2){ 0,  1}, (v2){-1,  0}, (v2){-1,  1}}, 
                                    (struct curve){(v2){-1,  0}, (v2){ 0, -1}, (v2){-1, -1}}, 
                                    (struct curve){(v2){ 0, -1}, (v2){ 1,  0}, (v2){ 1, -1}},
                                    
                                    (struct curve){(v2){-0.75,-0.75}, (v2){0.75, 0.75}, {0, 0}},
                                    (struct curve){(v2){-0.75,0.75}, (v2){0.75, -0.75}, {0, 0}},
                                   };
  struct shape test_shape   = {test_curves, 6};
  struct object test_object = {&test_shape, 0.5, 0xFFA0A0A0, (v2){0, 0}};
  
  precalculate_bbox(&test_object);
  
  bool has_zoomed = false;

  long long t = millis();
  while (!state.quit) {
    char title[256];
    sprintf(title, "logic | framtime: %lldms", (millis()-t));
    SDL_SetWindowTitle(state.window, title);
    t = millis();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch(e.type) {
        case SDL_EVENT_MOUSE_MOTION:
          update_pointer();
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          switch(e.button.button) {
            case SDL_BUTTON_LEFT:
              test_object.pos = state.camera.pointer;
              instance_object(test_object);
              break;
            case SDL_BUTTON_RIGHT:
              for (usize i = 0; i < state.objects.n; i++) {
                if (point_in_object(state.objects.objects[i], state.camera.pointer)) {
                  destroy_object(i);
                  break;
                }
              }
              break;
          }
          break;
        case SDL_EVENT_QUIT:
          state.quit = true;
          break;
      }
    }
    
    const bool *keystate = SDL_GetKeyboardState(NULL);

    if (keystate[SDL_SCANCODE_MINUS] && !has_zoomed)
      state.camera.scale *= 0.9;
    if (keystate[SDL_SCANCODE_EQUALS] && !has_zoomed)
      state.camera.scale /= 0.9;
    
    has_zoomed = false;
    if (keystate[SDL_SCANCODE_MINUS] || keystate[SDL_SCANCODE_EQUALS]) {
      has_zoomed = true;
      update_pointer();
    }

    if (keystate[SDL_SCANCODE_LEFT]) {
      state.camera.pos.x -= 1 / state.camera.scale;
      update_pointer();
    }
    if (keystate[SDL_SCANCODE_RIGHT]) {
      state.camera.pos.x += 1 / state.camera.scale;
      update_pointer();
    }
    if (keystate[SDL_SCANCODE_DOWN]) { 
      state.camera.pos.y -= 1 / state.camera.scale;
      update_pointer();
    }
    if (keystate[SDL_SCANCODE_UP]) {
      state.camera.pos.y += 1 / state.camera.scale;
      update_pointer();
    } 
    
    fill_pixels(0xFF282011);
    render();

    SDL_UpdateTexture(state.texture, NULL, state.pixels, SCREEN_WIDTH * 4);
    SDL_RenderTextureRotated(state.renderer, state.texture, NULL, NULL, 0.0, NULL, SDL_FLIP_VERTICAL);

    SDL_RenderPresent(state.renderer);
  }

  free(state.pixels);
  free(state.objects.objects);

  SDL_DestroyTexture(state.texture);
  SDL_DestroyRenderer(state.renderer);
  SDL_DestroyWindow(state.window);
  SDL_Quit();
  return 0;
}
