#define GLFW_INCLUDE_ES2

#include <AL/al.h>
#include <AL/alc.h>
#include <emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>
#include <GLES/gl.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define RANDOM() (((float) rand()) / RAND_MAX)
#define _STR(x) #x
#define STR(x) _STR(x)

#define AUDIO_PATH "assets/murmur_44100.pcm"
#define STEPS 150.0

#define START_R (95.0f/255)
#define START_G (109.0f/255)
#define START_B (132.0f/255)


static const char *vertexShader = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char *fragShader = "precision highp float;" \
                                "uniform float time;" \
                                "uniform vec2 origin;" \
                                "uniform vec2 resolution;" \
                                "uniform vec4 currentColor;"
                                "uniform vec4 colorMove;" \
                                "float steps = " STR(STEPS) ";" \
                                "float scale = 1.0/sqrt(2.0);" \

                                "void main()" \
                                "{" \
                                    "float dist = length(gl_FragCoord.xy / resolution - origin) * scale;" \
                                    "gl_FragColor = min(time / (dist * steps), 1.0) * colorMove + currentColor;" \
                                "}";

static float scaleFactor;
static ALCcontext *context;
static ALuint source, buffer;

static float currentRed, currentGreen, currentBlue;
static float newRed, newGreen, newBlue;
static float progress = STEPS;
static float fullScreenQuad[] = {-1.0, -1.0, 1.0, -1.0, -1.0, 1.0, 1.0, 1.0};
static GLuint program, vbo, hTime, hOrigin, hResolution, hCurrentColor, hColorMove;


void resize_window(GLFWwindow *window, int width, int height)
{
    glfwSetWindowSize(window, width, height);
    glViewport(0, 0, width, height);

    scaleFactor = 2 * M_PI / width;

    if (program)
    {
        glUniform2f(hResolution, (float) width, (float) height);
    }
}


void shutdownAL(void)
{
    if (context)
    {
        alDeleteBuffers(1, &buffer);
        alDeleteSources(1, &source);

        alcMakeContextCurrent(NULL);

        alcCloseDevice(alcGetContextsDevice(context));
        alcDestroyContext(context);
        
        context = NULL;
    }

    emscripten_set_mousemove_callback(NULL, NULL, 0, NULL);
}


void fallback_audio(int err)
{
    EM_ASM(
    {
        console.log("ur browser still sukcs ass (ass errno. " + $0 + ") ... but i have A Fallback");
        document.body.innerHTML += "<audio autoplay loop>" +
                                       "<source src='assets/murmur_fallback.m4a'>" +
                                       "<source src='assets/murmur_fallback.ogg'>" +
                                   "</audio>";
    }, err);
}


void err(int errno)
{
    EM_ASM({alert("sorry... you\"re browser probably sucks ass... ass errno. " + $0)}, errno);
}


void begin_playback(emscripten_fetch_t *fetch)
{
    emscripten_fetch_close(fetch);

    alGenBuffers(1, &buffer);

    if (!buffer)
    {
        shutdownAL();
        fallback_audio(9);

        return;
    }

    alBufferData(buffer, AL_FORMAT_MONO16, fetch->data, fetch->numBytes, 48000);

    alSourcei(source, AL_BUFFER, buffer);
    alSourcei(source, AL_LOOPING, AL_TRUE);

    alSourcePlay(source);
}


void download_fail(emscripten_fetch_t *fetch)
{
    emscripten_fetch_close(fetch);
    shutdownAL();

    EM_ASM_({alert("couldn;t fetch the audio...")});
}


char initAL(void)
{
    ALCdevice *device = alcOpenDevice(NULL);

    if (device == NULL)
    {
        fallback_audio(7);
        return 0;
    }

    context = alcCreateContext(device, NULL);
    alcMakeContextCurrent(context);

    alGenSources(1, &source);

    if (alGetError() != AL_NO_ERROR)
    {
        shutdownAL();
        fallback_audio(8);

        return 0;
    }

    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 1.0f);

    emscripten_fetch_attr_t fetch;
    emscripten_fetch_attr_init(&fetch);

    fetch.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    fetch.requestMethod[0] = 'G';
    fetch.requestMethod[1] = 'E';
    fetch.requestMethod[2] = 'T';
    fetch.requestMethod[3] = '\0';

    fetch.onsuccess = begin_playback;
    fetch.onerror = download_fail;

    emscripten_fetch(&fetch, AUDIO_PATH);

    return 1;
}


char initGL(void)
{
    EM_ASM_({Module.canvas = document.getElementById("c");});

    int width = EM_ASM_INT({return window.innerWidth});
    int height = EM_ASM_INT({return window.innerHeight});

    scaleFactor = M_PI / width;

    if (glfwInit() != GL_TRUE)
    {
        err(1);
        return 0;
    }

    GLFWwindow *window = glfwCreateWindow(width, height, "", NULL, NULL);

    if (window == NULL)
    {
        err(2);
        return 0;
    }

    glfwMakeContextCurrent(window);
    glfwSetWindowSizeCallback(window, resize_window);

    GLuint vShaderHandle, fShaderHandle;
    GLint compile;

    vShaderHandle = glCreateShader(GL_VERTEX_SHADER);
    if (!vShaderHandle)
    {
        err(3);
        return 0;
    }

    fShaderHandle = glCreateShader(GL_FRAGMENT_SHADER);
    if (!fShaderHandle)
    {
        err(4);
        return 0;
    }

    glShaderSource(vShaderHandle, 1, &vertexShader, NULL);
    glCompileShader(vShaderHandle);

    glGetShaderiv(vShaderHandle, GL_COMPILE_STATUS, &compile);

    if (!compile)
    {
        EM_ASM_({alert("you have not begun to witnsee my true power... *supplexs a bookcase, breaks all the vertex shaders*")});

        glDeleteShader(vShaderHandle);
        glDeleteShader(fShaderHandle);

        return 0;
    }

    glShaderSource(fShaderHandle, 1, &fragShader, NULL);
    glCompileShader(fShaderHandle);

    glGetShaderiv(fShaderHandle, GL_COMPILE_STATUS, &compile);

    if (!compile)
    {
        EM_ASM_({alert("you have not begun to witnsee my true power... *supplexs a bookcase, breaks all the fragment shaders*")});

        glDeleteShader(vShaderHandle);
        glDeleteShader(fShaderHandle);

        return 0;
    }

    program = glCreateProgram();

    if (!program)
    {
        err(5);
        return 0;
    }

    glAttachShader(program, vShaderHandle);
    glAttachShader(program, fShaderHandle);

    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);

    if (!linked)
    {
        EM_ASM_({alert("just caught someone triyng to cut all the chains on my bicycle with a buterknife")});
        glDeleteProgram(program);
        program = 0;

        return 0;
    }

    glUseProgram(program);

    glBindAttribLocation(program, 0, "pos");

    hTime = glGetUniformLocation(program, "time");
    hResolution = glGetUniformLocation(program, "resolution");
    hOrigin = glGetUniformLocation(program, "origin");
    hCurrentColor = glGetUniformLocation(program, "currentColor");
    hColorMove = glGetUniformLocation(program, "colorMove");

    glUniform2f(hResolution, (float) width, (float) height);

    glGenBuffers(1, &vbo);

    if (!vbo)
    {
        err(6);
        program = 0;

        return 0;
    }

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBufferData(GL_ARRAY_BUFFER, sizeof(fullScreenQuad), fullScreenQuad, GL_STATIC_DRAW);

    newRed = START_R;
    newGreen = START_G;
    newBlue = START_B;

    return 1;
}


void shutdownGL(void)
{
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);

    program = 0;
}


void think(void)
{
    if (progress >= STEPS)
    {
        currentRed = newRed;
        currentGreen = newGreen;
        currentBlue = newBlue;

        newRed = RANDOM();
        newGreen = RANDOM();
        newBlue = RANDOM();

        glUniform4f(hCurrentColor, currentRed, currentGreen, currentBlue, 1.0f);
        glUniform4f(hColorMove, newRed - currentRed, newGreen - currentGreen, newBlue - currentBlue, 1.0f);
        glUniform2f(hOrigin, RANDOM(), RANDOM());

        progress = 1.0f;
    }

    glUniform1f(hTime, progress);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, sizeof(fullScreenQuad) / (sizeof(float) * 2));

    progress++;
}


EM_BOOL update_pos(int type, const EmscriptenMouseEvent *event, void *_)
{
    float rad = ((float) event->screenX) * scaleFactor;

    alSource3f(source, AL_POSITION, sin(rad), 0, cos(rad));

    return 1;
}


int main(void)
{
    srand(time(NULL));

    if (initAL())
    {
        emscripten_set_mousemove_callback(NULL, NULL, 0, update_pos);
    }

    if (initGL())
    {
        emscripten_set_main_loop(think, 0, 1);
    }

    glfwTerminate();
    shutdownGL();
    shutdownAL();
}
