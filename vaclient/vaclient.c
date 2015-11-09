#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/time.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <GL/glut.h>
#include <va/va.h>
#include <va/va_x11.h>
#include <va/va_glx.h>
#include "h264.h"

#define CHECK_VASTATUS(va_status,func)                                  \
if (va_status != VA_STATUS_SUCCESS) {                                   \
    fprintf(stderr,"%s:%s (%d) failed va_status=%x,exit\n", __func__, func, __LINE__,va_status); \
    exit(1);                                                            \
}

#define CLIP_WIDTH  H264_CLIP_WIDTH
#define CLIP_HEIGHT H264_CLIP_HEIGHT

#define WIN_WIDTH  (CLIP_WIDTH<<1)
#define WIN_HEIGHT (CLIP_HEIGHT<<1)

#define FIFO "/tmp/fifo"

extern glmake(Display* display, int width, int height);
extern void glswap();
extern void glrelease();

void* glsurface;
int texture_id;

Display *x11_display;
Window win;
GC context;

VASurfaceID surface_id;
VAEntrypoint entrypoints[5];
int num_entrypoints,vld_entrypoint;
VAConfigAttrib attrib;
VAConfigID config_id;

VAContextID context_id;
VABufferID pic_param_buf,iqmatrix_buf,slice_param_buf,slice_data_buf;
int major_ver, minor_ver;
VADisplay	va_dpy;
VAStatus va_status;

int kk,entrycnt=0;
pthread_mutex_t surface_mutex;

/*
union semun {
  int             val;
  struct semid_ds *buf;
  unsigned short  *array;
};

static int semaphore_p(int sem_id) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = -1;
    sem_b.sem_flg = SEM_UNDO;
    if(semop(sem_id, &sem_b, 1) == -1)
    {
        fprintf(stderr, "semaphore_p failed\n");
        return 0;
    }
    return 1;
}

static int semaphore_v(int sem_id) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = 1;//P()
    sem_b.sem_flg = SEM_UNDO;
    if(semop(sem_id, &sem_b, 1) == -1)
    {
        fprintf(stderr, "semaphore_v failed\n");
        return 0;
    }
    return 1;
}

static int set_semvalue(int sem_id, int value) {
    union semun sem_union;
    sem_union.val = value;
    if(semctl(sem_id, 0, SETVAL, sem_union) == -1)
        return 0;
    return 1;
}

static void del_semvalue(int sem_id) {
    union semun sem_union;
    sem_union.val = 0;

    if(semctl(sem_id, 0, IPC_RMID, sem_union) == -1)
        fprintf(stderr, "Failed to delete semaphore\n");
}
*/


static void *open_display(void) {
    return XOpenDisplay(NULL);
}

static void close_display(void *win_display) {
    XCloseDisplay(win_display);
}

static Window create_window(void *win_display, int x, int y, int width, int height) {
    Display *x11_display = (Display *)win_display;
    int screen = DefaultScreen(x11_display);
    Window root, win;

    root = RootWindow(x11_display, screen);
    win = XCreateSimpleWindow(x11_display, root, x, y, width, height,
                              0, 0, BlackPixel(x11_display, 0));

    if (win) {
        XSizeHints sizehints;
        sizehints.width  = width;
        sizehints.height = height;
        sizehints.flags = USSize;
        XSetNormalHints(x11_display, win, &sizehints);
        XSetStandardProperties(x11_display, win, "XWindow", "XWindow",
                               None, (char **)NULL, 0, &sizehints);

        XMapWindow(x11_display, win);
    }
    context = XCreateGC(x11_display, win, 0, 0);
    XSync(x11_display, False);
    return win;
}

void* render(void* param) {
  pthread_mutex_lock(&surface_mutex);

#ifdef PUT_XWINDOW
  VAStatus va_status;

  printf("Put va surface into x window.\n");
  // render the temp surface, it should be same with original surface without color conversion test
  va_status = vaPutSurface(va_dpy, surface_id, (Drawable)win,
                           0,0,CLIP_WIDTH,CLIP_HEIGHT,
                           0,0,WIN_WIDTH,WIN_HEIGHT,
                           NULL,
                           0,VA_FRAME_PICTURE);
  CHECK_VASTATUS(va_status,"vaPutSurface");
#else
  printf("Copy va surface into texture.\n");
  glEnable(GL_TEXTURE_2D);
  glGenTextures(1,&texture_id);
  printf("texture_id = %x11_display\n",texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D,0, GL_RGBA, CLIP_WIDTH, CLIP_HEIGHT, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

  va_status=vaCreateSurfaceGLX(va_dpy, GL_TEXTURE_2D, texture_id, &glsurface);
  CHECK_VASTATUS(va_status, "vaCreateSurfaceGLX");
  va_status=vaCopySurfaceGLX(va_dpy,glsurface,surface_id,0);
  CHECK_VASTATUS(va_status, "vaCopySurfaceGLX");

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  glBegin(GL_QUADS);
  glTexCoord2f(0, 0); glVertex2f(-1.0f, -1.0f);
  glTexCoord2f(0, 1); glVertex2f(1.0f, -1.0f);
  glTexCoord2f(1, 1); glVertex2f(1.0f,  1.0f);
  glTexCoord2f(1, 0); glVertex2f(-1.0f, 1.0f);
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glswap();
#endif
  pthread_mutex_unlock(&surface_mutex);
}

int main(int argc,char **argv)
{
   x11_display = XOpenDisplay(NULL);
   if (!x11_display) {
       fprintf(stderr, "error: can't connect to X server!\n");
       return -1;
   }

   pthread_mutex_init(&surface_mutex, NULL);

#ifdef PUT_XWINDOW
   win = create_window(x11_display, 0, 0, WIN_WIDTH, WIN_HEIGHT);
#else
   win = glmake(x11_display, WIN_WIDTH, WIN_HEIGHT);
#endif

   va_dpy = vaGetDisplayGLX(x11_display);

   va_status = vaInitialize(va_dpy, &major_ver, &minor_ver);
   assert(va_status == VA_STATUS_SUCCESS);
   va_status = vaQueryConfigEntrypoints(va_dpy, VAProfileH264High, entrypoints,
                                        &num_entrypoints);
   CHECK_VASTATUS(va_status, "vaQueryConfigEntrypoints");

   for	(vld_entrypoint = 0; vld_entrypoint < num_entrypoints; vld_entrypoint++) {
       if (entrypoints[vld_entrypoint] == VAEntrypointVLD)
           break;
   }

   for	(kk = 0; kk < num_entrypoints; kk++) {
       if (entrypoints[kk] == VAEntrypointVLD)
           entrycnt++;
   }
   printf("entry count of VAEntrypointVLD for VAProfileH264High is %d\n",entrycnt);

   if (vld_entrypoint == num_entrypoints) {
       /* not find VLD entry point */
       assert(0);
   }

   /* Assuming finding VLD, find out the format for the render target */
   attrib.type = VAConfigAttribRTFormat;
   vaGetConfigAttributes(va_dpy, VAProfileH264High, VAEntrypointVLD,
                         &attrib, 1);
   {
      printf("Support below color format for VAProfileH264High, VAEntrypointVLD:\n");
      if ((attrib.value & VA_RT_FORMAT_YUV420) != 0) printf("  VA_RT_FORMAT_YUV420\n");
      if ((attrib.value & VA_RT_FORMAT_YUV422) != 0) printf("  VA_RT_FORMAT_YUV422\n");
      if ((attrib.value & VA_RT_FORMAT_YUV444) != 0) printf("  VA_RT_FORMAT_YUV444\n");
      if ((attrib.value & VA_RT_FORMAT_YUV411) != 0) printf("  VA_RT_FORMAT_YUV411\n");
      if ((attrib.value & VA_RT_FORMAT_YUV400) != 0) printf("  VA_RT_FORMAT_YUV400\n");
      if ((attrib.value & VA_RT_FORMAT_RGB16) != 0) printf("  VA_RT_FORMAT_RGB16\n");
      if ((attrib.value & VA_RT_FORMAT_RGB32) != 0) printf("  VA_RT_FORMAT_RGB32\n");
      if ((attrib.value & VA_RT_FORMAT_RGBP) != 0) printf("  VA_RT_FORMAT_RGBP\n");
      if ((attrib.value & VA_RT_FORMAT_PROTECTED) != 0) printf("  VA_RT_FORMAT_PROTECTED\n");
   }
   if ((attrib.value & VA_RT_FORMAT_YUV420) == 0) {
       /* not find desired YUV420 RT format */
       assert(0);
   }

   va_status = vaCreateConfig(va_dpy, VAProfileH264High, VAEntrypointVLD,
                             &attrib, 1,&config_id);
   CHECK_VASTATUS(va_status, "vaQueryConfigEntrypoints");

   va_status = vaCreateSurfaces(
       va_dpy,
       VA_RT_FORMAT_YUV420, CLIP_WIDTH, CLIP_HEIGHT,
       &surface_id, 1,
       NULL, 0
   );
   CHECK_VASTATUS(va_status, "vaCreateSurfaces");

   /* Create a context for this decode pipe */
   va_status = vaCreateContext(va_dpy, config_id,
                              CLIP_WIDTH,
                              ((CLIP_HEIGHT+15)/16)*16,
                              VA_PROGRESSIVE,
                              &surface_id,
                              1,
                              &context_id);
   CHECK_VASTATUS(va_status, "vaCreateContext");

   h264_pic_param.frame_num = 0;
   h264_pic_param.CurrPic.picture_id = surface_id;
   h264_pic_param.CurrPic.TopFieldOrderCnt = 0;
   h264_pic_param.CurrPic.BottomFieldOrderCnt = 0;
   for(kk=0;kk<16;kk++){
       h264_pic_param.ReferenceFrames[kk].picture_id          = 0xffffffff;
       h264_pic_param.ReferenceFrames[kk].flags               = VA_PICTURE_H264_INVALID;
       h264_pic_param.ReferenceFrames[kk].TopFieldOrderCnt    = 0;
       h264_pic_param.ReferenceFrames[kk].BottomFieldOrderCnt = 0;
   }

   va_status = vaCreateBuffer(va_dpy, context_id,
                             VAPictureParameterBufferType,
                             sizeof(VAPictureParameterBufferH264),
                             1, &h264_pic_param,
                             &pic_param_buf);
   CHECK_VASTATUS(va_status, "vaCreateBuffer");

   va_status = vaCreateBuffer(va_dpy, context_id,
                             VAIQMatrixBufferType,
                             sizeof(VAIQMatrixBufferH264),
                             1, &h264_iq_matrix,
                             &iqmatrix_buf );
   CHECK_VASTATUS(va_status, "vaCreateBuffer");

   for (kk = 0; kk < 32; kk++) {
       h264_slice_param.RefPicList0[kk].picture_id          = 0xffffffff;
       h264_slice_param.RefPicList0[kk].flags               = VA_PICTURE_H264_INVALID;
       h264_slice_param.RefPicList0[kk].TopFieldOrderCnt    = 0;
       h264_slice_param.RefPicList0[kk].BottomFieldOrderCnt = 0;
       h264_slice_param.RefPicList1[kk].picture_id          = 0xffffffff;
       h264_slice_param.RefPicList1[kk].flags               = VA_PICTURE_H264_INVALID;
       h264_slice_param.RefPicList1[kk].TopFieldOrderCnt    = 0;
       h264_slice_param.RefPicList1[kk].BottomFieldOrderCnt = 0;

   }
   h264_slice_param.slice_data_size   = H264_CLIP_SLICE_SIZE;
   h264_slice_param.slice_data_offset = 0;
   h264_slice_param.slice_data_flag   = VA_SLICE_DATA_FLAG_ALL;

   va_status = vaCreateBuffer(va_dpy, context_id,
                             VASliceParameterBufferType,
                             sizeof(VASliceParameterBufferH264),
                             1,
                             &h264_slice_param, &slice_param_buf);
   CHECK_VASTATUS(va_status, "vaCreateBuffer");

   va_status = vaCreateBuffer(va_dpy, context_id,
                             VASliceDataBufferType,
                             H264_CLIP_SLICE_SIZE,
                             1,
                             h264_clip+H264_CLIP_SLICE_OFFSET,
                             &slice_data_buf);
   CHECK_VASTATUS(va_status, "vaCreateBuffer");

   va_status = vaBeginPicture(va_dpy, context_id, surface_id);
   CHECK_VASTATUS(va_status, "vaBeginPicture");

   va_status = vaRenderPicture(va_dpy,context_id, &pic_param_buf, 1);
   CHECK_VASTATUS(va_status, "vaRenderPicture");

   va_status = vaRenderPicture(va_dpy,context_id, &iqmatrix_buf, 1);
   CHECK_VASTATUS(va_status, "vaRenderPicture");

   va_status = vaRenderPicture(va_dpy,context_id, &slice_param_buf, 1);
   CHECK_VASTATUS(va_status, "vaRenderPicture");

   va_status = vaRenderPicture(va_dpy,context_id, &slice_data_buf, 1);
   CHECK_VASTATUS(va_status, "vaRenderPicture");

   va_status = vaEndPicture(va_dpy,context_id);
   CHECK_VASTATUS(va_status, "vaEndPicture");

   va_status = vaSyncSurface(va_dpy, surface_id);
   CHECK_VASTATUS(va_status, "vaSyncSurface");



#ifdef FORK_PROCESS
   key_t key;
   int sem_id;
   pid_t p;
   p = fork();

   //key = ftok("/tmp/sem", 'a');
   //sem_id = semget(key, 1, 0666 | IPC_CREAT);
   //set_semvalue(sem_id, 1);

   if (p > 0) {
    //semaphore_p(sem_id);
    printf("It's father process %d\n", getpid());
    //semaphore_v(sem_id);
   } else if (p == 0) {
    //semaphore_p(sem_id);
    printf("It's child process %d\n", getpid());
    render(NULL);
    //semaphore_v(sem_id);
   }
#elif defined(CROSS_PROCESS)
   int ret;
   int fd;
   unlink(FIFO);
   int fifo = mkfifo(FIFO, 0666);
   if (-1 == fifo) {
      fprintf(stderr, "Failed to create fifo.\n");
      return -1;
   }

   fd = open(FIFO, O_WRONLY);
   if (-1 == fd) {
      fprintf(stderr, "Failed to open fifo.\n");
      close(fd);
      return -1;
   }

   void* param[3];
   param[0] = (void*)va_dpy;
   param[1] = (void*)surface_id;
   param[2] = (void*)win;

   printf("writing...  \nVA DISPLAY: %p surface ID: %p Window: %p\n", (void*)va_dpy, (void*)surface_id, (void*)win);

   ret = write(fd, param, sizeof(param));
   if (ret != sizeof(param)) {
    fprintf(stderr, "Failed to write fifo.\n");
    close(fd);
    return -1;
   } else {
    printf("Write %x11_display byte.\n", ret);
   }
#else
   render(NULL);
#endif

   printf("press any key to exit\n");
   char is_get_char = getchar();

#ifndef PUT_XWINDOW
   va_status = vaDestroySurfaceGLX(va_dpy, glsurface);
   CHECK_VASTATUS(va_status, "vaDestroySurfaceGLX");

   glDeleteTextures(1, &texture_id);
   glrelease();
#else
   if(win) {
     XUnmapWindow(x11_display, win);
     XDestroyWindow(x11_display, win);
     win = NULL;
   }
#endif

    //vaDestroySurfaces(va_dpy,&surface_id,1);
    //vaDestroyConfig(va_dpy,config_id);
    //vaDestroyContext(va_dpy,context_id);
    //vaTerminate(va_dpy);
    //close_display(x11_display);

   return 0;
}




