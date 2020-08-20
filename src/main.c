/* This is an attempt at recreating the astar.py program in c
 * with opengl4 used for visualization.
 * GLFW3 is used as main helper library instead of SDL2, this
 * might change.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <poll.h>
#include <errno.h>

#include <GL/glew.h>
#define GLFW_DLL
#include <GLFW/glfw3.h>
//Found in AUR, hopefully substitution for glm
//#include <cglm/cglm.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>
#define GL_LOG_FILE "gl.log"

//#define INBUILD 1

/* function declarations */

static int initgl();
static char* openshader(const char* filename);
void reload_shader_program(GLuint* program, const char* vertex_shader_filename, const char* fragment_shader_filename);
void handle_events(int fd, int* wd, char* fragment_shader_filename);
char* get_bin_dir();
const char* make_path(const char* filename);
bool restart_gl_log();
bool gl_log(const char* message, ...);
bool gl_log_err(const char* message, ...);
void glfw_error_callback(int error, const char* description);
void glfw_window_size_callback(GLFWwindow* window, unsigned int width, unsigned int height);
void log_gl_params();

/* variables */
GLFWwindow* window;
unsigned int g_gl_width = 640, g_gl_height = 480;

typedef struct watcher {
	char buf;
	int fd, poll_num;
	int *wd;
	nfds_t nfds;
	struct pollfd fds[2];
} watcher; 

static int initgl() {

	assert(restart_gl_log());
	// start GL context and OS window using the GLFW helper library
	gl_log("starting GLFW\n%s\n", glfwGetVersionString());
	// register the error call-back function
	glfwSetErrorCallback(glfw_error_callback);
	if(!glfwInit()) {
		fprintf(stderr, "ERROR: could not start GLFW3\n");
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	glfwWindowHint(GLFW_SAMPLES, 4);

	glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
	glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

	window = glfwCreateWindow(g_gl_width, g_gl_height, "OpenGL and A*", NULL, NULL);
	if(!window) {
		fprintf(stderr, "ERROR: could not create window\n");
		glfwTerminate();
		return 1;
	}
	glfwMakeContextCurrent(window);

	// Start GLEW extension handler
	glewExperimental = GL_TRUE;
	glewInit();

	// Get version info
	const GLubyte* renderer = glGetString(GL_RENDERER);
	const GLubyte* version = glGetString(GL_VERSION);
	printf("Renderer: %s\n", renderer);
	printf("OpenGL version: %s\n", version);

	log_gl_params();

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CW);

	return 0;
}

static char* openshader(const char* filename) {

	const char* location = make_path(filename);
	FILE *fp;

	fp = fopen(location, "r");
	if(!fp){
		fprintf(stderr, "ERROR: could not open file");
		return NULL;
	}

	/* use fseek and ftell to get the length of the file containing the shader */
	if(fseek(fp, 0, SEEK_END) != 0) {
		fprintf(stderr, "ERROR: could not seek to end of File");
		return NULL;
	}
	size_t sizeOfFile = ftell(fp);
	if(fseek(fp, 0, SEEK_SET) != 0) {
		fprintf(stderr, "ERROR: could not seek to beginning of File");
		return NULL;
	}

	char* shaderText = malloc(sizeof(char) * sizeOfFile);
	if(fread(shaderText, sizeOfFile, 1, fp) != 1) { /* The contents should decay okay into the pointer version but it might not */
		fprintf(stderr, "WARNING: empty file encountered or mismatch of nmemb and return value");
	}
	// Crude removal of all linebreaks except the first
	for(unsigned int i = 0, u = 0; i < sizeOfFile; i++) {
		if(shaderText[i] == '\n') {
			if(u >= 1) {
				shaderText[i] = ' ';
			}
			u++;
		}
	}

	fclose(fp);
	return shaderText;
}

void reload_shader_program(GLuint* program, const char* vertex_shader_filename, const char* fragment_shader_filename) {

	assert( program && vertex_shader_filename && fragment_shader_filename );

	GLuint reloaded_program = glCreateProgram();
	const char* vertex_shader = openshader(vertex_shader_filename);
	const char* fragment_shader = openshader(fragment_shader_filename);

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vertex_shader, NULL);
	glCompileShader(vs);
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fragment_shader, NULL);
	glCompileShader(fs);

	glAttachShader(reloaded_program, fs);
	glAttachShader(reloaded_program, vs);
	glLinkProgram(reloaded_program);

	if( reloaded_program ) {
		glDeleteProgram( *program );
		*program = reloaded_program;
	}
}

void handle_events(int fd, int* wd, char* fragment_shader_filename) { /* TODO: use pointer to struct */
	char buf[4096];
	const struct inotify_event *event;
	ssize_t len;
	char *ptr;

	/* Loop while events can be read from inotify file descriptor. */

	for(;;) {
		/* Read some events. */
		len = read(fd, buf, sizeof(buf));
		if(len == -1 && errno != EAGAIN) {
			gl_log_err("read");
			exit(EXIT_FAILURE);
		}

		if(len <= 0)
			break;

		for(ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
			
			event = (const struct inotify_event *) ptr;

			/* Print event type */
			if(event->mask & IN_OPEN)
				printf("IN_OPEN: ");
			if(event->mask & IN_CLOSE_NOWRITE)
				printf("IN_CLOSE_NOWRITE: ");
			if(event->mask & IN_CLOSE_WRITE)
				printf("IN_CLOSE_WRITE: ");

			/* Print the name of the watched directory */
			if(wd[0] == event->wd) {
				printf("%s/", event->name);
				//break;
			}

			/* Print the name of the file */
			if(event->len)
				printf("%s", event->name);

			/* Print type of filesystem object */
			if(event->mask & IN_ISDIR)
				printf(" [directory]\n");
			else
				printf(" [file]\n");
		}


	}
}

char* get_bin_dir() { /* helper function to get files from the dir that the binary resides in */

	size_t path_size = sizeof(char) * 64;
	char* path_buf = malloc(path_size);
	ssize_t path_size_used;

	while(1) {

		path_size_used = readlink("/proc/self/exe", path_buf, path_size);

		if(path_size_used < 0) {
			gl_log_err("readlink");
			free(path_buf);
			return NULL;
		}

		if(path_size_used < (ssize_t)(path_size)) {
			break;
		}

		path_size += sizeof(char) * 64;
		path_buf = realloc(path_buf, path_size);
		if(path_buf == NULL) {
			gl_log_err("realloc");
			free(path_buf);
			return NULL;
		}
	}

	/* Remove the binary name from the string */
	for(int i = path_size_used; i > 0; i--) {
		if(path_buf[i] != '/') {
			path_buf[i] = 0;
			path_size_used--;
		}else {
			break;
		}
	}

	/* Try reallocating the path_size to minimum size. */
	{
		char* tmp;
		tmp = realloc(path_buf, path_size_used + 1);
		if(tmp) {
			path_buf = tmp;
			path_size = path_size_used + 1;
		}
	}

	path_buf[path_size] = '\0';
	return path_buf;
}

const char* make_path(const char* filename) { /* Helper for creating absolute paths from relative paths */
	char* bin_dir = get_bin_dir();
	ssize_t combined_size = (sizeof(filename) * strlen(filename)) + (sizeof(bin_dir) * strlen(bin_dir));

	bin_dir = realloc(bin_dir, combined_size);
	bin_dir = strcat(bin_dir, filename);

	return bin_dir;
}

bool restart_gl_log() {
	FILE* file = fopen(GL_LOG_FILE, "w");
	if(!file) {
		fprintf(stderr, "ERROR: could not open GL_LOG_FILE log file %s for writing\n", GL_LOG_FILE);
		return false;
	}

	time_t now = time(NULL);
	char* date = ctime(&now);
	fprintf(file, "GL_LOG_FILE log. local time %s\n", date);
	fclose(file);
	return true;
}

bool gl_log(const char* message, ...) {
	va_list argptr;
	FILE* file = fopen(GL_LOG_FILE, "a");
	if(!file) {
		fprintf(stderr, "ERROR: could not open GL_LOG_FILE %s for appending\n", GL_LOG_FILE);
		return false;
	}
	va_start(argptr, message);
	vfprintf(file, message, argptr);
	va_end(argptr);
	fclose(file);
	return true;
}

bool gl_log_err(const char* message, ...) {
	va_list argptr;
	FILE* file = fopen(GL_LOG_FILE, "a");
	if(!file) {
		fprintf(stderr, "ERROR: could not open GL_LOG_FILE %s file for appending\n", GL_LOG_FILE);
		return false;
	}
	va_start(argptr, message);
	vfprintf(file, message, argptr);
	va_end(argptr);
	va_start(argptr, message);
	vfprintf(stderr, message, argptr);
	va_end(argptr);
	fclose(file);
	return true;
}

void glfw_error_callback(int error, const char* description) {
	gl_log_err("GLFW ERROR: code %i msg: $s\n", error, description);
}

void glfw_window_size_callback(GLFWwindow* window, unsigned int width, unsigned int height) { /* for updating any perspective matices used */
	g_gl_height = height;
	g_gl_width = width;
}

void log_gl_params() {
  GLenum params[] = {
    GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
    GL_MAX_CUBE_MAP_TEXTURE_SIZE,
    GL_MAX_DRAW_BUFFERS,
    GL_MAX_FRAGMENT_UNIFORM_COMPONENTS,
    GL_MAX_TEXTURE_IMAGE_UNITS,
    GL_MAX_TEXTURE_SIZE,
    GL_MAX_VARYING_FLOATS,
    GL_MAX_VERTEX_ATTRIBS,
    GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
    GL_MAX_VERTEX_UNIFORM_COMPONENTS,
    GL_MAX_VIEWPORT_DIMS,
    GL_STEREO,
  };
  const char* names[] = {
    "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS",
    "GL_MAX_CUBE_MAP_TEXTURE_SIZE",
    "GL_MAX_DRAW_BUFFERS",
    "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS",
    "GL_MAX_TEXTURE_IMAGE_UNITS",
    "GL_MAX_TEXTURE_SIZE",
    "GL_MAX_VARYING_FLOATS",
    "GL_MAX_VERTEX_ATTRIBS",
    "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS",
    "GL_MAX_VERTEX_UNIFORM_COMPONENTS",
    "GL_MAX_VIEWPORT_DIMS",
    "GL_STEREO",
  };
  gl_log("\nGL Context Params:\n");
  
  // integers - only works if the order is 0-10 integer return types
  for (int i = 0; i < 10; i++) {
    GLint v = 0;
    glGetIntegerv(params[i], &v);
    gl_log("%s %i\n", names[i], v);
  }

  // others
  GLint v[2];
  v[0] = v[1] = 0;
  glGetIntegerv(params[10], v);
  gl_log("%s %i %i\n", names[10], v[0], v[1]);

  unsigned char s = 0;
  glGetBooleanv(params[11], &s);
  gl_log("%s %u\n", names[11], (unsigned int)s);
  gl_log("-----------------------------\n");
}

int main(int argc, char *argv[]) { /* cmd args unused for now */

	char* tmpstr = get_bin_dir();
	fprintf(stdout, "%s\n", tmpstr);
	free(tmpstr);

	make_path("vertex_shader.glsl");

	watcher fw;

	fw.fd = inotify_init1(IN_NONBLOCK);
	if(fw.fd == -1) {
		gl_log("inotify_init1");
		exit(EXIT_FAILURE);
	}

	fw.wd = calloc(1, sizeof(int));
	fw.wd[0] = inotify_add_watch(fw.fd, get_bin_dir(), IN_CLOSE | IN_MODIFY);

	if(fw.wd[0] == -1) {
		gl_log_err("Cannot watch bin_dir");
		exit(EXIT_FAILURE);
	}

	fw.nfds = 2;
	/* Console input */
	fw.fds[0].fd = STDIN_FILENO;
	fw.fds[0].events = POLLIN;

	/* Inotify input */
	fw.fds[1].fd = fw.fd;
	fw.fds[1].events = POLLIN;


	if(initgl() != 0) {
		return 1;
	}

	float points[] = {
		0.0f,  0.5f,  0.0f,
   		0.5f, 0.0f,  0.0f,
		-0.5f, 0.0f,  0.0f
	};
	float colours[] = {
		1.0f, 0.5f, 0.0f,
		0.5f, 1.0f, 0.0f,
		0.5f, 0.0f, 1.0f
	};

	GLuint points_vbo = 0;
	glGenBuffers(1, &points_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, points_vbo);
	glBufferData(GL_ARRAY_BUFFER, 9 * sizeof(float), points, GL_STATIC_DRAW);

	GLuint colour_vbo = 0;
	glGenBuffers(1, &colour_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, colour_vbo);
	glBufferData(GL_ARRAY_BUFFER, 9 * sizeof(float), colours, GL_STATIC_DRAW);

	GLuint vao = 0;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, points_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
	glBindBuffer(GL_ARRAY_BUFFER, colour_vbo);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	/*if(openshader("vertex_shader.glsl") != NULL){
		printf("vertex_shader: %s", openshader("vertex_shader.glsl"));
	}*/


#ifdef INBUILD
	const char* vertex_shader =
		"#version 400\n"
		"in vec3 vp;"
		"void main() {"
		"	gl_Position = vec4(vp, 1.0);"
		"}";

	const char* fragment_shader =
		"#version 400\n"
		"out vec4 frag_colour;"
		"void main() {"
		"	frag_colour = vec4(0.5, 0.0, 0.5, 1.0);"
		"}";
#endif
#ifndef INBUILD
	GLuint shader_programme = glCreateProgram();
	reload_shader_program(&shader_programme, "vertex_shader.glsl", "fragment_shader.glsl");
	/*int fd = inotify_init();
	if( fd == -1 ) {
		fprintf(stderr, "ERROR inotify_init() failed");
		return 1;
	}*/
	//int watch_vertex_shader = inotify_add_watch(fd, make_path("vertex_shader.glsl"), IN_MODIFY);
	//int watch_fragment_shader = inotify_add_watch(fd, make_path("fragment_shader.glsl"), IN_MODIFY);
#endif

	while(!glfwWindowShouldClose(window)) {
		// wipe drawing surface clear
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glViewport(0, 0, g_gl_width, g_gl_height);
		glUseProgram(shader_programme);
		glBindVertexArray(vao);
		// draw point 0-3 from the currently bound VAO with current in use shader_programme
		glDrawArrays(GL_TRIANGLES, 0, 3);
		// update other events like input handling
		glfwPollEvents();
		// put stuff on display
		glfwSwapBuffers(window);

		if(GLFW_PRESS == glfwGetKey(window, GLFW_KEY_ESCAPE)) {
			glfwSetWindowShouldClose(window, 1);
		}

		fw.poll_num = poll(fw.fds, fw.nfds, -1);
		if(fw.poll_num > 0) {
			if(fw.fds[1].revents & POLLIN) {
				handle_events(fw.fd, fw.wd, get_bin_dir());
			}
		}
	}

	// close GL context and any other GLFW resources
	glfwTerminate();
	free(fw.wd);
	return 0;
}
