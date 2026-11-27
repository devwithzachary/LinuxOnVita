// SPDX-License-Identifier: GPL-2.0-only
/*
 * vita-drm-test.c - Hardware KMS VSync & Tear-Free Page-Flip Validator
 *
 * Zero-libdrm userspace test utility for /dev/dri/card0 (vita-drm):
 *   - Queries DRM version, connector, encoder, CRTC, and preferred mode.
 *   - Allocates two XRGB8888 scanout buffers in CDRAM via DUMB_CREATE.
 *   - Runs N hardware-VSynced page flips (DRM_MODE_PAGE_FLIP_EVENT) with a
 *     moving vertical tear-test bar over R/G/B bands and reports FPS/jitter.
 *   - Restores the original fbdev console CRTC state cleanly on exit.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Self-contained Linux DRM/KMS UAPI definitions */
#define DRM_IOCTL_BASE			'd'
#define DRM_IO(nr)			_IO(DRM_IOCTL_BASE, nr)
#define DRM_IOWR(nr, type)		_IOWR(DRM_IOCTL_BASE, nr, type)

#define DRM_DISPLAY_MODE_LEN		32
#define DRM_MODE_PAGE_FLIP_EVENT	0x01

struct drm_version {
	int version_major;
	int version_minor;
	int version_patchlevel;
	size_t name_len;
	char *name;
	size_t date_len;
	char *date;
	size_t desc_len;
	char *desc;
};

struct drm_mode_modeinfo {
	uint32_t clock;
	uint16_t hdisplay;
	uint16_t hsync_start;
	uint16_t hsync_end;
	uint16_t htotal;
	uint16_t hskew;
	uint16_t vdisplay;
	uint16_t vsync_start;
	uint16_t vsync_end;
	uint16_t vtotal;
	uint16_t vscan;
	uint32_t vrefresh;
	uint32_t flags;
	uint32_t type;
	char name[DRM_DISPLAY_MODE_LEN];
};

struct drm_mode_card_res {
	uint64_t fb_id_ptr;
	uint64_t crtc_id_ptr;
	uint64_t connector_id_ptr;
	uint64_t encoder_id_ptr;
	uint32_t count_fbs;
	uint32_t count_crtcs;
	uint32_t count_connectors;
	uint32_t count_encoders;
	uint32_t min_width;
	uint32_t max_width;
	uint32_t min_height;
	uint32_t max_height;
};

struct drm_mode_crtc {
	uint64_t set_connectors_ptr;
	uint32_t count_connectors;
	uint32_t crtc_id;
	uint32_t fb_id;
	uint32_t x;
	uint32_t y;
	uint32_t gamma_size;
	uint32_t mode_valid;
	struct drm_mode_modeinfo mode;
};

struct drm_mode_get_connector {
	uint64_t encoders_ptr;
	uint64_t modes_ptr;
	uint64_t props_ptr;
	uint64_t prop_values_ptr;
	uint32_t count_modes;
	uint32_t count_props;
	uint32_t count_encoders;
	uint32_t encoder_id;
	uint32_t connector_id;
	uint32_t connector_type;
	uint32_t connector_type_id;
	uint32_t connection;
	uint32_t mm_width;
	uint32_t mm_height;
	uint32_t subpixel;
	uint32_t pad;
};

struct drm_mode_fb_cmd {
	uint32_t fb_id;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t bpp;
	uint32_t depth;
	uint32_t handle;
};

struct drm_mode_crtc_page_flip {
	uint32_t crtc_id;
	uint32_t fb_id;
	uint32_t flags;
	uint32_t reserved;
	uint64_t user_data;
};

struct drm_mode_create_dumb {
	uint32_t height;
	uint32_t width;
	uint32_t bpp;
	uint32_t flags;
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
};

struct drm_mode_map_dumb {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
};

struct drm_mode_destroy_dumb {
	uint32_t handle;
};

#define DRM_IOCTL_VERSION		DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_SET_MASTER		DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER		DRM_IO(0x1f)
#define DRM_IOCTL_MODE_GETRESOURCES	DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC		DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC		DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETCONNECTOR	DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_ADDFB		DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB		DRM_IOWR(0xAF, unsigned int)
#define DRM_IOCTL_MODE_PAGE_FLIP	DRM_IOWR(0xB0, struct drm_mode_crtc_page_flip)
#define DRM_IOCTL_MODE_CREATE_DUMB	DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB		DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB	DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)

struct dumb_buf {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t handle;
	uint32_t fb_id;
	uint64_t size;
	uint32_t *map;
};

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && errno == EINTR);
	return r;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int create_dumb_fb(int fd, uint32_t w, uint32_t h, struct dumb_buf *b)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_fb_cmd fbreq;
	struct drm_mode_map_dumb mreq;

	memset(b, 0, sizeof(*b));
	memset(&creq, 0, sizeof(creq));
	creq.width = w;
	creq.height = h;
	creq.bpp = 32;
	if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		perror("DRM_IOCTL_MODE_CREATE_DUMB");
		return -1;
	}

	b->width = w;
	b->height = h;
	b->stride = creq.pitch;
	b->size = creq.size;
	b->handle = creq.handle;

	memset(&fbreq, 0, sizeof(fbreq));
	fbreq.width = w;
	fbreq.height = h;
	fbreq.pitch = b->stride;
	fbreq.bpp = 32;
	fbreq.depth = 24; /* DRM_FORMAT_XRGB8888 */
	fbreq.handle = b->handle;
	if (xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbreq) < 0) {
		perror("DRM_IOCTL_MODE_ADDFB");
		return -1;
	}
	b->fb_id = fbreq.fb_id;

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = b->handle;
	if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		perror("DRM_IOCTL_MODE_MAP_DUMB");
		return -1;
	}

	b->map = mmap(NULL, (size_t)b->size, PROT_READ | PROT_WRITE,
		      MAP_SHARED, fd, (off_t)mreq.offset);
	if (b->map == MAP_FAILED) {
		perror("mmap dumb buffer");
		b->map = NULL;
		return -1;
	}

	return 0;
}

static void destroy_dumb_fb(int fd, struct dumb_buf *b)
{
	struct drm_mode_destroy_dumb dreq;

	if (b->map)
		munmap(b->map, (size_t)b->size);
	if (b->fb_id)
		xioctl(fd, DRM_IOCTL_MODE_RMFB, &b->fb_id);
	if (b->handle) {
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = b->handle;
		xioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	}
}

static void draw_frame(struct dumb_buf *b, int frame)
{
	uint32_t pitch_px = b->stride / 4;
	int bar_x = (frame * 8) % (int)b->width;
	uint32_t x, y;

	for (y = 0; y < b->height; y++) {
		uint32_t *row = b->map + y * pitch_px;
		uint32_t band_color;
		uint8_t shade = (uint8_t)(64 + ((y * 191) / b->height));

		if (y < b->height / 3)
			band_color = 0xFF000000U | ((uint32_t)shade << 16); /* Red */
		else if (y < (2 * b->height) / 3)
			band_color = 0xFF000000U | ((uint32_t)shade << 8);  /* Green */
		else
			band_color = 0xFF000000U | (uint32_t)shade;         /* Blue */

		for (x = 0; x < b->width; x++) {
			if ((int)x >= bar_x && (int)x < bar_x + 24)
				row[x] = 0xFFFFFFFFU; /* Moving white VSync bar */
			else
				row[x] = band_color;
		}
	}
}

int main(int argc, char **argv)
{
	int info_only = 0;
	int total_frames = 120;
	int fd, i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--info") == 0)
			info_only = 1;
		else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
			total_frames = atoi(argv[++i]);
	}

	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open /dev/dri/card0");
		return 1;
	}

	/* Print DRM driver version */
	char name[64] = {0}, desc[128] = {0}, date[32] = {0};
	struct drm_version ver;

	memset(&ver, 0, sizeof(ver));
	ver.name_len = sizeof(name) - 1;
	ver.name = name;
	ver.date_len = sizeof(date) - 1;
	ver.date = date;
	ver.desc_len = sizeof(desc) - 1;
	ver.desc = desc;
	if (xioctl(fd, DRM_IOCTL_VERSION, &ver) == 0) {
		printf("[vita-drm-test] Driver: %s v%d.%d (%s) - %s\n",
		       name, ver.version_major, ver.version_minor, date, desc);
	}

	/* Query KMS resources */
	uint32_t conn_ids[8] = {0}, crtc_ids[8] = {0};
	struct drm_mode_card_res res;

	memset(&res, 0, sizeof(res));
	res.count_connectors = 8;
	res.connector_id_ptr = (uintptr_t)conn_ids;
	res.count_crtcs = 8;
	res.crtc_id_ptr = (uintptr_t)crtc_ids;
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 ||
	    res.count_connectors == 0 || res.count_crtcs == 0) {
		perror("DRM_IOCTL_MODE_GETRESOURCES");
		close(fd);
		return 1;
	}

	struct drm_mode_modeinfo modes[8];
	uint32_t enc_ids[4] = {0};
	struct drm_mode_get_connector conn;

	memset(&conn, 0, sizeof(conn));
	memset(modes, 0, sizeof(modes));
	conn.connector_id = conn_ids[0];
	conn.count_modes = 8;
	conn.modes_ptr = (uintptr_t)modes;
	conn.count_encoders = 4;
	conn.encoders_ptr = (uintptr_t)enc_ids;
	if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0 ||
	    conn.count_modes == 0) {
		perror("DRM_IOCTL_MODE_GETCONNECTOR");
		close(fd);
		return 1;
	}

	struct drm_mode_crtc saved_crtc;

	memset(&saved_crtc, 0, sizeof(saved_crtc));
	saved_crtc.crtc_id = crtc_ids[0];
	xioctl(fd, DRM_IOCTL_MODE_GETCRTC, &saved_crtc);

	printf("[vita-drm-test] Connector %u: %ux%u (htotal=%u, vtotal=%u, clk=%u kHz), CRTC=%u (saved_fb=%u)\n",
	       conn.connector_id, modes[0].hdisplay, modes[0].vdisplay,
	       modes[0].htotal, modes[0].vtotal, modes[0].clock,
	       crtc_ids[0], saved_crtc.fb_id);

	if (info_only) {
		close(fd);
		return 0;
	}

	xioctl(fd, DRM_IOCTL_SET_MASTER, 0);

	struct dumb_buf bufs[2];

	if (create_dumb_fb(fd, modes[0].hdisplay, modes[0].vdisplay, &bufs[0]) < 0 ||
	    create_dumb_fb(fd, modes[0].hdisplay, modes[0].vdisplay, &bufs[1]) < 0) {
		destroy_dumb_fb(fd, &bufs[0]);
		close(fd);
		return 1;
	}

	printf("[vita-drm-test] Allocated 2 CDRAM XRGB8888 buffers (%ux%u, pitch=%u, fb_ids=%u,%u)\n",
	       bufs[0].width, bufs[0].height, bufs[0].stride,
	       bufs[0].fb_id, bufs[1].fb_id);

	draw_frame(&bufs[0], 0);

	struct drm_mode_crtc set_crtc;

	memset(&set_crtc, 0, sizeof(set_crtc));
	set_crtc.crtc_id = crtc_ids[0];
	set_crtc.fb_id = bufs[0].fb_id;
	set_crtc.set_connectors_ptr = (uintptr_t)&conn_ids[0];
	set_crtc.count_connectors = 1;
	set_crtc.mode = modes[0];
	set_crtc.mode_valid = 1;
	if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &set_crtc) < 0) {
		perror("DRM_IOCTL_MODE_SETCRTC");
		goto cleanup;
	}

	uint64_t t_start = now_ns();
	uint64_t t_prev = t_start;
	double min_ms = 1e9, max_ms = 0.0;
	int front = 0, flips = 0;

	for (i = 1; i <= total_frames; i++) {
		int back = front ^ 1;
		struct drm_mode_crtc_page_flip flip;
		struct pollfd pfd;
		uint8_t evbuf[256];

		draw_frame(&bufs[back], i);

		memset(&flip, 0, sizeof(flip));
		flip.crtc_id = crtc_ids[0];
		flip.fb_id = bufs[back].fb_id;
		flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
		flip.user_data = (uint64_t)i;
		if (xioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) < 0) {
			perror("DRM_IOCTL_MODE_PAGE_FLIP");
			break;
		}

		pfd.fd = fd;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 1000) <= 0) {
			fprintf(stderr, "[vita-drm-test] VBlank poll timeout at frame %d\n", i);
			break;
		}
		if (read(fd, evbuf, sizeof(evbuf)) <= 0)
			break;

		uint64_t t_now = now_ns();
		double dt_ms = (double)(t_now - t_prev) / 1e6;

		if (i > 1) {
			if (dt_ms < min_ms)
				min_ms = dt_ms;
			if (dt_ms > max_ms)
				max_ms = dt_ms;
		}
		t_prev = t_now;
		front = back;
		flips++;
	}

	double elapsed_s = (double)(now_ns() - t_start) / 1e9;

	if (flips > 0 && elapsed_s > 0.0) {
		printf("[vita-drm-test] Completed %d hardware VSync page flips in %.3f s (%.2f FPS, frame interval min=%.2f ms avg=%.2f ms max=%.2f ms)\n",
		       flips, elapsed_s, (double)flips / elapsed_s,
		       min_ms < 1e8 ? min_ms : 0.0,
		       (elapsed_s * 1000.0) / (double)flips,
		       max_ms);
	}

cleanup:
	if (saved_crtc.mode_valid && saved_crtc.fb_id) {
		saved_crtc.set_connectors_ptr = (uintptr_t)&conn_ids[0];
		saved_crtc.count_connectors = 1;
		xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &saved_crtc);
	}
	destroy_dumb_fb(fd, &bufs[1]);
	destroy_dumb_fb(fd, &bufs[0]);
	xioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
	close(fd);
	return 0;
}
