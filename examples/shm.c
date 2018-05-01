#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>


#include <fcntl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <cairo/cairo.h>

#include "util.h"
#include "ui.h"
#include "draw.h"
#include "shm.h"

#define SURFACE_OPAQUE 0x01
#define SURFACE_SHM    0x02

#define SURFACE_HINT_RESIZE 0x10

#define SURFACE_HINT_RGB565 0x100

struct shm_pool {
	struct wl_shm_pool *pool;
	size_t size;
	size_t used;
	void *data;
};

struct shm_surface_data {
	struct wl_buffer *buffer;
	struct shm_pool *pool;
};

struct window {
	struct rectangle rectangle;
	struct wayland_t *ui;
	struct wl_surface *surface;
	cairo_surface_t *cairo_surface;
};

static const cairo_user_data_key_t shm_surface_data_key;

struct wl_buffer *
get_buffer_from_cairo_surface(cairo_surface_t *surface)
{
	struct shm_surface_data *data;

	data = cairo_surface_get_user_data(surface, &shm_surface_data_key);

	return data->buffer;
}

static void
shm_pool_destroy(struct shm_pool *pool);

static void
shm_surface_data_destroy(void *p)
{
	struct shm_surface_data *data = p;
	wl_buffer_destroy(data->buffer);
	if (data->pool)
		shm_pool_destroy(data->pool);

	free(data);
}

static struct wl_shm_pool *
make_shm_pool(struct wl_shm *shm, int size, void **data)
{
	struct wl_shm_pool *pool;
	int fd;

	fd = os_create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
			size);
		return NULL;
	}

	*data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (*data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	pool = wl_shm_create_pool(shm, fd, size);

	close(fd);

	return pool;
}

static struct shm_pool *
shm_pool_create(struct wl_shm *shm, size_t size)
{
	struct shm_pool *pool = malloc(sizeof *pool);

	if (!pool)
		return NULL;

	pool->pool = make_shm_pool(shm, size, &pool->data);
	if (!pool->pool) {
		free(pool);
		return NULL;
	}

	pool->size = size;
	pool->used = 0;

	return pool;
}

static void *
shm_pool_allocate(struct shm_pool *pool, size_t size, int *offset)
{
	if (pool->used + size > pool->size)
		return NULL;

	*offset = pool->used;
	pool->used += size;

	return (char *) pool->data + *offset;
}

/* destroy the pool. this does not unmap the memory though */
static void
shm_pool_destroy(struct shm_pool *pool)
{
	munmap(pool->data, pool->size);
	wl_shm_pool_destroy(pool->pool);
	free(pool);
}

static int
data_length_for_shm_surface(struct rectangle *rect)
{
	int stride;

	stride = cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32,
						rect->width);
	return stride * rect->height;
}

static cairo_surface_t *
create_shm_surface_from_pool(void *none,
							struct rectangle *rectangle,
							struct shm_pool *pool)
{
	struct shm_surface_data *data;
	uint32_t format;
	cairo_surface_t *surface;
	cairo_format_t cairo_format;
	int stride, length, offset;
	void *map;

	data = malloc(sizeof *data);
	if (data == NULL)
		return NULL;

	cairo_format = CAIRO_FORMAT_ARGB32; /*or CAIRO_FORMAT_RGB16_565 who knows??*/

	stride = cairo_format_stride_for_width (cairo_format, rectangle->width);
	length = stride * rectangle->height;
	data->pool = NULL;
	map = shm_pool_allocate(pool, length, &offset);

	if (!map) {
		free(data);
		return NULL;
	}

	surface = cairo_image_surface_create_for_data (map,
							cairo_format,
							rectangle->width,
							rectangle->height,
							stride);

	cairo_surface_set_user_data(surface, &shm_surface_data_key,
				    data, shm_surface_data_destroy);

	format = WL_SHM_FORMAT_ARGB8888; /*or WL_SHM_FORMAT_RGB565*/
	
	data->buffer = wl_shm_pool_create_buffer(pool->pool, offset,
							rectangle->width,
							rectangle->height,
							stride, format);

	return surface;
}

struct window *
window_create(struct wayland_t *ui, int width, int height){
	struct window *window;
	struct shm_surface_data *data;
	struct shm_pool *pool;

	window = malloc(sizeof *window);

	window->rectangle.x = 0;
	window->rectangle.y = 0;
	window->rectangle.width = width;
	window->rectangle.height = height;

	window->ui = ui;
	window->surface = ui->surface;

	pool = shm_pool_create(ui->shm,
			       data_length_for_shm_surface(&window->rectangle));
	if (!pool)
		return NULL;

	window->cairo_surface =
		create_shm_surface_from_pool(ui->shm, &window->rectangle, pool);

	if (!window->cairo_surface) {
		shm_pool_destroy(pool);
		return NULL;
	}

	/* make sure we destroy the pool when the surface is destroyed */
	data = cairo_surface_get_user_data(window->cairo_surface, &shm_surface_data_key);
	data->pool = pool;

	return window;
}

void
window_destroy(struct window *window){
	cairo_surface_destroy(window->cairo_surface);
	free(window);
}

void
window_resize(struct window *window, int width, int height){
	struct wayland_t *ui = window->ui;
	window_destroy(window);
	window = window_create(ui, width, height);
}

void
window_redraw(struct window *window){
	paint_surface(window->cairo_surface,window->ui);
	wl_surface_attach(window->surface,get_buffer_from_cairo_surface(window->cairo_surface),0,0);
	/* repaint all the pixels in the surface, change size to only repaint changed area*/
	wl_surface_damage(window->surface, window->rectangle.x, 
							window->rectangle.y, 
							window->rectangle.width, 
							window->rectangle.height);
	wl_surface_commit(window->surface);
}

void
window_get_width_height(struct window *window, int *w, int *h){
	*w = window->rectangle.width;
	*h = window->rectangle.height;
}
