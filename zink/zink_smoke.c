/*
 * Outsider PS5 - Zink smoke test: OpenGL through Mesa's Zink on RADV, before anything of Outsider runs on it.
 *
 * zink-smoke.bin, the title's third program, started from the game list's "Zink test" button. It opens the EGL
 * subset of zink/ps5-mesa-zink.patch (ps5_zink_egl.c), logs what the GL reports, and for about ten seconds clears
 * the screen through a colour cycle and draws a triangle with a shader written as WebGL 1 writes them (GLSL ES 1.00:
 * attribute, varying, precision, gl_FragColor), the shaders a WebGL layer would hand the GL unchanged. Then it
 * goes back to the game list.
 *
 * The log is /data/outsider-ps5/zink-smoke.log, and klog. Lines that matter:
 *   GL_RENDERER / GL_VERSION     Zink is up, on RADV
 *   es100 shader: ok             GLSL ES 1.00 compiles on the desktop context (ARB_ES2_compatibility)
 *   fps=...                      presents, and how fast
 *
 * Copyright (C) 2026 the Outsider PS5 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>

/* glext.h has no pointer types for OpenGL 1.0 and 1.1 */
typedef const GLubyte *(APIENTRYP PFNGLGETSTRINGPROC)(GLenum name);
typedef GLenum(APIENTRYP PFNGLGETERRORPROC)(void);
typedef void(APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void(APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void(APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void(APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);

int sceSystemServiceLoadExec(const char *path, const char *const *argv);
int sceKernelDebugOutText(int channel, const char *text);

/* ps5_zink_egl.c sends Mesa's stderr to klog with this, from the newer payload SDK forks' platform layer. This
   no-op lets the program link with an SDK without it; it can also shadow the platform's (an archive member is not
   pulled in for a name already defined), and Mesa's own messages are then not in klog. Drop it once the image's SDK
   is known to have the function. */
__attribute__((weak)) int ps5_klog_capture_stderr(const char *prefix)
{
	(void)prefix;
	return 0;
}

static FILE *logFile;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	sceKernelDebugOutText(0, line);
	if (logFile) {
		fputs(line, logFile);
		fflush(logFile);
	}
}

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

#define GL_FUNCTIONS(X)                                                                                            \
	X(PFNGLGETSTRINGPROC, glGetString)                                                                         \
	X(PFNGLGETERRORPROC, glGetError)                                                                           \
	X(PFNGLVIEWPORTPROC, glViewport)                                                                           \
	X(PFNGLCLEARCOLORPROC, glClearColor)                                                                       \
	X(PFNGLCLEARPROC, glClear)                                                                                 \
	X(PFNGLCREATESHADERPROC, glCreateShader)                                                                   \
	X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                                   \
	X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                                 \
	X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                                     \
	X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                           \
	X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                                 \
	X(PFNGLATTACHSHADERPROC, glAttachShader)                                                                   \
	X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                                                       \
	X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                                     \
	X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                                   \
	X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                                         \
	X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                                       \
	X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                                       \
	X(PFNGLUNIFORM1FPROC, glUniform1f)                                                                         \
	X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                                       \
	X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                                       \
	X(PFNGLBUFFERDATAPROC, glBufferData)                                                                       \
	X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                             \
	X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                             \
	X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                             \
	X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                                     \
	X(PFNGLDRAWARRAYSPROC, glDrawArrays)

#define DECLARE(type, name) static type p_##name;
GL_FUNCTIONS(DECLARE)

static int loadGL(void)
{
	int missing = 0;
#define LOAD(type, name)                                                                                           \
	if (!(p_##name = (type)eglGetProcAddress(#name))) {                                                        \
		say("[zink-smoke] missing %s\n", #name);                                                           \
		missing++;                                                                                         \
	}
	GL_FUNCTIONS(LOAD)
	return missing == 0;
}

/* A WebGL 1 shader pair, as a plugin's PIXI filter or raw WebGL code would pass it */
static const char *const vertexSource = "#version 100\n"
					"attribute vec2 aPosition;\n"
					"attribute vec3 aColor;\n"
					"uniform float uTime;\n"
					"varying vec3 vColor;\n"
					"void main() {\n"
					"    float c = cos(uTime), s = sin(uTime);\n"
					"    gl_Position = vec4(mat2(c, s, -s, c) * aPosition, 0.0, 1.0);\n"
					"    vColor = aColor;\n"
					"}\n";
static const char *const fragmentSource = "#version 100\n"
					  "precision mediump float;\n"
					  "varying vec3 vColor;\n"
					  "void main() {\n"
					  "    gl_FragColor = vec4(vColor, 1.0);\n"
					  "}\n";

static GLuint compile(GLenum stage, const char *source)
{
	GLuint shader = p_glCreateShader(stage);
	GLint ok = 0;
	char info[1024] = "";
	p_glShaderSource(shader, 1, &source, NULL);
	p_glCompileShader(shader);
	p_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		p_glGetShaderInfoLog(shader, sizeof info, NULL, info);
		say("[zink-smoke] es100 %s shader: failed: %s\n", stage == GL_VERTEX_SHADER ? "vertex" : "fragment",
		    info);
		return 0;
	}
	return shader;
}

static GLuint buildProgram(void)
{
	GLuint vs = compile(GL_VERTEX_SHADER, vertexSource);
	GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSource);
	if (!vs || !fs)
		return 0;
	GLuint program = p_glCreateProgram();
	GLint ok = 0;
	char info[1024] = "";
	p_glAttachShader(program, vs);
	p_glAttachShader(program, fs);
	p_glBindAttribLocation(program, 0, "aPosition");
	p_glBindAttribLocation(program, 1, "aColor");
	p_glLinkProgram(program);
	p_glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		p_glGetProgramInfoLog(program, sizeof info, NULL, info);
		say("[zink-smoke] es100 shader: link failed: %s\n", info);
		return 0;
	}
	say("[zink-smoke] es100 shader: ok\n");
	return program;
}

static void run(void)
{
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0, n = 0, w = 0, h = 0;
	EGLConfig cfg;
	if (!eglInitialize(dpy, &major, &minor)) {
		say("[zink-smoke] eglInitialize failed 0x%x\n", eglGetError());
		return;
	}
	eglChooseConfig(dpy, NULL, &cfg, 1, &n);
	EGLSurface surf = eglCreateWindowSurface(dpy, cfg, 0, NULL);
	if (surf == EGL_NO_SURFACE) {
		say("[zink-smoke] eglCreateWindowSurface failed 0x%x\n", eglGetError());
		return;
	}
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
	if (ctx == EGL_NO_CONTEXT) {
		say("[zink-smoke] eglCreateContext failed 0x%x\n", eglGetError());
		return;
	}
	if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
		say("[zink-smoke] eglMakeCurrent failed 0x%x\n", eglGetError());
		return;
	}
	if (!loadGL())
		return;
	say("[zink-smoke] EGL %d.%d\n", major, minor);
	say("[zink-smoke] GL_VENDOR=%s\n", (const char *)p_glGetString(GL_VENDOR));
	say("[zink-smoke] GL_RENDERER=%s\n", (const char *)p_glGetString(GL_RENDERER));
	say("[zink-smoke] GL_VERSION=%s\n", (const char *)p_glGetString(GL_VERSION));
	say("[zink-smoke] GL_SHADING_LANGUAGE_VERSION=%s\n", (const char *)p_glGetString(GL_SHADING_LANGUAGE_VERSION));
	eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
	eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
	say("[zink-smoke] surface %dx%d\n", w, h);

	static const GLfloat vertices[] = {
		-0.6f, -0.5f, 1, 0, 0, 0.6f, -0.5f, 0, 1, 0, 0.0f, 0.6f, 0, 0, 1,
	};
	GLuint vao = 0, vbo = 0;
	p_glGenVertexArrays(1, &vao);
	p_glBindVertexArray(vao);
	p_glGenBuffers(1, &vbo);
	p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
	p_glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
	p_glEnableVertexAttribArray(0);
	p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (const void *)0);
	p_glEnableVertexAttribArray(1);
	p_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (const void *)(2 * sizeof(GLfloat)));
	GLuint program = buildProgram();
	GLint uTime = program ? p_glGetUniformLocation(program, "uTime") : -1;
	if (program)
		p_glUseProgram(program);
	p_glViewport(0, 0, w, h);
	eglSwapInterval(dpy, 1);

	double start = now(), last = start;
	int frames = 0, total = 0;
	while (now() - start < 10.0) {
		float t = (float)(now() - start);
		p_glClearColor(0.5f + 0.5f * (t - (int)t), 0.2f, 0.6f - 0.05f * t, 1.0f);
		p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (program) {
			p_glUniform1f(uTime, t);
			p_glDrawArrays(GL_TRIANGLES, 0, 3);
		}
		if (!eglSwapBuffers(dpy, surf)) {
			say("[zink-smoke] eglSwapBuffers failed 0x%x at frame %d\n", eglGetError(), total);
			break;
		}
		frames++;
		total++;
		if (now() - last >= 1.0) {
			say("[zink-smoke] fps=%.1f frames=%d\n", frames / (now() - last), total);
			frames = 0;
			last = now();
		}
	}
	GLenum error = p_glGetError();
	say("[zink-smoke] done, %d frames, glGetError 0x%x\n", total, error);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

int main(void)
{
	logFile = fopen("/data/outsider-ps5/zink-smoke.log", "w");
	say("[zink-smoke] start\n");
	run();
	say("[zink-smoke] back to the game list\n");
	if (logFile)
		fclose(logFile);
	static const char *const noArgs[] = { NULL };
	if (sceSystemServiceLoadExec("/app0/eboot.bin", noArgs) < 0)
		sceSystemServiceLoadExec("exit", NULL);
	for (;;)
		sleep(1);
}
