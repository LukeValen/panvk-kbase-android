/* Host check for csf-v11/117 (sw X11 present via MIT-SHM AttachFd).
 *
 * Mirrors the driver logic in wsi_common_x11.c: probe MIT-SHM >= 1.2, attach a
 * memfd segment with xcb_shm_attach_fd, copy the frame into it and
 * xcb_shm_put_image, then a GetGeometry round trip per frame (the driver's
 * OUT_OF_DATE check; on the shm path it is sent after the put so its reply
 * orders the server's segment read before the next copy).
 * Falls back to xcb_put_image when the probe or attach fails.
 *
 *   cc -O2 -o x11shm x11_shm_present_test.c -lxcb -lxcb-shm
 *   DISPLAY=:N ./x11shm [W H FRAMES]
 * Exit 0 = pixels round-trip correctly on the chosen path.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xcb/shm.h>
#include <xcb/xcb.h>

static double now_s(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec * 1e-9;
}

static int shm_supported(xcb_connection_t *c)
{
   xcb_query_extension_reply_t *e =
      xcb_query_extension_reply(c, xcb_query_extension(c, 7, "MIT-SHM"), NULL);
   int present = e && e->present;
   free(e);
   if (!present)
      return 0;
   xcb_shm_query_version_reply_t *v =
      xcb_shm_query_version_reply(c, xcb_shm_query_version(c), NULL);
   int ok = v && (v->major_version > 1 || (v->major_version == 1 && v->minor_version >= 2));
   free(v);
   return ok;
}

static void *attach_memfd(xcb_connection_t *c, xcb_shm_seg_t seg, size_t size)
{
   int fd = memfd_create("wsi-x11-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
   if (fd < 0 || ftruncate(fd, size) < 0)
      return NULL;
   /* Driver: seal size so the server mapping cannot be SIGBUSed. */
   if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) < 0)
      fprintf(stderr, "seal failed\n");
   else if (ftruncate(fd, size / 2) == 0) {
      fprintf(stderr, "FAIL: sealed memfd shrank\n");
      exit(1);
   }
   void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (map == MAP_FAILED) {
      close(fd);
      return NULL;
   }
   xcb_generic_error_t *err = xcb_request_check(c, xcb_shm_attach_fd_checked(c, seg, fd, 1));
   if (err) {
      free(err);
      munmap(map, size);
      return NULL;
   }
   return map;
}

int main(int argc, char **argv)
{
   int w = argc > 2 ? atoi(argv[1]) : 1280, h = argc > 2 ? atoi(argv[2]) : 720;
   int frames = argc > 3 ? atoi(argv[3]) : 300;
   const char *force = getenv("FORCE_PUTIMAGE");
   xcb_connection_t *c = xcb_connect(NULL, NULL);
   if (xcb_connection_has_error(c))
      return 2;
   xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
   /* Draw into a pixmap: headless servers may have no viewable window to
    * read back from, and the request path is the same for any drawable. */
   xcb_pixmap_t win = xcb_generate_id(c);
   xcb_create_pixmap(c, s->root_depth, win, s->root, w, h);
   xcb_gcontext_t gc = xcb_generate_id(c);
   xcb_create_gc(c, gc, win, XCB_GC_GRAPHICS_EXPOSURES, (uint32_t[]){ 0 });

   size_t stride = (size_t)w * 4, size = stride * h;
   uint32_t *src = malloc(size);
   xcb_shm_seg_t seg = xcb_generate_id(c);
   void *map = (!force && shm_supported(c)) ? attach_memfd(c, seg, size) : NULL;
   uint64_t max_req = xcb_get_maximum_request_length(c);
   int lines = ((max_req << 2) - sizeof(xcb_put_image_request_t)) / stride;

   double t0 = now_s();
   for (int f = 0; f < frames; f++) {
      for (size_t i = 0; i < (size_t)w * h; i += 64)
         src[i] = 0xff000000u | (f * 2654435761u + i);
      src[0] = 0xff000000u | f;
      xcb_get_geometry_cookie_t gk;
      if (map) {
         memcpy(map, src, size);
         xcb_shm_put_image(c, win, gc, w, h, 0, 0, w, h, 0, 0, s->root_depth,
                           XCB_IMAGE_FORMAT_Z_PIXMAP, 0, seg, 0);
         gk = xcb_get_geometry(c, win); /* after the put: reply = segment read */
      } else {
         gk = xcb_get_geometry(c, win);
         for (int y = 0; y < h; y += lines) {
            int n = h - y < lines ? h - y : lines;
            xcb_put_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, win, gc, w, n, 0, y, 0,
                          s->root_depth, n * stride, (uint8_t *)src + y * stride);
         }
      }
      xcb_flush(c);
      free(xcb_get_geometry_reply(c, gk, NULL));
   }
   double dt = now_s() - t0;

   /* Read back pixel (0,0) and one sampled pixel. */
   xcb_generic_error_t *gerr = NULL;
   xcb_get_image_reply_t *img = xcb_get_image_reply(
      c, xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, win, 0, 0, w, h, ~0u), &gerr);
   int ok = 0;
   if (gerr) {
      fprintf(stderr, "GetImage error %d\n", gerr->error_code);
      free(gerr);
   }
   if (img) {
      uint32_t *px = (uint32_t *)xcb_get_image_data(img);
      ok = (px[0] & 0xffffff) == (src[0] & 0xffffff) &&
           (px[64 * 7] & 0xffffff) == (src[64 * 7] & 0xffffff);
      free(img);
   }
   printf("path=%s size=%dx%d frames=%d fps=%.1f readback=%s\n",
          map ? "shm_put_image(attach_fd)" : "put_image", w, h, frames,
          frames / dt, ok ? "ok" : "MISMATCH");
   xcb_free_pixmap(c, win);
   if (map) {
      xcb_shm_detach(c, seg);
      munmap(map, size);
   }
   xcb_disconnect(c);
   return ok ? 0 : 1;
}
