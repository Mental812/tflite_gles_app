/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2019 terryky1220@gmail.com
 * ------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <GLES2/gl2.h>
#include "util_egl.h"
#include "util_debugstr.h"
#include "util_pmeter.h"
#include "util_texture.h"
#include "util_render2d.h"
#include "util_matrix.h"
#include "tflite_face_segmentation.h"
#include "util_camera_capture.h"
#include "util_video_decode.h"

#define UNUSED(x) (void)(x)





/* resize image to DNN network input size and convert to fp32. */
void
feed_face_detect_image(texture_2d_t *srctex, int win_w, int win_h)
{
    int x, y, w, h;
    float *buf_fp32 = (float *)get_face_detect_input_buf (&w, &h);
    unsigned char *buf_ui8 = NULL;
    static unsigned char *pui8 = NULL;

    if (pui8 == NULL)
        pui8 = (unsigned char *)malloc(w * h * 4);

    buf_ui8 = pui8;

    draw_2d_texture_ex (srctex, 0, win_h - h, w, h, 1);

    glPixelStorei (GL_PACK_ALIGNMENT, 4);
    glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf_ui8);

    /* convert UI8 [0, 255] ==> FP32 [-1, 1] */
    float mean = 128.0f;
    float std  = 128.0f;
    for (y = 0; y < h; y ++)
    {
        for (x = 0; x < w; x ++)
        {
            int r = *buf_ui8 ++;
            int g = *buf_ui8 ++;
            int b = *buf_ui8 ++;
            buf_ui8 ++;          /* skip alpha */
            *buf_fp32 ++ = (float)(r - mean) / std;
            *buf_fp32 ++ = (float)(g - mean) / std;
            *buf_fp32 ++ = (float)(b - mean) / std;
        }
    }

    return;
}

void
feed_bisenetv2_image(texture_2d_t *srctex, int win_w, int win_h, face_detect_result_t *detection, unsigned int face_id)
{
    int x, y, w, h;
    float *buf_fp32 = (float *)get_bisenetv2_input_buf (&w, &h);
    unsigned char *buf_ui8 = NULL;
    static unsigned char *pui8 = NULL;

    if (pui8 == NULL)
        pui8 = (unsigned char *)malloc(w * h * 4);

    buf_ui8 = pui8;

    float texcoord[] = { 0.0f, 1.0f,
                         0.0f, 0.0f,
                         1.0f, 1.0f,
                         1.0f, 0.0f };
    
    if (detection->num > face_id)
    {
        face_t *face = &(detection->faces[face_id]);
        float x0 = face->face_pos[0].x;
        float y0 = face->face_pos[0].y;
        float x1 = face->face_pos[1].x; //    0--------1
        float y1 = face->face_pos[1].y; //    |        |
        float x2 = face->face_pos[2].x; //    |        |
        float y2 = face->face_pos[2].y; //    3--------2
        float x3 = face->face_pos[3].x;
        float y3 = face->face_pos[3].y;
        texcoord[0] = x3;   texcoord[1] = y3;
        texcoord[2] = x0;   texcoord[3] = y0;
        texcoord[4] = x2;   texcoord[5] = y2;
        texcoord[6] = x1;   texcoord[7] = y1;
    }

    draw_2d_texture_ex_texcoord (srctex, 0, win_h - h, w, h, texcoord);

    glPixelStorei (GL_PACK_ALIGNMENT, 4);
    glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf_ui8);

    /* convert UI8 [0, 255] ==> FP32 [0, 1] */
    float mean = 128.0f;
    float std  = 128.0f;
    for (y = 0; y < h; y ++)
    {
        for (x = 0; x < w; x ++)
        {
            int r = *buf_ui8 ++;
            int g = *buf_ui8 ++;
            int b = *buf_ui8 ++;
            buf_ui8 ++;          /* skip alpha */
            *buf_fp32 ++ = (float)(r - mean) / std;
            *buf_fp32 ++ = (float)(g - mean) / std;
            *buf_fp32 ++ = (float)(b - mean) / std;
        }
    }

    return;
}


static void
render_detect_region (int ofstx, int ofsty, int texw, int texh,
                      face_detect_result_t *detection)
{
    float col_red[]   = {1.0f, 0.0f, 0.0f, 1.0f};
    float col_white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    for (int i = 0; i < detection->num; i ++)
    {
        face_t *face = &(detection->faces[i]);
        float x1 = face->topleft.x  * texw + ofstx;
        float y1 = face->topleft.y  * texh + ofsty;
        float x2 = face->btmright.x * texw + ofstx;
        float y2 = face->btmright.y * texh + ofsty;
        float score = face->score;

        /* rectangle region */
        draw_2d_rect (x1, y1, x2-x1, y2-y1, col_red, 2.0f);

        /* class name */
        char buf[512];
        sprintf (buf, "%d", (int)(score * 100));
        draw_dbgstr_ex (buf, x1, y1, 1.0f, col_white, col_red);
#if 0
        /* key points */
        for (int j = 0; j < kFaceKeyNum; j ++)
        {
            float x = face->keys[j].x * texw + ofstx;
            float y = face->keys[j].y * texh + ofsty;

            int r = 4;
            draw_2d_fillrect (x - (r/2), y - (r/2), r, r, col_red);
        }
#endif
    }
}


static void
render_cropped_face_image (texture_2d_t *srctex, int ofstx, int ofsty, int texw, int texh,
                           face_detect_result_t *detection, unsigned int face_id)
{
    float texcoord[8];

    if (detection->num <= face_id)
        return;

    face_t *face = &(detection->faces[face_id]);
    float x0 = face->face_pos[0].x;
    float y0 = face->face_pos[0].y;
    float x1 = face->face_pos[1].x; //    0--------1
    float y1 = face->face_pos[1].y; //    |        |
    float x2 = face->face_pos[2].x; //    |        |
    float y2 = face->face_pos[2].y; //    3--------2
    float x3 = face->face_pos[3].x;
    float y3 = face->face_pos[3].y;
    texcoord[0] = x0;   texcoord[1] = y0;
    texcoord[2] = x3;   texcoord[3] = y3;
    texcoord[4] = x1;   texcoord[5] = y1;
    texcoord[6] = x2;   texcoord[7] = y2;

    draw_2d_texture_ex_texcoord (srctex, ofstx, ofsty, texw, texh, texcoord);
}

float
fclampf (float val)
{
    val = fmaxf (0.0f, val);
    val = fminf (1.0f, val);
    return val;
}

static void
render_animface_image (texture_2d_t *srctex, int ofstx, int ofsty, int texw, int texh,
                       face_detect_result_t *detection, unsigned int face_id, bisenetv2_result_t *bisenetv2_ret)
{
    if (detection->num <= face_id)
        return;

    int64_t *segmap = bisenetv2_ret->segmentmap;
    int segmap_w  = bisenetv2_ret->segmentmap_dims[0];
    int segmap_h  = bisenetv2_ret->segmentmap_dims[1];
    int x, y;
    unsigned int imgbuf[segmap_h][segmap_w];

#if 1
    unsigned char alpha = 200;
    unsigned char color[19 * 4] = 
    {
        0,   0,   0,   32,     /* [0 ] background */
        128, 0,   0,   alpha,  /* [1 ] skin */
        0,   200, 0,   alpha,  /* [2 ] l_brow */
        200, 200, 0,   alpha,  /* [3 ] r_brow */
        0,   0,   200, alpha,  /* [4 ] l_eye */
        200, 0,   200, alpha,  /* [5 ] r_eye */
        0,   200, 200, alpha,  /* [6 ] eye_g */
        0,   64,  0,   alpha,  /* [7 ] l_ear */
        64,  0,   0,   alpha,  /* [8 ] r_ear */
        192, 0,   0,   alpha,  /* [9 ] ear_r */
        128, 255, 0,   alpha,  /* [10] nose */
        192, 128, 64,  alpha,  /* [11] mouth */
        64,  0,   128, alpha,  /* [12] u_lip */
        192, 0,   128, alpha,  /* [13] l_lip */
        64,  128, 128, alpha,  /* [14] neck */
        192, 128, 128, alpha,  /* [15] neck_l */
        0,   64,  0,   alpha,  /* [16] cloth */
        128, 128, 128, alpha,  /* [17] hair */
        0,   192, 0,   alpha,  /* [18] hat */
    };
#else
    unsigned char alpha = 190;
    unsigned char color[19 * 4] = 
    {
        0,   0,   0,   0,      /* [0 ] background */
        240, 240, 250, alpha,  /* [1 ] skin */
        0,   0,   0,   255,    /* [2 ] l_brow */
        0,   0,   0,   255,    /* [3 ] r_brow */
        0,   0,   200, 0,      /* [4 ] l_eye */
        200, 0,   200, 0,      /* [5 ] r_eye */
        0,   200, 200, 0,      /* [6 ] eye_g */
        200, 200, 200, 0,      /* [7 ] l_ear */
        128, 0,   0,   0,      /* [8 ] r_ear */
        192, 0,   0,   0,      /* [9 ] ear_r */
        250, 250, 255, alpha,  /* [10] nose */
        192, 128, 0,   0,      /* [11] mouth */
        200, 0,   0,   alpha,  /* [12] u_lip */
        200, 0,   0,   alpha,  /* [13] l_lip */
        255, 255, 255, 0,      /* [14] neck */
        255, 255, 255, 0,      /* [15] neck_l */
        255, 255, 0,   0,      /* [16] cloth */
        0,   0,   0,   alpha,  /* [17] hair */
        0,   192, 0,   0,      /* [18] hat */
    };
#endif
    /* find the most confident class for each pixel. */
    for (y = 0; y < segmap_h; y ++)
    {
        for (x = 0; x < segmap_w; x ++)
        {
            int64_t val = segmap[y * segmap_w + x];
            if (val > 18)
                val = 0;
            
            unsigned char r = color[4 * val + 0];
            unsigned char g = color[4 * val + 1];
            unsigned char b = color[4 * val + 2];
            unsigned char a = color[4 * val + 3];

            imgbuf[y][x] = (a << 24) | (b << 16) | (g << 8) | (r);
        }
    }

    face_t *face = &(detection->faces[face_id]);
    float cx     = face->face_cx * texw; //    0--------1
    float cy     = face->face_cy * texh; //    |        |
    float face_w = face->face_w  * texw; //    |        |
    float face_h = face->face_h  * texh; //    3--------2
    float bx     = cx - face_w * 0.5f;
    float by     = cy - face_h * 0.5f;
    float rot    = RAD_TO_DEG (face->rotation);

    texture_2d_t animtex;
    create_2d_texture_ex (&animtex, imgbuf, segmap_w, segmap_h, pixfmt_fourcc ('R', 'G', 'B', 'A'));
    draw_2d_texture_ex_texcoord_rot (&animtex, ofstx + bx, ofsty + by, face_w, face_h, 0, 0.5, 0.5, rot);

    glDeleteTextures (1, &animtex.texid);
}

/* Adjust the texture size to fit the window size
 *
 *                      Portrait
 *     Landscape        +------+
 *     +-+------+-+     +------+
 *     | |      | |     |      |
 *     | |      | |     |      |
 *     +-+------+-+     +------+
 *                      +------+
 */
static void
adjust_texture (int win_w, int win_h, int texw, int texh, 
                int *dx, int *dy, int *dw, int *dh)
{
    float win_aspect = (float)win_w / (float)win_h;
    float tex_aspect = (float)texw  / (float)texh;
    float scale;
    float scaled_w, scaled_h;
    float offset_x, offset_y;

    if (win_aspect > tex_aspect)
    {
        scale = (float)win_h / (float)texh;
        scaled_w = scale * texw;
        scaled_h = scale * texh;
        offset_x = (win_w - scaled_w) * 0.5f;
        offset_y = 0;
    }
    else
    {
        scale = (float)win_w / (float)texw;
        scaled_w = scale * texw;
        scaled_h = scale * texh;
        offset_x = 0;
        offset_y = (win_h - scaled_h) * 0.5f;
    }

    *dx = (int)offset_x;
    *dy = (int)offset_y;
    *dw = (int)scaled_w;
    *dh = (int)scaled_h;
}


/*--------------------------------------------------------------------------- *
 *      M A I N    F U N C T I O N
 *--------------------------------------------------------------------------- */
int
main(int argc, char *argv[])
{
    char input_name_default[] = "assets/pakutaso.jpg";
    char *input_name = NULL;
    int count;
    int win_w = 800;
    int win_h = 800;
    int texid;
    int texw, texh, draw_x, draw_y, draw_w, draw_h;
    texture_2d_t captex = {0};
    double ttime[10] = {0}, interval, invoke_ms0 = 0, invoke_ms1 = 0;
    int use_quantized_tflite = 0;
    int enable_camera = 1;
    UNUSED (argc);
    UNUSED (*argv);
#if defined (USE_INPUT_VIDEO_DECODE)
    int enable_video = 0;
#endif

    {
        int c;
        const char *optstring = "qv:x";

        while ((c = getopt (argc, argv, optstring)) != -1)
        {
            switch (c)
            {
            case 'q':
                use_quantized_tflite = 1;
                break;
#if defined (USE_INPUT_VIDEO_DECODE)
            case 'v':
                enable_video = 1;
                input_name = optarg;
                break;
#endif
            case 'x':
                enable_camera = 0;
                break;
            }
        }

        while (optind < argc)
        {
            input_name = argv[optind];
            optind++;
        }
    }

    if (input_name == NULL)
        input_name = input_name_default;

    egl_init_with_platform_window_surface (2, 0, 0, 0, win_w * 2, win_h);

    init_2d_renderer (win_w, win_h);
    init_pmeter (win_w, win_h, 500);
    init_dbgstr (win_w, win_h);

    init_tflite_bisenetv2 (use_quantized_tflite);

#if defined (USE_GL_DELEGATE) || defined (USE_GPU_DELEGATEV2)
    /* we need to recover framebuffer because GPU Delegate changes the FBO binding */
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    glViewport (0, 0, win_w, win_h);
#endif

#if defined (USE_INPUT_VIDEO_DECODE)
    /* initialize FFmpeg video decode */
    if (enable_video && init_video_decode () == 0)
    {
        create_video_texture (&captex, input_name);
        texw = captex.width;
        texh = captex.height;
        enable_camera = 0;
    }
    else
#endif
#if defined (USE_INPUT_CAMERA_CAPTURE)
    /* initialize V4L2 capture function */
    if (enable_camera && init_capture (CAPTURE_SQUARED_CROP) == 0)
    {
        create_capture_texture (&captex);
        texw = captex.width;
        texh = captex.height;
    }
    else
#endif
    {
        load_jpg_texture (input_name, &texid, &texw, &texh);
        captex.texid  = texid;
        captex.width  = texw;
        captex.height = texh;
        captex.format = pixfmt_fourcc ('R', 'G', 'B', 'A');
        enable_camera = 0;
    }
    adjust_texture (win_w, win_h, texw, texh, &draw_x, &draw_y, &draw_w, &draw_h);

    glClearColor (0.f, 0.f, 0.f, 1.0f);

    /* --------------------------------------- *
     *  Render Loop
     * --------------------------------------- */
    for (count = 0; ; count ++)
    {
        face_detect_result_t face_detect_ret = {0};
        bisenetv2_result_t   bisenetv2_result[MAX_FACE_NUM] = {0};
        char strbuf[512];

        PMETER_RESET_LAP ();
        PMETER_SET_LAP ();

        ttime[1] = pmeter_get_time_ms ();
        interval = (count > 0) ? ttime[1] - ttime[0] : 0;
        ttime[0] = ttime[1];

        glClear (GL_COLOR_BUFFER_BIT);
        glViewport (0, 0, win_w, win_h);

#if defined (USE_INPUT_VIDEO_DECODE)
        /* initialize FFmpeg video decode */
        if (enable_video)
        {
            update_video_texture (&captex);
        }
#endif
#if defined (USE_INPUT_CAMERA_CAPTURE)
        if (enable_camera)
        {
            update_capture_texture (&captex);
        }
#endif

        /* --------------------------------------- *
         *  face detection
         * --------------------------------------- */
        feed_face_detect_image (&captex, win_w, win_h);

        ttime[2] = pmeter_get_time_ms ();
        invoke_face_detect (&face_detect_ret);
        ttime[3] = pmeter_get_time_ms ();
        invoke_ms0 = ttime[3] - ttime[2];

        /* --------------------------------------- *
         *  Bisenetv2
         * --------------------------------------- */
        invoke_ms1 = 0;
        for (int face_id = 0; face_id < face_detect_ret.num; face_id ++)
        {
            feed_bisenetv2_image (&captex, win_w, win_h, &face_detect_ret, face_id);

            ttime[4] = pmeter_get_time_ms ();
            invoke_bisenetv2 (&bisenetv2_result[face_id]);
            ttime[5] = pmeter_get_time_ms ();
            invoke_ms1 += ttime[5] - ttime[4];
        }

        /* --------------------------------------- *
         *  render scene (left half)
         * --------------------------------------- */
        glClear (GL_COLOR_BUFFER_BIT);
        draw_2d_texture_ex (&captex, draw_x, draw_y, draw_w, draw_h, 0);
        render_detect_region (draw_x, draw_y, draw_w, draw_h, &face_detect_ret);

        /* visualize the segmentation results. */
        /* draw cropped image of the face area */
        for (int face_id = 0; face_id < face_detect_ret.num; face_id ++)
        {
            float w = 100;
            float h = 100;
            float x = win_w - w - 10;
            float y = h * face_id + 10;
            float col_white[] = {1.0f, 1.0f, 1.0f, 1.0f};

            render_cropped_face_image (&captex, x, y, w, h, &face_detect_ret, face_id);
            draw_2d_rect (x, y, w, h, col_white, 2.0f);
        }

        /* --------------------------------------- *
         *  render scene  (right half)
         * --------------------------------------- */
        glViewport (win_w, 0, win_w, win_h);
        draw_2d_texture_ex (&captex, draw_x, draw_y, draw_w, draw_h, 0);

        for (int face_id = 0; face_id < face_detect_ret.num; face_id ++)
        {
            render_animface_image (&captex, draw_x, draw_y, draw_w, draw_h, &face_detect_ret, face_id, &bisenetv2_result[face_id]);
        }

        /* --------------------------------------- *
         *  post process
         * --------------------------------------- */
        glViewport (0, 0, win_w, win_h);
        draw_pmeter (0, 40);

        sprintf (strbuf, "Interval:%5.1f [ms]\nTFLite0 :%5.1f [ms]\nTFLite1 :%5.1f [ms]",
            interval, invoke_ms0, invoke_ms1);
        draw_dbgstr (strbuf, 10, 10);

        egl_swap();
    }

    return 0;
}

