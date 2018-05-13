#include <cairo/cairo.h>
#include "ui.h"

int nredraw = 0;

void
paint_surface(cairo_surface_t *cairo_surface, struct wayland_t *ui){
	cairo_t *cr;
	cr = cairo_create(cairo_surface);

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

	// set up font
	cairo_select_font_face(cr, "serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 12);

	cairo_paint(cr);

	cairo_set_source_rgba(cr, 0, 1, 0, 1);
	cairo_rectangle(cr, 0, 0, nredraw, 3);
	if (nredraw++ > ui->width) {
		nredraw = 0;
	}

	cairo_move_to(cr, ui->width / 2, ui->height / 2);

	cairo_show_text(cr, "fudi");

	cairo_fill(cr);
	cairo_destroy(cr);
}
