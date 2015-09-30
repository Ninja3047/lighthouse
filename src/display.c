/** @file display.c
 *  @author Bram Wasti <bwasti@cmu.edu>
 *
 *  @brief This file contains the logic that draws to the screen.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wordexp.h>

#include "display.h"
#include "globals.h"

#define min(a,b) ((a) < (b) ? (a) : (b))

/* @brief Type used to pass around x,y offsets. */
typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t image_y;
} offset_t;

/* @brief Returns the offset for a line of text.
 *
 * @param line the index of the line to be drawn (counting from the top).
 * @return the line's offset.
 */
static inline offset_t calculate_line_offset(uint32_t line) {
  offset_t result;
  result.x  = settings.horiz_padding;
  result.y  = settings.height * line;
  result.image_y = result.y;
  result.y +=  + global.real_font_size;
  /* To draw a picture cairo need to know the top left corner
   * position. But to draw a text cairo need to know the bottom
   * left corner position.
   */

  return result;
}

/* @brief Draw a line of text with a cursor to a cairo context.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param text The text to be drawn.
 * @param line The index of the line to be drawn (counting from the top).
 * @param cursor The index of the cursor into the text to be drawn.
 * @param foreground The color of the text.
 * @param background The color of the background.
 * @return Void.
 */
static void draw_typed_line(cairo_t *cr, char *text, uint32_t line, uint32_t cursor, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);

  /* Set the background. */
  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  cairo_rectangle(cr, 0, line * settings.height, settings.width, (line + 1) * settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);

  /* Set the foreground color and font. */
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, settings.font_name, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  offset_t offset = calculate_line_offset(line);
  /* Find the cursor relative to the text. */
  cairo_text_extents_t extents;
  char saved_char = text[cursor];
  text[cursor] = '\0';
  cairo_text_extents(cr, text, &extents);
  text[cursor] = saved_char;
  int32_t cursor_x = extents.x_advance;

  /* Find the text offset. */
  cairo_text_extents(cr, text, &extents);
  if (settings.width < extents.width) {
    offset.x = settings.width - extents.x_advance;
  }

  cursor_x += offset.x;

  /* if the cursor would be off the back end, set its position to 0 and scroll text instead */
  if(cursor_x < 0) {
      offset.x -= (cursor_x-3);
      cursor_x = 0;
  }

  /* Draw the text. */
  cairo_move_to(cr, offset.x, offset.y);
  cairo_set_font_size(cr, settings.font_size);
  cairo_show_text(cr, text);

  /* Draw the cursor. */
  if (settings.cursor_is_underline) {
    cairo_show_text(cr, "_");
  } else {
    uint32_t cursor_y = offset.y - settings.font_size - settings.cursor_padding;
    cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
    cairo_rectangle(cr, cursor_x + 2, cursor_y, 0, settings.font_size + (settings.cursor_padding * 2));
    cairo_stroke_preserve(cr);
    cairo_fill(cr);
  }

  pthread_mutex_unlock(&global.draw_mutex);
}

#ifndef NO_PANGO
/* @brief Draw text at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param charac Characteristic of the text to be drawn.
 * @param line_width Width of the current line (depend of desc or line) that
 *      can still be used.
 * @param *offset Pointer to offset, because some modifiers will modify directly
 *      the offset.
 * @param foreground The color of the text.
 * @param font_description pango font description provide info on the font.
 * @return The advance in the x direction.
 */
static uint32_t draw_text(cairo_t *cr, draw_t *charac, uint32_t line_width, offset_t *offset, color_t *foreground, PangoFontDescription *font_description) {
  /* Checking every previous modifier and setting up the parameter. */
  for (uint32_t i=0; i < charac->modifiers_array_length; i++) {
      switch ((charac->modifiers_array)[i]) {
          case CENTER:
              offset->x += (line_width - charac->data_length) / 2;
              break;
          case BOLD:
              pango_font_description_set_weight(font_description, PANGO_WEIGHT_BOLD);
              break;
          case NONE:
          default:
              break;
      }
  }

  /* Drawing the text */
  PangoLayout *layout;
  layout = pango_cairo_create_layout(cr);
  pango_layout_set_font_description(layout, font_description);
  pango_layout_set_text (layout, charac->data, -1);

  cairo_move_to(cr, offset->x, offset->y);
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  pango_cairo_update_layout(cr, layout);

  int height;
  int width;
  pango_layout_get_pixel_size(layout, &width, &height);

  pango_cairo_show_layout_line(cr, pango_layout_get_line (layout, 0));

  g_object_unref(layout);

  return width;
}
#else
/* @brief Draw text at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param charac Characteristic of the text to be drawn.
 * @param line_width Width of the current line (depend of desc or line) that
 *      can still be used.
 * @param *offset Pointer to offset, because some modifiers will modify directly
 *      the offset.
 * @param foreground The color of the text.
 * @return The advance in the x direction.
 */
static uint32_t draw_text(cairo_t *cr, draw_t *charac, uint32_t line_width, offset_t *offset, color_t *foreground, uint32_t font_size) {
  /* Checking every previous modifier and setting up the parameter. */
  cairo_font_weight_t weight = CAIRO_FONT_WEIGHT_NORMAL;
  for (uint32_t i=0; i < charac->modifiers_array_length; i++) {
      switch ((charac->modifiers_array)[i]) {
          case CENTER: ;
              uint32_t tmp = offset->x;
              offset->x += (line_width - tmp - charac->data_length) / 2;
              break;
          case BOLD:
              weight = CAIRO_FONT_WEIGHT_BOLD;
              break;
          case NONE:
          default:
              break;
      }
  }

  cairo_text_extents_t extents;
  cairo_text_extents(cr, charac->data, &extents);
  cairo_move_to(cr, offset->x, offset->y);
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, settings.font_name, CAIRO_FONT_SLANT_NORMAL, weight);
  cairo_set_font_size(cr, font_size);
  cairo_show_text(cr, charac->data);
  return extents.x_advance;
}
#endif

#ifndef NO_GDK
/* @brief Return the new format for a picture to fit in a window.
 *
 * @param Current offset in the line/desc, used to know the image position.
 * @param width Width of the picture.
 * @param height Height of the picture.
 * @param win_size_x Width of the window.
 * @param win_size_y Height of the window.
 *
 * @return The advance in the x and y direction.
 */
static inline void get_new_size(uint32_t width, uint32_t height, uint32_t win_size_x, uint32_t win_size_y, image_format_t *format) {
  if (width > win_size_x || height > win_size_y) {
      /* Formatting only the big picture. */
      float prop = min((float)win_size_x / width,
              (float)win_size_y / height);
      /* Finding the best proportion to fit the picture. */
      format->width = prop * width;
      format->height = prop * height;

      debug("Resizing the image to %ix%i (prop = %f)\n", format->width, format->height, prop);
  }
}

/* @brief Draw an image at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param charac Characteristics of the image
 *      (will contain the option (center,...) and filename)
 * @param Current offset in the line/desc, used to know the image position.
 * @param win_size_x Width of the drawable part of the window.
 * @param win_size_y Height of the drawable part of the window.
 * @param format a reference to a image_format_t to change directly the values.
 * @return The advance in the x direction.
 */
static void draw_image_with_gdk(cairo_t *cr, draw_t *charac, offset_t *offset, uint32_t win_size_x, uint32_t win_size_y, image_format_t *format) {
  GdkPixbuf *image;
  GError *error = NULL;

  image = gdk_pixbuf_new_from_file(charac->data, &error);
  if (error != NULL) {
      debug("Image opening failed (tried to open %s): %s\n", charac->data, error->message);
      g_error_free(error);
      return ;
  }

  get_new_size(gdk_pixbuf_get_width(image), gdk_pixbuf_get_height(image), win_size_x, win_size_y, format);

  /* Resizing */
  GdkPixbuf *resize = gdk_pixbuf_scale_simple(image, format->width, format->height, GDK_INTERP_BILINEAR);

  /* Checking every previous modifier and setting up the parameter. */
  for (uint32_t i=0; i < charac->modifiers_array_length; i++) {
      switch ((charac->modifiers_array)[i]) {
          case CENTER:
              offset->x += (win_size_x - format->width) / 2;
              break;
          case BOLD:
          case NONE:
          default:
              break;
      }
  }

  gdk_cairo_set_source_pixbuf(cr, resize, offset->x, offset->image_y);
  cairo_paint (cr);
}
#else

/* @brief Resize an image.
 *
 * @param *surface The image to resize.
 * @param width The width of the current image.
 * @param height The height of the current image.
 * @param new_width The width of the image when resized.
 * @param new_height The height of the image when resized.
 */
cairo_surface_t * scale_surface (cairo_surface_t *surface, int width, int height,
                int new_width, int new_height) {
  cairo_surface_t *new_surface = cairo_surface_create_similar(surface,
                    CAIRO_CONTENT_COLOR_ALPHA, new_width, new_height);
  cairo_t *cr = cairo_create (new_surface);

  cairo_scale (cr, (double)new_width / width, (double)new_height / height);
  cairo_set_source_surface (cr, surface, 0, 0);

  cairo_pattern_set_extend (cairo_get_source(cr), CAIRO_EXTEND_REFLECT);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

  cairo_paint (cr);

  cairo_destroy (cr);

  return new_surface;
}

/* @brief Draw a png at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param charac Characteristics of the image
 *      (will contain the option (center,...) and filename)
 * @param Current offset in the line/desc, used to know the image position.
 * @param win_size_x Width of the drawable part of the window.
 * @param win_size_y Height of the drawable part of the window.
 * @param format a reference to a image_format_t.
 * @return The advance in the x direction.
 */
static void draw_png(cairo_t *cr, draw_t *charac, offset_t *offset, uint32_t win_size_x, uint32_t win_size_y, image_format_t *format) {
  cairo_surface_t *img;
  img = cairo_image_surface_create_from_png(charac->data);
  format->width = cairo_image_surface_get_width(img);
  format->height = cairo_image_surface_get_height(img);

  /* Checking every previous modifier and setting up the parameter. */
  for (uint32_t i=0; i < charac->modifiers_array_length; i++) {
      switch ((charac->modifiers_array)[i]) {
          case CENTER:
              offset->x += (win_size_x - format->width) / 2;
              break;
          case BOLD:
          case NONE:
          default:
              break;
      }
  }

  if (format->width > win_size_x || format->height > win_size_y) {
      /* Formatting only the big picture. */
      float prop = min((float)win_size_x / (*format).width,
              (float)win_size_y / (*format).height);
      /* Finding the best proportion to fit the picture. */
      image_format_t new_format;
      new_format.width = prop * format->width;
      new_format.height = prop * format->height;

      img = scale_surface(img, format->width, format->height,
              new_format.width, new_format.height);
      *format = new_format;
      debug("Resizing the image to %ix%i (prop = %f)\n", format->width, format->height, prop);
  }

  cairo_set_source_surface(cr, img, offset->x, offset->image_y);
  cairo_mask_surface(cr, img, offset->x, offset->image_y);
}
#endif

/* @brief Draw an image at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param charac Characteristics of the image to pass in the image draw function
 *      (will contain the option (center,...) and filename).
 * @param Current offset in the line/desc, used to know the image position.
 * @param win_size_x Width of the drawable part of the window.
 * @param win_size_y Height of the drawable part of the window.
 * @return The advance in the x direction.
 */
static image_format_t draw_image(cairo_t *cr, draw_t *charac, offset_t offset, uint32_t win_size_x, uint32_t win_size_y) {
  wordexp_t expanded_file;
  image_format_t format = {0, 0};

  if (wordexp(charac->data, &expanded_file, 0)) {
    fprintf(stderr, "Error expanding file %s\n", charac->data);
  } else {
    charac->data = expanded_file.we_wordv[0];
  }

  if (access(charac->data, F_OK) == -1) {
    fprintf(stderr, "Cannot open image file %s\n", charac->data);
    format.width = 0;
    format.height = 0;
    return format;
  }

  FILE *picture = fopen(charac->data, "r");
  switch (fgetc(picture)) {
    /* https://en.wikipedia.org/wiki/Magic_number_%28programming%29#Magic_numbers_in_files */
#ifndef NO_GDK
    case 137:
        debug("PNG found\n");
        draw_image_with_gdk(cr, charac, &offset, win_size_x, win_size_y, &format);
        break;
    case 255:
        debug("JPEG found\n");
        draw_image_with_gdk(cr, charac, &offset, win_size_x, win_size_y, &format);
        break;
    case 47:
        debug("GIF found\n");
        draw_image_with_gdk(cr, charac, &offset, win_size_x, win_size_y, &format);
        break;
#else
    case 137:
        draw_png(cr, charac, &offset, win_size_x, win_size_y, &format);
        break;
#endif
    default:
        debug("Unknown image format found: %s\n", charac->data);
        break;
  }
  fclose(picture);
  return format;
}

/* @brief Draw a line of text to a cairo context.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param text The text to be drawn.
 * @param line The index of the line to be drawn (counting from the top).
 * @param foreground The color of the text.
 * @param background The color of the background.
 * @return Void.
 */
static void draw_line(cairo_t *cr, const char *text, uint32_t line, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);

  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  /* Add 2 offset to height to prevent flickery drawing over the typed text.
   * TODO: Use better math all around. */
  cairo_rectangle(cr, 0, line * settings.height + 2, settings.width, (line + 1) * settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  offset_t offset = calculate_line_offset(line);

#ifndef NO_PANGO
  PangoFontDescription *font_description;
  font_description = pango_font_description_new();
  pango_font_description_set_family(font_description, settings.font_name);
  pango_font_description_set_absolute_size(font_description, settings.font_size * PANGO_SCALE);
#endif

  modifier_type_t *modifiers_array = malloc(0);
  /* Setting up the pointer that need to be used to stock the modifiers type.
   * This one is created in this function for the ease of use:
   *   It will only be freed when the entire string is parsed.
   */

  /* Parse the result line as we draw it. */
  char *c = (char *)text;
  while (c && *c != '\0') {
#ifndef NO_PANGO
    draw_t d = parse_result_line(cr, &c, settings.width - offset.x, &modifiers_array, font_description);
#else
    draw_t d = parse_result_line(cr, &c, settings.width - offset.x, &modifiers_array);
#endif
    /* Checking if there are still char to draw. */ // TODO
    if (d.data == NULL)
        break;

    char saved = *c;
    *c = '\0';
    /* Checking for the type of "d" and then if it's relevant apply the modfiers. */
    switch (d.type) {
        case DRAW_LINE:
        case NEW_LINE:
            break;
        case DRAW_IMAGE:
            offset.x += draw_image(cr, &d, offset, settings.width - offset.x, settings.height).width;
            break;
        case DRAW_TEXT:
        default:
#ifndef NO_PANGO
            pango_font_description_set_weight(font_description, PANGO_WEIGHT_NORMAL);
            offset.x += draw_text(cr, &d, settings.width - offset.x, &offset, foreground, font_description);
#else
            offset.x += draw_text(cr, &d, settings.width - offset.x, &offset, foreground, settings.font_size);
#endif
            break;
    }
    *c = saved;
  }
#ifndef NO_PANGO
  pango_font_description_free (font_description);
#endif
  free(modifiers_array);
  pthread_mutex_unlock(&global.draw_mutex);
}

/* @brief Draw a description to a cairo context.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param text The description to be drawn.
 * @param foreground The color of the text.
 * @param background The color of the background.
 * @return Void.
 */
static void draw_desc(cairo_t *cr, const char *text, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);
  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  uint32_t desc_height = settings.height*(global.result_count+1);
  cairo_rectangle(cr, settings.width + 2, 0,
          settings.width+settings.desc_size, desc_height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  offset_t offset = {settings.width + 2, global.real_desc_font_size, 0};

#ifndef NO_PANGO
  PangoFontDescription *font_description;
  font_description = pango_font_description_new();
  pango_font_description_set_family(font_description, settings.font_name);
  pango_font_description_set_weight(font_description, PANGO_WEIGHT_NORMAL);
  pango_font_description_set_absolute_size(font_description, settings.desc_font_size * PANGO_SCALE);
#endif

  modifier_type_t *modifiers_array = malloc(0);
  /* Setting up the pointer that need to be used to stock the modifiers type.
   * This one is created in this function for the ease of use:
   *   It will only be freed when the entire string is parsed.
   */

  /* Parse the result line as we draw it. */
  char *c = (char *)text;
  while (c && *c != '\0') {
#ifndef NO_PANGO
    draw_t d = parse_result_line(cr, &c, settings.desc_size + settings.width - offset.x, &modifiers_array, font_description);
#else
    draw_t d = parse_result_line(cr, &c, settings.width - offset.x, &modifiers_array);
#endif
    char saved = *c;
    *c = '\0';

    /* Checking for the type of "d" and then if it's relevant apply the modfiers. */
    switch (d.type) {
      case DRAW_IMAGE: ;
        image_format_t format;
        format = draw_image(cr, &d, offset, settings.desc_size - (offset.x - settings.width), desc_height - offset.image_y);
        offset.image_y += format.height;
        offset.y = offset.image_y;
        offset.x += format.width;
        /* We set the offset.y and x next to the picture so the user can choose to
         * return to the next line or not.
         */
        break;
      case DRAW_LINE:
        offset.y += (global.real_desc_font_size / 2);
        offset.x = settings.width;
        cairo_set_source_rgb(cr, settings.result_bg.r, settings.result_bg.g, settings.result_bg.b);
        cairo_move_to(cr, offset.x + settings.line_gap, offset.y);
        cairo_line_to(cr, offset.x + settings.desc_size - settings.line_gap, offset.y);
        cairo_stroke(cr);
        offset.y += global.real_desc_font_size;
        offset.image_y += 2 * global.real_desc_font_size;
        break;
      case NEW_LINE:
        offset.x = settings.width;
        offset.y += global.real_desc_font_size;
        offset.image_y += global.real_desc_font_size;
        break;
      case DRAW_TEXT:
      default:
#ifndef NO_PANGO
        pango_font_description_set_weight(font_description, PANGO_WEIGHT_NORMAL);
        offset.x += draw_text(cr, &d, settings.width + settings.desc_size - offset.x, &offset, foreground, font_description);
#else
        offset.x += draw_text(cr, &d, settings.width + settings.desc_size - offset.x, &offset, foreground, settings.font_size);
#endif
        break;
    }
    *c = saved;
    if ((offset.x + settings.desc_font_size) > (settings.width + settings.desc_size)) {
        /* Checking if it's gonna write out of the square space. */
        offset.x = settings.width;
        offset.y += global.real_desc_font_size;
        offset.image_y += global.real_desc_font_size;
    }
  }
#ifndef NO_PANGO
  pango_font_description_free (font_description);
#endif
  free(modifiers_array);
  pthread_mutex_unlock(&global.draw_mutex);
}

void draw_query_text(cairo_t *cr, cairo_surface_t *surface, const char *text, uint32_t cursor) {
  draw_typed_line(cr, (char *)text, 0, cursor, &settings.query_fg, &settings.query_bg);
  cairo_surface_flush(surface);
}

void draw_result_text(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, result_t *results) {
  int32_t line, index;
  if (global.result_count - 1 < global.result_highlight) {
    global.result_highlight = global.result_count - 1;
  }

  uint32_t max_results = settings.max_height / settings.height - 1;
  uint32_t display_results = min(global.result_count, max_results);
  /* Set the offset. */
  if (global.result_count <= max_results) {
      /* Sometime you use an offset from a previous query and then you send another query
       * to your script but get only 2 response, you need to reset the offset to 0
       */
      global.result_offset = 0;
  } else if (global.result_count - max_results < global.result_offset) {
      /* When we need to adjust the offset. */
      global.result_offset = global.result_count - max_results;
  } else if ((global.result_offset + display_results) < (global.result_highlight + 1)) {
      /* Change the offset to match the highlight when scrolling down. */
      global.result_offset = global.result_highlight - (display_results - 1);
      display_results = global.result_count - global.result_offset;
  } else if (global.result_offset > global.result_highlight) {
      /* Used when scrolling up. */
      global.result_offset = global.result_highlight;
  }

  if ((global.result_highlight < global.result_count) &&
          results[global.result_highlight].desc) {
      if (settings.auto_center) {
        uint32_t values[] = { global.win_x_pos_with_desc, global.win_y_pos };
        xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
      }

      uint32_t new_height = min(settings.height * (global.result_count + 1), settings.max_height);
      uint32_t values[] = { settings.width+settings.desc_size, new_height };
      xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
      cairo_xcb_surface_set_size(surface, settings.width + settings.desc_size, new_height);
      draw_desc(cr, results[global.result_highlight].desc, &settings.highlight_fg, &settings.highlight_bg);
  } else {
      if (settings.auto_center) {
        uint32_t values[] = { global.win_x_pos, global.win_y_pos };
        xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
      }

      uint32_t new_height = min(settings.height * (global.result_count + 1), settings.max_height);
      uint32_t values[] = { settings.width, new_height };
      xcb_configure_window (connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
      cairo_xcb_surface_set_size(surface, settings.width, new_height);
  }

  for (index = global.result_offset, line = 1; index < global.result_offset + display_results; index++, line++) {
    if (!(results[index].action)) {
      /* Title */
      draw_line(cr, results[index].text, line, &settings.result_fg, &settings.result_bg);
      /* TODO Add options for titles. */
    } else if (index != global.result_highlight) {
      draw_line(cr, results[index].text, line, &settings.result_fg, &settings.result_bg);
    } else {
      draw_line(cr, results[index].text, line, &settings.highlight_fg, &settings.highlight_bg);
    }
  }
  cairo_surface_flush(surface);
  xcb_flush(connection);
}

void redraw_all(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, char *query_string, uint32_t query_cursor_index) {
  draw_query_text(cr, surface, query_string, query_cursor_index);
  draw_result_text(connection, window, cr, surface, global.results);
}

