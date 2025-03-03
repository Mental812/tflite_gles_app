/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2019 terryky1220@gmail.com
 * ------------------------------------------------ */

/*
    ## If you use Windows Host PC, prepare X Server on Windows.
    - Install VcXsrv
    - Launch VcXsrv with parameters below:
        * disable "Native opengl"
        * enable  "Disable access control"

    ## Connect to a target device via ssh with X Forwading.
    ```
        $ ssh -X username@192.168.1.1
    ```

    ## On ssh console of target device,
    ```
        $ export DISPLAY=:10.0
        $ make clean; make TARGET_ENV=headless
        $ ./gl2tri
    ```

    ## If it doesn't work well, try below.
    ```
        $ sudo apt install mesa-utils
        $ sudo apt install libgles2-mesa-dev libegl1-mesa-dev xorg-dev
        $ export LIBGL_DEBUG=verbose
        $ export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/mesa/libGL.so.1.2.0
    ```

    ## [Reference] Another environment variables
    ```
        $ export LIBGL_ALWAYS_SOFTWARE=true
        $ export LIBGL_ALWAYS_INDIRECT=true;
    ```
*/

/* =================================================
    Experimental Log of [Ubuntu PC] as a target.
   =================================================

    <<<<< Using Local Dispay (DISPLAY=:0.0) >>>>>

      [TARGET_ENV=x11]      @@ SUCCESS @@
          ---------------------------------------
          GL_RENDERER     : GeForce GTX 970/PCIe/SSE2
          GL_VERSION      : OpenGL ES 3.2 NVIDIA 418.67
          GL_VENDOR       : NVIDIA Corporation
          ---------------------------------------

      [TARGET_ENV=headless] ## ERROR ##
      (libGL.so.1 => /usr/lib/nvidia-418/libGL.so.1)
          Failed to initialize GLX.

      [TARGET_ENV=headless] ## ERROR ##
      (export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/mesa/libGL.so.1.2.0)
          libGL error: No matching fbConfigs or visuals found
          libGL error: failed to load driver: swrast
          X server has no RGB visual

    <<<<< Using Remote Dispay (DISPLAY=:10.0) >>>>>

      [TARGET_ENV=headless] @@ SUCCESS @@
      (export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/mesa/libGL.so.1.2.0)
      [VcXsrv=Software OpenGL]
          ---------------------------------------
          GL_RENDERER   = llvmpipe (LLVM 6.0, 256 bits)
          GL_VERSION    = 3.0 Mesa 18.0.5
          GL_VENDOR     = VMware, Inc.
          ---------------------------------------

      [TARGET_ENV=headless] ## ERROR ##
      (export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/mesa/libGL.so.1.2.0)
      [VcXsrv=Native OpenGL]
          libGL error: No matching fbConfigs or visuals found
          libGL error: failed to load driver: swrast
          ---------------------------------------
          GL_RENDERER   = GeForce GTX TITAN X/PCIe/SSE2
          GL_VERSION    = 1.4 (4.6.0 NVIDIA 391.24)
          GL_VENDOR     = NVIDIA Corporation
          ---------------------------------------

      [TARGET_ENV=headless] ## ERROR ##
      (libGL.so.1 => /usr/lib/nvidia-418/libGL.so.1)
      [VcXsrv=Native OpenGL/Software OpenGL]
          Failed to initialize GLX.

      [TARGET_ENV=x11]      ## ERROR ##
      [VcXsrv=Native OpenGL/Software OpenGL]
          ERR at eglGetDisplay()
*/

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#define GL_GLEXT_PROTOTYPES  (1)
#define GLX_GLXEXT_PROTOTYPES (1)
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#define UNUSED(x) (void)(x)

static Display *s_xdpy;
static Window  s_xwin;

static void (*s_motion_func)(int x, int y) = NULL;
static void (*s_button_func)(int button, int state, int x, int y) = NULL;
static void (*s_key_func)(int key, int state, int x, int y) = NULL;

int
glx_initialize (int glx_version, int depth_size, int stencil_size, int sample_num,
                int win_w, int win_h)
{
    UNUSED (glx_version);
    UNUSED (sample_num);
    int dblBuf[] = { GLX_RGBA,
        GLX_RED_SIZE,   1,
        GLX_GREEN_SIZE, 1,
        GLX_BLUE_SIZE,  1,
        GLX_DEPTH_SIZE, 1,
        GLX_DOUBLEBUFFER,
        None };
    
    /* Open a connection to the X server */
    Display *xdpy = XOpenDisplay (NULL);
    if (xdpy == NULL)
    {
        fprintf (stderr, "Can't open XDisplay.\n");
        return -1;
    }

    /* Make sure OpenGL's GLX extension is supported */
    int ebase;
    if (!glXQueryExtension (xdpy, &ebase, &ebase))
    {
        fprintf (stderr, "X server has no OpenGL GLX extension\n");
        return -1;
    }

    /* Find an appropriate OpenGL-capable visual. */
    XVisualInfo *visual = glXChooseVisual(xdpy, DefaultScreen(xdpy), dblBuf);
    if (visual == NULL)
    {
        fprintf (stderr, "X server has no RGB visual\n");
        return -1;
    }
    if (visual->class != TrueColor)
    {
        fprintf (stderr, "TrueColor visual required for this program\n");
        return -1;
    }

    /* Create an OpenGL rendering context. */
    GLXContext ctx = glXCreateContext (xdpy, visual, None, True);
    if (ctx == NULL)
    {
        fprintf (stderr, "could not create rendering context");
        return -1;
    }

    /* Create an X window with the selected visual. */
    Colormap cmap = XCreateColormap (xdpy, RootWindow (xdpy, visual->screen), visual->visual, AllocNone);

    XSetWindowAttributes swa;
    swa.colormap     = cmap;
    swa.border_pixel = 0;
    swa.event_mask   = ExposureMask | ButtonPressMask | StructureNotifyMask;

    Window xwin = XCreateWindow (xdpy, RootWindow (xdpy, visual->screen), 
        0, 0, win_w, win_h,
        0, visual->depth, InputOutput, visual->visual,
        CWBorderPixel | CWColormap | CWEventMask, &swa);

    glXMakeCurrent (xdpy, xwin, ctx);
    XMapWindow (xdpy, xwin);
    XSelectInput (xdpy, xwin, ButtonPressMask | ButtonReleaseMask | Button1MotionMask);
    XFlush (xdpy);

    s_xdpy = xdpy;
    s_xwin = xwin;

    if (glGetString(GL_VERSION) == NULL)
    {
        fprintf (stderr, "\n");
        fprintf (stderr, "Failed to initialize GLX.\n");
        fprintf (stderr, "Please retry after setting the environment variables:\n");
        fprintf (stderr, "  $ export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/mesa/libGL.so.1.2.0\n");
        fprintf (stderr, "  $ export DISPLAY=192.168.1.100:0.0   (specify your IP address)\n");
        fprintf (stderr, "\n");
        exit (-1);
    }

    printf ("---------------------------------------\n");
    printf ("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
    printf ("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
    printf ("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));
    //printf ("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
    printf ("---------------------------------------\n");

    return 0;
}

int
glx_terminate ()
{
    return 0;
}

int
glx_swap ()
{
    glXSwapBuffers (s_xdpy, s_xwin);

    XEvent event;
    while (XPending (s_xdpy))
    {
        XNextEvent (s_xdpy, &event);
        switch (event.type)
        {
        case ButtonPress:
            if (s_button_func)
            {
                s_button_func (0/*event.xbutton.button*/, 1, event.xbutton.x, event.xbutton.y);
            }
            break;
        case ButtonRelease:
            if (s_button_func)
            {
                s_button_func (0/*event.xbutton.button*/, 0, event.xbutton.x, event.xbutton.y);
            }
            break;
        case MotionNotify:
            if (s_motion_func)
            {
                s_motion_func (event.xmotion.x, event.xmotion.y);
            }
            break;
        default:
            /* Unknown event type, ignore it */
            break;
        }
    }
    return 0;
}


void egl_set_motion_func (void (*func)(int x, int y))
{
    s_motion_func = func;
}

void egl_set_button_func (void (*func)(int button, int state, int x, int y))
{
    s_button_func = func;
}

void egl_set_key_func (void (*func)(int key, int state, int x, int y))
{
    s_key_func = func;
}


#if 1 /* work around for NVIDIA Jetson */
int
drmSyncobjTimelineSignal (int fd, const uint32_t *handles, uint64_t *points, uint32_t handle_count)
{
    return 0;
}

int
drmSyncobjTransfer (int fd, uint32_t dst_handle, uint64_t dst_point, uint32_t src_handle, uint64_t src_point, uint32_t flags)
{
    return 0;
}

int
drmSyncobjTimelineWait (int fd, uint32_t *handles, uint64_t *points, unsigned num_handles, int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled)
{
    return 0;
}
int
drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points,  uint32_t handle_count)
{
    return 0;
}
int
drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points, uint32_t handle_count, uint32_t flags)
{
    return 0;
}
#endif

