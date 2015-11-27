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

void* glsurface;
int   texture_id;

Display              *x11_display;
Window               root;
Window               win;
GLint                att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
GC                   context;
XSetWindowAttributes swa;
XVisualInfo          *vi;
Pixmap               pixmap;
int                  pixmap_width = WIN_WIDTH, pixmap_height = WIN_HEIGHT;
GC                   gc;
GLXContext           glc;

typedef void (*t_glx_bind)(Display *, GLXDrawable, int , const int *);
typedef void (*t_glx_release)(Display *, GLXDrawable, int);
t_glx_bind glXBindTexImageEXT = 0;
t_glx_release glXReleaseTexImageEXT = 0;

const int pixmap_config[] = {
    GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
    GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
    GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
    GLX_DOUBLEBUFFER, False,
    GLX_Y_INVERTED_EXT, GLX_DONT_CARE,
    None
};

const int pixmap_attribs[] = {
    GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
    GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGB_EXT,
    None
};

GLXPixmap glxpixmap = 0;
GLXFBConfig * configs = 0;

/**** VA *****/
VASurfaceID    surface_id;
VAEntrypoint   entrypoints[5];
int            num_entrypoints,vld_entrypoint;
VAConfigAttrib attrib;
VAConfigID     config_id;
VAContextID    context_id;
VABufferID     pic_param_buf,iqmatrix_buf,slice_param_buf,slice_data_buf;
int            major_ver, minor_ver;
VADisplay      va_dpy;
VAStatus       va_status;

int kk,entrycnt=0;

static void *open_display(void) {
    return XOpenDisplay(NULL);
}

static void close_display(void *win_display) {
    XCloseDisplay(win_display);
}


static void Redraw() {
    XWindowAttributes  gwa;

    XGetWindowAttributes(x11_display, win, &gwa);
    glViewport(0, 0, gwa.width, gwa.height);
    glClearColor(0.3, 0.3, 0.3, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.25, 1.25, -1.25, 1.25, 1., 20.);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0., 0., 10., 0., 0., 0., 0., 1., 0.);

    glColor3f(1.0, 1.0, 1.0);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0); glVertex3f(-1.0,  1.0, 0.0);
    glTexCoord2f(1.0, 0.0); glVertex3f( 1.0,  1.0, 0.0);
    glTexCoord2f(1.0, 1.0); glVertex3f( 1.0, -1.0, 0.0);
    glTexCoord2f(0.0, 1.0); glVertex3f(-1.0, -1.0, 0.0);
    glEnd();

    glXSwapBuffers(x11_display, win);
}


static Window create_window(void *win_display, int x, int y, int width, int height) {
    if(x11_display == NULL) {
        printf("\n\tcannot open display\n\n");
        exit(0); 
    }

    root = DefaultRootWindow(x11_display);

    vi = glXChooseVisual(x11_display, 0, att);

    if(vi == NULL) {
        printf("\n\tno appropriate visual found\n\n");
        exit(0);
    }

    swa.event_mask = ExposureMask | KeyPressMask;
    swa.colormap   = XCreateColormap(x11_display, root, vi->visual, AllocNone);

    win = XCreateWindow(x11_display, root, 0, 0, 600, 600, 0, vi->depth, InputOutput, vi->visual, CWEventMask  | CWColormap, &swa);
    XMapWindow(x11_display, win);
    XStoreName(x11_display, win, "PIXMAP TO TEXTURE");
    glc = glXCreateContext(x11_display, vi, NULL, GL_TRUE);

    if(glc == NULL) {
        printf("\n\tcannot create gl context\n\n");
        exit(0);
    }

    glXMakeCurrent(x11_display, win, glc);
    glEnable(GL_DEPTH_TEST);

    /* CREATE A PIXMAP AND DRAW SOMETHING */
    pixmap = XCreatePixmap(x11_display, root, pixmap_width, pixmap_height, vi->depth);
    gc = DefaultGC(x11_display, 0);

    XSetForeground(x11_display, gc, 0x00c0c0);
    XFillRectangle(x11_display, pixmap, gc, 0, 0, pixmap_width, pixmap_height);

    XSetForeground(x11_display, gc, 0x000000);
    XFillArc(x11_display, pixmap, gc, 15, 25, 50, 50, 0, 360*64);

    XSetForeground(x11_display, gc, 0x0000ff);
    XDrawString(x11_display, pixmap, gc, 10, 15, "PIXMAP TO TEXTURE", strlen("PIXMAP TO TEXTURE"));

    XSetForeground(x11_display, gc, 0xff0000);
    XFillRectangle(x11_display, pixmap, gc, 75, 75, 45, 35);

    XFlush(x11_display);

    return win;
}

void* render(void* param) {
  VAStatus va_status;

  printf("Put va surface into x pixmap.\n");
  // render the temp surface, it should be same with original surface without color conversion test
  /////////////////////////////////
  struct timeval start_count;
  struct timeval end_count;
  gettimeofday(&start_count, 0);
  va_status = vaPutSurface(va_dpy, surface_id, (Drawable)pixmap,
                           0,0,CLIP_WIDTH, CLIP_HEIGHT,
                           0,0,WIN_WIDTH,WIN_HEIGHT,
                           NULL,
                           0,VA_FRAME_PICTURE);
  CHECK_VASTATUS(va_status,"vaPutSurface");
  gettimeofday(&end_count, 0);

  double starttime_in_micro_sec = (start_count.tv_sec * 1000000) + start_count.tv_usec;
  double endtime_in_micro_sec = (end_count.tv_sec * 1000000) + end_count.tv_usec;
  double duration_in_milli_sec = (endtime_in_micro_sec - starttime_in_micro_sec) * 0.001;

  printf ("\n\n");
  printf ("****************************************************\n");
  printf ("The duration of vaPutSurface is %d milliseconds \n", (int)duration_in_milli_sec);
  printf ("****************************************************\n");
  /////////////////////////////////

  // Create a texture with GLX_texture_from_pixmap
  const char * exts = glXQueryExtensionsString(x11_display, 0);

  printf("Extensions: %s\n", exts);
  if(! strstr(exts, "GLX_EXT_texture_from_pixmap"))
  {
      fprintf(stderr, "GLX_EXT_texture_from_pixmap not supported!\n");
      return 1;
  }

  glXBindTexImageEXT = (t_glx_bind) glXGetProcAddress((const GLubyte *)"glXBindTexImageEXT");
  glXReleaseTexImageEXT = (t_glx_release) glXGetProcAddress((const GLubyte *)"glXReleaseTexImageEXT");

  if(!glXBindTexImageEXT || !glXReleaseTexImageEXT)
  {
      fprintf(stderr, "Some extension functions missing!");
      return 1;
  }

  int c=0;

  configs = glXChooseFBConfig(x11_display, 0, pixmap_config, &c);
  if(!configs) {
    fprintf(stderr, "No appropriate GLX FBConfig available!\n");
    return 1;
  }

  glxpixmap = glXCreatePixmap(x11_display, configs[0], pixmap, pixmap_attribs);

  gettimeofday(&start_count, 0);
  glEnable(GL_TEXTURE_2D);
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glXBindTexImageEXT(x11_display, glxpixmap, GLX_FRONT_EXT, NULL);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); 
  gettimeofday(&end_count, 0);

  starttime_in_micro_sec = (start_count.tv_sec * 1000000) + start_count.tv_usec;
  endtime_in_micro_sec = (end_count.tv_sec * 1000000) + end_count.tv_usec;
  duration_in_milli_sec = (endtime_in_micro_sec - starttime_in_micro_sec) * 0.001;

  printf ("\n\n");
  printf ("****************************************************\n");
  printf ("The duration of bind textimageext is %d milliseconds \n", (int)duration_in_milli_sec);
  printf ("****************************************************\n");

  XEvent     xev;
  while(1) {
    XNextEvent(x11_display, &xev);

    if(xev.type == Expose) {
      Redraw();
    } else if(xev.type == KeyPress) {
      glXReleaseTexImageEXT(x11_display, glxpixmap, GLX_FRONT_EXT);
      XFree(configs);
      XFreePixmap(x11_display, pixmap);

      glXMakeCurrent(x11_display, None, NULL);
      glXDestroyContext(x11_display, glc);
      XDestroyWindow(x11_display, win);
      XCloseDisplay(x11_display);
      exit(0);
    }
  } /* while(1) */
}

int main(int argc,char **argv)
{
   x11_display = XOpenDisplay(NULL);
   if (!x11_display) {
       fprintf(stderr, "error: can't connect to X server!\n");
       return -1;
   }

   win = create_window(x11_display, 0, 0, WIN_WIDTH, WIN_HEIGHT);

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

   render(NULL);

   printf("press any key to exit\n");
   char is_get_char = getchar();

   if(win) {
     XUnmapWindow(x11_display, win);
     XDestroyWindow(x11_display, win);
     win = NULL;
   }

    //vaDestroySurfaces(va_dpy,&surface_id,1);
    //vaDestroyConfig(va_dpy,config_id);
    //vaDestroyContext(va_dpy,context_id);
    //vaTerminate(va_dpy);
    //close_display(x11_display);

   return 0;
}




