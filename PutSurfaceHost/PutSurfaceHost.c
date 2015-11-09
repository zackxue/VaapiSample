#include <fcntl.h>
#include <stdio.h>
#include <va/va.h>
#include <va/va_x11.h>
#include <va/va_glx.h>

#define CHECK_VASTATUS(va_status,func)                                  \
if (va_status != VA_STATUS_SUCCESS) {                                   \
    fprintf(stderr,"%s:%s (%d) failed va_status=%x,exit\n", __func__, func, __LINE__,va_status); \
    return -1;                                                         \
}

#define CLIP_WIDTH  320
#define CLIP_HEIGHT 240

#define WIN_WIDTH  (CLIP_WIDTH<<1)
#define WIN_HEIGHT (CLIP_HEIGHT<<1)

#define FIFO "/tmp/fifo"

int main() {
  int fd;
  int ret;

  fd = open(FIFO, O_RDONLY);
  if (-1 == fd) {
    fprintf(stderr, "Open fifo failed.\n");
    return -1;
  }

  void* param[3];
  int size = 0;
  int total_size = 0;
  while (total_size != sizeof(void*) * 3) {
    size = read(fd, &param, sizeof(void*) * 3);
    if (-1 == size) {
      close(fd);
      fprintf(stderr, "Failed to read fifo.\n");
      return -1;
    } else {
      if (size > 0) {
        printf("Read %d bytes\n", size);
        total_size += size;
      } else {
        printf("Reach EOF.\n");
        break;
      }
    }
  }

  VADisplay va_dpy       = (VADisplay)param[0];
  VASurfaceID surface_id = (VASurfaceID)param[1];
  Window win             = (Window)param[2];

  printf("VA DISPLAY: %p surface ID: %p Window: %p\n", (void*)va_dpy, (void*)surface_id, (void*)win);

  VAStatus va_status;

  printf("Put va surface into x window.\n");
  // render the temp surface, it should be same with original surface without color conversion test
  va_status = vaPutSurface((VADisplay)va_dpy, surface_id, win,
                           0,0,CLIP_WIDTH,CLIP_HEIGHT,
                           0,0,WIN_WIDTH,WIN_HEIGHT,
                           NULL,
                           0,VA_FRAME_PICTURE);
  CHECK_VASTATUS(va_status,"vaPutSurface");

  getchar();

}