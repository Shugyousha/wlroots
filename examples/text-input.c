#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>
#include <wlr/render/egl.h>

#include "text-input-unstable-v3-client-protocol.h"
#include "ui.h"
#include "shm.h"

#include <linux/input-event-codes.h>

/**
 * Usage: text-input [seconds]
 * Creates a xdg-toplevel using the text-input protocol.
 * It will be solid green when it has no text input focus, green-yellow	when it
 * has focus, and solid yellow when it was notified that the focused moved away
 * but still didn't give up the text input ability.
 * Time is optional and defines the delay between getting notified of lost
 * focus and releasing text input.
 */

int main(int argc, char **argv) {
	struct wayland_t *ui = init_ui();
	if (!ui) {
		fprintf(stderr, "could not create client\n");
		return EXIT_FAILURE;
	}

	int ret = 0;
	while(ret != -1) {
		ui_redraw(ui);
		ret = wl_display_dispatch(ui->display);
	}

	exit_ui(ui);

	return EXIT_SUCCESS;
}
