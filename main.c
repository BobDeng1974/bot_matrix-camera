/*
 * Copyright Brian Starkey <stark3y@gmail.com> 2017
 *
 * With thanks to Ciro Santilli for his example here:
 * https://github.com/cirosantilli/cpp-cheat/blob/master/opengl/gles/triangle.c
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include <pam.h>

#include <GLES2/gl2.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include "pint.h"
#include "shader.h"
#include "texture.h"
#include "mesh.h"
#include "feed.h"
#include "drawcall.h"

#include "EGL/egl.h"

#if defined(USE_PI_CAMERA)
#include "camera.h"
#include "EGL/eglext.h"
#include "EGL/eglext.h"
#include "EGL/eglext_brcm.h"
#endif

#define check(_cond) { if (!(_cond)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(errno)); exit(EXIT_FAILURE); }}

#define WIDTH 640
#define HEIGHT 480
#define MESHPOINTS 32

volatile bool should_exit = 0;

void intHandler(int dummy) {
	printf("Caught signal.\n");
	should_exit = 1;
}

/*
 * Simple MVP matrix which flips the Y axis (so 0,0 is top left) and
 * scales/translates everything so that on-screen points are 0-1
 */
static const GLfloat mat[] = {
	2.0f,  0.0f,  0.0f, -1.0f,
	0.0f, -2.0f,  0.0f,  1.0f,
	0.0f,  0.0f,  0.0f,  0.0f,
	0.0f,  0.0f,  0.0f,  1.0f,
};

static const GLfloat mat2[] = {
	0.3f,  0.0f,  0.0f, -1.0f,
	0.0f, -0.3f,  0.0f,  1.0f,
	0.0f,  0.0f,  0.0f,  0.0f,
	0.0f,  0.0f,  0.0f,  1.0f,
};

float coeffs[] = { 0, 0, 0, 1.0 };

void brown(float xcoord, float ycoord, float *xout, float *yout)
{
	double asp = (double)(WIDTH) / (double)(HEIGHT);
	double xoffs = ((double)(WIDTH - HEIGHT) / 2.0f) / (double)HEIGHT;
	float *K = coeffs;
	//double xdiff = (xcoord * 2) - 1;
	double xdiff = (((xcoord * asp) - xoffs) * 2) - 1;
	double ydiff = (ycoord * 2) - 1;
	double r = sqrt(xdiff*xdiff + ydiff*ydiff);
	double newr;
	double xunit;
	double yunit;

	xunit = xdiff / r;
	if (isnan(xunit)) {
		xunit = 0;
	}

	yunit = ydiff / r;
	if (isnan(yunit)) {
		yunit = 0;
	}

	K[3] = K[3] - (K[0] + K[1] + K[2]);

	// Same algorithm used by ImageMagick.
	// Defined by Professor Helmut Dersch:
	// http://replay.waybackmachine.org/20090613040829/http://www.all-in-one.ee/~dersch/barrel/barrel.html
	// http://www.imagemagick.org/Usage/distorts/#barrel
	newr = r * (K[0]*pow(r, 3) + K[1]*pow(r,2) + K[2]*r + K[3]);

	*xout = (((newr*xunit + 1) / 2) + xoffs) / asp;
	*yout = (newr*yunit + 1) / 2;

	/*
	*xout = (0.5 + (xdiff / (1 + K[0]*(r*r) + K[1]*(r*r*r*r))));
	*yout = (0.5 + (ydiff / (1 + K[0]*(r*r) + K[1]*(r*r*r*r))));
	*/

	/*
	*xout = xcoord + (xdiff * K[0] * r * r) + (xdiff * K[1] * r * r *r * r);
	*yout = ycoord + (ydiff * K[0] * r * r) + (ydiff * K[1] * r * r *r * r);
	*/
}

GLint get_shader(void)
{
	char *vertex_shader_source, *fragment_shader_source;

	vertex_shader_source = shader_load("vertex_shader.glsl");
	if (!vertex_shader_source) {
		return -1;
	}
	printf("Vertex shader:\n");
	printf("%s\n", vertex_shader_source);

#if defined(USE_PI_CAMERA)
	fragment_shader_source = shader_load("fragment_external_oes_shader.glsl");
#else
	fragment_shader_source = shader_load("fragment_shader.glsl");
#endif
	if (!fragment_shader_source) {
		return -1;
	}
	printf("Fragment shader:\n");
	printf("%s\n", fragment_shader_source);

	return shader_compile(vertex_shader_source, fragment_shader_source);
}

struct mesh {
	GLfloat *mesh;
	unsigned int nverts;
	GLuint mhandle;

	GLshort *indices;
	unsigned int nindices;
	GLuint ihandle;
};

struct mesh *get_mesh()
{
	struct mesh *mesh = calloc(1, sizeof(*mesh));
	if (!mesh) {
		return NULL;
	}

	mesh->mesh = mesh_build(MESHPOINTS, MESHPOINTS, brown, &mesh->nverts);
	if (!mesh->mesh) {
		free(mesh);
		return NULL;
	}

	glGenBuffers(1, &mesh->mhandle);
	glBindBuffer(GL_ARRAY_BUFFER, mesh->mhandle);
	glBufferData(GL_ARRAY_BUFFER, sizeof(mesh->mesh[0]) * mesh->nverts, mesh->mesh, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	mesh->indices = mesh_build_indices(MESHPOINTS, MESHPOINTS, &mesh->nindices);
	if (!mesh->indices) {
		free(mesh->mesh);
		free(mesh);
		return NULL;
	}

	glGenBuffers(1, &mesh->ihandle);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ihandle);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(mesh->indices[0]) * mesh->nindices, mesh->indices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	return mesh;
}

long elapsed_nanos(struct timespec a, struct timespec b)
{
	unsigned int sec = b.tv_sec - a.tv_sec;
	long nanos = b.tv_nsec - a.tv_nsec;

	return nanos + (1000000000 * sec);
}

GLint posLoc, tcLoc, mvpLoc, texLoc;

struct drawcall *setup_draw(const GLfloat *mat, struct mesh *mesh)
{
	struct drawcall *dc = calloc(1, sizeof(*dc));
	int ret;

	dc->yidx = dc->uidx = dc->vidx = -1;

	ret = get_shader();
	check(ret >= 0);
	dc->shader_program = ret;

	glUseProgram(dc->shader_program);

	posLoc = glGetAttribLocation(dc->shader_program, "position");
	tcLoc = glGetAttribLocation(dc->shader_program, "tc");
	mvpLoc = glGetUniformLocation(dc->shader_program, "mvp");
	texLoc = glGetUniformLocation(dc->shader_program, "tex");

	glBindBuffer(GL_ARRAY_BUFFER, mesh->mhandle);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ihandle);
	glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(mesh->mesh[0]) * 4, (GLvoid*)0);
	glVertexAttribPointer(tcLoc, 2, GL_FLOAT, GL_FALSE, sizeof(mesh->mesh[0]) * 4, (GLvoid*)(sizeof(mesh->mesh[0]) * 2));
	glEnableVertexAttribArray(posLoc);
	glEnableVertexAttribArray(tcLoc);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glUniform1i(texLoc, 0);
	glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mat);

	dc->n_buffers = 2;
	dc->buffers[0] = (struct bind){ .bind = GL_ARRAY_BUFFER, .handle = mesh->mhandle };
	dc->buffers[1] = (struct bind){ .bind = GL_ELEMENT_ARRAY_BUFFER, .handle = mesh->ihandle };
	dc->n_indices = mesh->nindices;
	// free mesh

	dc->n_textures = 1;
	// TEXTURE0 is Y
	dc->yidx = 0;

	dc->draw = draw_elements;

	glUseProgram(0);

	return dc;
}

int main(int argc, char *argv[]) {
	int i;
	struct timespec a, b;
	struct pint *pint = pint_initialise(WIDTH, HEIGHT);
	check(pint);

	signal(SIGINT, intHandler);

	if (argc == 5) {
		sscanf(argv[1], "%f", &coeffs[0]);
		sscanf(argv[2], "%f", &coeffs[1]);
		sscanf(argv[3], "%f", &coeffs[2]);
		sscanf(argv[4], "%f", &coeffs[3]);
	}
	pm_init(argv[0], 0);

	printf("GL_VERSION  : %s\n", glGetString(GL_VERSION) );
	printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER) );

	struct mesh *mesh;
	mesh = get_mesh();
	check(mesh);

	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	glViewport(0, 0, WIDTH, HEIGHT);

	struct feed *feed = feed_init(pint);
	check(feed);

	struct drawcall *dcs[2];
	dcs[0] = setup_draw(mat, mesh);
	check(dcs[0]);
	dcs[1] = setup_draw(mat2, mesh);
	check(dcs[1]);

	clock_gettime(CLOCK_MONOTONIC, &a);
	while(!pint->should_end(pint)) {
		i = feed->dequeue(feed);
		if (i != 0) {
			fprintf(stderr, "Failed dequeueing\n");
			break;
		}

		glClear(GL_COLOR_BUFFER_BIT);

		for (i = 0; i < 2; i++) {
			drawcall_draw(feed, dcs[i]);
		}

		pint->swap_buffers(pint);

		feed->queue(feed);

		clock_gettime(CLOCK_MONOTONIC, &b);
		if (a.tv_sec != b.tv_sec) {
			long time = elapsed_nanos(a, b);
			printf("%.3f fps\n", 1000000000.0 / (float)time);
		}
		a = b;
	}

	feed->terminate(feed);
	pint->terminate(pint);

	return EXIT_SUCCESS;
}
