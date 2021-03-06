/*
 * Copyright Brian Starkey <stark3y@gmail.com> 2017
 */
#include <stdio.h>

#include <GLES2/gl2.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

#include "camera.h"
#include "feed.h"
#include "EGL/eglext.h"
#include "EGL/eglext.h"
#include "EGL/eglext_brcm.h"

#define CAMERA_WIDTH 640
#define CAMERA_HEIGHT 480
#define CAMERA_FPS 60

struct feed_camera {
	struct feed base;

	struct camera *camera;
	struct camera_buffer *buf;
	EGLDisplay display;
	EGLImageKHR yimg, uimg, vimg;
};

static void terminate(struct feed *f)
{
	struct feed_camera *feed = (struct feed_camera *)f;

	if(feed->yimg != EGL_NO_IMAGE_KHR){
		eglDestroyImageKHR(feed->display, feed->yimg);
		feed->yimg = EGL_NO_IMAGE_KHR;
	}
	glDeleteTextures(1, &feed->base.ytex.handle);

	camera_exit(feed->camera);

	free(feed);
}

static int dequeue(struct feed *f)
{
	struct feed_camera *feed = (struct feed_camera *)f;

	feed->buf = camera_dequeue_buffer(feed->camera);
	if (!feed->buf) {
		fprintf(stderr, "Failed to dequeue camera buffer!\n");
		return -1;
	}
	if(feed->yimg != EGL_NO_IMAGE_KHR){
		eglDestroyImageKHR(feed->display, feed->yimg);
		feed->yimg = EGL_NO_IMAGE_KHR;
	}
	feed->yimg = eglCreateImageKHR(feed->display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_MULTIMEDIA_Y, feed->buf->egl_buf, NULL);
	if (feed->yimg == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "Failed to get yimg!\n");
		return -1;
	}
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, feed->base.ytex.handle);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, feed->yimg);

	if(feed->uimg != EGL_NO_IMAGE_KHR){
		eglDestroyImageKHR(feed->display, feed->uimg);
		feed->uimg = EGL_NO_IMAGE_KHR;
	}
	glFinish();

	feed->uimg = eglCreateImageKHR(feed->display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_MULTIMEDIA_Y, feed->buf->egl_buf, NULL);
	if (feed->uimg == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "Failed to get uimg!\n");
		return -1;
	}
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, feed->base.utex.handle);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, feed->uimg);

	if(feed->vimg != EGL_NO_IMAGE_KHR){
		eglDestroyImageKHR(feed->display, feed->vimg);
		feed->vimg = EGL_NO_IMAGE_KHR;
	}
	feed->vimg = eglCreateImageKHR(feed->display, EGL_NO_CONTEXT, EGL_IMAGE_BRCM_MULTIMEDIA_Y, feed->buf->egl_buf, NULL);
	if (feed->vimg == EGL_NO_IMAGE_KHR) {
		fprintf(stderr, "Failed to get vimg!\n");
		return -1;
	}
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, feed->base.vtex.handle);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, feed->vimg);

	glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
	/*
	 * This seems to be needed, otherwise there's garbage for the
	 * first few frames
	 */
	glFinish();

	return 0;
}

static void queue(struct feed *f)
{
	struct feed_camera *feed = (struct feed_camera *)f;
	camera_queue_buffer(feed->camera, feed->buf);
	feed->buf = NULL;
}

struct feed *feed_init(struct pint *pint)
{
	struct feed_camera *feed = calloc(1, sizeof(*feed));
	if (!feed)
		return NULL;

	feed->display = pint->get_egl_display(pint);
	feed->camera = camera_init(CAMERA_WIDTH, CAMERA_HEIGHT, CAMERA_FPS);
	if (!feed->camera) {
		fprintf(stderr, "Camera init failed\n");
		exit(1);
	}

	feed->base.ytex.bind = GL_TEXTURE_EXTERNAL_OES;
	glGenTextures(1, &feed->base.ytex.handle);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, feed->base.ytex.handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	feed->base.utex.bind = GL_TEXTURE_EXTERNAL_OES;
	glGenTextures(1, &feed->base.utex.handle);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, feed->base.utex.handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	feed->base.vtex.bind = GL_TEXTURE_EXTERNAL_OES;
	glGenTextures(1, &feed->base.vtex.handle);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, feed->base.vtex.handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

	feed->base.terminate = terminate;
	feed->base.dequeue = dequeue;
	feed->base.queue = queue;

	return &feed->base;
}
