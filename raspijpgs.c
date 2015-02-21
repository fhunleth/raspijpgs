/*
Copyright (c) 2013, Broadcom Europe Ltd
Copyright (c) 2013, Silvan Melchior
Copyright (c) 2013, James Hughes
Copyright (c) 2015, Frank Hunleth
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * \file raspijpgs.c
 * Command line program to capture and stream MJPEG video on a Raspberry Pi.
 *
 * Description
 *
 * Raspijpgs is a Unix commandline-friendly MJPEG streaming program with parts
 * copied from RaspiMJPEG, RaspiVid, and RaspiStill. It can be run as either
 * a client or server. The server connects to the Pi Camera via the MMAL
 * interface. It can either record video itself or send it to clients. All
 * interprocess communication is done via Unix Domain sockets.
 *
 * For usage and examples, see README.md
 */

#define _GNU_SOURCE // for asprintf()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <err.h>
#include <ctype.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "bcm_host.h"
#include "interface/vcos/vcos.h"
#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

#define MAX_CLIENTS                 8
#define MAX_DATA_BUFFER_SIZE        65536

#define UNUSED(expr) do { (void)(expr); } while (0)

// Environment config keys
#define RASPIJPGS_WIDTH             "RASPIJPGS_WIDTH"
#define RASPIJPGS_ANNOTATION        "RASPIJPGS_ANNOTATION"
#define RASPIJPGS_ANNO_BACKGROUND   "RASPIJPGS_ANNO_BACKGROUND"
#define RASPIJPGS_SHARPNESS         "RASPIJPGS_SHARPNESS"
#define RASPIJPGS_CONTRAST          "RASPIJPGS_CONTRAST"
#define RASPIJPGS_BRIGHTNESS        "RASPIJPGS_BRIGHTNESS"
#define RASPIJPGS_SATURATION        "RASPIJPGS_SATURATION"
#define RASPIJPGS_ISO               "RASPIJPGS_ISO"
#define RASPIJPGS_VSTAB             "RASPIJPGS_VSTAB"
#define RASPIJPGS_EV                "RASPIJPGS_EV"
#define RASPIJPGS_EXPOSURE          "RASPIJPGS_EXPOSURE"
#define RASPIJPGS_AWB               "RASPIJPGS_AWB"
#define RASPIJPGS_IMXFX             "RASPIJPGS_IMXFX"
#define RASPIJPGS_COLFX             "RASPIJPGS_COLFX"
#define RASPIJPGS_METERING          "RASPIJPGS_METERING"
#define RASPIJPGS_ROTATION          "RASPIJPGS_ROTATION"
#define RASPIJPGS_HFLIP             "RASPIJPGS_HFLIP"
#define RASPIJPGS_VFLIP             "RASPIJPGS_VFLIP"
#define RASPIJPGS_ROI               "RASPIJPGS_ROI"
#define RASPIJPGS_SHUTTER           "RASPIJPGS_SHUTTER"
#define RASPIJPGS_QUALITY           "RASPIJPGS_QUALITY"
#define RASPIJPGS_SOCKET            "RASPIJPGS_SOCKET"
#define RASPIJPGS_OUTPUT            "RASPIJPGS_OUTPUT"
#define RASPIJPGS_COUNT             "RASPIJPGS_COUNT"
#define RASPIJPGS_LOCKFILE          "RASPIJPGS_LOCKFILE"


// Globals

enum config_context {
    config_context_file,
    config_context_server_start,
    config_context_client_request
};

struct raspijpgs_state
{
    // Settings
    char *lock_filename;
    char *config_filename;
    char *framing;
    char *setlist;
    int count;

    // Commandline options to only run in client or server mode
    int user_wants_server;
    int user_wants_client;

    // 1 if we're a server; 0 if we're a client
    int is_server;

    // Communication
    int socket_fd;
    char *buffer;
    int buffer_ix;

    struct sockaddr_un server_addr;
    struct sockaddr_un client_addrs[MAX_CLIENTS];

    // MMAL resources
    MMAL_COMPONENT_T *camera;
    MMAL_COMPONENT_T *jpegencoder;
    MMAL_COMPONENT_T *resizer;
    MMAL_CONNECTION_T *con_cam_res;
    MMAL_CONNECTION_T *con_res_jpeg;
    MMAL_POOL_T *pool_jpegencoder;

    // MMAL callback -> main loop
    int mmal_callback_pipe[2];
};

static struct raspijpgs_state state = {0};

struct raspi_config_opt
{
    const char *long_option;
    const char *short_option;
    const char *env_key;
    const char *help;

    const char *default_value;

    // Record the value (called as options are set)
    // Set replace=0 to only set the value if it hasn't been set already.
    void (*set)(const struct raspi_config_opt *, const char *value, int replace);

    // Apply the option (called on every option)
    void (*apply)(const struct raspi_config_opt *, enum config_context context);
};
static struct raspi_config_opt opts[];

static void default_set(const struct raspi_config_opt *opt, const char *value, int replace)
{
    if (!opt->env_key)
        return;

    if (value) {
        // The replace option is set to 0, so that anything set in the environment
        // is an override.
        if (setenv(opt->env_key, value, replace) < 0)
            err(EXIT_FAILURE, "Error setting %s to %s", opt->env_key, opt->default_value);
    } else {
        if (replace && (unsetenv(opt->env_key) < 0))
            err(EXIT_FAILURE, "Error unsetting %s", opt->env_key);
    }
}

static void setstring(char **left, const char *right)
{
    if (*left)
        free(*left);
    *left = strdup(right);
}

static void config_set(const struct raspi_config_opt *opt, const char *value, int replace)
{
    UNUSED(opt); UNUSED(replace);
    setstring(&state.config_filename, value);
}
static void framing_set(const struct raspi_config_opt *opt, const char *value, int replace)
{
    UNUSED(opt); UNUSED(replace);
    setstring(&state.framing, value);
}
static void set_set(const struct raspi_config_opt *opt, const char *value, int replace)
{
    UNUSED(opt);
    UNUSED(replace);

    // Set lists are intended to look like config files for ease of parsing
    const char *equals = strchr(value, '=');
    char *key = equals ? strndup(value, equals - value) : strdup(value);
    const struct raspi_config_opt *o;
    for (o = opts; o->long_option; o++) {
        if (strcmp(key, o->long_option) == 0)
            break;
    }
    if (!o->long_option)
        errx(EXIT_FAILURE, "Unexpected key '%s' used in --set. Check help", key);
    free(key);

    if (state.setlist) {
        char *old_setlist = state.setlist;
        asprintf(&state.setlist, "%s\n%s", old_setlist, value);
        free(old_setlist);
    } else
        state.setlist = strdup(value);
}
static void quit_set(const struct raspi_config_opt *opt, const char *value, int replace)
{
    UNUSED(opt); UNUSED(value); UNUSED(replace);
    state.count = 0;
}
static void server_set(const struct raspi_config_opt *opt, const char *value, int replace)
{
    UNUSED(opt); UNUSED(value); UNUSED(replace);
    state.user_wants_server = 1;
}
static void client_set(const struct raspi_config_opt *opt, const char *value, int replace)
{
    UNUSED(opt); UNUSED(value); UNUSED(replace);
    state.user_wants_client = 1;
}

static void help(const struct raspi_config_opt *opt, const char *value, int replace);

static void width_apply(const struct raspi_config_opt *opt, enum config_context context) { UNUSED(opt); }
static void annotation_apply(const struct raspi_config_opt *opt, enum config_context context) { UNUSED(opt); }
static void anno_background_apply(const struct raspi_config_opt *opt, enum config_context context) { UNUSED(opt); }
static void rational_param_apply(int mmal_param, const struct raspi_config_opt *opt, enum config_context context)
{
    unsigned int value = strtoul(getenv(opt->env_key), 0, 0);
    if (value > 100) {
        if (context == config_context_server_start)
            errx(EXIT_FAILURE, "%s must be between 0 and 100", opt->long_option);
        else
            return;
    }
    MMAL_RATIONAL_T mmal_value = {value, 100};
    MMAL_STATUS_T status = mmal_port_parameter_set_rational(state.camera->control, mmal_param, mmal_value);
    if(status != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}

static void sharpness_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    rational_param_apply(MMAL_PARAMETER_SHARPNESS, opt, context);
}
static void contrast_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    rational_param_apply(MMAL_PARAMETER_CONTRAST, opt, context);
}
static void brightness_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    rational_param_apply(MMAL_PARAMETER_BRIGHTNESS, opt, context);
}
static void saturation_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    rational_param_apply(MMAL_PARAMETER_SATURATION, opt, context);
}

static void ISO_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    UNUSED(context);
    unsigned int value = strtoul(getenv(opt->env_key), 0, 0);
    MMAL_STATUS_T status = mmal_port_parameter_set_uint32(state.camera->control, MMAL_PARAMETER_ISO, value);
    if(status != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void vstab_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    UNUSED(context);
    unsigned int value = (strcmp(getenv(opt->env_key), "on") == 0);
    MMAL_STATUS_T status = mmal_port_parameter_set_uint32(state.camera->control, MMAL_PARAMETER_VIDEO_STABILISATION, value);
    if(status != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void ev_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    // TODO
    UNUSED(opt);
    UNUSED(context);
}
static void exposure_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    MMAL_PARAM_EXPOSUREMODE_T mode;
    const char *str = getenv(opt->env_key);
    if(strcmp(str, "off") == 0) mode = MMAL_PARAM_EXPOSUREMODE_OFF;
    else if(strcmp(str, "auto") == 0) mode = MMAL_PARAM_EXPOSUREMODE_AUTO;
    else if(strcmp(str, "night") == 0) mode = MMAL_PARAM_EXPOSUREMODE_NIGHT;
    else if(strcmp(str, "nightpreview") == 0) mode = MMAL_PARAM_EXPOSUREMODE_NIGHTPREVIEW;
    else if(strcmp(str, "backlight") == 0) mode = MMAL_PARAM_EXPOSUREMODE_BACKLIGHT;
    else if(strcmp(str, "spotlight") == 0) mode = MMAL_PARAM_EXPOSUREMODE_SPOTLIGHT;
    else if(strcmp(str, "sports") == 0) mode = MMAL_PARAM_EXPOSUREMODE_SPORTS;
    else if(strcmp(str, "snow") == 0) mode = MMAL_PARAM_EXPOSUREMODE_SNOW;
    else if(strcmp(str, "beach") == 0) mode = MMAL_PARAM_EXPOSUREMODE_BEACH;
    else if(strcmp(str, "verylong") == 0) mode = MMAL_PARAM_EXPOSUREMODE_VERYLONG;
    else if(strcmp(str, "fixedfps") == 0) mode = MMAL_PARAM_EXPOSUREMODE_FIXEDFPS;
    else if(strcmp(str, "antishake") == 0) mode = MMAL_PARAM_EXPOSUREMODE_ANTISHAKE;
    else if(strcmp(str, "fireworks") == 0) mode = MMAL_PARAM_EXPOSUREMODE_FIREWORKS;
    else {
        if (context == config_context_server_start)
            errx(EXIT_FAILURE, "Invalid %s", opt->long_option);
        else
            return;
    }

    MMAL_PARAMETER_EXPOSUREMODE_T param = {{MMAL_PARAMETER_EXPOSURE_MODE,sizeof(param)}, mode};
    if (mmal_port_parameter_set(state.camera->control, &param.hdr) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void awb_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    MMAL_PARAM_AWBMODE_T awb_mode;
    const char *str = getenv(opt->env_key);
    if(strcmp(str, "off") == 0) awb_mode = MMAL_PARAM_AWBMODE_OFF;
    else if(strcmp(str, "auto") == 0) awb_mode = MMAL_PARAM_AWBMODE_AUTO;
    else if(strcmp(str, "sun") == 0) awb_mode = MMAL_PARAM_AWBMODE_SUNLIGHT;
    else if(strcmp(str, "cloudy") == 0) awb_mode = MMAL_PARAM_AWBMODE_CLOUDY;
    else if(strcmp(str, "shade") == 0) awb_mode = MMAL_PARAM_AWBMODE_SHADE;
    else if(strcmp(str, "tungsten") == 0) awb_mode = MMAL_PARAM_AWBMODE_TUNGSTEN;
    else if(strcmp(str, "fluorescent") == 0) awb_mode = MMAL_PARAM_AWBMODE_FLUORESCENT;
    else if(strcmp(str, "incandescent") == 0) awb_mode = MMAL_PARAM_AWBMODE_INCANDESCENT;
    else if(strcmp(str, "flash") == 0) awb_mode = MMAL_PARAM_AWBMODE_FLASH;
    else if(strcmp(str, "horizon") == 0) awb_mode = MMAL_PARAM_AWBMODE_HORIZON;
    else {
        if (context == config_context_server_start)
            errx(EXIT_FAILURE, "Invalid %s", opt->long_option);
        else
            return;
    }
    MMAL_PARAMETER_AWBMODE_T param = {{MMAL_PARAMETER_AWB_MODE,sizeof(param)}, awb_mode};
    if (mmal_port_parameter_set(state.camera->control, &param.hdr) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void imxfx_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    MMAL_PARAM_IMAGEFX_T imageFX;
    const char *str = getenv(opt->env_key);
    if(strcmp(str, "none") == 0) imageFX = MMAL_PARAM_IMAGEFX_NONE;
    else if(strcmp(str, "negative") == 0) imageFX = MMAL_PARAM_IMAGEFX_NEGATIVE;
    else if(strcmp(str, "solarise") == 0) imageFX = MMAL_PARAM_IMAGEFX_SOLARIZE;
    else if(strcmp(str, "sketch") == 0) imageFX = MMAL_PARAM_IMAGEFX_SKETCH;
    else if(strcmp(str, "denoise") == 0) imageFX = MMAL_PARAM_IMAGEFX_DENOISE;
    else if(strcmp(str, "emboss") == 0) imageFX = MMAL_PARAM_IMAGEFX_EMBOSS;
    else if(strcmp(str, "oilpaint") == 0) imageFX = MMAL_PARAM_IMAGEFX_OILPAINT;
    else if(strcmp(str, "hatch") == 0) imageFX = MMAL_PARAM_IMAGEFX_HATCH;
    else if(strcmp(str, "gpen") == 0) imageFX = MMAL_PARAM_IMAGEFX_GPEN;
    else if(strcmp(str, "pastel") == 0) imageFX = MMAL_PARAM_IMAGEFX_PASTEL;
    else if(strcmp(str, "watercolour") == 0) imageFX = MMAL_PARAM_IMAGEFX_WATERCOLOUR;
    else if(strcmp(str, "film") == 0) imageFX = MMAL_PARAM_IMAGEFX_FILM;
    else if(strcmp(str, "blur") == 0) imageFX = MMAL_PARAM_IMAGEFX_BLUR;
    else if(strcmp(str, "saturation") == 0) imageFX = MMAL_PARAM_IMAGEFX_SATURATION;
    else if(strcmp(str, "colourswap") == 0) imageFX = MMAL_PARAM_IMAGEFX_COLOURSWAP;
    else if(strcmp(str, "washedout") == 0) imageFX = MMAL_PARAM_IMAGEFX_WASHEDOUT;
    else if(strcmp(str, "posterise") == 0) imageFX = MMAL_PARAM_IMAGEFX_POSTERISE;
    else if(strcmp(str, "colourpoint") == 0) imageFX = MMAL_PARAM_IMAGEFX_COLOURPOINT;
    else if(strcmp(str, "colourbalance") == 0) imageFX = MMAL_PARAM_IMAGEFX_COLOURBALANCE;
    else if(strcmp(str, "cartoon") == 0) imageFX = MMAL_PARAM_IMAGEFX_CARTOON;
    else {
        if (context == config_context_server_start)
            errx(EXIT_FAILURE, "Invalid %s", opt->long_option);
        else
            return;
    }
    MMAL_PARAMETER_IMAGEFX_T param = {{MMAL_PARAMETER_IMAGE_EFFECT,sizeof(param)}, imageFX};
    if (mmal_port_parameter_set(state.camera->control, &param.hdr) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void colfx_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    // Color effect is specified as u:v. Anything else means off.
    MMAL_PARAMETER_COLOURFX_T param = {{MMAL_PARAMETER_COLOUR_EFFECT,sizeof(param)}, 0, 0, 0};
    const char *str = getenv(opt->env_key);
    if (sscanf(str, "%d:%d", &param.u, &param.v) == 2 &&
            param.u < 256 &&
            param.v < 256)
        param.enable = 1;
    else
        param.enable = 0;
    if (mmal_port_parameter_set(state.camera->control, &param.hdr) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void metering_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    MMAL_PARAM_EXPOSUREMETERINGMODE_T m_mode;
    const char *str = getenv(opt->env_key);
    if(strcmp(str, "average") == 0) m_mode = MMAL_PARAM_EXPOSUREMETERINGMODE_AVERAGE;
    else if(strcmp(str, "spot") == 0) m_mode = MMAL_PARAM_EXPOSUREMETERINGMODE_SPOT;
    else if(strcmp(str, "backlit") == 0) m_mode = MMAL_PARAM_EXPOSUREMETERINGMODE_BACKLIT;
    else if(strcmp(str, "matrix") == 0) m_mode = MMAL_PARAM_EXPOSUREMETERINGMODE_MATRIX;
    else {
        if (context == config_context_server_start)
            errx(EXIT_FAILURE, "Invalid %s", opt->long_option);
        else
            return;
    }
    MMAL_PARAMETER_EXPOSUREMETERINGMODE_T param = {{MMAL_PARAMETER_EXP_METERING_MODE,sizeof(param)}, m_mode};
    if (mmal_port_parameter_set(state.camera->control, &param.hdr) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void rotation_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    UNUSED(context);
    int value = strtol(getenv(opt->env_key), NULL, 0);
    if (mmal_port_parameter_set_int32(state.camera->output[0], MMAL_PARAMETER_ROTATION, value) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void flip_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    UNUSED(context);

    MMAL_PARAMETER_MIRROR_T mirror = {{MMAL_PARAMETER_MIRROR, sizeof(MMAL_PARAMETER_MIRROR_T)}, MMAL_PARAM_MIRROR_NONE};
    if (strcmp(getenv(RASPIJPGS_HFLIP), "on") == 0)
        mirror.value = MMAL_PARAM_MIRROR_HORIZONTAL;
    if (strcmp(getenv(RASPIJPGS_VFLIP), "on") == 0)
        mirror.value = (mirror.value == MMAL_PARAM_MIRROR_HORIZONTAL ? MMAL_PARAM_MIRROR_BOTH : MMAL_PARAM_MIRROR_VERTICAL);

    if (mmal_port_parameter_set(state.camera->output[0], &mirror.hdr) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void roi_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    // TODO
    UNUSED(opt);
    UNUSED(context);
}
static void shutter_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    UNUSED(context);
    int value = strtoul(getenv(opt->env_key), NULL, 0);
    if (mmal_port_parameter_set_uint32(state.camera->control, MMAL_PARAMETER_SHUTTER_SPEED, value) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set %s", opt->long_option);
}
static void quality_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    // TODO
    UNUSED(opt);
    UNUSED(context);
}
static void count_apply(const struct raspi_config_opt *opt, enum config_context context)
{
    state.count = strtol(getenv(opt->env_key), NULL, 0);
}

static struct raspi_config_opt opts[] =
{
    // long_option  short   env_key                  help                                                    default
    {"width",       "w",    RASPIJPGS_WIDTH,        "Set image width <size>",                               "320",      default_set, width_apply},
    {"annotation",  "a",    RASPIJPGS_ANNOTATION,   "Annotation on the video frames",                       "",         default_set, annotation_apply},
    {"anno_background", "ab", RASPIJPGS_ANNO_BACKGROUND, "Turn on a black background behind the annotation", "off",     default_set, anno_background_apply},
    {"sharpness",   "sh",   RASPIJPGS_SHARPNESS,    "Set image sharpness (-100 to 100)",                    "0",        default_set, sharpness_apply},
    {"contrast",    "co",   RASPIJPGS_CONTRAST,     "Set image contrast (-100 to 100)",                     "0",        default_set, contrast_apply},
    {"brightness",  "br",   RASPIJPGS_BRIGHTNESS,   "Set image brightness (0 to 100)",                      "50",       default_set, brightness_apply},
    {"saturation",  "sa",   RASPIJPGS_SATURATION,   "Set image saturation (-100 to 100)",                   "0",        default_set, saturation_apply},
    {"ISO",         "ISO",  RASPIJPGS_ISO,          "Set capture ISO (100 to 800)",                         "0",        default_set, ISO_apply},
    {"vstab",       "vs",   RASPIJPGS_VSTAB,        "Turn on video stabilisation",                          "off",      default_set, vstab_apply},
    {"ev",          "ev",   RASPIJPGS_EV,           "Set EV compensation (-10 to 10)",                      "0",        default_set, ev_apply},
    {"exposure",    "ex",   RASPIJPGS_EXPOSURE,     "Set exposure mode",                                    "auto",     default_set, exposure_apply},
    {"awb",         "awb",  RASPIJPGS_AWB,          "Set Automatic White Balance (AWB) mode",               "auto",     default_set, awb_apply},
    {"imxfx",       "ifx",  RASPIJPGS_IMXFX,        "Set image effect",                                     "none",     default_set, imxfx_apply},
    {"colfx",       "cfx",  RASPIJPGS_COLFX,        "Set colour effect <U:V>",                              "",         default_set, colfx_apply},
    {"metering",    "mm",   RASPIJPGS_METERING,     "Set metering mode",                                    "average",  default_set, metering_apply},
    {"rotation",    "rot",  RASPIJPGS_ROTATION,     "Set image rotation (0-359)",                           "0",        default_set, rotation_apply},
    {"hflip",       "hf",   RASPIJPGS_HFLIP,        "Set horizontal flip",                                  "off",      default_set, flip_apply},
    {"vflip",       "vf",   RASPIJPGS_VFLIP,        "Set vertical flip",                                    "off",      default_set, flip_apply},
    {"roi",         "roi",  RASPIJPGS_ROI,          "Set sensor region of interest",                        "0:0:65536:65536", default_set, roi_apply},
    {"shutter",     "ss",   RASPIJPGS_SHUTTER,      "Set shutter speed",                                    "0",        default_set, shutter_apply},
    {"quality",     "q",    RASPIJPGS_QUALITY,      "Set the JPEG quality (0-100)",                         "75",       default_set, quality_apply},
    {"socket",      0,      RASPIJPGS_SOCKET,       "Specify the socket filename for communication",        "/tmp/raspijpgs_socket", default_set, 0},
    {"output",      "o",    RASPIJPGS_OUTPUT,       "Specify an output filename or '-' for stdout",         "",         default_set, 0},
    {"count",       0,      RASPIJPGS_COUNT,        "How many frames to capture before quiting (-1 = no limit)", "-1",  default_set, count_apply},
    {"lockfile",    0,      RASPIJPGS_LOCKFILE,     "Specify a lock filename to prevent multiple runs",     "/tmp/raspijpgs_lock", default_set, 0},

    // options that can't be overridden using environment variables
    {"config",      "c",    0,                       "Specify a config file to read for options",            0,          config_set, 0},
    {"framing",     "fr",   0,                       "Specify the output framing (cat, mime, header, replace)", "cat",   framing_set, 0},
    {"set",         0,      0,                       "Set this parameter on the server (e.g. --set shutter=1000)", 0,    set_set, 0},
    {"server",      0,      0,                       "Run as a server",                                      0,          server_set, 0},
    {"client",      0,      0,                       "Run as a client",                                      0,          client_set, 0},
    {"quit",        0,      0,                       "Tell a server to quit",                                0,          quit_set, 0},
    {"help",        "h",    0,                       "Print this help message",                              0,          help, 0},
    {0,             0,      0,                       0,                                                      0,          0,           0}
};

static void help(const struct raspi_config_opt *opt, const char *value, int replace)
{
    UNUSED(opt); UNUSED(value); UNUSED(replace);
    fprintf(stderr, "raspijpgs [options]\n");

    const struct raspi_config_opt *o;
    for (o = opts; o->long_option; o++) {
        if (o->short_option)
            fprintf(stderr, "  --%-15s (-%s)\t %s\n", o->long_option, o->short_option, o->help);
        else
            fprintf(stderr, "  --%-20s\t %s\n", o->long_option, o->help);
    }
}

static int is_long_option(const char *str)
{
    return strlen(str) >= 3 && str[0] == '-' && str[1] == '-';
}
static int is_short_option(const char *str)
{
    return strlen(str) >= 2 && str[0] == '-' && str[1] != '-';
}

static void fillin_defaults()
{
    const struct raspi_config_opt *opt;
    for (opt = opts; opt->long_option; opt++) {
        if (opt->env_key && opt->default_value) {
            // The replace option is set to 0, so that anything set in the environment
            // is an override.
            if (setenv(opt->env_key, opt->default_value, 0) < 0)
                err(EXIT_FAILURE, "Error setting %s to %s", opt->env_key, opt->default_value);
        }
    }
}

static void apply_parameters(enum config_context context)
{
    const struct raspi_config_opt *opt;
    for (opt = opts; opt->long_option; opt++) {
  	warnx("apply %s", opt->long_option);
        if (opt->apply)
            opt->apply(opt, context);
    }
}

static void parse_args(int argc, char *argv[])
{
    int i;
    for (i = 1; i < argc; i++) {
        const struct raspi_config_opt *opt = 0;
        char *value;
        if (is_long_option(argv[i])) {
            char *key = argv[i] + 2; // skip over "--"
            value = strchr(argv[i], '=');
            if (value)
                *value = '\0'; // zap the '=' so that key is trimmed
            for (opt = opts; opt->long_option; opt++)
                if (strcmp(opt->long_option, key) == 0)
                    break;
            if (!opt->long_option)
                errx(EXIT_FAILURE, "Unknown option '%s'", key);

            if (!value && i < argc - 1 && !is_long_option(argv[i + 1]) && !is_short_option(argv[i + 1]))
                value = argv[++i];
            if (!value)
                value = "on"; // if no value, then this is a boolean argument, so set to on
        } else if (is_short_option(argv[i])) {
            char *key = argv[i] + 1; // skip over the "-"
            for (opt = opts; opt->long_option; opt++)
                if (opt->short_option && strcmp(opt->short_option, key) == 0)
                    break;
            if (!opt->long_option)
                errx(EXIT_FAILURE, "Unknown option '%s'", key);

            if (i < argc - 1)
                value = argv[++i];
            else
                value = "on"; // if no value, then this is a boolean argument, so set to on
        } else
            errx(EXIT_FAILURE, "Unexpected parameter '%s'", argv[i]);

        opt->set(opt, value, 1); // "replace" -> commandline args have highest precedence
    }
}

static void trim_whitespace(char *s)
{
    char *left = s;
    while (*left != 0 && isspace(*left))
        left++;
    char *right = s + strlen(s) - 1;
    while (right >= left && isspace(*right))
        right--;

    int len = right - left + 1;
    if (len)
        memmove(s, left, len);
    s[len] = 0;
}

static void parse_config_line(const char *line, enum config_context context)
{
    char *str = strdup(line);
    // Trim everything after a comment
    char *comment = strchr(str, '#');
    if (comment)
        *comment = '\0';

    // Trim whitespace off the beginning and end
    trim_whitespace(str);

    if (*str == '\0') {
        free(str);
        return;
    }

    char *key = str;
    char *value = strchr(str, '=');
    if (value) {
        *value = '\0';
        value++;
        trim_whitespace(value);
        trim_whitespace(key);
    } else
        value = "on";

    const struct raspi_config_opt *opt;
    for (opt = opts; opt->long_option; opt++)
        if (strcmp(opt->long_option, key) == 0)
            break;
    if (!opt->long_option) {
        // Error out if we're parsing a file; otherwise ignore the bad option
        if (context == config_context_file)
            errx(EXIT_FAILURE, "Unknown option '%s' in file '%s'", key, state.config_filename);
        else {
            free(str);
            return;
        }
    }

    switch (context) {
    case config_context_file:
        opt->set(opt, value, 0); // "don't replace" -> file arguments can be overridden by the environment and commandline
        break;

    case config_context_client_request:
        opt->set(opt, value, 1);
        if (opt->apply)
            opt->apply(opt, context);
        break;
    }
    free(str);
}

static void load_config_file()
{
    if (!state.config_filename)
        return;

    FILE *fp = fopen(state.config_filename, "r");
    if (!fp)
        err(EXIT_FAILURE, "Cannot open '%s'", state.config_filename);

    char line[128];
    while (fgets(line, sizeof(line), fp))
        parse_config_line(line, config_context_file);

    fclose(fp);
}

static void remove_server_lock()
{
    unlink(state.lock_filename);
}

// Return 0 if a server's running; 1 if we are the server now
static int acquire_server_lock()
{
    // This lock isn't meant to protect against race conditions. It's just meant
    // to provide a better error message if the user accidentally starts up a
    // second server.
    const char *lockfile = getenv(RASPIJPGS_LOCKFILE);
    FILE *fp = fopen(lockfile, "r");
    if (fp) {
        char server_pid_str[16];
        pid_t server_pid;

        // Check that there's a process behind the pid in the lock file.
        if (fgets(server_pid_str, sizeof(server_pid_str), fp) != NULL &&
            (server_pid = strtoul(server_pid_str, NULL, 10)) != 0 &&
            server_pid > 0 &&
            kill(server_pid, 0) == 0) {
            // Yes, so we can't be a server.
            fclose(fp);
            return 0;
        }

        fclose(fp);
    }

    fp = fopen(lockfile, "w");
    if (!fp)
        err(EXIT_FAILURE, "Can't open lock file '%s'", lockfile);

    if (fprintf(fp, "%d", getpid()) < 0)
        err(EXIT_FAILURE, "Can't write to '%s'", lockfile);
    fclose(fp);

    // Record the name of the lock file that we used so that it
    // can be removed automatically on termination.
    state.lock_filename = strdup(lockfile);
    atexit(remove_server_lock);

    return 1;
}

static void add_client(const struct sockaddr_un *client_addr)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (state.client_addrs[i].sun_family == 0) {
            state.client_addrs[i] = *client_addr;
            return;
        }
    }
    warnx("Reached max number of clients (%d)", MAX_CLIENTS);
}

static void term_sighandler(int signum)
{
    UNUSED(signum);
    // Capture no more frames.
    state.count = 0;
}

static void cleanup_server()
{
    close(state.socket_fd);
    unlink(state.server_addr.sun_path);
}

static void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    // This is called from another thread. Don't access any data here.
    UNUSED(port);

    if(buffer->cmd != MMAL_EVENT_PARAMETER_CHANGED)
        errx(EXIT_FAILURE, "Camera sent invalid data");

    mmal_buffer_header_release(buffer);
}

#include <sys/syscall.h>
static int gettid()
{
	return syscall(SYS_gettid);
}

static void distribute_jpeg(char *buf, int len)
{

    printf("got %d; pid=%d,%d\n", len, getpid(), gettid());
}

static void jpegencoder_buffer_callback_impl()
{
    void *msg[2];
    if (read(state.mmal_callback_pipe[0], msg, sizeof(msg)) != sizeof(msg))
        err(EXIT_FAILURE, "read from internal pipe broke");

    MMAL_PORT_T *port = (MMAL_PORT_T *) msg[0];
    MMAL_BUFFER_HEADER_T *buffer = (MMAL_BUFFER_HEADER_T *) msg[1];

    mmal_buffer_header_mem_lock(buffer);

    if (state.buffer_ix == 0 &&
            (buffer->flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END) &&
            buffer->length <= MAX_DATA_BUFFER_SIZE) {
        // Easy case: JPEG all in one buffer
        distribute_jpeg(buffer->data, buffer->length);
    } else {
        // Hard case: assemble JPEG
        if (state.buffer_ix + buffer->length > MAX_DATA_BUFFER_SIZE) {
            warnx("Frame too large (%d bytes). Dropping. Adjust MAX_DATA_BUFFER_SIZE.", state.buffer_ix + buffer->length);
        } else {
            memcpy(&state.buffer[state.buffer_ix], buffer->data, buffer->length);
            state.buffer_ix += buffer->length;
            if (buffer->flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END)
                distribute_jpeg(state.buffer, state.buffer_ix);
        }
    }

    mmal_buffer_header_mem_unlock(buffer);

    if (state.count >= 0)
        state.count--;

    //cam_set_annotation();

    mmal_buffer_header_release(buffer);

    if (port->is_enabled) {
        MMAL_BUFFER_HEADER_T *new_buffer;

        if (!(new_buffer = mmal_queue_get(state.pool_jpegencoder->queue)) ||
             mmal_port_send_buffer(port, new_buffer) != MMAL_SUCCESS)
            errx(EXIT_FAILURE, "Could not send buffers to port");
    }
}

static void jpegencoder_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    // If the buffer contains something, notify our main thread to process it.
    // If not, recycle it.
    if (buffer->length) {
        void *msg[2];
        msg[0] = port;
        msg[1] = buffer;
        if (write(state.mmal_callback_pipe[1], msg, sizeof(msg)) != sizeof(msg))
            err(EXIT_FAILURE, "write to internal pipe broke");
    } else {
        mmal_buffer_header_release(buffer);

        if (port->is_enabled) {
            MMAL_BUFFER_HEADER_T *new_buffer;

            if (!(new_buffer = mmal_queue_get(state.pool_jpegencoder->queue)) ||
                 mmal_port_send_buffer(port, new_buffer) != MMAL_SUCCESS)
                errx(EXIT_FAILURE, "Could not send buffers to port");
        }
    }
}

void start_all()
{
    //
    // create camera
    //
    if (mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &state.camera) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not create camera");
    if (mmal_port_enable(state.camera->control, camera_control_callback) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not enable camera control port");

    int video_width = 1920;
    int video_height = 1080;
    MMAL_PARAMETER_CAMERA_CONFIG_T cam_config = {
        {MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config)},
        .max_stills_w = 2592, //image_width,
        .max_stills_h = 1944, //image_height,
        .stills_yuv422 = 0,
        .one_shot_stills = 1,
        .max_preview_video_w = video_width,
        .max_preview_video_h = video_height,
        .num_preview_video_frames = 3,
        .stills_capture_circular_buffer_height = 0,
        .fast_preview_resume = 0,
        .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
    };
    mmal_port_parameter_set(state.camera->control, &cam_config.hdr);

    MMAL_ES_FORMAT_T *format = state.camera->output[0]->format;
    format->es->video.width = video_width;
    format->es->video.height = video_height;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = video_width;
    format->es->video.crop.height = video_height;
    format->es->video.frame_rate.num = 0;
    format->es->video.frame_rate.den = 1;
    if (mmal_port_format_commit(state.camera->output[0]) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Coult not set preview format");

    if (mmal_component_enable(state.camera) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not enable camera");

    //
    // create jpeg-encoder
    //
    MMAL_STATUS_T status = mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_ENCODER, &state.jpegencoder);
    if (status != MMAL_SUCCESS && status != MMAL_ENOSYS)
        errx(EXIT_FAILURE, "Could not create image encoder");

    mmal_format_copy(state.jpegencoder->output[0]->format, state.jpegencoder->input[0]->format); // WHAT???
    state.jpegencoder->output[0]->format->encoding = MMAL_ENCODING_JPEG;
    state.jpegencoder->output[0]->buffer_size = state.jpegencoder->output[0]->buffer_size_recommended;
    if (state.jpegencoder->output[0]->buffer_size < state.jpegencoder->output[0]->buffer_size_min)
        state.jpegencoder->output[0]->buffer_size = state.jpegencoder->output[0]->buffer_size_min;
    state.jpegencoder->output[0]->buffer_num = state.jpegencoder->output[0]->buffer_num_recommended;
    if(state.jpegencoder->output[0]->buffer_num < state.jpegencoder->output[0]->buffer_num_min)
        state.jpegencoder->output[0]->buffer_num = state.jpegencoder->output[0]->buffer_num_min;
    if (mmal_port_format_commit(state.jpegencoder->output[0]) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set image format");

    int quality = strtol(getenv(RASPIJPGS_QUALITY), 0, 0);
    if (mmal_port_parameter_set_uint32(state.jpegencoder->output[0], MMAL_PARAMETER_JPEG_Q_FACTOR, quality) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set jpeg quality");

    if (mmal_component_enable(state.jpegencoder) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not enable image encoder");
    state.pool_jpegencoder = mmal_port_pool_create(state.jpegencoder->output[0], state.jpegencoder->output[0]->buffer_num, state.jpegencoder->output[0]->buffer_size);
    if (!state.pool_jpegencoder)
        errx(EXIT_FAILURE, "Could not create image buffer pool");

    //
    // create image-resizer
    //
    int width = strtol(getenv(RASPIJPGS_WIDTH), 0, 0);
    unsigned int height_temp = (unsigned long int)width*video_height/video_width;
    height_temp -= height_temp%16;
    status = mmal_component_create("vc.ril.resize", &state.resizer);
    if (status != MMAL_SUCCESS && status != MMAL_ENOSYS)
        errx(EXIT_FAILURE, "Could not create image resizer");

    format = state.resizer->output[0]->format;
    format->es->video.width = width;
    format->es->video.height = height_temp;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = width;
    format->es->video.crop.height = height_temp;
    format->es->video.frame_rate.num = 30;
    format->es->video.frame_rate.den = 1;
    if (mmal_port_format_commit(state.resizer->output[0]) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not set image resizer output");

    if (mmal_component_enable(state.resizer) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not enable image resizer");

    //
    // connect
    //
    if (mmal_connection_create(&state.con_cam_res, state.camera->output[0], state.resizer->input[0], MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not create connection camera -> resizer");
    if (mmal_connection_enable(state.con_cam_res) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not enable connection camera -> resizer");

    if (mmal_connection_create(&state.con_res_jpeg, state.resizer->output[0], state.jpegencoder->input[0], MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not create connection resizer -> encoder");
    if (mmal_connection_enable(state.con_res_jpeg) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not enable connection resizer -> encoder");

    if (mmal_port_enable(state.jpegencoder->output[0], jpegencoder_buffer_callback) != MMAL_SUCCESS)
        errx(EXIT_FAILURE, "Could not enable jpeg port");
    int max = mmal_queue_length(state.pool_jpegencoder->queue);
    int i;
    for (i = 0; i < max; i++) {
        MMAL_BUFFER_HEADER_T *jpegbuffer = mmal_queue_get(state.pool_jpegencoder->queue);
        if (!jpegbuffer)
            errx(EXIT_FAILURE, "Could not create jpeg buffer header");
        if (mmal_port_send_buffer(state.jpegencoder->output[0], jpegbuffer) != MMAL_SUCCESS)
            errx(EXIT_FAILURE, "Could not send buffers to jpeg port");
    }
#if 0
    //
    // settings
    //
    cam_set_sharpness();
    cam_set_contrast();
    cam_set_brightness();
    cam_set_saturation();
    cam_set_iso();
    cam_set_vs();
    cam_set_ec();
    cam_set_em();
    cam_set_wb();
    cam_set_mm();
    cam_set_ie();
    cam_set_ce();
    cam_set_rotation();
    cam_set_flip();
    cam_set_roi();
    cam_set_ss();
    cam_set_annotation();
#endif
}

void stop_all()
{
    mmal_port_disable(state.jpegencoder->output[0]);
    mmal_connection_destroy(state.con_cam_res);
    mmal_connection_destroy(state.con_res_jpeg);
    mmal_port_pool_destroy(state.jpegencoder->output[0], state.pool_jpegencoder);
    mmal_component_disable(state.jpegencoder);
    mmal_component_disable(state.camera);
    mmal_component_destroy(state.jpegencoder);
    mmal_component_destroy(state.camera);
}

static void server_service_client()
{
    struct sockaddr_un from_addr = {0};
    socklen_t from_addr_len = sizeof(struct sockaddr_un);

    int bytes_received = recvfrom(state.socket_fd,
                                  state.buffer, MAX_DATA_BUFFER_SIZE, 0,
                                  &from_addr, &from_addr_len);
    if (bytes_received < 0) {
        if (errno == EINTR)
            continue;

        err(EXIT_FAILURE, "recvfrom");
    }

    add_client(&from_addr);

    state.buffer[bytes_received] = 0;
    printf("Got %d bytes from client\n", bytes_received);
    printf("'%s'\n", state.buffer);
    char *line = state.buffer;
    char *line_end;
    do {
        line_end = strchr(line, '\n');
        if (line_end)
            *line_end = '\0';
        parse_config_line(line, config_context_client_request);
        line = line_end + 1;
    } while (line_end);

    // Test sending.
    int i;
    for (i = 0; i < 5; i++) {
        int j;
        for (j = 0; j < MAX_CLIENTS; j++) {
            if (state.client_addrs[j].sun_family) {
                if (sendto(state.socket_fd, "hello\n", 6, 0, &state.client_addrs[j], sizeof(struct sockaddr_un)) < 0) {
                    // If failure, then remove client.
                    state.client_addrs[j].sun_family = 0;
                }
            }
        }
    }
}

static void server_service_mmal()
{
    jpegencoder_buffer_callback_impl();
}

static void server_loop()
{
    // Check if the user meant to run as a client and the server is dead
    if (state.setlist)
        errx(EXIT_FAILURE, "Trying to run a set operation, but a raspijpgs server isn't running.");

    // Init hardware
    bcm_host_init();

    // Create the file descriptors for getting back to the main thread
    // from the MMAL callbacks.
    if (pipe(state.mmal_callback_pipe) < 0)
        err(EXIT_FAILURE, "pipe");

    start_all();
    apply_parameters(config_context_server_start);

    // Init communications
    unlink(state.server_addr.sun_path);
    if (bind(state.socket_fd, (const struct sockaddr *) &state.server_addr, sizeof(struct sockaddr_un)) < 0)
        err(EXIT_FAILURE, "Can't create Unix Domain socket at %s", state.server_addr.sun_path);
    atexit(cleanup_server);

    printf("server started in pid %d, tid %d\n", getpid(), gettid());

    // Main loop - keep going until we don't want any more JPEGs.
    struct pollfd fds[2];
    fds[0].fd = state.socket_fd;
    fds[0].events = POLLIN;
    fds[1].fd = state.mmal_callback_pipe[0];
    fds[1].events = POLLIN;

    while (state.count != 0) {
        int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno != EINTR)
                err(EXIT_FAILURE, "poll");
        } else {
            if (fds[0].revents)
                server_service_client();
            if (fds[1].revents)
                server_service_mmal();
        }
    }

    stop_all();
    close(state.mmal_callback_pipe[0]);
    close(state.mmal_callback_pipe[1]);
}

static void cleanup_client()
{
    close(state.socket_fd);
    unlink(state.client_addrs[0].sun_path);
}

static void client_loop()
{
    if (!getenv(RASPIJPGS_OUTPUT)) {
        // If no output, force the number of jpegs to capture to be 0 (no place to store them)
        setenv(RASPIJPGS_COUNT, "0", 1);

        if (!state.setlist)
            errx(EXIT_FAILURE, "No sets and no place to store output, so nothing to do.\n"
                               "If you meant to run as a server, there's one already running.");
    }

    // Create a unix domain socket for messages from the server.
    state.client_addrs[0].sun_family = AF_UNIX;
    sprintf(state.client_addrs[0].sun_path, "%s.client.%d", state.server_addr.sun_path, getpid());
    unlink(state.client_addrs[0].sun_path);
    if (bind(state.socket_fd, (const struct sockaddr *) &state.client_addrs[0], sizeof(struct sockaddr_un)) < 0)
        err(EXIT_FAILURE, "Can't create Unix Domain socket at %s", state.client_addrs[0].sun_path);
    atexit(cleanup_client);

    // Send our "sets" to the server or an empty string to make
    // contact with the server so that it knows about us.
    const char *setlist = state.setlist;
    if (!setlist)
        setlist = "";
    int tosend = strlen(setlist);
    int sent = sendto(state.socket_fd, setlist, tosend, 0,
                      (struct sockaddr *) &state.server_addr,
                      sizeof(struct sockaddr_un));
    if (sent != tosend)
        err(EXIT_FAILURE, "Error communicating with server");

    // Main loop - keep going until we don't want any more JPEGs.
    while (state.count != 0) {
        struct sockaddr_un from_addr = {0};
        socklen_t from_addr_len = sizeof(struct sockaddr_un);

        int bytes_received = recvfrom(state.socket_fd,
                                      state.buffer, MAX_DATA_BUFFER_SIZE, 0,
                                      &from_addr, &from_addr_len);
        if (bytes_received < 0) {
            if (errno == EINTR)
                continue;

            err(EXIT_FAILURE, "recvfrom");
        }
        if (from_addr.sun_family != state.server_addr.sun_family ||
            strcmp(from_addr.sun_path, state.server_addr.sun_path) != 0) {
            warnx("Dropping message from unexpected sender %s. Server should be %s",
                  from_addr.sun_path,
                  state.server_addr.sun_path);
            continue;
        }

        printf("Got %d bytes from server\n", bytes_received);
    }
}

int main(int argc, char* argv[])
{    
    // Parse commandline and config file arguments
    parse_args(argc, argv);
    load_config_file();

    // If anything still isn't set, then fill-in with defaults
    fillin_defaults();

    if (state.user_wants_client && state.user_wants_server)
        errx(EXIT_FAILURE, "Both --client and --server requested");

    state.buffer = (char *) malloc(MAX_DATA_BUFFER_SIZE);
    if (!state.buffer)
        err(EXIT_FAILURE, "malloc");

    // Capture SIGINT and SIGTERM so that we exit gracefully
    struct sigaction action = {0};
    action.sa_handler = term_sighandler;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    state.is_server = acquire_server_lock();
    if (state.user_wants_client && state.is_server)
        errx(EXIT_FAILURE, "Server not running");
    if (state.user_wants_server && !state.is_server)
        errx(EXIT_FAILURE, "Server already running");

    // Init datagram socket - needed for both server and client
    state.socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (state.socket_fd < 0)
        err(EXIT_FAILURE, "socket");

    state.server_addr.sun_family = AF_UNIX;
    strcpy(state.server_addr.sun_path, getenv(RASPIJPGS_SOCKET));

    if (state.is_server)
        server_loop();
    else
        client_loop();

    free(state.buffer);
    exit(EXIT_SUCCESS);
}
