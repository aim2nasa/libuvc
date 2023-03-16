#include "libuvc/libuvc.h"
#include <stdio.h>
#include <unistd.h>

void cb(uvc_frame_t *frame, void *ptr) {
  printf("frame_format = %d, width = %d, height = %d, length = %lu\n",
    frame->frame_format, frame->width, frame->height, frame->data_bytes);
}

void payload_cb(uint8_t *payload, size_t payload_len) {
  printf("[%zd] %x %x %x %x\n", payload_len, payload[0],payload[1],payload[2],payload[3]);
}

int main(int argc, char **argv) {
  uvc_context_t *ctx;
  uvc_device_t *dev;
  uvc_device_handle_t *devh;
  uvc_stream_ctrl_t ctrl;
  uvc_error_t res;

  /* Initialize a UVC service context. Libuvc will set up its own libusb
   * context. Replace NULL with a libusb_context pointer to run libuvc
   * from an existing libusb context. */
  res = uvc_init(&ctx, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_init");
    return res;
  }

  puts("UVC initialized");

  /* Locates the first attached UVC device, stores in dev */
  res = uvc_find_device(
      ctx, &dev,
      0, 0, NULL); /* filter devices: vendor_id, product_id, "serial_num" */

  if (res < 0) {
    uvc_perror(res, "uvc_find_device"); /* no devices found */
  } else {
    puts("Device found");

    /* Try to open the device: requires exclusive access */
    res = uvc_open(dev, &devh);

    if (res < 0) {
      uvc_perror(res, "uvc_open"); /* unable to open device */
    } else {
      puts("Device opened");

      /* Print out a message containing all the information that libuvc
       * knows about the device */
      uvc_print_diag(devh, stderr);

      const uvc_format_desc_t *format_desc = uvc_get_format_descs(devh);
      const uvc_frame_desc_t *frame_desc = format_desc->frame_descs;
      enum uvc_frame_format frame_format;
      int width = 640;
      int height = 480;
      int fps = 30;

      switch (format_desc->bDescriptorSubtype) {
      case UVC_VS_FORMAT_MJPEG:
        frame_format = UVC_COLOR_FORMAT_MJPEG;
        break;
      case UVC_VS_FORMAT_FRAME_BASED:
        frame_format = UVC_FRAME_FORMAT_H264;
        break;
      default:
        frame_format = UVC_FRAME_FORMAT_YUYV;
        break;
      }

      if (frame_desc) {
        width = frame_desc->wWidth;
        height = frame_desc->wHeight;
        fps = 10000000 / frame_desc->dwDefaultFrameInterval;
      }

      printf("\nFirst format: (%4s) %dx%d %dfps\n", format_desc->fourccFormat, width, height, fps);

      /* Try to negotiate first stream profile */
      res = uvc_get_stream_ctrl_format_size(
          devh, &ctrl, /* result stored in ctrl */
          frame_format,
          width, height, fps /* width, height, fps */
      );

      /* Print out the result */
      uvc_print_stream_ctrl(&ctrl, stderr);

      if (res < 0) {
        uvc_perror(res, "get_mode"); /* device doesn't provide a matching stream */
      } else {
        /* Start the video stream. The library will call user function cb:
         *   cb(frame, (void *) 12345)
         */
        res = uvc_start_streaming2(devh, &ctrl, cb, payload_cb, (void *) 12345, 0);

        if (res < 0) {
          uvc_perror(res, "start_streaming"); /* unable to start stream */
        } else {
          puts("Streaming...");

          /* enable auto exposure - see uvc_set_ae_mode documentation */
          puts("Enabling auto exposure ...");
          const uint8_t UVC_AUTO_EXPOSURE_MODE_AUTO = 2;
          res = uvc_set_ae_mode(devh, UVC_AUTO_EXPOSURE_MODE_AUTO);
          if (res == UVC_SUCCESS) {
            puts(" ... enabled auto exposure");
          } else if (res == UVC_ERROR_PIPE) {
            /* this error indicates that the camera does not support the full AE mode;
             * try again, using aperture priority mode (fixed aperture, variable exposure time) */
            puts(" ... full AE not supported, trying aperture priority mode");
            const uint8_t UVC_AUTO_EXPOSURE_MODE_APERTURE_PRIORITY = 8;
            res = uvc_set_ae_mode(devh, UVC_AUTO_EXPOSURE_MODE_APERTURE_PRIORITY);
            if (res < 0) {
              uvc_perror(res, " ... uvc_set_ae_mode failed to enable aperture priority mode");
            } else {
              puts(" ... enabled aperture priority auto exposure mode");
            }
          } else {
            uvc_perror(res, " ... uvc_set_ae_mode failed to enable auto exposure mode");
          }

          sleep(10); /* stream for 10 seconds */

          /* End the stream. Blocks until last callback is serviced */
          uvc_stop_streaming(devh);
          puts("Done streaming.");
        }
      }

      /* Release our handle on the device */
      uvc_close(devh);
      puts("Device closed");
    }

    /* Release the device descriptor */
    uvc_unref_device(dev);
  }

  /* Close the UVC context. This closes and cleans up any existing device handles,
   * and it closes the libusb context if one was not provided. */
  uvc_exit(ctx);
  puts("UVC exited");

  return 0;
}

