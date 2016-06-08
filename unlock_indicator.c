/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "xinerama.h"

#define BUTTON_RADIUS 90
#define BUTTON_SPACE (BUTTON_RADIUS + 5)
#define BUTTON_CENTER (BUTTON_RADIUS + 5)
#define BUTTON_DIAMETER (2 * BUTTON_SPACE)

typedef struct ui_ctx {
    xcb_visualtype_t *vistype;
    xcb_screen_t *screen;
    cairo_surface_t *img;
    ui_opts_t opts;
} ui_ctx_t;


/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

/* The lock window. */
extern xcb_window_t win;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

static void mods_to_string(char *buf, size_t len, const modifiers_t *mods) {

    size_t index = 0;

    memset(buf, 0x00, len);
    if (mods->caps) {
        index += snprintf(&buf[index], len - index, "CAPS ");
    }
    if (mods->alt) {
        index += snprintf(&buf[index], len - index, "ALT ");
    }
    if (mods->num) {
        index += snprintf(&buf[index], len - index, "NUM ");
    }
    if (mods->logo) {
        index += snprintf(&buf[index], len - index, "WIN ");
    }
}

static ui_ctx_t ui_ctx;
ui_ctx_t *ui_initialize(const ui_opts_t *ui_opts) {
    ui_ctx.vistype = get_root_visual_type(screen);
    memcpy(&ui_ctx.opts, ui_opts, sizeof(struct ui_opts));

    return &ui_ctx;
}

void ui_draw_button(cairo_surface_t *canvas, const ui_opts_t *ui_opts, const status_t *status) {

    cairo_t *ctx = cairo_create(canvas);

    cairo_scale(ctx, status->dpi/96.0, status->dpi/96.0);
    /* Draw a (centered) circle with transparent background. */
    cairo_set_line_width(ctx, 10.0);
    cairo_arc(ctx,
              BUTTON_CENTER /* x */,
              BUTTON_CENTER /* y */,
              BUTTON_RADIUS /* radius */,
              0 /* start */,
              2 * M_PI /* end */);

    /* Use the appropriate color for the different PAM states
     * (currently verifying, wrong password, or default) */
    switch (status->pam_state) {
        case STATE_PAM_VERIFY:
            cairo_set_source_rgba(ctx, 0, 114.0 / 255, 255.0 / 255, 0.75);
            break;
        case STATE_PAM_WRONG:
            cairo_set_source_rgba(ctx, 250.0 / 255, 0, 0, 0.75);
            break;
        default:
            cairo_set_source_rgba(ctx, 0, 0, 0, 0.75);
            break;
    }
    cairo_fill_preserve(ctx);

    switch (status->pam_state) {
        case STATE_PAM_VERIFY:
            cairo_set_source_rgb(ctx, 51.0 / 255, 0, 250.0 / 255);
            break;
        case STATE_PAM_WRONG:
            cairo_set_source_rgb(ctx, 125.0 / 255, 51.0 / 255, 0);
            break;
        case STATE_PAM_IDLE:
            cairo_set_source_rgb(ctx, 51.0 / 255, 125.0 / 255, 0);
            break;
    }
    cairo_stroke(ctx);

    /* Draw an inner seperator line. */
    cairo_set_source_rgb(ctx, 0, 0, 0);
    cairo_set_line_width(ctx, 2.0);
    cairo_arc(ctx,
              BUTTON_CENTER /* x */,
              BUTTON_CENTER /* y */,
              BUTTON_RADIUS - 5 /* radius */,
              0,
              2 * M_PI);
    cairo_stroke(ctx);

    cairo_set_line_width(ctx, 10.0);

    /* Display a (centered) text of the current PAM state. */
    char *text = NULL;
    /* We don't want to show more than a 3-digit number. */
    char buf[4];

    cairo_set_source_rgb(ctx, 0, 0, 0);
    cairo_set_font_size(ctx, 28.0);
    switch (status->pam_state) {
        case STATE_PAM_VERIFY:
            text = "verifying…";
            break;
        case STATE_PAM_WRONG:
            text = "wrong!";
            break;
        default:
            if (ui_opts->show_failed_attempts && status->failed_attempts > 0) {
                if (status->failed_attempts > 999) {
                    text = "> 999";
                } else {
                    snprintf(buf, sizeof(buf), "%d", status->failed_attempts);
                    text = buf;
                }
                cairo_set_source_rgb(ctx, 1, 0, 0);
                cairo_set_font_size(ctx, 32.0);
            }
            break;
    }

    if (text) {
        cairo_text_extents_t extents;
        double x, y;

        cairo_text_extents(ctx, text, &extents);
        x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
        y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing);

        cairo_move_to(ctx, x, y);
        cairo_show_text(ctx, text);
        cairo_close_path(ctx);
    }

    if (status->pam_state == STATE_PAM_WRONG && 
            (status->modifiers.caps || status->modifiers.alt || status->modifiers.num || status->modifiers.logo)) {
        cairo_text_extents_t extents;
        double x, y;

        cairo_set_font_size(ctx, 14.0);

        char modifier_string[64];
        mods_to_string(modifier_string, sizeof(modifier_string), &status->modifiers);
        cairo_text_extents(ctx, modifier_string, &extents);
        x = BUTTON_CENTER - ((extents.width / 2) + extents.x_bearing);
        y = BUTTON_CENTER - ((extents.height / 2) + extents.y_bearing) + 28.0;

        cairo_move_to(ctx, x, y);
        cairo_show_text(ctx, modifier_string);
        cairo_close_path(ctx);
    }

    /* After the user pressed any valid key or the backspace key, we
     * highlight a random part of the unlock indicator to confirm this
     * keypress. */
    if (status->unlock_state == STATE_KEY_ACTIVE ||
        status->unlock_state == STATE_BACKSPACE_ACTIVE) {
        cairo_new_sub_path(ctx);
        double highlight_start = (rand() % (int)(2 * M_PI * 100)) / 100.0;
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  highlight_start,
                  highlight_start + (M_PI / 3.0));
        if (status->unlock_state == STATE_KEY_ACTIVE) {
            /* For normal keys, we use a lighter green. */
            cairo_set_source_rgb(ctx, 51.0 / 255, 219.0 / 255, 0);
        } else {
            /* For backspace, we use red. */
            cairo_set_source_rgb(ctx, 219.0 / 255, 51.0 / 255, 0);
        }
        cairo_stroke(ctx);

        /* Draw two little separators for the highlighted part of the
         * unlock indicator. */
        cairo_set_source_rgb(ctx, 0, 0, 0);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  highlight_start /* start */,
                  highlight_start + (M_PI / 128.0) /* end */);
        cairo_stroke(ctx);
        cairo_arc(ctx,
                  BUTTON_CENTER /* x */,
                  BUTTON_CENTER /* y */,
                  BUTTON_RADIUS /* radius */,
                  highlight_start + (M_PI / 3.0) /* start */,
                  (highlight_start + (M_PI / 3.0)) + (M_PI / 128.0) /* end */);
        cairo_stroke(ctx);
    }
    cairo_destroy(ctx);
}

void ui_draw_background(cairo_surface_t *canvas, const ui_opts_t *ui_opts, const status_t *status) {
    cairo_t *ctx = cairo_create(canvas);
    if (img) {
        if (!ui_opts->tile) {
            cairo_set_source_surface(ctx, img, 0, 0);
            cairo_paint(ctx);
        } else {
            /* create a pattern and fill a rectangle as big as the screen */
            cairo_pattern_t *pattern;
            pattern = cairo_pattern_create_for_surface(img);
            cairo_set_source(ctx, pattern);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_rectangle(ctx, 0, 0, status->resolution[0], status->resolution[1]);
            cairo_fill(ctx);
            cairo_pattern_destroy(pattern);
        }
    } else {
        char strgroups[3][3] = {{ui_opts->color[0], ui_opts->color[1], '\0'},
                                {ui_opts->color[2], ui_opts->color[3], '\0'},
                                {ui_opts->color[4], ui_opts->color[5], '\0'}};
        uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                             (strtol(strgroups[1], NULL, 16)),
                             (strtol(strgroups[2], NULL, 16))};
        cairo_set_source_rgb(ctx, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
        cairo_rectangle(ctx, 0, 0, status->resolution[0], status->resolution[1]);
        cairo_fill(ctx);
    }
    cairo_destroy(ctx);

}

void ui_compose(cairo_surface_t *canvas, cairo_surface_t *background, cairo_surface_t *button, const ui_opts_t *ui_opts, const status_t *status) {
    int button_diameter_physical = ceil(status->dpi/96.0 * BUTTON_DIAMETER);
    cairo_t *ctx = cairo_create(canvas);
    cairo_set_source_surface(ctx, background, 0, 0);
    cairo_paint(ctx);

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int x = (xr_resolutions[screen].x + ((xr_resolutions[screen].width / 2) - (button_diameter_physical / 2)));
            int y = (xr_resolutions[screen].y + ((xr_resolutions[screen].height / 2) - (button_diameter_physical / 2)));
            cairo_set_source_surface(ctx, button, x, y);
            cairo_rectangle(ctx, x, y, button_diameter_physical, button_diameter_physical);
            cairo_fill(ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int x = (status->resolution[0] / 2) - (button_diameter_physical / 2);
        int y = (status->resolution[1] / 2) - (button_diameter_physical / 2);
        cairo_set_source_surface(ctx, button, x, y);
        cairo_rectangle(ctx, x, y, button_diameter_physical, button_diameter_physical);
        cairo_fill(ctx);
    }
    cairo_destroy(ctx);
}

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(ui_ctx_t *ctx, const status_t *status) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;
    int button_diameter_physical = ceil(status->dpi/96.0 * BUTTON_DIAMETER);
    DEBUG("scaling_factor is %.f, physical diameter is %d px\n",
          status->dpi/96.0, button_diameter_physical);

    bg_pixmap = create_bg_pixmap(conn, screen, (uint32_t *)status->resolution, (char *)ctx->opts.color);
    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. */
    cairo_surface_t *button = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, button_diameter_physical, button_diameter_physical);

    cairo_surface_t *background = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, status->resolution[0], status->resolution[1]);

    ui_draw_background(background, &ctx->opts, status);

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, ctx->vistype, status->resolution[0], status->resolution[1]);


    if (ctx->opts.unlock_indicator &&
        (status->unlock_state >= STATE_KEY_PRESSED || status->pam_state > STATE_PAM_IDLE)) {
        ui_draw_button(button, &ctx->opts, status);
    }


    ui_compose(xcb_output, background, button, &ctx->opts, status);

    cairo_surface_destroy(xcb_output);
    cairo_surface_destroy(button);
    cairo_surface_destroy(background);
    return bg_pixmap;
}

/*
 * Calls draw_image on a new pixmap and swaps that with the current pixmap
 *
 */
void redraw_screen(ui_ctx_t *ctx, const status_t *status) {
    DEBUG("redraw_screen(unlock_state = %d, pam_state = %d)\n", status->unlock_state, status->pam_state);
    xcb_pixmap_t bg_pixmap = draw_image(ctx, status);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, status->resolution[0], status->resolution[1]);
    xcb_free_pixmap(conn, bg_pixmap);
    xcb_flush(conn);
}

